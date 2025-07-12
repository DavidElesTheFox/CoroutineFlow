#pragma once

#include <future>
#include <utility>

namespace coroutine_flow::__details::testing
{
class event_t
{
  public:
    class token_t
    {
      public:
        explicit token_t(event_t& event)
            : m_triggered_future(event.m_triggered.get_future())
        {
        }

        bool is_triggered(const std::chrono::milliseconds& timeout) const
        {
          if (const auto future_status = m_triggered_future.wait_for(timeout);
              future_status == std::future_status::ready)
          {
            return true;
          }
          else
          {
            return false;
          }
        }

      private:
        std::future<bool> m_triggered_future;
    };

    static std::tuple<event_t, event_t::token_t>
        create(std::string_view event_name)
    {
      event_t result{ event_name };
      auto event_token = result.get_token();
      return { std::move(result), std::move(event_token) };
    }
    explicit event_t(std::string_view name)
        : m_name(name)
    {
    }

    token_t get_token() { return token_t{ *this }; }

    void trigger()
    {
      if (std::exchange(m_once_triggered, true) == false)
      {
        m_triggered.set_value(true);
      }
    }

  private:
    std::string m_name;
    std::promise<bool> m_triggered;
    bool m_once_triggered{ false };
};

} // namespace coroutine_flow::__details::testing