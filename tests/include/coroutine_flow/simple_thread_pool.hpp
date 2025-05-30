#pragma once

#include <coroutine_flow/profiler.hpp>
#include <coroutine_flow/task.hpp>
#include <coroutine_flow/test_exception.hpp>

#include <functional>
#include <list>
#include <thread>

class simple_thread_pool
{
  public:
    friend void tag_invoke(coroutine_flow::schedule_task_t,
                           simple_thread_pool* pool,
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
          [p_pool = pool, p_callback = callback](std::stop_token stop_token)
          {
            CF_PROFILE_SCOPE_N("simple_thread_pool::tag_invoke::schedule");
            try
            {
              if (stop_token.stop_requested())
              {
                return;
              }
              p_callback();
            }
            catch (...)
            {
              p_pool->on_error(std::current_exception());
            }
          });
    }
    simple_thread_pool() = default;
    simple_thread_pool(const simple_thread_pool&) = delete;
    simple_thread_pool(simple_thread_pool&&) = default;

    simple_thread_pool& operator=(const simple_thread_pool&) = delete;
    simple_thread_pool& operator=(simple_thread_pool&&) = default;

    ~simple_thread_pool()
    {
      request_stop();
      for (auto& thread : m_threads)
      {
        if (thread.joinable())
        {
          thread.join();
        }
      }
    }

    void request_stop() noexcept
    {
      for (auto& thread : m_threads)
      {
        thread.request_stop();
      }
    }
    void set_throw_at_schedule(uint32_t count) { m_throw_at_schedule = count; }
    void on_error(std::exception_ptr error)
    {
      std::lock_guard lock(m_errors_mutex);
      m_errors.push_back(error);
      request_stop();
    }
    std::vector<std::exception_ptr> clear_errors()
    {
      std::lock_guard lock(m_errors_mutex);
      return std::exchange(m_errors, {});
    }

  private:
    std::list<std::jthread> m_threads;
    std::optional<uint32_t> m_throw_at_schedule;
    uint32_t m_current_schedule_count = 0;
    std::mutex m_errors_mutex;
    std::vector<std::exception_ptr> m_errors;
};