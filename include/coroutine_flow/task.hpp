#pragma once

#include <coroutine_flow/__details/__continuation_coro.hpp>
#include <coroutine_flow/__details/continuation_data.hpp>
#include <coroutine_flow/__details/coroutine_chain.hpp>
#include <coroutine_flow/profiler.hpp>
#include <coroutine_flow/tag_invoke.hpp>

#include <atomic>
#include <coroutine>
#include <exception>
#include <expected>
#include <functional>

namespace coroutine_flow
{

struct schedule_task_t
{
};

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

      tag_invoke(schedule_task_t{},
                 scheduler,
                 [p_current_handle = m_coro_handle] { p_current_handle(); });
      m_coro_handle = {};
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
};

template <typename T>
struct task<T>::promise_t
{
    std::expected<std::optional<T>, std::exception_ptr> result;

    std::function<void(std::function<void()>)> schedule_callback;
    __details::coroutine_chain_t<task<T>::promise_t> coroutine_chain;

    ~promise_t() { CF_PROFILE_SCOPE(); }

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
    std::suspend_always final_suspend() noexcept
    {
      CF_PROFILE_SCOPE();
      return {};
    }

    template <typename R>
      requires std::is_move_constructible_v<std::remove_reference_t<R>> &&
               std::is_constructible_v<T, R&&>
    void return_value(R&& val)
    {
      CF_PROFILE_SCOPE();
      result = std::optional<T>(std::forward<R>(val));
    }
    template <typename R>
      requires std::is_copy_constructible_v<std::remove_reference_t<R>> &&
               std::is_constructible_v<T, const R&>
    void return_value(const R& val)
    {
      CF_PROFILE_SCOPE();
      result = std::optional<T>(val);
    }
    template <typename R>
      requires std::is_constructible_v<T, R*>
    void return_value(R* val)
    {
      CF_PROFILE_SCOPE();
      result = std::optional<T>(val);
    }
    void unhandled_exception()
    {
      CF_PROFILE_SCOPE();

      result = std::unexpected(std::current_exception());
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

    bool await_ready()
    {
      CF_PROFILE_SCOPE();
      return current_handle.done();
    }
    T await_resume()
    {
      CF_PROFILE_SCOPE();
      if constexpr (std::movable<T>)
      {
        return std::move(**current_handle.promise().result);
      }
      else
      {
        return **current_handle.promise().result;
      }
    }
    template <__details::coroutine_chain_holder other_promise_type>
    bool await_suspend(
        std::coroutine_handle<other_promise_type> suspended_handle)
    {
      CF_PROFILE_SCOPE();

      other_promise_type& promise = suspended_handle.promise();
      CF_ATTACH_NOTE("Suspended promise", suspended_handle.address());
      return promise.get_coroutine_chain().try_store_suspended_handle(
          suspended_handle);
    }
};
} // namespace coroutine_flow