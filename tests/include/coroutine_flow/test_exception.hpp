#pragma once

#include <stdexcept>

struct test_exception_t : std::runtime_error
{
    test_exception_t()
        : std::runtime_error("This exception is a test exception.")
    {
    }
};