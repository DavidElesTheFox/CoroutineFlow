#pragma once

#include <stdexcept>

struct TestException : std::runtime_error
{
    TestException()
        : std::runtime_error("This exception is a test exception.")
    {
    }
};