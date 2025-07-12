#pragma once

#include <coroutine_flow/__details/testing/test_injection.hpp>

namespace coroutine_flow::__details::testing
{
struct base_test_case_t
{
    base_test_case_t() { test_injection_dispatcher_t::instance().clear(); }
};
} // namespace coroutine_flow::__details::testing