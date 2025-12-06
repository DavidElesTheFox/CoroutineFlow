#pragma once

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
    using call_token_t = std::move_only_function<T()>;
    promise_extension_base_t() = default;
    promise_extension_base_t(promise_extension_base_t&&) = default;
    promise_extension_base_t& operator=(promise_extension_base_t&&) = default;
    std::unique_ptr<std::promise<T>> result_promise{
      std::make_unique<std::promise<T>>()
    };
};
template <typename T>
struct promise_extension_t : promise_extension_base_t<T>
{
    void operator()(std::expected<std::optional<T>, std::exception_ptr> result)
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
    typename promise_extension_base_t<T>::call_token_t get_call_token()
    {
      return [future = this->result_promise->get_future()] mutable
      { return std::move(future).get(); };
    }
};

template <>
struct promise_extension_t<void> : promise_extension_base_t<void>
{
    void operator()(std::expected<void, std::exception_ptr> result)
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
    promise_extension_base_t::call_token_t get_call_token()
    {
      return [future = result_promise->get_future().share()] mutable
      { future.get(); };
    }
};

} // namespace coroutine_flow::extensions