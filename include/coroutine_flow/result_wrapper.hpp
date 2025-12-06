#pragma once

#include <exception>
#include <expected>
#include <optional>
#include <type_traits>

namespace coroutine_flow
{

template <typename T>
using result_wrapper_t =
    std::conditional_t<std::is_same_v<T, void>,
                       std::expected<void, std::exception_ptr>,
                       std::expected<std::optional<T>, std::exception_ptr>>;
}