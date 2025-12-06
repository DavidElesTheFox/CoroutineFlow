#pragma once

#include <coroutine_flow/__details/continuation_data.hpp>
#include <coroutine_flow/__details/coroutine_chain.hpp>
#include <coroutine_flow/__details/final_coroutine.hpp>
#include <coroutine_flow/__details/task_awaiter.hpp>
#include <coroutine_flow/__details/task_promise.hpp>
#include <coroutine_flow/__details/testing/test_injection.hpp>
#include <coroutine_flow/profiler.hpp>
#include <coroutine_flow/result_wrapper.hpp>
#include <coroutine_flow/tag_invoke.hpp>

#include <atomic>
#include <cassert>
#include <coroutine>
#include <exception>
#include <functional>

#include <iostream>
#include <stacktrace>

namespace coroutine_flow
{

struct schedule_task_t
{
};
template <typename T, template <typename> typename Extension>
class task_t;
namespace __details
{

} // namespace __details

template <typename T, template <typename> typename Extension>
class task_t
{
  protected:
    template <__details::coroutine_chain_holder other_promise_type>
    using awaiter_t =
        __details::task_awaiter_t<T, Extension, other_promise_type>;

    using promise_t = __details::task_promise_t<T, Extension>;
    using handle_t = std::coroutine_handle<promise_t>;

    template <typename R, template <typename> typename E>
    friend struct __details::task_promise_t;

    friend struct __details::task_promise_t<T, Extension>;

  public:
    using promise_type = promise_t;
    explicit task_t(handle_t&& coro_handle)
        : m_coro_handle(std::move(coro_handle))
    {
      CF_PROFILE_SCOPE();
      using injection_point = __details::testing::test_injection_points_t;
      CF_TEST_INJECTION(injection_point::task__constructor,
                        coro_handle.address());
      CF_TEST_INJECTION(injection_point::object__construct, this);
    }

    void* address() { return m_coro_handle.address(); }

    ~task_t()
    {
      if (m_coro_handle)
      {
        m_coro_handle.promise().ready_to_release.test_and_set();
        m_coro_handle.destroy();
      }
      CF_TEST_INJECTION(
          __details::testing::test_injection_points_t::object__destruct,
          this);
    }

    template <typename U, template <typename> typename E, typename scheduler_t>
      requires(
          std::copyable<scheduler_t> &&
          is_tag_invocable<schedule_task_t, scheduler_t, std::function<void()>>)
    friend void run_async(task_t<U, E>&& task, scheduler_t scheduler);

    template <typename U, template <typename> typename E, typename scheduler_t>
      requires(
          std::copyable<scheduler_t> &&
          is_tag_invocable<schedule_task_t, scheduler_t, std::function<void()>>)
    friend U sync_wait(task_t<U, E>&& task, scheduler_t scheduler);

  private:
    template <typename scheduler_t>
      requires(
          std::copyable<scheduler_t> &&
          is_tag_invocable<schedule_task_t, scheduler_t, std::function<void()>>)
    handle_t schedule(scheduler_t scheduler, bool keep_handle_alive)
    {
      CF_PROFILE_SCOPE();
      get_promise().schedule_callback =
          [p_scheduler = scheduler](std::function<void()> callback)
      { tag_invoke(schedule_task_t{}, p_scheduler, std::move(callback)); };
      m_coro_handle.promise().execute_extension = true;
      m_coro_handle.promise().external_referenced = keep_handle_alive;
      tag_invoke(schedule_task_t{},
                 scheduler,
                 [p_current_handle = m_coro_handle] { p_current_handle(); });
      return std::exchange(m_coro_handle, {});
    }

    template <__details::coroutine_chain_holder other_promise_t>
    awaiter_t<other_promise_t> run_async_impl(
        std::function<void(std::function<void()>)> schedule_callback,
        other_promise_t* suspended_promise)
    {
      using injection_point = __details::testing::test_injection_points_t;
      CF_PROFILE_SCOPE();
      m_coro_handle.promise().external_referenced = false;

      get_promise().schedule_callback = schedule_callback;

      // Don't clear suspended_handle_resumed here - it should only be cleared
      // after we know the coroutine will actually suspend (in await_suspend)
      suspended_promise->internal_referenced.test_and_set(
          std::memory_order_release);
      suspended_promise->suspend_started.clear(std::memory_order_release);

      schedule_callback(
          [p_coro_handle = m_coro_handle,
           p_suspended_promise = suspended_promise]() mutable
          {
            CF_PROFILE_SCOPE_N("Task::AsyncRun");
            CF_ATTACH_NOTE("Executed handle", p_coro_handle.address());
            CF_ATTACH_NOTE("Context's handle",
                           std::coroutine_handle<other_promise_t>::from_promise(
                               *p_suspended_promise)
                               .address());

            p_coro_handle();
            CF_TEST_INJECTION(
                injection_point::task__run_async__async_call_finished,
                p_coro_handle.address());
            CF_TEST_INJECTION(
                injection_point::task__run_async__before_test_and_set,
                std::coroutine_handle<other_promise_t>::from_promise(
                    *p_suspended_promise)
                    .address());
            /*
            It is a tricky problem what if when this function call blocks the
            co_await so it never start to awaiting, thus suspended never gonna
            be stored. In this situation continue_suspended_handle() can't be
            called, because it will be a deadlock. We need to say somehow that
            co_await is really started. And when it is really started we can
            check weather the suspended_handle resumed or not.
            */

            const bool suspend_started =
                p_suspended_promise->suspend_started.test(
                    std::memory_order_acquire);
            if (suspend_started == false)
            {
              CF_ATTACH_NOTE("suspended handle were not started.");
              return;
            }
            if (p_coro_handle.done())
            {
              CF_PROFILE_ZONE(HandleDone, "Handle Done");
              const bool suspended_is_resumed =
                  p_suspended_promise->suspended_handle_resumed.test_and_set(
                      std::memory_order_acq_rel);
              if (suspended_is_resumed == false)
              {
                auto destroy_coro_at_end = __details::scope_exit_t{
                  [&]() noexcept
                  {
                    p_coro_handle.promise().wait_for_ready_to_release();
                    p_coro_handle.destroy();
                  }
                };

                p_suspended_promise->get_coroutine_chain()
                    .continue_suspended_handle();
              }
              else
              {
                CF_ATTACH_NOTE("suspended handle were not really suspended.");
                return;
              }
            }
            else
            {
              p_suspended_promise->get_coroutine_chain().move_into(
                  p_coro_handle.promise().get_coroutine_chain());
            }
          });

      awaiter_t<other_promise_t> result;
      result.current_handle = std::exchange(m_coro_handle, {});
      result.suspended_handle =
          std::coroutine_handle<other_promise_t>::from_promise(
              *suspended_promise);

      suspended_promise->suspend_started.test_and_set(
          std::memory_order_release);
      return result;
    }

    promise_t& get_promise() { return m_coro_handle.promise(); }

    handle_t m_coro_handle;
};

/****************************** run_async ***********************************/
template <typename T,
          template <typename> typename Extension,
          typename scheduler_t>
  requires(
      std::copyable<scheduler_t> &&
      is_tag_invocable<schedule_task_t, scheduler_t, std::function<void()>>)
void run_async(task_t<T, Extension>&& task, scheduler_t scheduler)
{
  constexpr const bool keep_handle_alive = false;
  std::move(task).schedule(scheduler, keep_handle_alive);
}

/****************************** sync_wait ***********************************/

template <typename T,
          template <typename> typename Extension,
          typename scheduler_t>
  requires(
      std::copyable<scheduler_t> &&
      is_tag_invocable<schedule_task_t, scheduler_t, std::function<void()>>)
T sync_wait(task_t<T, Extension>&& task, scheduler_t scheduler)
{
  constexpr const bool keep_handle_alive = true;

  auto handle = task.schedule(scheduler, keep_handle_alive);
  auto& coroutine_promise = handle.promise();
  [[maybe_unused]]
  auto* handle_address = handle.address();

  coroutine_promise.extension_token_available.wait(false,
                                                   std::memory_order_acquire);

  auto extension_token = std::move(coroutine_promise.extension_token);
  extension_token.wait();
  CF_TEST_INJECTION(
      __details::testing::test_injection_points_t::task__sync_wait__has_result,
      handle_address);
  handle.promise().internal_referenced.wait(true, std::memory_order_acquire);
  handle.promise().wait_for_ready_to_release();
  handle.destroy();
  CF_TEST_INJECTION(__details::testing::test_injection_points_t::
                        task__sync_wait__handle_destroy,
                    handle_address);
  return std::move(extension_token).get_result();
}

} // namespace coroutine_flow