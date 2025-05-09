#pragma once

#include <coroutine_flow/tag_invoke.hpp>

#include <atomic>
#include <coroutine>
#include <exception>
#include <expected>
#include <functional>
#include <iostream>

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

  public:
    using promise_type = promise_t;
    explicit task(handle_t&& coro_handle)
        : m_coro_handle(std::move(coro_handle))
    {
      std::cout << "[task::task()] {" << this << "}" << std::endl;
    }

    template <typename scheduler_t>
      requires(
          std::copyable<scheduler_t> &&
          is_tag_invocable<schedule_task_t, scheduler_t, std::function<void()>>)
    void run_async(scheduler_t scheduler)
    {
      get_promise().schedule_callback =
          [p_scheduler = scheduler](std::function<void()> handle)
      {
        auto task_call = [p_handle = handle]() { p_handle(); };
        tag_invoke(schedule_task_t{}, p_scheduler, std::move(task_call));
      };
      std::cout << "[task::run_async(scheduler)] {" << this << "} schedule"
                << std::endl;

      tag_invoke(schedule_task_t{},
                 scheduler,
                 [p_current_handle = m_coro_handle] { p_current_handle(); });
    }

  private:
    awaiter_t
        run_async(std::function<void(std::function<void()>)> schedule_callback,
                  promise_t* parent_promise)
    {
      std::cout << "[task::run_async(callback, promise)] {" << this
                << "} schedule" << std::endl;

      get_promise().schedule_callback = schedule_callback;
      schedule_callback(
          [p_coro_handle = m_coro_handle,
           p_this_ptr = this,
           p_parent_promise = parent_promise]
          {
            std::cout << "[task::lambda01] {" << p_this_ptr << "} run task"
                      << std::endl;

            p_coro_handle();
            std::cout << "[task::lambda01] {" << p_this_ptr
                      << "} check parent promise" << std::endl;

            const bool awaiter_suspended =
                p_parent_promise->suspended_handle_barrier.test_and_set(
                    std::memory_order_release);
            std::cout << "[task::lambda01] {" << p_this_ptr
                      << "} check state: awaiter_suspended="
                      << awaiter_suspended << std::endl;

            if (awaiter_suspended)
            {
              std::cout << "[task::lambda01] {" << p_this_ptr
                        << "} wait until it is stored" << std::endl;
              p_parent_promise->suspended_handle_stored.wait(
                  false,
                  std::memory_order_acquire);
              std::cout << "[task::lambda01] {" << p_this_ptr << "} resume"
                        << std::endl;

              p_parent_promise->suspended_handle();
            }
          });
      awaiter_t result;
      result.current_handle = m_coro_handle;
      return result;
    }
    promise_t& get_promise() { return m_coro_handle.promise(); }
    handle_t m_coro_handle;
};

template <typename T>
struct task<T>::promise_t
{
    std::function<void(std::function<void()>)> schedule_callback;
    std::expected<T, std::exception_ptr> result;
    std::coroutine_handle<> suspended_handle{ std::noop_coroutine() };
    std::atomic_flag suspended_handle_barrier;
    std::atomic_flag suspended_handle_stored;

    ~promise_t() { std::cout << "[~promise_t] {" << this << "}" << std::endl; }

    task<T> get_return_object()
    {
      std::cout << "[promise::get_return_object] {" << this << "}" << std::endl;
      return task<T>{ handle_t::from_promise(*this) };
    }
    std::suspend_always initial_suspend() noexcept
    {
      std::cout << "[promise::initial_suspend] {" << this << "}" << std::endl;
      return {};
    }
    std::suspend_never final_suspend() noexcept
    {
      std::cout << "[promise::final_suspend] {" << this << "}" << std::endl;
      return {};
    }
    T return_value(T&& t)
    {
      std::cout << "[promise::return_value] {" << this << "}" << std::endl;
      result = t;
      return t;
    }
    void unhandled_exception()
    {
      std::cout << "[promise::unhandled_exception] {" << this << "}"
                << std::endl;

      result = std::unexpected(std::current_exception());
    }
    template <typename U>
    auto await_transform(task<U> task)
    {
      std::cout << "[promise::await_transform] {" << this << "} run_async"
                << std::endl;
      return task.run_async(schedule_callback, this);
    }
};

template <typename T>
struct task<T>::awaiter_t
{
    std::coroutine_handle<promise_t> current_handle;
    ~awaiter_t() { std::cout << "[~awaiter_t] {" << this << "}" << std::endl; }
    bool await_ready() { return current_handle.done(); }
    T await_resume() { return *current_handle.promise().result; }
    bool await_suspend(std::coroutine_handle<promise_t> suspended_handle)
    {
      std::cout << "[awaiter::await_suspend] {" << this << "} check promise"
                << std::endl;

      promise_t& promise = suspended_handle.promise();
      if (const bool finished_meanwhile =
              promise.suspended_handle_barrier.test_and_set(
                  std::memory_order_acquire);
          finished_meanwhile)
      {
        std::cout << "[awaiter::await_suspend] {" << this << "} return false;"
                  << std::endl;

        return false;
      }
      else
      {
        std::cout << "[awaiter::await_suspend] {" << this
                  << "} promise.suspended_handle=suspended_handle;"
                  << std::endl;

        promise.suspended_handle = suspended_handle;
        promise.suspended_handle_stored.test_and_set(std::memory_order_release);
        promise.suspended_handle_stored.notify_all();
        return true;
      }
    }
};
} // namespace coroutine_flow