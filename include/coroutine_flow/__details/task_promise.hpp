#pragma once

#include <coroutine_flow/__details/final_coroutine.hpp>
#include <coroutine_flow/__details/final_executor.hpp>
#include <coroutine_flow/__details/task_promise.hpp>

#include <atomic>
#include <cassert>
#include <coroutine>
#include <exception>
#include <functional>

namespace coroutine_flow
{
template <typename T, template <typename> typename Extension>
class task_t;
}

namespace coroutine_flow::__details
{

template <typename P, typename T>
class return_policy_t
{
  public:
    template <typename R>
      requires std::is_move_constructible_v<std::remove_reference_t<R>> &&
               std::is_constructible_v<T, R&&>
    void return_value(R&& val)
    {
      CF_PROFILE_SCOPE();
      self()->result = std::optional<T>(std::forward<R>(val));
      self()->on_result_set();
      CF_ATTACH_NOTE("handle: ", P::handle_t::from_promise(*this).address());
    }
    template <typename R>
      requires std::is_copy_constructible_v<std::remove_reference_t<R>> &&
               std::is_constructible_v<T, const R&>
    void return_value(const R& val)
    {
      CF_PROFILE_SCOPE();
      self()->result = std::optional<T>(val);
      self()->on_result_set();
      CF_ATTACH_NOTE("handle: ", P::handle_t::from_promise(*this).address());
    }
    template <typename R>
      requires std::is_constructible_v<T, R*>
    void return_value(R* val)
    {
      CF_PROFILE_SCOPE();
      self()->result = std::optional<T>(val);
      self()->on_result_set();
      CF_ATTACH_NOTE("handle: ", P::handle_t::from_promise(*this).address());
    }

  protected:
    constexpr P* self() { return static_cast<P*>(this); }
};

template <typename P>
class return_policy_t<P, void>
{
  public:
    void return_void()
    {
      CF_PROFILE_SCOPE();
      self()->on_result_set();
      CF_ATTACH_NOTE("handle: ", P::handle_t::from_promise(*this).address());
    }

  protected:
    constexpr P* self() { return static_cast<P*>(this); }
};

template <typename T, template <typename> typename Extension>
struct task_promise_t : return_policy_t<task_promise_t<T, Extension>, T>
{
    using handle_t = std::coroutine_handle<task_promise_t>;

    result_wrapper_t<T> result;
    std::atomic_flag result_stored;
    std::atomic_flag suspended_handle_resumed;
    std::atomic_flag suspend_started;

    std::coroutine_handle<> finalizer;

    std::function<void(std::function<void()>)> schedule_callback;
    coroutine_chain_t<task_promise_t> coroutine_chain;

    /**
     * this token is a callable token, that returns with the
     * coroutine result after execution.
     *
     * @warning the object is valid only after schedule call.
     */
    typename Extension<T>::call_token_t extension_token;

    std::atomic_flag extension_token_available{ false };
    /**
     * Extension that will handle the final result.
     *
     * @warning Extension is invalid after the schedule call.
     */
    final_executor_t<T, Extension<T>> extension;
    bool execute_extension{ false };
    /**
     * When the promise is externally referenced during the final suspend
     * it won't let fall through the final_coroutine (the extension). Thus,
     * the hanlde and the promise will be still alive and can be referenced
     * after the execution is finished.
     *
     * When it is false the promise will be destroyed when the exection is
     * finished. It is done by the final coroutine which is let fall_through
     * and when the final coroutine is allowed to fall through it destroys the
     * suspended coroutine (this)
     */
    bool external_referenced{ true };
    /**
     * coroutine chain always checks whether the suspended handle is done or
     * not. Even if it externall referenced it does this check, but it can be
     * that at that time the coroutine handle (and its promise) already
     * destroyed externally (by the sync wait.). Although, it doesn't
     * necessary means that it is a crash it is an undefined behaviour, that
     * might lead to a crash.
     *
     * This flag tells to the external user whether this promise is still
     * referenced and used or not. Thus, it can be checked before destroy.
     *
     * By default it is false, because initially it is not part of any
     * coroutine chain. The chain is created when the task is co_awaited, so
     * when run_async is called. It becomes unreferenced when the done() flag
     * is checked by the chain.
     */
    std::atomic_flag internal_referenced{ false };

    /**
     * release can be called only when this flag is true, until that, the
     * coroutine might be still used in the final_coroutine.
     */
    std::atomic_flag ready_to_release{ false };
#if CF_ENABLE_INJECTIONS
    task_promise_t()
    {
      CF_TEST_INJECTION(testing::test_injection_points_t::object__construct,
                        this);
    }
#endif
    ~task_promise_t()
    {
      CF_PROFILE_SCOPE();
      CF_ATTACH_NOTE("handle", handle_t::from_promise(*this).address());
      CF_TEST_INJECTION(testing::test_injection_points_t::object__destruct,
                        this);
      assert(ready_to_release.test());
      if (finalizer)
      {
        finalizer.destroy();
      }
    }

    continuation_data& get_next() { return coroutine_chain.get_next(); }
    void set_next(continuation_data next)
    {
      coroutine_chain.set_next(std::move(next));
    }

    bool has_external_reference() const noexcept { return external_referenced; }
    void internal_release() noexcept
    {
      internal_referenced.clear(std::memory_order_release);
      internal_referenced.notify_all();
    }
    // TODO: Rename it to set_finalizer_and_release
    void set_finalizer(std::coroutine_handle<> value)
    {
      assert(!finalizer);
      finalizer = value;
      internal_release();
    }

    void wait_for_ready_to_release()
    {
      ready_to_release.wait(false, std::memory_order_acquire);
    }
    coroutine_chain_t<task_promise_t>& get_coroutine_chain()
    {
      return coroutine_chain;
    }
    task_t<T, Extension> get_return_object()
    {
      CF_PROFILE_SCOPE();
      return task_t<T, Extension>{ handle_t::from_promise(*this) };
    }
    std::suspend_always initial_suspend() noexcept
    {
      CF_PROFILE_SCOPE();
      return {};
    }
    auto final_suspend() noexcept
    {
      CF_PROFILE_SCOPE();
      const bool destroy_handle = external_referenced == false;
      CF_ATTACH_NOTE("destroy handle", destroy_handle);

      if (execute_extension)
      {
        extension_token = extension.get_extension().get_call_token();
        extension_token_available.test_and_set(std::memory_order_release);
        extension_token_available.notify_all();
        /*
         No need for memory synchronization because return_value should be
         written on the same thread where final_suspend
        */
        if constexpr (std::movable<T>)
        {
          auto owned_extension = std::exchange(extension, {});
          auto owned_result = std::move(result);

          // internal_release();
          if (destroy_handle)
          {
            return final_executor_t<T, Extension<T>>::execute_on(
                {},
                std::move(owned_extension),
                std::move(owned_result));
          }
          else
          {
            return final_executor_t<T, Extension<T>>::execute_on(
                std::move(owned_extension),
                std::move(owned_result));
          }
        }
        else
        {
          auto owned_extension = std::move(extension);
          auto owned_result = result;
          // internal_release();
          if (destroy_handle)
          {
            return final_executor_t<T, Extension<T>>::execute_on(
                {},
                std::move(owned_extension),
                owned_result);
          }
          else
          {
            return final_executor_t<T, Extension<T>>::execute_on(
                std::move(owned_extension),
                owned_result);
          }
        }
      }
      else
      {
        auto owned_extension = std::move(extension);
        // internal_release();
        return final_executor_t<T, Extension<T>>::skip(
            std::exchange(owned_extension, {}));
      }
    }

    void unhandled_exception()
    {
      CF_PROFILE_SCOPE();
      result = std::unexpected(std::current_exception());
      on_result_set();
      CF_ATTACH_NOTE("handle: ", handle_t::from_promise(*this).address());
    }
    template <typename U>
    auto await_transform(task_t<U, Extension> task)
    {
      CF_PROFILE_SCOPE();
      return task.run_async_impl(schedule_callback, this);
    }

    void on_result_set()
    {
      std::atomic_thread_fence(std::memory_order_release);
      result_stored.test_and_set(std::memory_order_relaxed);
      result_stored.notify_all();
    }
};

} // namespace coroutine_flow::__details