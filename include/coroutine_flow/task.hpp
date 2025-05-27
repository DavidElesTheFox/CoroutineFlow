#pragma once

#include <coroutine_flow/__details/continuation_coro.hpp>
#include <coroutine_flow/__details/continuation_data.hpp>
#include <coroutine_flow/__details/coroutine_chain.hpp>
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

namespace __details
{
  template <typename T>
  struct result_as_promise_t
  {
      std::promise<T> result_promise;
      final_coroutine_t operator()(
          std::expected<std::optional<T>, std::exception_ptr> result) noexcept
      {
        CF_PROFILE_SCOPE();
        if (result.has_value())
        {
          assert(result->has_value());
          if constexpr (std::movable<T>)
          {
            result_promise.set_value(std::move(**result));
          }
          else
          {
            result_promise.set_value(**result);
          }
        }
        else
        {
          result_promise.set_exception(result.error());
        }
        co_return;
      }

      final_coroutine_t operator()() noexcept
      {
        CF_PROFILE_SCOPE();
        co_return;
      }
  };
} // namespace __details

template <typename T>
class task
{
  protected:
    struct awaiter_t;
    struct promise_t;
    using handle_t = std::coroutine_handle<promise_t>;

    template <typename R>
    friend struct task<R>::promise_t;

  public:
    using promise_type = promise_t;
    explicit task(handle_t&& coro_handle)
        : m_coro_handle(std::move(coro_handle))
    {
      CF_PROFILE_SCOPE();
    }

    ~task()
    {
      if (m_coro_handle)
      {
        m_coro_handle.destroy();
      }
    }

    template <typename scheduler_t>
      requires(
          std::copyable<scheduler_t> &&
          is_tag_invocable<schedule_task_t, scheduler_t, std::function<void()>>)
    void run_async(scheduler_t scheduler) &&
    {
      CF_PROFILE_SCOPE();
      get_promise().schedule_callback =
          [p_scheduler = scheduler](std::function<void()> handle)
      {
        auto task_call = [p_handle = handle]() { p_handle(); };
        tag_invoke(schedule_task_t{}, p_scheduler, std::move(task_call));
      };
      m_coro_handle.promise().execute_extension = true;
      m_result_future =
          m_coro_handle.promise().extension.result_promise.get_future();
      tag_invoke(schedule_task_t{},
                 scheduler,
                 [p_current_handle = m_coro_handle] { p_current_handle(); });
      m_coro_handle = {};
    }
    template <typename scheduler_t>
      requires(
          std::copyable<scheduler_t> &&
          is_tag_invocable<schedule_task_t, scheduler_t, std::function<void()>>)
    T sync_wait(scheduler_t scheduler) &&
    {
      std::move(*this).run_async(scheduler);

      return m_result_future.get();
    }

  private:
    template <__details::coroutine_chain_holder other_promise_t>
    awaiter_t
        run_async(std::function<void(std::function<void()>)> schedule_callback,
                  other_promise_t* suspended_promise)
    {
      CF_PROFILE_SCOPE();

      get_promise().schedule_callback = schedule_callback;
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
      awaiter_t result;
      result.current_handle = std::exchange(m_coro_handle, {});
      return result;
    }
    promise_t& get_promise() { return m_coro_handle.promise(); }
    handle_t m_coro_handle;
    std::future<T> m_result_future;
};

template <typename T>
struct task<T>::promise_t
{
    std::expected<std::optional<T>, std::exception_ptr> result;
    std::atomic_flag result_stored;

    std::function<void(std::function<void()>)> schedule_callback;
    __details::coroutine_chain_t<task<T>::promise_t> coroutine_chain;

    __details::result_as_promise_t<T> extension;
    bool execute_extension{ false };

    ~promise_t()
    {
      CF_PROFILE_SCOPE();
      CF_ATTACH_NOTE("handle", handle_t::from_promise(*this).address());
    }

    __details::continuation_data& get_next()
    {
      return coroutine_chain.get_next();
    }
    void set_next(__details::continuation_data next)
    {
      coroutine_chain.set_next(std::move(next));
    }

    __details::coroutine_chain_t<task<T>::promise_t>& get_coroutine_chain()
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
      if (execute_extension)
      {
        /*
         No need for memory synchronization because return_value should be
         written on the same thread where final_suspend
        */
        if constexpr (std::movable<T>)
        {
          return extension(std::move(result));
        }
        else
        {
          return extension(result);
        }
      }
      else
      {
        return extension();
      }
    }

    template <typename R>
      requires std::is_move_constructible_v<std::remove_reference_t<R>> &&
               std::is_constructible_v<T, R&&>
    void return_value(R&& val)
    {
      CF_PROFILE_SCOPE();
      result = std::optional<T>(std::forward<R>(val));
      result_stored.test_and_set(std::memory_order_release);
      result_stored.notify_all();
    }
    template <typename R>
      requires std::is_copy_constructible_v<std::remove_reference_t<R>> &&
               std::is_constructible_v<T, const R&>
    void return_value(const R& val)
    {
      CF_PROFILE_SCOPE();
      result = std::optional<T>(val);
      result_stored.notify_all();
      result_stored.test_and_set(std::memory_order_release);
    }
    template <typename R>
      requires std::is_constructible_v<T, R*>
    void return_value(R* val)
    {
      CF_PROFILE_SCOPE();
      result = std::optional<T>(val);
      result_stored.test_and_set(std::memory_order_release);
      result_stored.notify_all();
    }
    void unhandled_exception()
    {
      CF_PROFILE_SCOPE();

      result = std::unexpected(std::current_exception());
      result_stored.test_and_set(std::memory_order_release);
      result_stored.notify_all();
    }
    template <typename U>
    auto await_transform(task<U> task)
    {
      CF_PROFILE_SCOPE();
      return task.run_async(schedule_callback, this);
    }
};

template <typename T>
struct task<T>::awaiter_t
{
    std::coroutine_handle<promise_t> current_handle;

    static bool await_ready()
    {
      CF_PROFILE_SCOPE();
      return false;
    }
    T await_resume()
    {
      CF_PROFILE_SCOPE();
      promise_t& promise = current_handle.promise();
      promise.result_stored.wait(false, std::memory_order_acquire);
      if (promise.result.has_value())
      {
        if constexpr (std::movable<T>)
        {
          return std::move(current_handle.promise().result->value());
        }
        else
        {
          return current_handle.promise().result->value();
        }
      }
      else
      {
        std::rethrow_exception(promise.result.error());
      }
    }
    template <__details::coroutine_chain_holder other_promise_type>
    static void await_suspend(
        std::coroutine_handle<other_promise_type> suspended_handle)
    {
      CF_PROFILE_SCOPE();

      other_promise_type& promise = suspended_handle.promise();
      CF_ATTACH_NOTE("Suspended promise", suspended_handle.address());
      promise.get_coroutine_chain().store_suspended_handle(suspended_handle);
    }
};
} // namespace coroutine_flow