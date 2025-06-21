#pragma once

#include <coroutine_flow/__details/continuation_data.hpp>
#include <coroutine_flow/__details/testing/test_injection.hpp>
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
    struct fall_through_t
    {
    };

  protected:
    struct final_awaiter
    {
        bool fall_through{ false };
        bool await_ready() noexcept { return fall_through; }

        void await_resume() noexcept {}

        void await_suspend(std::coroutine_handle<>) noexcept {}
    };
    struct awaiter_t
    {
        continuation_data handle;
        bool destroy_suspended_handle{ false };
        bool await_ready() noexcept { return false; }

        void await_resume() noexcept {}

        template <continuable_promise other_promise_type>
        std::coroutine_handle<> await_suspend(
            std::coroutine_handle<other_promise_type> suspended_handle) noexcept
        {
          CF_PROFILE_SCOPE();
          // Handle can be empty when basically it is a null extension.
          assert(handle.is_empty() == false);
          assert(handle.coro.done() == false &&
                 "The final coroutine shouldn't be done already.");
          assert(
              suspended_handle.done() &&
              "The final handle will not continue the suspended handle. Thus "
              "we expect that it is already finished, but not destroyed.");

          CF_ATTACH_NOTE("suspended_handle: ", suspended_handle.address());
          CF_ATTACH_NOTE("handle: ", handle.coro.address());
          CF_ATTACH_NOTE("destroy_suspended_handle", destroy_suspended_handle);
          /*
          Do not store continuation here. This is a final coroutine. It
          means that the suspended handle won't be continued by this
          coroutine.

          auto suspended_data =
              continuation_data::create_data(suspended_handle);
          handle.set_next(std::move(suspended_data));
          */
          /*
          Here we delete the suspended handle. It will delete its extension but
          it is ok, while during final_suspend it moves it into the coroutine
          frame. And we are right now in that coroutine frame and just about to
          run the coroutine.
           */
          if (destroy_suspended_handle)
          {
            suspended_handle.destroy();
          }
          else
          {
            // ensure that current coroutine is destroyed
            suspended_handle.promise().set_finalizer(handle.coro);
          }
          auto coro = handle.coro;
          return coro;
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
    /**
     * When this is true the final_coroutine will fall through and it will
     * destroy automatically this promise. Also, it will propagate the suspended
     * handle and it will destroy the suspended coroutine (the extension) as
     * well.
     */
    bool fall_through{ false };
    // final coroutine should never be referenced externally
    bool has_external_reference() const noexcept { return false; }

    promise_t()
    {
      CF_PROFILE_SCOPE_N("final_coroutine_t::promise_t::promise_t");
      CF_ATTACH_NOTE("this", this);
      CF_TEST_INJECTION(testing::test_injection_points_t::object__construct,
                        this);
    }
    template <typename T, typename... Args>
    promise_t(T&, fall_through_t, Args&&...) noexcept
        : fall_through(true)
    {
      CF_PROFILE_SCOPE_N(
          "final_coroutine_t::promise_t::promise_t[fall_through]");
      CF_ATTACH_NOTE("this", this);
      CF_TEST_INJECTION(testing::test_injection_points_t::object__construct,
                        this);
    }
    template <typename... Args>
    promise_t(fall_through_t, Args&&...) noexcept
        : fall_through(true)
    {
      CF_PROFILE_SCOPE_N(
          "final_coroutine_t::promise_t::promise_t[fall_through]");
      CF_ATTACH_NOTE("this", this);
      CF_TEST_INJECTION(testing::test_injection_points_t::object__construct,
                        this);
    }

    ~promise_t()
    {
      CF_PROFILE_SCOPE();
      CF_ATTACH_NOTE("this", this);
      CF_TEST_INJECTION(testing::test_injection_points_t::object__destruct,
                        this);
    }
    final_coroutine_t get_return_object()
    {
      return final_coroutine_t(handle_t::from_promise(*this));
    }

    continuation_data& get_next() { return next; }
    void set_next(continuation_data&& value) { next = std::move(value); }
    void internal_release() noexcept {};

    std::suspend_always initial_suspend() { return {}; }
    final_awaiter final_suspend() noexcept { return { fall_through }; }
    void return_void() {}
    void unhandled_exception() { std::abort(); }

    void set_finalizer(std::coroutine_handle<>)
    {
      assert(false && "Final coroutine should be the final and never a "
                      "suspended coroutine");
    }
};
final_coroutine_t::awaiter_t final_coroutine_t::operator co_await() noexcept
{
  return { .handle = continuation_data::create_data(m_handle),
           .destroy_suspended_handle = m_handle.promise().fall_through };
}

} // namespace coroutine_flow::__details
