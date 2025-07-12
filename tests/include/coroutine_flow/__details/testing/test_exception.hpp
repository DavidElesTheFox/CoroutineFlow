#pragma once

#include <stdexcept>

namespace coroutine_flow::__details::testing
{
struct test_exception_t : std::runtime_error
{
    test_exception_t()
        : std::runtime_error("This exception is a test exception.")
    {
    }
};
} // namespace coroutine_flow::__details::testing