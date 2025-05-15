#pragma once

#include <coroutine_flow/profiler.hpp>
#include <coroutine_flow/tag_invoke.hpp>

#include <atomic>
#include <coroutine>
#include <exception>
#include <expected>
#include <functional>
#include <iostream>
#include <list>
#include <ranges>

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
    void run_async(scheduler_t scheduler)
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
    awaiter_t
        run_async(std::function<void(std::function<void()>)> schedule_callback,
                  promise_t* parent_promise)
    {
      CF_PROFILE_SCOPE();

      get_promise().schedule_callback = schedule_callback;
      schedule_callback(
          [p_coro_handle = m_coro_handle,
           p_this_ptr = this,
           p_parent_promise = parent_promise]() mutable
          {
            CF_PROFILE_SCOPE_N("Task::AsyncRun");
            CF_ATTACH_NOTE("Executed handle", p_coro_handle.address());
            CF_ATTACH_NOTE("Context's handle",
                           handle_t::from_promise(*p_parent_promise).address());
            for (auto& p : p_parent_promise->predecessors)
            {
              CF_ATTACH_NOTE("Context's predecessor's handle", p.address());
            }
            for (auto& p : p_coro_handle.promise().predecessors)
            {
              CF_ATTACH_NOTE("Current predecessor's handle", p.address());
            }

            p_coro_handle();

            if (p_coro_handle.done())
            {
              CF_PROFILE_ZONE(HandleDone, "Handle Done");

              const bool awaiter_suspended =
                  p_parent_promise->suspended_handle_barrier.test_and_set(
                      std::memory_order_release);

              if (awaiter_suspended)
              {
                CF_PROFILE_ZONE(Continue, "Continue");

                p_parent_promise->suspended_handle_stored.wait(
                    false,
                    std::memory_order_acquire);
                auto suspended_handle =
                    std::exchange(p_parent_promise->suspended_handle,
                                  std::nullopt);
                p_parent_promise->suspended_handle_barrier.clear(
                    std::memory_order_release);
                p_parent_promise->suspended_handle_stored.clear(
                    std::memory_order_release);
                suspended_handle->resume();
                if (suspended_handle->done())
                {
                  CF_PROFILE_ZONE(ContinuePredecessors, "Continue Predecessor");
                  CF_ATTACH_NOTE("Predecessors parent: ",
                                 p_parent_promise->predecessors.size());
                  CF_ATTACH_NOTE("Predecessors current: ",
                                 p_coro_handle.promise().predecessors.size());
                  for (auto it = p_parent_promise->predecessors.begin();
                       it != p_parent_promise->predecessors.end();)
                  {
                    CF_PROFILE_ZONE(ContinueParent, "Continue Parent");
                    auto parent = *it;
                    if (parent.done() == false)
                    {
                      parent();
                    }
                    it = p_parent_promise->predecessors.erase(it);

                    if (parent.done() == false)
                    {
                      CF_ATTACH_NOTE("Suspended");
                      auto& new_parent_promise = parent.promise();
                      if (p_parent_promise->suspended_handle != std::nullopt)
                      {
                        new_parent_promise.predecessors.push_back(
                            *p_parent_promise->suspended_handle);
                      }
                      std::copy(
                          p_parent_promise->predecessors.begin(),
                          p_parent_promise->predecessors.end(),
                          std::back_inserter(new_parent_promise.predecessors));
                      p_parent_promise->suspended_handle = std::nullopt;
                      p_parent_promise->predecessors.clear();
                      break;
                    }
                    else
                    {
                      CF_ATTACH_NOTE("Done");
                    }
                  }
                }
              }
            }
            else
            {
              CF_PROFILE_ZONE(StoreContinuation, "Store Confinuation");

              p_parent_promise->suspended_handle_stored.wait(
                  false,
                  std::memory_order_acquire);

              p_coro_handle.promise().predecessors.push_back(
                  *p_parent_promise->suspended_handle);
              std::copy(
                  p_parent_promise->predecessors.begin(),
                  p_parent_promise->predecessors.end(),
                  std::back_inserter(p_coro_handle.promise().predecessors));
              p_parent_promise->predecessors.clear();
              p_parent_promise->suspended_handle = std::nullopt;
              p_parent_promise->suspended_handle_barrier.clear(
                  std::memory_order_release);
              p_parent_promise->suspended_handle_stored.clear(
                  std::memory_order_release);
              p_parent_promise->suspended_handle_barrier.notify_all();
              p_parent_promise->suspended_handle_stored.notify_all();
              CF_ATTACH_NOTE("Predecessors: ",
                             p_coro_handle.promise().predecessors.size());
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
    std::function<void(std::function<void()>)> schedule_callback;
    std::expected<T, std::exception_ptr> result;
    std::optional<std::coroutine_handle<promise_t>> suspended_handle;
    std::atomic_flag suspended_handle_barrier;
    std::atomic_flag suspended_handle_stored;
    std::list<std::coroutine_handle<promise_t>> predecessors;

    ~promise_t() { CF_PROFILE_SCOPE(); }

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
    T return_value(T&& t)
    {
      CF_PROFILE_SCOPE();
      result = t;
      return t;
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
      return *current_handle.promise().result;
    }
    bool await_suspend(std::coroutine_handle<promise_t> suspended_handle)
    {
      CF_PROFILE_SCOPE();

      promise_t& promise = suspended_handle.promise();
      CF_ATTACH_NOTE("Suspended promise", suspended_handle.address());
      if (const bool finished_meanwhile =
              promise.suspended_handle_barrier.test_and_set(
                  std::memory_order_acquire);
          finished_meanwhile)
      {
        CF_PROFILE_ZONE(suspend_no_wait, "Suspend no wait");

        return false;
      }
      else
      {
        CF_PROFILE_ZONE(suspend_wait, "Suspend store");

        promise.suspended_handle = suspended_handle;
        promise.suspended_handle_stored.test_and_set(std::memory_order_release);
        promise.suspended_handle_stored.notify_all();
        return true;
      }
    }
};
} // namespace coroutine_flow