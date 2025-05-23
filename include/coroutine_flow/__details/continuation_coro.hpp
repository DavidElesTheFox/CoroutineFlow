#pragma once

#include <coroutine_flow/__details/continuation_data.hpp>

#include <coroutine>
#include <utility>

namespace coroutine_flow::__details
{

class coroutine_extension
{
  protected:
    struct promise_t;

  public:
    using handle_t = std::coroutine_handle<promise_t>;
    using promise_type = promise_t;

  protected:
    struct awaiter_t
    {
        continuation_data handle;
        bool await_ready() noexcept { return false; }
        void await_resume() noexcept {}

        template <continuable_promise other_promise_type>
        std::coroutine_handle<> await_suspend(
            std::coroutine_handle<other_promise_type> suspended_handle) noexcept
        {
          if (handle.is_empty() == false && handle.coro.done() == false)
          {
            auto suspended_data =
                continuation_data::create_data(suspended_handle);
            handle.set_next(std::move(suspended_data));
            auto coro = handle.coro;
            handle.clear();
            return coro;
          }
          else if (suspended_handle.done() == false)
          {
            return suspended_handle;
          }
          else
          {
            return std::noop_coroutine();
          }
        }
    };

  public:
    explicit coroutine_extension(handle_t handle)
        : m_handle(std::move(handle))
    {
    }

    awaiter_t operator co_await() noexcept;

  private:
    handle_t m_handle;
};

struct coroutine_extension::promise_t
{
    continuation_data next;
    coroutine_extension get_return_object()
    {
      return coroutine_extension(handle_t::from_promise(*this));
    }

    continuation_data& get_next() { return next; }
    void set_next(continuation_data&& value) { next = std::move(value); }

    std::suspend_always initial_suspend() { return {}; }
    awaiter_t final_suspend() noexcept { return { std::exchange(next, {}) }; }
    void return_void() {}
    void unhandled_exception() { std::abort(); }
};
coroutine_extension::awaiter_t coroutine_extension::operator co_await() noexcept
{
  return { continuation_data::create_data(m_handle) };
}

} // namespace coroutine_flow::__details
