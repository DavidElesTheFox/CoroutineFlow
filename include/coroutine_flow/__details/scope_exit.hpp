#pragma once

#include <concepts>
#include <functional>

namespace coroutine_flow
{
namespace __details
{
  template <typename callable_t>
    requires std::invocable<callable_t>
  struct [[nodiscard]] scope_exit_t
  {
      std::move_only_function<void() noexcept> callback;
      scope_exit_t() = delete;
      scope_exit_t(callable_t&& callback)
          : callback(std::move(callback))
      {
      }
      ~scope_exit_t() { callback(); }
  };
} // namespace __details
} // namespace coroutine_flow