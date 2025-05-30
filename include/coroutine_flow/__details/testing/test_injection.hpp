#pragma once

#include <coroutine_flow/lib_config.hpp>

#include <functional>
#include <unordered_map>
namespace coroutine_flow::__details::testing
{
enum class test_injection_points_t
{
  task__constructor,
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
  task__run_async__async_call_finished,
  task__run_async__before_test_and_set,
  task__run_async__after_test_and_set,
};

#if CF_ENABLE_INJECTIONS
class test_injection_dispatcher_t
{
  public:
    // Warning: do not use this in shared object
    static test_injection_dispatcher_t& instance()
    {
      static test_injection_dispatcher_t m_instance;
      return m_instance;
    }

    void touch(test_injection_points_t point, void* object)
    {
      for (const auto& callback : m_callbacks[point])
      {
        callback(object);
      }
    }

    void register_callback(test_injection_points_t at,
                           std::function<void(void*)> callback)
    {
      m_callbacks[at].push_back(std::move(callback));
    }
    void clear() { m_callbacks.clear(); }

  private:
    std::unordered_map<test_injection_points_t,
                       std::vector<std::function<void(void*)>>>
        m_callbacks;
};

#define TEST_INJECTION(point, object)                                          \
  ::coroutine_flow::__details::testing::test_injection_dispatcher_t::          \
      instance()                                                               \
          .touch(point, object)
#else
#define TEST_INJECTION(point, object)
#endif
} // namespace coroutine_flow::__details::testing