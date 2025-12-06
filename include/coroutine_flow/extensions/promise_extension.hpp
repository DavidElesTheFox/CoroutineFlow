#pragma once

#include <coroutine_flow/result_wrapper.hpp>

#include <cassert>
#include <expected>
#include <functional>
#include <future>
#include <optional>
namespace coroutine_flow::extensions
{

template <typename T>
struct promise_extension_base_t
{
    struct call_token_t
    {
        std::future<T> result_future;
        void wait() { result_future.wait(); }

        T get_result() && { return result_future.get(); }
    };

    promise_extension_base_t() = default;
    promise_extension_base_t(promise_extension_base_t&&) = default;
    promise_extension_base_t& operator=(promise_extension_base_t&&) = default;
    std::unique_ptr<std::promise<T>> result_promise{
      std::make_unique<std::promise<T>>()
    };
    call_token_t get_call_token()
    {
      return { this->result_promise->get_future() };
    }
};
template <typename T>
struct promise_extension_t : promise_extension_base_t<T>
{
    void operator()(result_wrapper_t<T> result)
    {
      if (result.has_value())
      {
        assert(result->has_value());
        if constexpr (std::movable<T>)
        {
          this->result_promise->set_value(std::move(**result));
        }
        else
        {
          this->result_promise->set_value(**result);
        }
      }
      else
      {
        this->result_promise->set_exception(result.error());
      }
    }
};

template <>
struct promise_extension_t<void> : promise_extension_base_t<void>
{
    void operator()(result_wrapper_t<void> result)
    {
      if (result.has_value())
      {
        this->result_promise->set_value();
      }
      else
      {
        this->result_promise->set_exception(result.error());
      }
    }
};

} // namespace coroutine_flow::extensions