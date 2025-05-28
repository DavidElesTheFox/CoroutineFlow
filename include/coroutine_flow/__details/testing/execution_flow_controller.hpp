#pragma once

#include <coroutine_flow/__details/testing/test_injection.hpp>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
namespace coroutine_flow::__details::testing
{
template <typename enum_t>
  requires std::is_scoped_enum_v<enum_t>
class execution_flow_controller
{
  public:
    // WARNING do not use this in shared object
    static execution_flow_controller& instance()
    {
      static execution_flow_controller m_instance;
      return m_instance;
    }
    class thread_safety_token_t
    {
      public:
        friend class execution_flow_controller;
        thread_safety_token_t() = default;

        operator bool() const { return m_lock.owns_lock(); }

        bool owns_lock() const { return m_lock.owns_lock(); }

      private:
        explicit thread_safety_token_t(std::unique_lock<std::mutex>&& lock)
            : m_lock(std::move(lock))
        {
        }
        std::unique_lock<std::mutex> m_lock;
    };
    struct event_t
    {
        std::string name;
        enum_t point;
        void* object;
        std::function<void(thread_safety_token_t)> callback;
    };

    execution_flow_controller& insert_after(
        std::string_view name,
        event_t event,
        const thread_safety_token_t& safety_token = thread_safety_token_t{})
    {
      std::unique_lock lock(m_event_flow_mutex, std::defer_lock);
      if (safety_token == false)
      {
        lock.lock();
      }
      auto it = std::ranges::find_if(m_event_flow,
                                     [&](const event_t& e)
                                     { return e.name == name; });
      if (it == m_event_flow.end())
      {
        using namespace std::string_literals;
        throw std::runtime_error("Can't find event: "s + name.data());
      }

      m_event_flow.insert(it + 1, std::move(event));
      // notifying while holding the lock shouldn't be an issue because this
      // code doesn't used in performance dependent context. But it might be
      // chained together with other operations which needs to run in one
      // 'transaction' holding the thread_safety_token. For functional tests
      // determism got higher priority than performance.
      m_event_flow_condition.notify_all();

      return *this;
    }

    execution_flow_controller&
        append(event_t& event, const thread_safety_token_t& safety_token = {})
    {
      append({ event }, safety_token);
      return *this;
    }

    execution_flow_controller&
        append(std::initializer_list<event_t>&& events,
               const thread_safety_token_t& safety_token = {})
    {
      std::unique_lock lock(m_event_flow_mutex, std::defer_lock);
      if (safety_token == false)
      {
        lock.lock();
      }

      std::copy(events.begin(), events.end(), std::back_inserter(m_event_flow));
      // notifying while holding the lock shouldn't be an issue because this
      // code doesn't used in performance dependent context. But it might be
      // chained together with other operations which needs to run in one
      // 'transaction' holding the thread_safety_token. For functional tests
      // determism got higher priority than performance.
      m_event_flow_condition.notify_all();
      return *this;
    }

    bool touch(enum_t point, void* object)
    {
      {
        std::unique_lock lock(m_event_flow_mutex);
        if (m_event_flow.empty())
        {
          return false;
        }
        auto comperator = [&](const event_t& event)
        { return event.object == object && event.point == point; };

        if (comperator(m_event_flow.front()))
        {
          event_t current_event = m_event_flow.front();
          m_event_flow.pop_front();
          if (current_event.callback != nullptr)
          {
            current_event.callback(thread_safety_token_t{ std::move(lock) });
          }
          else
          {
            lock.unlock();
          }
          m_event_flow_condition.notify_all();
          return true;
        }
        if (std::ranges::find_if(m_event_flow, comperator) ==
            m_event_flow.end())
        {
          return false;
        }
        m_event_flow_condition.wait(lock,
                                    [&]()
                                    {
                                      return m_event_flow.empty() ||
                                             comperator(m_event_flow.front());
                                    });
        // in case of clear is called.
        if (m_event_flow.empty())
        {
          return false;
        }
        event_t current_event = m_event_flow.front();
        m_event_flow.pop_front();
        if (current_event.callback != nullptr)
        {
          current_event.callback(thread_safety_token_t{ std::move(lock) });
        }
      }
      m_event_flow_condition.notify_all();
      return true;
    }

    void clear()
    {
      {
        std::unique_lock lock(m_event_flow_mutex);
        m_event_flow.clear();
      }
      m_event_flow_condition.notify_all();
    }

  private:
    std::mutex m_event_flow_mutex;
    std::condition_variable m_event_flow_condition;
    std::deque<event_t> m_event_flow;
};
} // namespace coroutine_flow::__details::testing