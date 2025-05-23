#pragma once

#include <coroutine>
#include <functional>

namespace coroutine_flow::__details
{

struct continuation_data;
template <typename T>
concept continuable_promise = requires(T promise, continuation_data data) {
  { promise.set_next(std::move(data)) };
  { promise.get_next() } -> std::same_as<continuation_data&>;
};

struct continuation_data
{
    std::coroutine_handle<> coro;
    std::move_only_function<void(continuation_data) noexcept> set_next;
    std::move_only_function<continuation_data&() noexcept> get_next;

    bool is_empty() const { return set_next == nullptr; }
    void clear()
    {
      set_next = nullptr;
      get_next = nullptr;
      coro = nullptr;
    }

    template <continuable_promise other_promise_type>
    static continuation_data
        create_data(std::coroutine_handle<other_promise_type> handler) noexcept
    {
      continuation_data result;
      result.coro = handler;
      result.set_next = [=](continuation_data continuation_data) noexcept
      {
        other_promise_type& promise = handler.promise();
        promise.set_next(std::move(continuation_data));
      };
      result.get_next = [=]() noexcept -> continuation_data&
      {
        other_promise_type& promise = handler.promise();
        return promise.get_next();
      };
      return result;
    }
};
} // namespace coroutine_flow::__details