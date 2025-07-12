#pragma once

#include <coroutine_flow/__details/testing/test_exception.hpp>
#include <coroutine_flow/profiler.hpp>
#include <coroutine_flow/task.hpp>

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <list>
#include <sstream>
#include <thread>

namespace coroutine_flow::__details::testing
{
class simple_thread_pool_t
{
  public:
    friend void tag_invoke(coroutine_flow::schedule_task_t,
                           simple_thread_pool_t* pool,
                           std::function<void()> callback)
    {
      auto check_and_throw = [&](uint32_t limit) -> std::optional<int>
      {
        if (limit <= pool->m_current_schedule_count)
        {
          throw test_exception_t();
        }
        return limit;
      };

      pool->m_throw_at_schedule.and_then(check_and_throw);
      pool->m_current_schedule_count++;

      pool->m_threads.emplace_back(
          [p_pool = pool, p_callback = callback](std::stop_token stop_token)
          {
            CF_PROFILE_SCOPE_N("simple_thread_pool_t::tag_invoke::schedule");
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
    simple_thread_pool_t() = default;
    simple_thread_pool_t(const simple_thread_pool_t&) = delete;
    simple_thread_pool_t(simple_thread_pool_t&&) = delete;

    simple_thread_pool_t& operator=(const simple_thread_pool_t&) = delete;
    simple_thread_pool_t& operator=(simple_thread_pool_t&&) = delete;

    ~simple_thread_pool_t()
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
void handle_error(simple_thread_pool_t&& thread_pool)
{
  std::vector<std::exception_ptr> errors = thread_pool.clear_errors();
  if (errors.empty())
  {
    return;
  }
  std::ostringstream os;
  os << "Error count: " << errors.size() << std::endl;
  for (std::exception_ptr ex : errors)
  {
    try
    {
      std::rethrow_exception(ex);
    }
    catch (const std::exception& e)
    {
      os << "Error: " << e.what() << std::endl;
    }
  }
  FAIL("Error occurred in thread pool" + os.str());
}

} // namespace coroutine_flow::__details::testing
