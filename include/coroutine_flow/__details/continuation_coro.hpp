#pragma once

#include <coroutine_flow/__details/continuation_data.hpp>
#include <coroutine_flow/profiler.hpp>

#include <cassert>
#include <coroutine>
#include <utility>

namespace coroutine_flow::__details
{

class final_coroutine_t
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
          CF_PROFILE_SCOPE();
          // Handle can be empty when basically it is a null extension.
          if (handle.is_empty() == false)
          {
            CF_PROFILE_SCOPE_N("handle not empty");
            assert(handle.coro.done() == false &&
                   "The final coroutine shouldn't be done already.");
            assert(
                suspended_handle.done() &&
                "The final handle will not continue the suspended handle. Thus "
                "we expect that it is already finished, but not destroyed.");

            CF_ATTACH_NOTE("suspended_handle: ", suspended_handle.address());
            CF_ATTACH_NOTE("handle: ", handle.coro.address());
            /*
            Do not store continuation here. This is a final coroutine. It
            means that the suspended handle won't be continued by this
            coroutine.

            auto suspended_data =
                continuation_data::create_data(suspended_handle);
            handle.set_next(std::move(suspended_data));
            */
            auto coro = handle.coro;
            handle.clear();
            return coro;
          }
          else
          {
            CF_PROFILE_SCOPE_N("NOOP");

            return std::noop_coroutine();
          }
        }
    };

  public:
    explicit final_coroutine_t(handle_t handle)
        : m_handle(std::move(handle))
    {
    }

    awaiter_t operator co_await() noexcept;

  private:
    handle_t m_handle;
};

struct final_coroutine_t::promise_t
{
    continuation_data next;
    final_coroutine_t get_return_object()
    {
      return final_coroutine_t(handle_t::from_promise(*this));
    }

    continuation_data& get_next() { return next; }
    void set_next(continuation_data&& value) { next = std::move(value); }

    std::suspend_always initial_suspend() { return {}; }
    awaiter_t final_suspend() noexcept { return { std::exchange(next, {}) }; }
    void return_void() {}
    void unhandled_exception() { std::abort(); }
};
final_coroutine_t::awaiter_t final_coroutine_t::operator co_await() noexcept
{
  return { continuation_data::create_data(m_handle) };
}

} // namespace coroutine_flow::__details
