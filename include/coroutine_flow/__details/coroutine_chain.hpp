#pragma once

#include <coroutine_flow/__details/scope_exit.hpp>
#include <coroutine_flow/profiler.hpp>

#include <atomic>
#include <coroutine>
#include <exception>
#include <functional>
#include <optional>
#include <utility>

namespace coroutine_flow::__details
{
template <typename promise_type>
class coroutine_chain_t;

template <typename promise_type>
concept coroutine_chain_holder = requires(promise_type promise) {
  {
    promise.get_coroutine_chain()
  } -> std::same_as<coroutine_chain_t<promise_type>&>;
};

template <typename promise_type>
class coroutine_chain_t
{
  private:
    std::optional<std::coroutine_handle<promise_type>> m_suspended_handle;
    std::atomic_flag m_suspended_handle_stored;
    continuation_data m_next;

  public:
    template <typename other_promise_type>
    friend class coroutine_chain_t;

    continuation_data& get_next() { return m_next; }
    void set_next(continuation_data value) { m_next = std::move(value); }

    std::optional<std::coroutine_handle<promise_type>> reset_suspended_handle()
    {
      auto result = std::exchange(m_suspended_handle, std::nullopt);
      std::atomic_thread_fence(std::memory_order_release);
      m_suspended_handle_stored.clear(std::memory_order_relaxed);
      m_suspended_handle_stored.notify_all();
      return result;
    }

    void continue_suspended_handle()
    {
      CF_PROFILE_SCOPE();

      m_suspended_handle_stored.wait(false, std::memory_order_relaxed);
      std::atomic_thread_fence(std::memory_order_acquire);

      auto suspended_handle = reset_suspended_handle();
      suspended_handle.value().resume();
      if (suspended_handle->done() == false)
      {
        return;
      }
      std::vector<std::coroutine_handle<>> handles_to_destroy;
      handles_to_destroy.push_back(*suspended_handle);
      auto destroy_suspended_at_end =
          scope_exit_t{ [&]() noexcept
                        {
                          for (auto handle : handles_to_destroy)
                          {
                            handle.destroy();
                          }
                        } };

      continuation_data current = std::exchange(m_next, {});
      while (current.is_empty() == false)
      {
        CF_PROFILE_ZONE(SetNext, "Continue next");
        CF_ATTACH_NOTE("coro: ", current.coro.address());

        current.coro();
        if (current.coro.done())
        {
          CF_ATTACH_NOTE("Is done");
          handles_to_destroy.push_back(current.coro);
          current = std::exchange(current.get_next(), {});
        }
        else
        {
          CF_ATTACH_NOTE("Is not done");
          break;
        }
      }
    }

    template <coroutine_chain_holder other_promise_type>
    void move_into(coroutine_chain_t<other_promise_type>& o) noexcept
    {
      CF_PROFILE_SCOPE();

      m_suspended_handle_stored.wait(false, std::memory_order_relaxed);
      std::atomic_thread_fence(std::memory_order_acquire);
      CF_ATTACH_NOTE("current:", m_suspended_handle->address());
      CF_ATTACH_NOTE("next:", o.m_next.coro.address());

      auto suspended_data =
          continuation_data::create_data(*reset_suspended_handle());
      suspended_data.set_next(std::exchange(m_next, {}));
      o.m_next = std::move(suspended_data);
    }

    void store_suspended_handle(
        std::coroutine_handle<promise_type> suspended_handle) noexcept
    {

      CF_PROFILE_ZONE(suspend_wait, "Suspend store");

      m_suspended_handle = suspended_handle;
      std::atomic_thread_fence(std::memory_order_release);
      m_suspended_handle_stored.test_and_set(std::memory_order_relaxed);
      m_suspended_handle_stored.notify_all();
    }
};

} // namespace coroutine_flow::__details