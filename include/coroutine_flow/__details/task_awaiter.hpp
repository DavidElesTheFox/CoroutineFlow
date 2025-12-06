#pragma once
#include <coroutine_flow/__details/coroutine_chain.hpp>

#include <coroutine>

namespace coroutine_flow::__details
{
template <typename T, template <typename> typename Extension>
struct task_promise_t;

template <typename T,
          template <typename> typename Extension,
          coroutine_chain_holder other_promise_type>
struct task_awaiter_t
{
    using promise_t = task_promise_t<T, Extension>;
    std::coroutine_handle<promise_t> current_handle;
    std::coroutine_handle<other_promise_type> suspended_handle;
    bool has_been_suspended = true;

    bool await_ready()
    {
      using injection_point = __details::testing::test_injection_points_t;
      CF_PROFILE_SCOPE();
      CF_TEST_INJECTION(injection_point::task__await_ready__begin,
                        suspended_handle.address());
      scope_exit_t await_ready_ends = [&]() noexcept
      {
        CF_TEST_INJECTION(injection_point::task__await_ready__end,
                          suspended_handle.address());
      };

      // Clear the flag at the start of each await operation to ensure
      // it's in the correct initial state. This must happen before any
      // checks to avoid race conditions with the callback.
      suspended_handle.promise().suspended_handle_resumed.clear(
          std::memory_order_release);

      if (current_handle.done())
      {
        CF_ATTACH_NOTE("async call is already finished");

        const bool already_ready =
            suspended_handle.promise().suspended_handle_resumed.test_and_set(
                std::memory_order_acq_rel);
        // when we are saying: we are ready, it measn that we are not
        // suspended
        has_been_suspended = already_ready == false;
        CF_ATTACH_NOTE("suspended handle is ready? ", already_ready);
        CF_TEST_INJECTION(
            injection_point::task__await_ready__after_test_and_set,
            suspended_handle.address());

        return already_ready;
      }

      return false;
    }
    T await_resume()
    {
      CF_PROFILE_SCOPE();
      CF_ATTACH_NOTE("caused async call a suspend? ", has_been_suspended);
      CF_ATTACH_NOTE("Async task: ", current_handle.address());

      promise_t& promise = current_handle.promise();
      promise.result_stored.wait(false, std::memory_order_relaxed);
      std::atomic_thread_fence(std::memory_order_acquire);

      auto result = std::move(promise.result);
      if (has_been_suspended == false)
      {
        promise.internal_release();
        if (promise.external_referenced == false)
        {
          promise.wait_for_ready_to_release();
          current_handle.destroy();
        }
      }
      if (result.has_value())
      {
        if constexpr (std::is_same_v<T, void>)
        {
        }
        else if constexpr (std::movable<T>)
        {
          return std::move(result->value());
        }
        else
        {
          return result->value();
        }
      }
      else
      {
        std::rethrow_exception(result.error());
      }
    }
    bool await_suspend(
        std::coroutine_handle<other_promise_type> suspended_handle)
    {
      CF_PROFILE_SCOPE();
      using injection_point = __details::testing::test_injection_points_t;
      if (current_handle.done())
      {
        CF_ATTACH_NOTE("Async call is finished");

        const bool already_ready =
            suspended_handle.promise().suspended_handle_resumed.test_and_set(
                std::memory_order_acq_rel);
        has_been_suspended = already_ready == false;

        CF_ATTACH_NOTE("Has been resumed? ", already_ready);
        CF_TEST_INJECTION(
            injection_point::task__await_suspend__after_test_and_set,
            suspended_handle.address());

        if (already_ready)
        {
          // current_handle.destroy();
          return false;
        }
      }
      assert(suspended_handle.address() == this->suspended_handle.address());

      other_promise_type& promise = suspended_handle.promise();
      CF_ATTACH_NOTE("Suspended promise", suspended_handle.address());
      // Flag was already cleared in await_ready(), so we can safely store
      // the suspended handle now
      promise.get_coroutine_chain().store_suspended_handle(suspended_handle);
      return true;
    }
};
} // namespace coroutine_flow::__details