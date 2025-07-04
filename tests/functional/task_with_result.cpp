#include <coroutine_flow/__details/testing/execution_flow_controller.hpp>
#include <coroutine_flow/__details/testing/simple_thread_pool.hpp>
#include <coroutine_flow/__details/testing/test_injection.hpp>
#include <coroutine_flow/task.hpp>

#include <catch2/catch_test_macros.hpp>

namespace cf = coroutine_flow;
using points_t = cf::__details::testing::test_injection_points_t;
using flow_controller_t =
    cf::__details::testing::execution_flow_controller<points_t>;
using test_injection_dispatcher_t =
    cf::__details::testing::test_injection_dispatcher_t;

using cf::__details::testing::simple_thread_pool_t;
using cf::__details::testing::test_exception_t;

// TODO: test case: sync_wait destroys coroutine and then
// suspended_handle_continued called.
namespace
{
void register_flow_control_for(points_t point,
                               flow_controller_t& flow_controller)
{
  test_injection_dispatcher_t::instance().register_callback(
      point,
      [&](void* object) { flow_controller.touch(point, object); });
}
} // namespace

SCENARIO("smart await")
{
  GIVEN("Coroutine 'A' that calls 'B'")
  {
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
      std::promise<void*> coro_A_address;
      std::shared_future<void*> coro_A_address_future =
          coro_A_address.get_future();
      std::promise<void*> coro_B_address;
      std::shared_future<void*> coro_B_address_future =
          coro_B_address.get_future();
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

      register_flow_control_for(points_t::task__run_async__before_test_and_set,
                                flow_controller);
      register_flow_control_for(points_t::task__run_async__async_call_finished,
                                flow_controller);
      register_flow_control_for(points_t::task__await_ready__after_test_and_set,
                                flow_controller);
      register_flow_control_for(points_t::task__await_ready__begin,
                                flow_controller);

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
        simple_thread_pool_t thread_pool;
        cf::sync_wait(coro_A(), &thread_pool);
        REQUIRE(called_A);
        REQUIRE(called_B);
        SUCCEED(
            "Test was not blocked, thus it went according to the defined flow");
      }
    }
    WHEN("'B' finishes while 'A' is suspended")
    {
      std::promise<void*> coro_A_address;
      std::shared_future<void*> coro_A_address_future =
          coro_A_address.get_future();
      std::promise<void*> coro_B_address;
      std::shared_future<void*> coro_B_address_future =
          coro_B_address.get_future();
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
      register_flow_control_for(points_t::task__run_async__before_test_and_set,
                                flow_controller);
      register_flow_control_for(points_t::task__run_async__async_call_finished,
                                flow_controller);
      register_flow_control_for(points_t::task__await_ready__after_test_and_set,
                                flow_controller);
      register_flow_control_for(
          points_t::task__await_suspend__after_test_and_set,
          flow_controller);
      flow_controller.append({ "'A' end of await ready",
                               points_t::task__await_ready__after_test_and_set,
                               coro_A_address_future });
      flow_controller.append({ "'B' finished",
                               points_t::task__run_async__async_call_finished,
                               coro_B_address_future });
      flow_controller.append({ "'A' decided not suspend",
                               points_t::task__await_ready__after_test_and_set,
                               coro_A_address_future });
      flow_controller.append({ "A trys to continue suspended handle",
                               points_t::task__run_async__before_test_and_set,
                               coro_A_address_future });
      THEN("During execute everything should be called")
      {
        simple_thread_pool_t thread_pool;
        cf::sync_wait(coro_A(), &thread_pool);
        REQUIRE(called_A);
        REQUIRE(called_B);
        SUCCEED(
            "Test was not blocked, thus it went according to the defined flow");
      }
    }
  }
}

SCENARIO("sync wait")
{
  GIVEN("Coroutine 'A' that calls 'B'")
  {
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

    WHEN("'B' finished and finishes 'A', but sync waits want to destroy first "
         "'A' sync wait needs to wait")
    {
      std::promise<void*> coro_A_address;
      std::shared_future<void*> coro_A_address_future =
          coro_A_address.get_future();
      std::promise<void*> coro_B_address;
      std::shared_future<void*> coro_B_address_future =
          coro_B_address.get_future();
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

      register_flow_control_for(points_t::task__sync_wait__has_result,
                                flow_controller);
      register_flow_control_for(points_t::task__sync_wait__handle_destroy,
                                flow_controller);
      register_flow_control_for(
          points_t::task__run_async__after_resume_suspended,
          flow_controller);
      register_flow_control_for(
          points_t::task__run_async__after_released_suspended,
          flow_controller);

      flow_controller.append(
          { "B finished and A also finished",
            points_t::task__run_async__after_resume_suspended,
            coro_A_address_future });
      flow_controller.append({ "Sync wait has the result",
                               points_t::task__sync_wait__has_result,
                               coro_A_address_future });
      flow_controller.append(
          { "A is not used anymore",
            points_t::task__run_async__after_released_suspended,
            coro_A_address_future });
      flow_controller.append({ "A destroyed",
                               points_t::task__sync_wait__handle_destroy,
                               coro_A_address_future });
      THEN("During execute everything should be called")
      {
        simple_thread_pool_t thread_pool;
        cf::sync_wait(coro_A(), &thread_pool);
        REQUIRE(called_A);
        REQUIRE(called_B);
        SUCCEED(
            "Test was not blocked, thus it went according to the defined flow");
      }
    }
  }
}
