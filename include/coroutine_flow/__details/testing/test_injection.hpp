#pragma once

#include <coroutine_flow/lib_config.hpp>

namespace coroutine_flow::__details::testing
{
enum class test_injection_points_t
{
  task__await_ready__begin,
  task__await_ready__before_test_and_set,
  task__await_ready__after_test_and_set,

  task__await_resume__before_wait,

  task__await_suspend__begin,
  task__await_suspend__before_test_and_set,
  task__await_suspend__after_test_and_set,
  task__await_suspend__before_store_handle,
  task__await_suspend__after_store_handle,

  task__return_value__before_test_and_set,
  task__return_value__after_test_and_set,
  task__run_async__before_async_handle,
  task__run_async__before_test_and_set,
  task__run_async__after_test_and_set,
};
} // namespace coroutine_flow::__details::testing