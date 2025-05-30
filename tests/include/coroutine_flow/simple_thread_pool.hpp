#pragma once

#include <coroutine_flow/profiler.hpp>
#include <coroutine_flow/task.hpp>
#include <coroutine_flow/test_exception.hpp>

#include <functional>
#include <list>
#include <thread>

class SimpleThreadPool
{
  public:
    friend void tag_invoke(coroutine_flow::schedule_task_t,
                           SimpleThreadPool* pool,
                           std::function<void()> callback)
    {
      auto check_and_throw = [&](uint32_t limit) -> std::optional<int>
      {
        if (limit <= pool->m_current_schedule_count)
        {
          throw TestException();
        }
        return limit;
      };

      pool->m_throw_at_schedule.and_then(check_and_throw);
      pool->m_current_schedule_count++;

      pool->m_threads.emplace_back(
          [p_callback = callback](std::stop_token stop_token)
          {
            CF_PROFILE_SCOPE_N("SimpleThreadPool::tag_invoke::schedule");
            if (stop_token.stop_requested())
            {
              return;
            }
            p_callback();
          });
    }
    SimpleThreadPool() = default;
    SimpleThreadPool(const SimpleThreadPool&) = delete;
    SimpleThreadPool(SimpleThreadPool&&) = default;

    SimpleThreadPool& operator=(const SimpleThreadPool&) = delete;
    SimpleThreadPool& operator=(SimpleThreadPool&&) = default;

    ~SimpleThreadPool()
    {
      for (auto& thread : m_threads)
      {
        thread.request_stop();
        if (thread.joinable())
        {
          thread.join();
        }
      }
    }
    void set_throw_at_schedule(uint32_t count) { m_throw_at_schedule = count; }

  private:
    std::list<std::jthread> m_threads;
    std::optional<uint32_t> m_throw_at_schedule;
    uint32_t m_current_schedule_count = 0;
};