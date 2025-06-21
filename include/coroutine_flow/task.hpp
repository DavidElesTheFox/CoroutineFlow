#pragma once

#include <coroutine_flow/__details/continuation_coro.hpp>
#include <coroutine_flow/__details/continuation_data.hpp>
#include <coroutine_flow/__details/coroutine_chain.hpp>
#include <coroutine_flow/__details/testing/test_injection.hpp>
#include <coroutine_flow/profiler.hpp>
#include <coroutine_flow/tag_invoke.hpp>

#include <atomic>
#include <cassert>
#include <coroutine>
#include <exception>
#include <expected>
#include <functional>
#include <future>

namespace coroutine_flow
{

struct schedule_task_t
{
};
template <typename T>
class task;
namespace __details
{
  template <typename T>
  struct result_as_promise_t
  {
#if CF_ENABLE_INJECTIONS
      result_as_promise_t()
      {
        CF_TEST_INJECTION(testing::test_injection_points_t::object__construct,
                          this);
      }
      ~result_as_promise_t()
      {
        CF_TEST_INJECTION(testing::test_injection_points_t::object__destruct,
                          this);
      }
#endif
      result_as_promise_t(result_as_promise_t&&) = default;
      std::unique_ptr<std::promise<T>> result_promise{
        std::make_unique<std::promise<T>>()
      };

      static final_coroutine_t
          execute_on(final_coroutine_t::fall_through_t _,
                     result_as_promise_t extension,
                     std::expected<std::optional<T>, std::exception_ptr> result)
      {
        if constexpr (std::movable<T>)
        {
          extension(std::move(result));
        }
        else
        {
          extension(result);
        }
        co_return;
      }
      static final_coroutine_t
          execute_on(result_as_promise_t extension,
                     std::expected<std::optional<T>, std::exception_ptr> result)
      {
        if constexpr (std::movable<T>)
        {
          extension(std::move(result));
        }
        else
        {
          extension(result);
        }
        co_return;
      }
      static final_coroutine_t skip(result_as_promise_t&& extension)
      {
        extension();
        co_return;
      }

    private:
      void operator()(
          std::expected<std::optional<T>, std::exception_ptr> result) noexcept
      {
        CF_PROFILE_SCOPE_N("result_as_promise");
        if (result.has_value())
        {
          assert(result->has_value());
          if constexpr (std::movable<T>)
          {
            result_promise->set_value(std::move(**result));
          }
          else
          {
            result_promise->set_value(**result);
          }
        }
        else
        {
          result_promise->set_exception(result.error());
        }
      }
      void operator()() noexcept { CF_PROFILE_SCOPE(); }
  };

  template <typename T>
  struct task_promise_t
  {
      using handle_t = std::coroutine_handle<task_promise_t>;

      std::expected<std::optional<T>, std::exception_ptr> result;
      std::atomic_flag result_stored;
      std::atomic_flag suspended_handle_resumed;

      std::coroutine_handle<> finalizer;

      std::function<void(std::function<void()>)> schedule_callback;
      __details::coroutine_chain_t<task_promise_t> coroutine_chain;

      __details::result_as_promise_t<T> extension;
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
      void set_finalizer(std::coroutine_handle<> value) { finalizer = value; }
      __details::coroutine_chain_t<task_promise_t>& get_coroutine_chain()
      {
        return coroutine_chain;
      }
      task<T> get_return_object()
      {
        CF_PROFILE_SCOPE();
        return task<T>{ handle_t::from_promise(*this) };
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
          /*
           No need for memory synchronization because return_value should be
           written on the same thread where final_suspend
          */
          if constexpr (std::movable<T>)
          {
            if (destroy_handle)
            {
              return result_as_promise_t<T>::execute_on({},
                                                        std::move(extension),
                                                        std::move(result));
            }
            else
            {
              return result_as_promise_t<T>::execute_on(std::move(extension),
                                                        std::move(result));
            }
          }
          else
          {
            if (destroy_handle)
            {
              return result_as_promise_t<T>::execute_on({},
                                                        std::move(extension),
                                                        result);
            }
            else
            {
              return result_as_promise_t<T>::execute_on(std::move(extension),
                                                        result);
            }
          }
        }
        else
        {
          return result_as_promise_t<T>::skip(std::move(extension));
        }
      }

      template <typename R>
        requires std::is_move_constructible_v<std::remove_reference_t<R>> &&
                 std::is_constructible_v<T, R&&>
      void return_value(R&& val)
      {
        CF_PROFILE_SCOPE();
        result = std::optional<T>(std::forward<R>(val));
        on_result_set();
        CF_ATTACH_NOTE("handle: ", handle_t::from_promise(*this).address());
      }
      template <typename R>
        requires std::is_copy_constructible_v<std::remove_reference_t<R>> &&
                 std::is_constructible_v<T, const R&>
      void return_value(const R& val)
      {
        CF_PROFILE_SCOPE();
        result = std::optional<T>(val);
        on_result_set();
        CF_ATTACH_NOTE("handle: ", handle_t::from_promise(*this).address());
      }
      template <typename R>
        requires std::is_constructible_v<T, R*>
      void return_value(R* val)
      {
        CF_PROFILE_SCOPE();
        result = std::optional<T>(val);
        on_result_set();
        CF_ATTACH_NOTE("handle: ", handle_t::from_promise(*this).address());
      }
      void unhandled_exception()
      {
        CF_PROFILE_SCOPE();
        result = std::unexpected(std::current_exception());
        on_result_set();
        CF_ATTACH_NOTE("handle: ", handle_t::from_promise(*this).address());
      }
      template <typename U>
      auto await_transform(task<U> task);

      void on_result_set()
      {
        std::atomic_thread_fence(std::memory_order_release);
        result_stored.test_and_set(std::memory_order_relaxed);
        result_stored.notify_all();
      }
  };
  template <typename T>
  template <typename U>
  auto task_promise_t<T>::await_transform(task<U> task)
  {
    CF_PROFILE_SCOPE();
    return task.run_async_impl(schedule_callback, this);
  }
  template <typename T, coroutine_chain_holder other_promise_type>
  struct task_awaiter_t
  {
      using promise_t = task_promise_t<T>;
      std::coroutine_handle<promise_t> current_handle;
      std::coroutine_handle<other_promise_type> suspended_handle;
      bool was_not_suspended = false;

      bool await_ready()
      {
        using injection_point = __details::testing::test_injection_points_t;
        CF_PROFILE_SCOPE();
        CF_TEST_INJECTION(injection_point::task__await_ready__begin,
                          suspended_handle.address());

        if (current_handle.done())
        {
          CF_ATTACH_NOTE("async call is already finished");

          const bool has_been_resumed =
              suspended_handle.promise().suspended_handle_resumed.test_and_set(
                  std::memory_order_acquire);
          was_not_suspended = has_been_resumed == false;

          CF_ATTACH_NOTE("Do we suspend now? ", was_not_suspended);
          CF_TEST_INJECTION(
              injection_point::task__await_ready__after_test_and_set,
              suspended_handle.address());

          return was_not_suspended;
        }
        return false;
      }
      T await_resume()
      {
        CF_PROFILE_SCOPE();
        CF_ATTACH_NOTE("caused async call a suspend? ", was_not_suspended);
        CF_ATTACH_NOTE("Async task: ", current_handle.address());

        promise_t& promise = current_handle.promise();
        promise.result_stored.wait(false, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);

        auto result = std::move(promise.result);
        if (was_not_suspended)
        {
          current_handle.destroy();
        }
        if (result.has_value())
        {
          if constexpr (std::movable<T>)
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
          const bool has_been_resumed =
              suspended_handle.promise().suspended_handle_resumed.test_and_set(
                  std::memory_order_acquire);
          CF_ATTACH_NOTE("Has been resumed? ", has_been_resumed);
          CF_TEST_INJECTION(
              injection_point::task__await_suspend__after_test_and_set,
              suspended_handle.address());
          if (has_been_resumed == false)
          {
            return false;
          }
        }
        assert(suspended_handle.address() == this->suspended_handle.address());

        other_promise_type& promise = suspended_handle.promise();
        CF_ATTACH_NOTE("Suspended promise", suspended_handle.address());
        promise.get_coroutine_chain().store_suspended_handle(suspended_handle);
        return true;
      }
  };

} // namespace __details

template <typename T>
class task
{
  protected:
    template <__details::coroutine_chain_holder other_promise_type>
    using awaiter_t = __details::task_awaiter_t<T, other_promise_type>;

    using promise_t = __details::task_promise_t<T>;
    using handle_t = std::coroutine_handle<promise_t>;

    template <typename R>
    friend struct __details::task_promise_t;

    friend struct __details::task_promise_t<T>;

  public:
    using promise_type = promise_t;
    explicit task(handle_t&& coro_handle)
        : m_coro_handle(std::move(coro_handle))
    {
      CF_PROFILE_SCOPE();
      using injection_point = __details::testing::test_injection_points_t;
      CF_TEST_INJECTION(injection_point::task__constructor,
                        coro_handle.address());
      CF_TEST_INJECTION(injection_point::object__construct, this);
    }

    void* address() { return m_coro_handle.address(); }

    ~task()
    {
      if (m_coro_handle)
      {
        m_coro_handle.destroy();
      }
      CF_TEST_INJECTION(
          __details::testing::test_injection_points_t::object__destruct,
          this);
    }

    template <typename U, typename scheduler_t>
      requires(
          std::copyable<scheduler_t> &&
          is_tag_invocable<schedule_task_t, scheduler_t, std::function<void()>>)
    friend void run_async(task<U>&& task, scheduler_t scheduler);

    template <typename U, typename scheduler_t>
      requires(
          std::copyable<scheduler_t> &&
          is_tag_invocable<schedule_task_t, scheduler_t, std::function<void()>>)
    friend U sync_wait(task<U>&& task, scheduler_t scheduler);

  private:
    template <typename scheduler_t>
      requires(
          std::copyable<scheduler_t> &&
          is_tag_invocable<schedule_task_t, scheduler_t, std::function<void()>>)
    handle_t schedule(scheduler_t scheduler, bool keep_handle_alive)
    {
      CF_PROFILE_SCOPE();
      get_promise().schedule_callback =
          [p_scheduler = scheduler](std::function<void()> handle)
      {
        auto task_call = [p_handle = handle]() { p_handle(); };
        tag_invoke(schedule_task_t{}, p_scheduler, std::move(task_call));
      };
      m_coro_handle.promise().execute_extension = true;
      m_coro_handle.promise().external_referenced = keep_handle_alive;
      m_result_future =
          m_coro_handle.promise().extension.result_promise->get_future();
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
    std::future<T> m_result_future;
};

template <typename T, typename scheduler_t>
  requires(
      std::copyable<scheduler_t> &&
      is_tag_invocable<schedule_task_t, scheduler_t, std::function<void()>>)
void run_async(task<T>&& task, scheduler_t scheduler)
{
  constexpr const bool keep_handle_alive = false;
  std::move(task).schedule(scheduler, keep_handle_alive);
}

template <typename T, typename scheduler_t>
  requires(
      std::copyable<scheduler_t> &&
      is_tag_invocable<schedule_task_t, scheduler_t, std::function<void()>>)
T sync_wait(task<T>&& task, scheduler_t scheduler)
{
  constexpr const bool keep_handle_alive = true;

  auto handle = task.schedule(scheduler, keep_handle_alive);
  assert(task.m_result_future.valid());

  try
  {
    [[maybe_unused]]
    auto* handle_address = handle.address();
    if constexpr (std::movable<T>)
    {
      auto result = std::move(task.m_result_future.get());
      CF_TEST_INJECTION(__details::testing::test_injection_points_t::
                            task__sync_wait__has_result,
                        handle_address);
      handle.promise().internal_referenced.wait(true,
                                                std::memory_order_acquire);
      handle.destroy();
      CF_TEST_INJECTION(__details::testing::test_injection_points_t::
                            task__sync_wait__handle_destroy,
                        handle_address);
      return std::move(result);
    }
    else
    {
      T result = task.m_result_future.get();
      CF_TEST_INJECTION(__details::testing::test_injection_points_t::
                            task__sync_wait__has_result,
                        handle_address);
      handle.promise().internal_referenced.wait(true,
                                                std::memory_order_acquire);
      handle.destroy();
      CF_TEST_INJECTION(__details::testing::test_injection_points_t::
                            task__sync_wait__handle_destroy,
                        handle_address);
      return { result };
    }
  }
  catch (...)
  {
    handle.destroy();
    std::rethrow_exception(std::current_exception());
  }
}

template <typename T>
template <__details::coroutine_chain_holder other_promise_t>
task<T>::awaiter_t<other_promise_t> task<T>::run_async_impl(
    std::function<void(std::function<void()>)> schedule_callback,
    other_promise_t* suspended_promise)
{
  using injection_point = __details::testing::test_injection_points_t;
  CF_PROFILE_SCOPE();
  m_coro_handle.promise().external_referenced = false;

  get_promise().schedule_callback = schedule_callback;
  suspended_promise->suspended_handle_resumed.clear();
  suspended_promise->internal_referenced.test_and_set(
      std::memory_order_release);
  schedule_callback(
      [p_coro_handle = m_coro_handle,
       p_this_ptr = this,
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
        const bool suspended_is_not_suspended =
            p_suspended_promise->suspended_handle_resumed.test_and_set(
                std::memory_order_acquire);
        if (suspended_is_not_suspended)
        {
          CF_ATTACH_NOTE("suspended handle were not really suspended.");
          return;
        }
        if (p_coro_handle.done())
        {
          CF_PROFILE_ZONE(HandleDone, "Handle Done");

          auto destroy_coro_at_end =
              __details::scope_exit_t{ [&]() noexcept
                                       { p_coro_handle.destroy(); } };

          p_suspended_promise->get_coroutine_chain()
              .continue_suspended_handle();
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
  return result;
}

} // namespace coroutine_flow