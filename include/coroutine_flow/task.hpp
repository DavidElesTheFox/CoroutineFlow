#pragma once

#include <coroutine_flow/__details/continuation_coro.hpp>
#include <coroutine_flow/__details/continuation_data.hpp>
#include <coroutine_flow/__details/coroutine_chain.hpp>
#include <coroutine_flow/__details/testing/test_injection.hpp>
#include <coroutine_flow/profiler.hpp>
#include <coroutine_flow/result_wrapper.hpp>
#include <coroutine_flow/tag_invoke.hpp>

#include <atomic>
#include <cassert>
#include <coroutine>
#include <exception>
#include <functional>

#include <iostream>
#include <stacktrace>

namespace coroutine_flow
{

struct schedule_task_t
{
};
template <typename T, template <typename> typename Extension>
class task_t;
namespace __details
{

  template <typename T, typename Extension>
  struct final_executor_t
  {
#if CF_ENABLE_INJECTIONS
      final_executor_t()
      {
        CF_TEST_INJECTION(testing::test_injection_points_t::object__construct,
                          this);
      }
      ~final_executor_t()
      {
        CF_TEST_INJECTION(testing::test_injection_points_t::object__destruct,
                          this);
      }
#endif
      final_executor_t(final_executor_t&&) = default;
      final_executor_t& operator=(final_executor_t&&) = default;

      static final_coroutine_t execute_on(final_coroutine_t::fall_through_t _,
                                          final_executor_t executor,
                                          result_wrapper_t<T> result)
      {
        if constexpr (std::movable<T>)
        {
          executor.m_extension(std::move(result));
        }
        else
        {
          executor.m_extension(result);
        }
        co_return;
      }
      static final_coroutine_t execute_on(final_executor_t executor,
                                          result_wrapper_t<T> result)
      {
        if constexpr (std::movable<T>)
        {
          executor.m_extension(std::move(result));
        }
        else
        {
          executor.m_extension(result);
        }
        co_return;
      }
      static final_coroutine_t skip(final_executor_t extension)
      {
        CF_PROFILE_SCOPE();
        co_return;
      }
      Extension& get_extension() { return m_extension; }

    private:
      Extension m_extension;
  };

  template <typename P, typename T>
  class return_policy_t
  {
    public:
      template <typename R>
        requires std::is_move_constructible_v<std::remove_reference_t<R>> &&
                 std::is_constructible_v<T, R&&>
      void return_value(R&& val)
      {
        CF_PROFILE_SCOPE();
        self()->result = std::optional<T>(std::forward<R>(val));
        self()->on_result_set();
        CF_ATTACH_NOTE("handle: ", P::handle_t::from_promise(*this).address());
      }
      template <typename R>
        requires std::is_copy_constructible_v<std::remove_reference_t<R>> &&
                 std::is_constructible_v<T, const R&>
      void return_value(const R& val)
      {
        CF_PROFILE_SCOPE();
        self()->result = std::optional<T>(val);
        self()->on_result_set();
        CF_ATTACH_NOTE("handle: ", P::handle_t::from_promise(*this).address());
      }
      template <typename R>
        requires std::is_constructible_v<T, R*>
      void return_value(R* val)
      {
        CF_PROFILE_SCOPE();
        self()->result = std::optional<T>(val);
        self()->on_result_set();
        CF_ATTACH_NOTE("handle: ", P::handle_t::from_promise(*this).address());
      }

    protected:
      constexpr P* self() { return static_cast<P*>(this); }
  };

  template <typename P>
  class return_policy_t<P, void>
  {
    public:
      void return_void()
      {
        CF_PROFILE_SCOPE();
        self()->on_result_set();
        CF_ATTACH_NOTE("handle: ", P::handle_t::from_promise(*this).address());
      }

    protected:
      constexpr P* self() { return static_cast<P*>(this); }
  };

  template <typename T, template <typename> typename Extension>
  struct task_promise_t : return_policy_t<task_promise_t<T, Extension>, T>
  {
      using handle_t = std::coroutine_handle<task_promise_t>;

      result_wrapper_t<T> result;
      std::atomic_flag result_stored;
      std::atomic_flag suspended_handle_resumed;
      std::atomic_flag suspend_started;

      std::coroutine_handle<> finalizer;

      std::function<void(std::function<void()>)> schedule_callback;
      __details::coroutine_chain_t<task_promise_t> coroutine_chain;

      /**
       * this token is a callable token, that returns with the
       * coroutine result after execution.
       *
       * @warning the object is valid only after schedule call.
       */
      typename Extension<T>::call_token_t extension_token;

      std::atomic_flag extension_token_available{ false };
      /**
       * Extension that will handle the final result.
       *
       * @warning Extension is invalid after the schedule call.
       */
      __details::final_executor_t<T, Extension<T>> extension;
      bool execute_extension{ false };
      /**
       * When the promise is externally referenced during the final suspend
       * it won't let fall through the final_coroutine (the extension). Thus,
       * the hanlde and the promise will be still alive and can be referenced
       * after the execution is finished.
       *
       * When it is false the promise will be destroyed when the exection is
       * finished. It is done by the final coroutine which is let fall_through
       * and when the final coroutine is allowed to fall through it destroys the
       * suspended coroutine (this)
       */
      bool external_referenced{ true };
      /**
       * coroutine chain always checks whether the suspended handle is done or
       * not. Even if it externall referenced it does this check, but it can be
       * that at that time the coroutine handle (and its promise) already
       * destroyed externally (by the sync wait.). Although, it doesn't
       * necessary means that it is a crash it is an undefined behaviour, that
       * might lead to a crash.
       *
       * This flag tells to the external user whether this promise is still
       * referenced and used or not. Thus, it can be checked before destroy.
       *
       * By default it is false, because initially it is not part of any
       * coroutine chain. The chain is created when the task is co_awaited, so
       * when run_async is called. It becomes unreferenced when the done() flag
       * is checked by the chain.
       */
      std::atomic_flag internal_referenced{ false };

      /**
       * release can be called only when this flag is true, until that, the
       * coroutine might be still used in the final_coroutine.
       */
      std::atomic_flag ready_to_release{ false };
#if CF_ENABLE_INJECTIONS
      task_promise_t()
      {
        CF_TEST_INJECTION(testing::test_injection_points_t::object__construct,
                          this);
      }
#endif
      ~task_promise_t()
      {
        CF_PROFILE_SCOPE();
        CF_ATTACH_NOTE("handle", handle_t::from_promise(*this).address());
        CF_TEST_INJECTION(testing::test_injection_points_t::object__destruct,
                          this);
        assert(ready_to_release.test());
        if (finalizer)
        {
          finalizer.destroy();
        }
      }

      __details::continuation_data& get_next()
      {
        return coroutine_chain.get_next();
      }
      void set_next(__details::continuation_data next)
      {
        coroutine_chain.set_next(std::move(next));
      }

      bool has_external_reference() const noexcept
      {
        return external_referenced;
      }
      void internal_release() noexcept
      {
        internal_referenced.clear(std::memory_order_release);
        internal_referenced.notify_all();
      }
      // TODO: Rename it to set_finalizer_and_release
      void set_finalizer(std::coroutine_handle<> value)
      {
        assert(!finalizer);
        finalizer = value;
        internal_release();
      }

      void wait_for_ready_to_release()
      {
        ready_to_release.wait(false, std::memory_order_acquire);
      }
      __details::coroutine_chain_t<task_promise_t>& get_coroutine_chain()
      {
        return coroutine_chain;
      }
      task_t<T, Extension> get_return_object()
      {
        CF_PROFILE_SCOPE();
        return task_t<T, Extension>{ handle_t::from_promise(*this) };
      }
      std::suspend_always initial_suspend() noexcept
      {
        CF_PROFILE_SCOPE();
        return {};
      }
      auto final_suspend() noexcept
      {
        CF_PROFILE_SCOPE();
        const bool destroy_handle = external_referenced == false;
        CF_ATTACH_NOTE("destroy handle", destroy_handle);

        if (execute_extension)
        {
          extension_token = extension.get_extension().get_call_token();
          extension_token_available.test_and_set(std::memory_order_release);
          extension_token_available.notify_all();
          /*
           No need for memory synchronization because return_value should be
           written on the same thread where final_suspend
          */
          if constexpr (std::movable<T>)
          {
            auto owned_extension = std::exchange(extension, {});
            auto owned_result = std::move(result);

            // internal_release();
            if (destroy_handle)
            {
              return final_executor_t<T, Extension<T>>::execute_on(
                  {},
                  std::move(owned_extension),
                  std::move(owned_result));
            }
            else
            {
              return final_executor_t<T, Extension<T>>::execute_on(
                  std::move(owned_extension),
                  std::move(owned_result));
            }
          }
          else
          {
            auto owned_extension = std::move(extension);
            auto owned_result = result;
            // internal_release();
            if (destroy_handle)
            {
              return final_executor_t<T, Extension<T>>::execute_on(
                  {},
                  std::move(owned_extension),
                  owned_result);
            }
            else
            {
              return final_executor_t<T, Extension<T>>::execute_on(
                  std::move(owned_extension),
                  owned_result);
            }
          }
        }
        else
        {
          auto owned_extension = std::move(extension);
          // internal_release();
          return final_executor_t<T, Extension<T>>::skip(
              std::exchange(owned_extension, {}));
        }
      }

      void unhandled_exception()
      {
        CF_PROFILE_SCOPE();
        result = std::unexpected(std::current_exception());
        on_result_set();
        CF_ATTACH_NOTE("handle: ", handle_t::from_promise(*this).address());
      }
      template <typename U>
      auto await_transform(task_t<U, Extension> task);

      void on_result_set()
      {
        std::atomic_thread_fence(std::memory_order_release);
        result_stored.test_and_set(std::memory_order_relaxed);
        result_stored.notify_all();
      }
  };
  template <typename T, template <typename> typename Extension>
  template <typename U>
  auto task_promise_t<T, Extension>::await_transform(task_t<U, Extension> task)
  {
    CF_PROFILE_SCOPE();
    return task.run_async_impl(schedule_callback, this);
  }
  template <typename T,
            template <typename> typename Extension,
            coroutine_chain_holder other_promise_type>
  struct task_awaiter_t
  {
      using promise_t = task_promise_t<T, Extension>;
      std::coroutine_handle<promise_t> current_handle;
      std::coroutine_handle<other_promise_type> suspended_handle;
      bool has_been_suspended = true;

      bool await_ready()
      {
        using injection_point = __details::testing::test_injection_points_t;
        CF_PROFILE_SCOPE();
        CF_TEST_INJECTION(injection_point::task__await_ready__begin,
                          suspended_handle.address());
        scope_exit_t await_ready_ends = [&]() noexcept
        {
          CF_TEST_INJECTION(injection_point::task__await_ready__end,
                            suspended_handle.address());
        };

        // Clear the flag at the start of each await operation to ensure
        // it's in the correct initial state. This must happen before any
        // checks to avoid race conditions with the callback.
        suspended_handle.promise().suspended_handle_resumed.clear(
            std::memory_order_release);

        if (current_handle.done())
        {
          CF_ATTACH_NOTE("async call is already finished");

          const bool already_ready =
              suspended_handle.promise().suspended_handle_resumed.test_and_set(
                  std::memory_order_acq_rel);
          // when we are saying: we are ready, it measn that we are not
          // suspended
          has_been_suspended = already_ready == false;
          CF_ATTACH_NOTE("suspended handle is ready? ", already_ready);
          CF_TEST_INJECTION(
              injection_point::task__await_ready__after_test_and_set,
              suspended_handle.address());

          return already_ready;
        }

        return false;
      }
      T await_resume()
      {
        CF_PROFILE_SCOPE();
        CF_ATTACH_NOTE("caused async call a suspend? ", has_been_suspended);
        CF_ATTACH_NOTE("Async task: ", current_handle.address());

        promise_t& promise = current_handle.promise();
        promise.result_stored.wait(false, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);

        auto result = std::move(promise.result);
        if (has_been_suspended == false)
        {
          promise.internal_release();
          if (promise.external_referenced == false)
          {
            promise.wait_for_ready_to_release();
            current_handle.destroy();
          }
        }
        if (result.has_value())
        {
          if constexpr (std::is_same_v<T, void>)
          {
          }
          else if constexpr (std::movable<T>)
          {
            return std::move(result->value());
          }
          else
          {
            return result->value();
          }
        }
        else
        {
          std::rethrow_exception(result.error());
        }
      }
      bool await_suspend(
          std::coroutine_handle<other_promise_type> suspended_handle)
      {
        CF_PROFILE_SCOPE();
        using injection_point = __details::testing::test_injection_points_t;
        if (current_handle.done())
        {
          CF_ATTACH_NOTE("Async call is finished");

          const bool already_ready =
              suspended_handle.promise().suspended_handle_resumed.test_and_set(
                  std::memory_order_acq_rel);
          has_been_suspended = already_ready == false;

          CF_ATTACH_NOTE("Has been resumed? ", already_ready);
          CF_TEST_INJECTION(
              injection_point::task__await_suspend__after_test_and_set,
              suspended_handle.address());

          if (already_ready)
          {
            // current_handle.destroy();
            return false;
          }
        }
        assert(suspended_handle.address() == this->suspended_handle.address());

        other_promise_type& promise = suspended_handle.promise();
        CF_ATTACH_NOTE("Suspended promise", suspended_handle.address());
        // Flag was already cleared in await_ready(), so we can safely store
        // the suspended handle now
        promise.get_coroutine_chain().store_suspended_handle(suspended_handle);
        return true;
      }
  };

} // namespace __details

template <typename T, template <typename> typename Extension>
class task_t
{
  protected:
    template <__details::coroutine_chain_holder other_promise_type>
    using awaiter_t =
        __details::task_awaiter_t<T, Extension, other_promise_type>;

    using promise_t = __details::task_promise_t<T, Extension>;
    using handle_t = std::coroutine_handle<promise_t>;

    template <typename R, template <typename> typename E>
    friend struct __details::task_promise_t;

    friend struct __details::task_promise_t<T, Extension>;

  public:
    using promise_type = promise_t;
    explicit task_t(handle_t&& coro_handle)
        : m_coro_handle(std::move(coro_handle))
    {
      CF_PROFILE_SCOPE();
      using injection_point = __details::testing::test_injection_points_t;
      CF_TEST_INJECTION(injection_point::task__constructor,
                        coro_handle.address());
      CF_TEST_INJECTION(injection_point::object__construct, this);
    }

    void* address() { return m_coro_handle.address(); }

    ~task_t()
    {
      if (m_coro_handle)
      {
        m_coro_handle.promise().ready_to_release.test_and_set();
        m_coro_handle.destroy();
      }
      CF_TEST_INJECTION(
          __details::testing::test_injection_points_t::object__destruct,
          this);
    }

    template <typename U, template <typename> typename E, typename scheduler_t>
      requires(
          std::copyable<scheduler_t> &&
          is_tag_invocable<schedule_task_t, scheduler_t, std::function<void()>>)
    friend void run_async(task_t<U, E>&& task, scheduler_t scheduler);

    template <typename U, template <typename> typename E, typename scheduler_t>
      requires(
          std::copyable<scheduler_t> &&
          is_tag_invocable<schedule_task_t, scheduler_t, std::function<void()>>)
    friend U sync_wait(task_t<U, E>&& task, scheduler_t scheduler);

  private:
    template <typename scheduler_t>
      requires(
          std::copyable<scheduler_t> &&
          is_tag_invocable<schedule_task_t, scheduler_t, std::function<void()>>)
    handle_t schedule(scheduler_t scheduler, bool keep_handle_alive)
    {
      CF_PROFILE_SCOPE();
      get_promise().schedule_callback =
          [p_scheduler = scheduler](std::function<void()> callback)
      { tag_invoke(schedule_task_t{}, p_scheduler, std::move(callback)); };
      m_coro_handle.promise().execute_extension = true;
      m_coro_handle.promise().external_referenced = keep_handle_alive;
      tag_invoke(schedule_task_t{},
                 scheduler,
                 [p_current_handle = m_coro_handle] { p_current_handle(); });
      return std::exchange(m_coro_handle, {});
    }

    template <__details::coroutine_chain_holder other_promise_t>
    awaiter_t<other_promise_t> run_async_impl(
        std::function<void(std::function<void()>)> schedule_callback,
        other_promise_t* suspended_promise);

    promise_t& get_promise() { return m_coro_handle.promise(); }
    handle_t m_coro_handle;
};

template <typename T,
          template <typename> typename Extension,
          typename scheduler_t>
  requires(
      std::copyable<scheduler_t> &&
      is_tag_invocable<schedule_task_t, scheduler_t, std::function<void()>>)
void run_async(task_t<T, Extension>&& task, scheduler_t scheduler)
{
  constexpr const bool keep_handle_alive = false;
  std::move(task).schedule(scheduler, keep_handle_alive);
}

template <typename T,
          template <typename> typename Extension,
          typename scheduler_t>
  requires(
      std::copyable<scheduler_t> &&
      is_tag_invocable<schedule_task_t, scheduler_t, std::function<void()>>)
T sync_wait(task_t<T, Extension>&& task, scheduler_t scheduler)
{
  constexpr const bool keep_handle_alive = true;

  auto handle = task.schedule(scheduler, keep_handle_alive);
  auto& coroutine_promise = handle.promise();
  [[maybe_unused]]
  auto* handle_address = handle.address();

  coroutine_promise.extension_token_available.wait(false,
                                                   std::memory_order_acquire);

  auto extension_token = std::move(coroutine_promise.extension_token);
  extension_token.wait();
  CF_TEST_INJECTION(
      __details::testing::test_injection_points_t::task__sync_wait__has_result,
      handle_address);
  handle.promise().internal_referenced.wait(true, std::memory_order_acquire);
  handle.promise().wait_for_ready_to_release();
  handle.destroy();
  CF_TEST_INJECTION(__details::testing::test_injection_points_t::
                        task__sync_wait__handle_destroy,
                    handle_address);
  return std::move(extension_token).get_result();
}

template <typename T, template <typename> typename Extension>
template <__details::coroutine_chain_holder other_promise_t>
task_t<T, Extension>::awaiter_t<other_promise_t>
    task_t<T, Extension>::run_async_impl(
        std::function<void(std::function<void()>)> schedule_callback,
        other_promise_t* suspended_promise)
{
  using injection_point = __details::testing::test_injection_points_t;
  CF_PROFILE_SCOPE();
  m_coro_handle.promise().external_referenced = false;

  get_promise().schedule_callback = schedule_callback;

  // Don't clear suspended_handle_resumed here - it should only be cleared
  // after we know the coroutine will actually suspend (in await_suspend)
  suspended_promise->internal_referenced.test_and_set(
      std::memory_order_release);
  suspended_promise->suspend_started.clear(std::memory_order_release);

  schedule_callback(
      [p_coro_handle = m_coro_handle,
       p_suspended_promise = suspended_promise]() mutable
      {
        CF_PROFILE_SCOPE_N("Task::AsyncRun");
        CF_ATTACH_NOTE("Executed handle", p_coro_handle.address());
        CF_ATTACH_NOTE("Context's handle",
                       std::coroutine_handle<other_promise_t>::from_promise(
                           *p_suspended_promise)
                           .address());

        p_coro_handle();
        CF_TEST_INJECTION(injection_point::task__run_async__async_call_finished,
                          p_coro_handle.address());
        CF_TEST_INJECTION(injection_point::task__run_async__before_test_and_set,
                          std::coroutine_handle<other_promise_t>::from_promise(
                              *p_suspended_promise)
                              .address());
        /*
        It is a tricky problem what if when this function call blocks the
        co_await so it never start to awaiting, thus suspended never gonna be
        stored. In this situation continue_suspended_handle() can't be called,
        because it will be a deadlock. We need to say somehow that co_await is
        really started. And when it is really started we can check weather the
        suspended_handle resumed or not.
        */

        const bool suspend_started = p_suspended_promise->suspend_started.test(
            std::memory_order_acquire);
        if (suspend_started == false)
        {
          CF_ATTACH_NOTE("suspended handle were not started.");
          return;
        }
        if (p_coro_handle.done())
        {
          CF_PROFILE_ZONE(HandleDone, "Handle Done");
          const bool suspended_is_resumed =
              p_suspended_promise->suspended_handle_resumed.test_and_set(
                  std::memory_order_acq_rel);
          if (suspended_is_resumed == false)
          {
            auto destroy_coro_at_end = __details::scope_exit_t{
              [&]() noexcept
              {
                p_coro_handle.promise().wait_for_ready_to_release();
                p_coro_handle.destroy();
              }
            };

            p_suspended_promise->get_coroutine_chain()
                .continue_suspended_handle();
          }
          else
          {
            CF_ATTACH_NOTE("suspended handle were not really suspended.");
            return;
          }
        }
        else
        {
          p_suspended_promise->get_coroutine_chain().move_into(
              p_coro_handle.promise().get_coroutine_chain());
        }
      });

  awaiter_t<other_promise_t> result;
  result.current_handle = std::exchange(m_coro_handle, {});
  result.suspended_handle =
      std::coroutine_handle<other_promise_t>::from_promise(*suspended_promise);

  suspended_promise->suspend_started.test_and_set(std::memory_order_release);
  return result;
}

} // namespace coroutine_flow