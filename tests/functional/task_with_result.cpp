#include <coroutine_flow/__details/testing/execution_flow_controller.hpp>
#include <coroutine_flow/__details/testing/test_injection.hpp>
#include <coroutine_flow/simple_thread_pool.hpp>
#include <coroutine_flow/task.hpp>

#include <catch2/catch_test_macros.hpp>

/*
 - Functional
   - testing 'smart await'
   - testing 'waiting for result in await_resume'
   - testing 'waiting for stored result in async`
   - testing 'waiting return value' in promise
*/

namespace cf = coroutine_flow;
using points_t = cf::__details::testing::test_injection_points_t;
using flow_controller_t =
    cf::__details::testing::execution_flow_controller<points_t>;
using test_injection_dispatcher_t =
    cf::__details::testing::test_injection_dispatcher_t;

SCENARIO("smart await")
{

  GIVEN("Coroutine 'A' that calls 'B'")
  {
    std::promise<void*> coro_A_address;
    std::shared_future<void*> coro_A_address_future =
        coro_A_address.get_future();
    std::promise<void*> coro_B_address;
    std::shared_future<void*> coro_B_address_future =
        coro_B_address.get_future();

    std::atomic_bool called_A{ false };
    std::atomic_bool called_B{ false };

    auto coro_B = [&]() -> cf::task<int>
    {
      CF_PROFILE_MARK("B");
      called_B = true;
      co_return 2;
    };

    auto coro_A = [&]() mutable -> cf::task<int>
    {
      CF_PROFILE_MARK("A");
      co_await coro_B();
      CF_PROFILE_MARK("A continued");
      called_A = true;
      co_return 4;
    };

    WHEN("'B' finishes before 'A' is suspended")
    {
      flow_controller_t flow_controller;

      test_injection_dispatcher_t::instance().clear();
      test_injection_dispatcher_t::instance().register_callback(
          points_t::task__constructor,
          [&](void* object) mutable
          {
            static bool coro_a_is_stored = false;

            if (coro_a_is_stored == false)
            {
              coro_a_is_stored = true;
              coro_A_address.set_value(object);
            }
            else
            {
              coro_B_address.set_value(object);
            }
          });

      test_injection_dispatcher_t::instance().register_callback(
          points_t::task__run_async__before_test_and_set,
          [&](void* object)
          {
            flow_controller.touch(
                points_t::task__run_async__before_test_and_set,
                object);
          });
      test_injection_dispatcher_t::instance().register_callback(
          points_t::task__run_async__async_call_finished,
          [&](void* object)
          {
            flow_controller.touch(
                points_t::task__run_async__async_call_finished,
                object);
          });
      test_injection_dispatcher_t::instance().register_callback(
          points_t::task__await_ready__after_test_and_set,
          [&](void* object)
          {
            flow_controller.touch(
                points_t::task__await_ready__after_test_and_set,
                object);
          });
      test_injection_dispatcher_t::instance().register_callback(
          points_t::task__await_ready__begin,
          [&](void* object)
          {
            flow_controller.touch(points_t::task__await_ready__begin, object);
          });
      flow_controller.append({ "B finished",
                               points_t::task__run_async__async_call_finished,
                               coro_B_address_future });
      flow_controller.append({ "A before check ready",
                               points_t::task__await_ready__begin,
                               coro_A_address_future });
      flow_controller.append({ "A end of await ready",
                               points_t::task__await_ready__after_test_and_set,
                               coro_A_address_future });
      flow_controller.append({ "A trys to continue suspended handle",
                               points_t::task__run_async__before_test_and_set,
                               coro_A_address_future });
      THEN("During execute everything should be called")
      {
        simple_thread_pool thread_pool;
        coro_A().sync_wait(&thread_pool);
        REQUIRE(called_A);
        REQUIRE(called_B);
      }
    }
  }
}