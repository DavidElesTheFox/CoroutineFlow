#pragma once

#include <coroutine_flow/__details/__continuation_coro.hpp>
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

namespace details__
{
  struct continuation_data
  {
      std::coroutine_handle<> coro;
      // std::function<void(std::list<continuation_data>&)> take_over;
      std::function<void(continuation_data)> set_next;
      std::function<continuation_data&()> get_next;
      bool is_empty() const { return set_next == nullptr; }
      void clear() { set_next = nullptr; }
  };

  template <typename promise_type>
  class ExecutionDirector;

  template <typename promise_type>
  concept coroutine_executable = requires(promise_type promise) {
    {
      promise.get_execution_director()
    } -> std::same_as<ExecutionDirector<promise_type>&>;
  };

  template <typename promise_type>
  class ExecutionDirector
  {
    private:
      std::optional<std::coroutine_handle<promise_type>> m_suspended_handle;
      std::atomic_flag m_suspended_handle_barrier;
      std::atomic_flag m_suspended_handle_stored;
      // std::list<details__::continuation_data> m_predecessors;
      details__::continuation_data m_next;

    public:
      template <typename other_promise_type>
      friend class ExecutionDirector;
      /*
            const std::list<details__::continuation_data>& get_predecessors()
         const
            {
              return m_predecessors;
            }
      */
      std::optional<std::coroutine_handle<promise_type>>
          reset_suspended_handle()
      {
        auto result = std::exchange(m_suspended_handle, std::nullopt);
        m_suspended_handle_barrier.clear(std::memory_order_release);
        m_suspended_handle_stored.clear(std::memory_order_release);
        m_suspended_handle_barrier.notify_all();
        m_suspended_handle_stored.notify_all();
        return result;
      }
      template <coroutine_executable other_promise_type>
      static continuation_data
          create_data(std::coroutine_handle<other_promise_type> handler)
      {
        continuation_data result;
        result.coro = handler;
        /*result.take_over = [=](std::list<continuation_data>& p_predecessors)
        {
          other_promise_type& promise = handler.promise();
          promise.get_execution_director().take_over(p_predecessors);
        };*/
        result.set_next = [=](continuation_data continuation_data)
        {
          other_promise_type& promise = handler.promise();
          promise.get_execution_director().m_next =
              std::move(continuation_data);
        };
        result.get_next = [=]() -> continuation_data&
        {
          other_promise_type& promise = handler.promise();
          return promise.get_execution_director().m_next;
        };
        return result;
      }

      /*     template <coroutine_executable other_promise_type>
           void take_over(ExecutionDirector<other_promise_type>& o)
           {
             take_over(o.m_predecessors);
           }
     */
      void continue_suspended_handle()
      {
        CF_PROFILE_SCOPE();

        const bool awaiter_suspended =
            m_suspended_handle_barrier.test_and_set(std::memory_order_release);

        if (awaiter_suspended == false)
        {
          return;
        }

        CF_PROFILE_ZONE(Continue, "Continue all predecessors");

        m_suspended_handle_stored.wait(false, std::memory_order_acquire);

        auto suspended_handle = reset_suspended_handle();
        suspended_handle->resume();
        if (suspended_handle->done() == false)
        {
          return;
        }
        continuation_data next = m_next;
        if (next.is_empty())
        {
          CF_ATTACH_NOTE("No next");
        }
        else
        {
          CF_ATTACH_NOTE("next:", next.coro.address());
        }
        while (next.is_empty() == false)
        {
          CF_PROFILE_ZONE(SetNext, "Continue next");
          // TODO: check this condition. When exception occurred resume was null
          if (next.coro.done() == false)
          {
            next.coro();
          }
          if (next.coro.done() == false)
          {
            CF_ATTACH_NOTE("Suspended", next.coro.address());
            CF_ATTACH_NOTE("insert", m_suspended_handle->address());
            auto current_data = create_data(*reset_suspended_handle());
            current_data.set_next(next.get_next());
            next.set_next(current_data);
            break;
          }
          else
          {
            CF_ATTACH_NOTE("Finished");
            next = next.get_next();
            CF_ATTACH_NOTE(next.coro.address());
          }
        }
        /*
                CF_PROFILE_ZONE(ContinuePredecessors, "Continue Predecessor");
                CF_ATTACH_NOTE("Predecessors count: ", m_predecessors.size());
                for (auto it = m_predecessors.begin(); it !=
           m_predecessors.end();)
                {
                  CF_PROFILE_ZONE(ContinueParent, "Continue an anchestor");
                  auto next_continuation = *it;
                  next_continuation.coro.resume();
                  it = m_predecessors.erase(it);

                  if (next_continuation.coro.done() == false)
                  {
                    CF_ATTACH_NOTE("Suspended");

                    if (m_suspended_handle != std::nullopt)
                    {
                      auto data = create_data(*reset_suspended_handle());
                      m_predecessors.push_front(std::move(data));
                    }

                    next_continuation.take_over(m_predecessors);
                    break;
                  }
                  else
                  {
                    CF_ATTACH_NOTE("Done");
                  }
                }
                  */
      }

      template <coroutine_executable other_promise_type>
      void move_into(ExecutionDirector<other_promise_type>& o)
      {
        CF_PROFILE_SCOPE();

        m_suspended_handle_stored.wait(false, std::memory_order_acquire);
        CF_ATTACH_NOTE("current:", m_suspended_handle->address());
        CF_ATTACH_NOTE("next:", o.m_next.coro.address());
        auto current_data = create_data(*reset_suspended_handle());
        current_data.set_next(m_next);
        o.m_next = current_data;
        /*
        if (m_suspended_handle != std::nullopt)
        {
          auto data = create_data(*reset_suspended_handle());
          m_predecessors.push_front(std::move(data));
        }
        o.take_over(*this);
        CF_ATTACH_NOTE("Predecessors: ", o.m_predecessors.size());
        */
      }

      bool try_store_suspended_handle(
          std::coroutine_handle<promise_type> suspended_handle)
      {
        if (const bool finished_meanwhile =
                m_suspended_handle_barrier.test_and_set(
                    std::memory_order_acquire);
            finished_meanwhile)
        {
          CF_PROFILE_ZONE(suspend_no_wait, "Suspend no wait");

          return false;
        }
        else
        {
          CF_PROFILE_ZONE(suspend_wait, "Suspend store");

          m_suspended_handle = suspended_handle;
          m_suspended_handle_stored.test_and_set(std::memory_order_release);
          m_suspended_handle_stored.notify_all();
          return true;
        }
      }

    private:
      /*
        void take_over(std::list<continuation_data>& predecessors)
        {
          std::copy(predecessors.begin(),
                    predecessors.end(),
                    std::back_inserter(m_predecessors));
          predecessors.clear();
        }
          */
  };

} // namespace details__

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
    template <details__::coroutine_executable other_promise_t>
    awaiter_t
        run_async(std::function<void(std::function<void()>)> schedule_callback,
                  other_promise_t* coro_context)
    {
      CF_PROFILE_SCOPE();

      get_promise().schedule_callback = schedule_callback;
      schedule_callback(
          [p_coro_handle = m_coro_handle,
           p_this_ptr = this,
           p_coro_context = coro_context]() mutable
          {
            CF_PROFILE_SCOPE_N("Task::AsyncRun");
            CF_ATTACH_NOTE("Executed handle", p_coro_handle.address());
            CF_ATTACH_NOTE("Context's handle",
                           std::coroutine_handle<other_promise_t>::from_promise(
                               *p_coro_context)
                               .address());
            /*
#if CF_PROFILER_ACTIVE
for (const auto& p :
p_coro_context->get_execution_director().get_predecessors())
{
CF_ATTACH_NOTE("Context's predecessor's handle",
          p.coro.address());
}
for (const auto& p : p_coro_handle.promise()
                  .get_execution_director()
                  .get_predecessors())
{
CF_ATTACH_NOTE("Current predecessor's handle", p.coro.address());
}
#endif
**/

            p_coro_handle();

            if (p_coro_handle.done())
            {
              CF_PROFILE_ZONE(HandleDone, "Handle Done");

              p_coro_context->get_execution_director()
                  .continue_suspended_handle();
            }
            else
            {
              p_coro_context->get_execution_director().move_into(
                  p_coro_handle.promise().get_execution_director());
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
    details__::ExecutionDirector<task<T>::promise_t> execution_director;

    ~promise_t() { CF_PROFILE_SCOPE(); }

    details__::ExecutionDirector<task<T>::promise_t>& get_execution_director()
    {
      return execution_director;
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
    template <details__::coroutine_executable other_promise_type>
    bool await_suspend(
        std::coroutine_handle<other_promise_type> suspended_handle)
    {
      CF_PROFILE_SCOPE();

      other_promise_type& promise = suspended_handle.promise();
      CF_ATTACH_NOTE("Suspended promise", suspended_handle.address());
      return promise.get_execution_director().try_store_suspended_handle(
          suspended_handle);
    }
};
} // namespace coroutine_flow