#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <coroutine_flow/__details/testing/base_test_case.hpp>
#include <coroutine_flow/__details/testing/event.hpp>
#include <coroutine_flow/__details/testing/inline_scheduler.hpp>
#include <coroutine_flow/__details/testing/memory_check.hpp>
#include <coroutine_flow/__details/testing/memory_sentinel.hpp>
#include <coroutine_flow/__details/testing/simple_thread_pool.hpp>
#include <coroutine_flow/__details/testing/test_config.hpp>

#include <coroutine_flow/profiler.hpp>
#include <coroutine_flow/task.hpp>

namespace cf = coroutine_flow;
using namespace std::chrono_literals;

using cf::__details::testing::base_test_case_t;
using cf::__details::testing::event_t;
using cf::__details::testing::inline_scheduler_t;
using cf::__details::testing::memory_check_t;
using cf::__details::testing::simple_thread_pool_t;
using cf::__details::testing::test_exception_t;

constexpr const auto c_test_case_timeout =
    cf::__details::testing::c_test_case_timeout;

struct async_execution_policy_t
{
    template <typename scheduler_t, typename R>
    auto run_task(scheduler_t* scheduler, cf::task<R>&& task)
    {
      cf::run_async(std::move(task), scheduler);
    }
};

struct sync_execution_policy_t
{
    template <typename scheduler_t, typename R>
    auto run_task(scheduler_t* scheduler, cf::task<R>&& task)
    {
      return cf::sync_wait(std::move(task), scheduler);
    }
};

struct std_thread_pool_policy_t
{
    auto create_scheduler() { return simple_thread_pool_t(); }
};

struct inline_scheduler_policy_t
{
    auto create_scheduler() { return inline_scheduler_t(); }
};

template <typename... Ts>
struct union_of_t : Ts...
{
};

using async_inline_configuration_t =
    union_of_t<async_execution_policy_t, inline_scheduler_policy_t>;
using async_pool_configuration_t =
    union_of_t<async_execution_policy_t, std_thread_pool_policy_t>;

using sync_inline_configuration_t =
    union_of_t<sync_execution_policy_t, inline_scheduler_policy_t>;
using sync_pool_configuration_t =
    union_of_t<sync_execution_policy_t, std_thread_pool_policy_t>;
template <typename test_configuration>
struct test_controller_t
    : test_configuration
    , protected cf::__details::testing::base_test_case_t
{
};
TEMPLATE_TEST_CASE_METHOD(test_controller_t,
                          "Neasted coroutine level 1",
                          "[task]",
                          async_inline_configuration_t,
                          async_pool_configuration_t,
                          sync_inline_configuration_t,
                          sync_pool_configuration_t)
{
  memory_check_t memory_checker;
  {
    auto thread_pool = test_controller_t<TestType>::create_scheduler();
    auto [called_event, called_token] = event_t::create("coroutine is called");

    auto coro_1 = [p_called_event =
                       std::move(called_event)]() mutable -> cf::task<int>
    {
      p_called_event.trigger();
      co_return 1;
    };
    test_controller_t<TestType>::run_task(&thread_pool, coro_1());

    REQUIRE(called_token.is_triggered(c_test_case_timeout));
    handle_error(std::move(thread_pool));
  }
  memory_checker.check();
}

TEMPLATE_TEST_CASE_METHOD(test_controller_t,
                          "Neasted coroutine level 2",
                          "[task]",
                          async_inline_configuration_t,
                          async_pool_configuration_t,
                          sync_inline_configuration_t,
                          sync_pool_configuration_t)
{
  memory_check_t memory_checker;
  {
    auto thread_pool = test_controller_t<TestType>::create_scheduler();
    auto [called_event_1, called_token_1] =
        event_t::create("coroutine 1 is called");

    auto coro_1 = [p_called_event =
                       std::move(called_event_1)]() mutable -> cf::task<int>
    {
      CF_PROFILE_MARK("coro_1");
      p_called_event.trigger();
      co_return 1;
    };

    auto [called_event_2, called_token_2] =
        event_t::create("coroutine 2 is called");

    auto coro_2 = [p_called_event = std::move(called_event_2),
                   &coro_1]() mutable -> cf::task<int>
    {
      CF_PROFILE_MARK("coro_2_1");

      int result = co_await coro_1();
      CF_PROFILE_MARK("coro_2_2");
      REQUIRE(result == 1);
      p_called_event.trigger();
      co_return 2;
    };

    test_controller_t<TestType>::run_task(&thread_pool, coro_2());

    REQUIRE(called_token_1.is_triggered(c_test_case_timeout));
    REQUIRE(called_token_2.is_triggered(c_test_case_timeout));
    handle_error(std::move(thread_pool));
  }
  memory_checker.check();
}

TEMPLATE_TEST_CASE_METHOD(test_controller_t,
                          "Neasted coroutine level 3",
                          "[task]",
                          async_inline_configuration_t,
                          async_pool_configuration_t,
                          sync_inline_configuration_t,
                          sync_pool_configuration_t)
{
  memory_check_t memory_checker;
  {
    auto thread_pool = test_controller_t<TestType>::create_scheduler();

    // Coroutine 1
    auto [called_event_1, called_token_1] =
        event_t::create("coroutine 1 is called");

    auto coro_1 = [p_called_event =
                       std::move(called_event_1)]() mutable -> cf::task<int>
    {
      CF_PROFILE_MARK("coro_1");

      p_called_event.trigger();
      co_return 1;
    };

    // Coroutine 2
    auto [called_event_2, called_token_2] =
        event_t::create("coroutine 2 is called");

    auto coro_2 = [p_called_event = std::move(called_event_2),
                   &coro_1]() mutable -> cf::task<int>
    {
      CF_PROFILE_MARK("coro_2");

      int result = co_await coro_1();
      REQUIRE(result == 1);
      CF_PROFILE_MARK("coro_2-2");
      p_called_event.trigger();
      co_return 2;
    };

    // Coroutine 3
    auto [called_event_3, called_token_3] =
        event_t::create("coroutine 3 is called");

    auto coro_3 = [p_called_event = std::move(called_event_3),
                   &coro_2]() mutable -> cf::task<int>
    {
      CF_PROFILE_MARK("coro_3");

      int result = co_await coro_2();
      CF_PROFILE_MARK("coro_3-2");
      REQUIRE(result == 2);
      p_called_event.trigger();
      co_return 3;
    };

    test_controller_t<TestType>::run_task(&thread_pool, coro_3());

    REQUIRE(called_token_1.is_triggered(c_test_case_timeout));
    REQUIRE(called_token_2.is_triggered(c_test_case_timeout));
    REQUIRE(called_token_3.is_triggered(c_test_case_timeout));
    handle_error(std::move(thread_pool));
  }
  memory_checker.check();
}

TEMPLATE_TEST_CASE_METHOD(test_controller_t,
                          "Neasted coroutine level 4",
                          "[task]",
                          async_inline_configuration_t,
                          async_pool_configuration_t,
                          sync_inline_configuration_t,
                          sync_pool_configuration_t)
{
  memory_check_t memory_checker;
  {
    auto thread_pool = test_controller_t<TestType>::create_scheduler();

    // Coroutine 1
    auto [called_event_1, called_token_1] =
        event_t::create("coroutine 1 is called");

    auto coro_1 = [p_called_event =
                       std::move(called_event_1)]() mutable -> cf::task<int>
    {
      CF_PROFILE_MARK("coro_1");
      p_called_event.trigger();
      co_return 1;
    };

    // Coroutine 2
    auto [called_event_2, called_token_2] =
        event_t::create("coroutine 2 is called");

    auto coro_2 = [p_called_event = std::move(called_event_2),
                   &coro_1]() mutable -> cf::task<int>
    {
      CF_PROFILE_MARK("coro_2-0");
      int result = co_await coro_1();
      CF_PROFILE_MARK("coro_2-1");
      REQUIRE(result == 1);
      p_called_event.trigger();
      co_return 2;
    };

    // Coroutine 3
    auto [called_event_3, called_token_3] =
        event_t::create("coroutine 3 is called");

    auto coro_3 = [p_called_event = std::move(called_event_3),
                   &coro_2]() mutable -> cf::task<int>
    {
      CF_PROFILE_MARK("coro_3-0");
      int result = co_await coro_2();
      CF_PROFILE_MARK("coro_3-1");
      REQUIRE(result == 2);
      p_called_event.trigger();
      co_return 3;
    };
    // Coroutine 4
    auto [called_event_4, called_token_4] =
        event_t::create("coroutine 4 is called");
    auto coro_4 = [p_called_event = std::move(called_event_4),
                   &coro_3]() mutable -> cf::task<int>
    {
      CF_PROFILE_MARK("coro_4-0");
      int result = co_await coro_3();
      CF_PROFILE_MARK("coro_4-1");
      REQUIRE(result == 3);
      p_called_event.trigger();
      co_return 4;
    };

    test_controller_t<TestType>::run_task(&thread_pool, coro_4());

    REQUIRE(called_token_1.is_triggered(c_test_case_timeout));
    REQUIRE(called_token_2.is_triggered(c_test_case_timeout));
    REQUIRE(called_token_3.is_triggered(c_test_case_timeout));
    REQUIRE(called_token_4.is_triggered(c_test_case_timeout));
    handle_error(std::move(thread_pool));
  }
  memory_checker.check();
}

TEMPLATE_TEST_CASE_METHOD(test_controller_t,
                          "Neasted coroutine level 2; waits 2",
                          "[task]",
                          async_inline_configuration_t,
                          async_pool_configuration_t,
                          sync_inline_configuration_t,
                          sync_pool_configuration_t)
{
  CF_PROFILE_SCOPE();
  memory_check_t memory_checker;
  {
    auto thread_pool = test_controller_t<TestType>::create_scheduler();
    auto [called_event_1, called_token_1] =
        event_t::create("coroutine 1 is called");
    std::atomic_uint32_t coro_1_call_count = 0;
    auto coro_1 = [p_called_event = &called_event_1,
                   &coro_1_call_count]() mutable -> cf::task<int>
    {
      CF_PROFILE_MARK("coro_1");
      coro_1_call_count++;
      p_called_event->trigger();
      co_return 1;
    };

    auto [called_event_2, called_token_2] =
        event_t::create("coroutine 2 is called");
    std::atomic_uint32_t coro_2_call_count = 0;

    auto coro_2 = [p_called_event = std::move(called_event_2),
                   &coro_1,
                   &p_coro_call_count =
                       coro_2_call_count]() mutable -> cf::task<int>
    {
      CF_PROFILE_MARK("coro_2 01");
      int result = co_await coro_1();
      CF_PROFILE_MARK("coro_2 02");
      REQUIRE(result == 1);
      result = co_await coro_1();
      CF_PROFILE_MARK("coro_2 03");
      REQUIRE(result == 1);
      p_coro_call_count++;
      p_called_event.trigger();
      co_return 2;
    };

    test_controller_t<TestType>::run_task(&thread_pool, coro_2());

    REQUIRE(called_token_1.is_triggered(c_test_case_timeout));
    REQUIRE(called_token_2.is_triggered(c_test_case_timeout));
    REQUIRE(coro_1_call_count == 2);
    REQUIRE(coro_2_call_count == 1);
    handle_error(std::move(thread_pool));
  }
  memory_checker.check();
}

TEMPLATE_TEST_CASE_METHOD(test_controller_t,
                          "Neasted coroutine level 3; waits 2",
                          "[task]",
                          async_inline_configuration_t,
                          async_pool_configuration_t,
                          sync_inline_configuration_t,
                          sync_pool_configuration_t)
{

  CF_PROFILE_SCOPE();
  memory_check_t memory_checker;
  {
    auto thread_pool = test_controller_t<TestType>::create_scheduler();

    // Coroutine 1
    auto [called_event_1, called_token_1] =
        event_t::create("coroutine 1 is called");
    std::atomic_uint32_t coro_1_call_count = 0;

    auto coro_1 = [p_called_event = std::move(called_event_1),
                   &p_coro_call_count =
                       coro_1_call_count]() mutable -> cf::task<int>
    {
      CF_PROFILE_MARK("coro_1");
      p_coro_call_count++;
      p_called_event.trigger();
      co_return 1;
    };

    // Coroutine 2
    auto [called_event_2, called_token_2] =
        event_t::create("coroutine 2 is called");
    std::atomic_uint32_t coro_2_call_count = 0;

    auto coro_2 = [p_called_event = std::move(called_event_2),
                   &coro_1,
                   &p_coro_call_count =
                       coro_2_call_count]() mutable -> cf::task<int>
    {
      CF_PROFILE_MARK("coro_2 01");

      int result = co_await coro_1();
      CF_PROFILE_MARK("coro_2 02");

      REQUIRE(result == 1);
      result = co_await coro_1();
      CF_PROFILE_MARK("coro_2 03");

      REQUIRE(result == 1);
      p_coro_call_count++;
      p_called_event.trigger();
      co_return 2;
    };

    // Coroutine 3
    auto [called_event_3, called_token_3] =
        event_t::create("coroutine 3 is called");
    std::atomic_uint32_t coro_3_call_count = 0;

    auto coro_3 = [p_called_event = std::move(called_event_3),
                   &coro_2,
                   &p_coro_call_count =
                       coro_3_call_count]() mutable -> cf::task<int>
    {
      CF_PROFILE_MARK("coro_3 01");

      int result = co_await coro_2();
      CF_PROFILE_MARK("coro_3 02");

      REQUIRE(result == 2);
      result = co_await coro_2();
      CF_PROFILE_MARK("coro_3 03");

      REQUIRE(result == 2);
      p_coro_call_count++;
      p_called_event.trigger();

      co_return 3;
    };

    test_controller_t<TestType>::run_task(&thread_pool, coro_3());

    REQUIRE(called_token_1.is_triggered(c_test_case_timeout));
    REQUIRE(called_token_2.is_triggered(c_test_case_timeout));
    REQUIRE(called_token_3.is_triggered(c_test_case_timeout));
    REQUIRE(coro_1_call_count == 4);
    REQUIRE(coro_2_call_count == 2);
    REQUIRE(coro_3_call_count == 1);
    handle_error(std::move(thread_pool));
  }
  memory_checker.check();
}

TEMPLATE_TEST_CASE_METHOD(test_controller_t,
                          "Neasted coroutine level 4; waits 2",
                          "[task]",
                          async_inline_configuration_t,
                          async_pool_configuration_t,
                          sync_inline_configuration_t,
                          sync_pool_configuration_t)
{
  CF_PROFILE_SCOPE();
  memory_check_t memory_checker;
  {

    auto thread_pool = test_controller_t<TestType>::create_scheduler();

    // Coroutine 1
    auto [called_event_1, called_token_1] =
        event_t::create("coroutine 1 is called");
    std::atomic_uint32_t coro_1_call_count = 0;

    auto coro_1 = [p_called_event = std::move(called_event_1),
                   &p_coro_call_count =
                       coro_1_call_count]() mutable -> cf::task<int>
    {
      CF_PROFILE_MARK("coro_1");
      p_coro_call_count++;
      p_called_event.trigger();
      co_return 1;
    };

    // Coroutine 2
    auto [called_event_2, called_token_2] =
        event_t::create("coroutine 2 is called");
    std::atomic_uint32_t coro_2_call_count = 0;

    auto coro_2 = [p_called_event = std::move(called_event_2),
                   &coro_1,
                   &p_coro_call_count =
                       coro_2_call_count]() mutable -> cf::task<int>
    {
      CF_PROFILE_MARK("coro_2 01");

      int result = co_await coro_1();
      CF_PROFILE_MARK("coro_2 02");

      REQUIRE(result == 1);
      result = co_await coro_1();
      CF_PROFILE_MARK("coro_2 03");

      REQUIRE(result == 1);
      p_coro_call_count++;
      p_called_event.trigger();
      co_return 2;
    };

    // Coroutine 3
    auto [called_event_3, called_token_3] =
        event_t::create("coroutine 3 is called");
    std::atomic_uint32_t coro_3_call_count = 0;

    auto coro_3 = [p_called_event = std::move(called_event_3),
                   &coro_2,
                   &p_coro_call_count =
                       coro_3_call_count]() mutable -> cf::task<int>
    {
      CF_PROFILE_MARK("coro_3 01");

      int result = co_await coro_2();
      CF_PROFILE_MARK("coro_3 02");

      REQUIRE(result == 2);
      result = co_await coro_2();
      CF_PROFILE_MARK("coro_3 03");

      REQUIRE(result == 2);
      p_coro_call_count++;
      p_called_event.trigger();
      co_return 3;
    };

    // Coroutine 4
    auto [called_event_4, called_token_4] =
        event_t::create("coroutine 4 is called");
    std::atomic_uint32_t coro_4_call_count = 0;

    auto coro_4 = [p_called_event = std::move(called_event_4),
                   &coro_3,
                   &p_coro_call_count =
                       coro_4_call_count]() mutable -> cf::task<int>
    {
      CF_PROFILE_MARK("coro_4 01");

      int result = co_await coro_3();
      REQUIRE(result == 3);
      result = co_await coro_3();
      CF_PROFILE_MARK("coro_4 03");
      REQUIRE(result == 3);
      p_coro_call_count++;
      p_called_event.trigger();
      co_return 3;
    };

    test_controller_t<TestType>::run_task(&thread_pool, coro_4());

    REQUIRE(called_token_1.is_triggered(c_test_case_timeout));
    REQUIRE(called_token_2.is_triggered(c_test_case_timeout));
    REQUIRE(called_token_3.is_triggered(c_test_case_timeout));
    REQUIRE(called_token_4.is_triggered(c_test_case_timeout));
    REQUIRE(coro_1_call_count == 8);
    REQUIRE(coro_2_call_count == 4);
    REQUIRE(coro_3_call_count == 2);
    REQUIRE(coro_4_call_count == 1);
    handle_error(std::move(thread_pool));
  }
  memory_checker.check();
}

TEMPLATE_TEST_CASE_METHOD(test_controller_t,
                          "Neasted coroutine level 2; waits 3",
                          "[task]",
                          async_inline_configuration_t,
                          async_pool_configuration_t,
                          sync_inline_configuration_t,
                          sync_pool_configuration_t)

{
  memory_check_t memory_checker;
  {
    auto thread_pool = test_controller_t<TestType>::create_scheduler();
    auto [called_event_1, called_token_1] =
        event_t::create("coroutine 1 is called");
    std::atomic_uint32_t coro_1_call_count = 0;
    auto coro_1 = [p_called_event = &called_event_1,
                   &coro_1_call_count]() mutable -> cf::task<int>
    {
      coro_1_call_count++;
      p_called_event->trigger();
      co_return 1;
    };

    auto [called_event_2, called_token_2] =
        event_t::create("coroutine 2 is called");
    std::atomic_uint32_t coro_2_call_count = 0;

    auto coro_2 = [p_called_event = std::move(called_event_2),
                   &coro_1,
                   &p_coro_call_count =
                       coro_2_call_count]() mutable -> cf::task<int>
    {
      int result = co_await coro_1();
      REQUIRE(result == 1);
      result = co_await coro_1();
      REQUIRE(result == 1);
      result = co_await coro_1();
      REQUIRE(result == 1);
      p_coro_call_count++;
      p_called_event.trigger();
      co_return 2;
    };

    test_controller_t<TestType>::run_task(&thread_pool, coro_2());

    REQUIRE(called_token_1.is_triggered(c_test_case_timeout));
    REQUIRE(called_token_2.is_triggered(c_test_case_timeout));
    REQUIRE(coro_1_call_count == 3);
    REQUIRE(coro_2_call_count == 1);
    handle_error(std::move(thread_pool));
  }
  memory_checker.check();
}
TEMPLATE_TEST_CASE_METHOD(test_controller_t,
                          "Neasted coroutine level 3; waits 3",
                          "[task]",
                          async_inline_configuration_t,
                          async_pool_configuration_t,
                          sync_inline_configuration_t,
                          sync_pool_configuration_t)
{
  CF_PROFILE_SCOPE();
  memory_check_t memory_checker;
  {
    auto thread_pool = test_controller_t<TestType>::create_scheduler();

    // Coroutine 1
    auto [called_event_1, called_token_1] =
        event_t::create("coroutine 1 is called");
    std::atomic_uint32_t coro_1_call_count = 0;

    auto coro_1 = [p_called_event = std::move(called_event_1),
                   &p_coro_call_count =
                       coro_1_call_count]() mutable -> cf::task<int>
    {
      CF_PROFILE_MARK("coro_1");
      p_coro_call_count++;
      p_called_event.trigger();
      co_return 1;
    };

    // Coroutine 2
    auto [called_event_2, called_token_2] =
        event_t::create("coroutine 2 is called");
    std::atomic_uint32_t coro_2_call_count = 0;

    auto coro_2 = [p_called_event = std::move(called_event_2),
                   &coro_1,
                   &p_coro_call_count =
                       coro_2_call_count]() mutable -> cf::task<int>
    {
      CF_PROFILE_MARK("coro_2 01");

      int result = co_await coro_1();
      CF_PROFILE_MARK("coro_2 02");
      REQUIRE(result == 1);

      result = co_await coro_1();
      CF_PROFILE_MARK("coro_2 03");
      REQUIRE(result == 1);

      result = co_await coro_1();
      CF_PROFILE_MARK("coro_2 04");
      REQUIRE(result == 1);

      p_coro_call_count++;
      p_called_event.trigger();
      co_return 2;
    };

    // Coroutine 3
    auto [called_event_3, called_token_3] =
        event_t::create("coroutine 3 is called");
    std::atomic_uint32_t coro_3_call_count = 0;

    auto coro_3 = [p_called_event = std::move(called_event_3),
                   &coro_2,
                   &p_coro_call_count =
                       coro_3_call_count]() mutable -> cf::task<int>
    {
      CF_PROFILE_MARK("coro_3 01");

      int result = co_await coro_2();
      CF_PROFILE_MARK("coro_3 02");
      REQUIRE(result == 2);

      result = co_await coro_2();
      CF_PROFILE_MARK("coro_3 03");
      REQUIRE(result == 2);

      result = co_await coro_2();
      CF_PROFILE_MARK("coro_3 04");
      REQUIRE(result == 2);

      p_coro_call_count++;
      p_called_event.trigger();

      co_return 3;
    };

    test_controller_t<TestType>::run_task(&thread_pool, coro_3());

    REQUIRE(called_token_1.is_triggered(c_test_case_timeout));
    REQUIRE(called_token_2.is_triggered(c_test_case_timeout));
    REQUIRE(called_token_3.is_triggered(c_test_case_timeout));
    REQUIRE(coro_1_call_count == 9);
    REQUIRE(coro_2_call_count == 3);
    REQUIRE(coro_3_call_count == 1);
    handle_error(std::move(thread_pool));
  }
  memory_checker.check();
}

TEMPLATE_TEST_CASE_METHOD(test_controller_t,
                          "Neasted coroutine level 4; waits 3",
                          "[task]",
                          async_inline_configuration_t,
                          async_pool_configuration_t,
                          sync_inline_configuration_t,
                          sync_pool_configuration_t)
{
  CF_PROFILE_SCOPE();

  memory_check_t memory_checker;
  {
    auto thread_pool = test_controller_t<TestType>::create_scheduler();

    // Coroutine 1
    auto [called_event_1, called_token_1] =
        event_t::create("coroutine 1 is called");
    std::atomic_uint32_t coro_1_call_count = 0;

    auto coro_1 = [p_called_event = std::move(called_event_1),
                   &p_coro_call_count =
                       coro_1_call_count]() mutable -> cf::task<int>
    {
      CF_PROFILE_MARK("coro_1");
      p_coro_call_count++;
      p_called_event.trigger();
      co_return 1;
    };

    // Coroutine 2
    auto [called_event_2, called_token_2] =
        event_t::create("coroutine 2 is called");
    std::atomic_uint32_t coro_2_call_count = 0;

    auto coro_2 = [p_called_event = std::move(called_event_2),
                   &coro_1,
                   &p_coro_call_count =
                       coro_2_call_count]() mutable -> cf::task<int>
    {
      CF_PROFILE_MARK("coro_2 01");

      int result = co_await coro_1();
      CF_PROFILE_MARK("coro_2 02");
      REQUIRE(result == 1);

      result = co_await coro_1();
      CF_PROFILE_MARK("coro_2 03");
      REQUIRE(result == 1);

      result = co_await coro_1();
      CF_PROFILE_MARK("coro_2 04");
      REQUIRE(result == 1);

      p_coro_call_count++;
      p_called_event.trigger();
      co_return 2;
    };

    // Coroutine 3
    auto [called_event_3, called_token_3] =
        event_t::create("coroutine 3 is called");
    std::atomic_uint32_t coro_3_call_count = 0;

    auto coro_3 = [p_called_event = std::move(called_event_3),
                   &coro_2,
                   &p_coro_call_count =
                       coro_3_call_count]() mutable -> cf::task<int>
    {
      CF_PROFILE_MARK("coro_3 01");

      int result = co_await coro_2();
      CF_PROFILE_MARK("coro_3 02");
      REQUIRE(result == 2);

      result = co_await coro_2();
      CF_PROFILE_MARK("coro_3 03");
      REQUIRE(result == 2);

      result = co_await coro_2();
      CF_PROFILE_MARK("coro_3 04");
      REQUIRE(result == 2);

      p_coro_call_count++;
      p_called_event.trigger();
      co_return 3;
    };

    // Coroutine 4
    auto [called_event_4, called_token_4] =
        event_t::create("coroutine 4 is called");
    std::atomic_uint32_t coro_4_call_count = 0;

    auto coro_4 = [p_called_event = std::move(called_event_4),
                   &coro_3,
                   &p_coro_call_count =
                       coro_4_call_count]() mutable -> cf::task<int>
    {
      CF_PROFILE_MARK("coro_4 01");

      int result = co_await coro_3();
      CF_PROFILE_MARK("coro_4 02");
      REQUIRE(result == 3);

      result = co_await coro_3();
      CF_PROFILE_MARK("coro_4 03");
      REQUIRE(result == 3);

      result = co_await coro_3();
      CF_PROFILE_MARK("coro_4 04");
      REQUIRE(result == 3);

      p_coro_call_count++;
      p_called_event.trigger();
      co_return 3;
    };

    test_controller_t<TestType>::run_task(&thread_pool, coro_4());

    REQUIRE(called_token_1.is_triggered(c_test_case_timeout));
    REQUIRE(called_token_2.is_triggered(c_test_case_timeout));
    REQUIRE(called_token_3.is_triggered(c_test_case_timeout));
    REQUIRE(called_token_4.is_triggered(c_test_case_timeout));
    REQUIRE(coro_1_call_count == 27);
    REQUIRE(coro_2_call_count == 9);
    REQUIRE(coro_3_call_count == 3);
    REQUIRE(coro_4_call_count == 1);
    handle_error(std::move(thread_pool));
  }
  memory_checker.check();
}

TEMPLATE_TEST_CASE_METHOD(test_controller_t,
                          "Mixed data",
                          "[task]",
                          async_inline_configuration_t,
                          async_pool_configuration_t,
                          sync_inline_configuration_t,
                          sync_pool_configuration_t)
{
  CF_PROFILE_SCOPE();

  memory_check_t memory_checker;
  {
    auto thread_pool = test_controller_t<TestType>::create_scheduler();
    auto [called_event_1, called_token_1] =
        event_t::create("coroutine 1 is called");
    auto [called_event_2, called_token_2] =
        event_t::create("coroutine 2 is called");
    auto [called_event_3, called_token_3] =
        event_t::create("coroutine 3 is called");
    auto coro_1 = [event = std::move(called_event_1)]() mutable -> cf::task<int>
    {
      CF_PROFILE_MARK("coro_1");
      event.trigger();
      co_return 2;
    };

    auto coro_2 =
        [event = std::move(called_event_2)]() mutable -> cf::task<std::string>
    {
      CF_PROFILE_MARK("coro_2");
      event.trigger();
      co_return "42";
    };

    auto coro_3 = [&,
                   event = std::move(called_event_3)]() mutable -> cf::task<int>
    {
      CF_PROFILE_MARK("coro_3 01");
      int number = co_await coro_1();
      CF_PROFILE_MARK("coro_3 02");
      std::string str = co_await coro_2();
      CF_PROFILE_MARK("coro_3 03");
      REQUIRE(number == 2);
      REQUIRE(str == "42");
      event.trigger();
      co_return 2;
    };

    test_controller_t<TestType>::run_task(&thread_pool, coro_3());
    REQUIRE(called_token_1.is_triggered(c_test_case_timeout));
    REQUIRE(called_token_2.is_triggered(c_test_case_timeout));
    REQUIRE(called_token_3.is_triggered(c_test_case_timeout));
    handle_error(std::move(thread_pool));
  }
  memory_checker.check();
}

TEST_CASE_METHOD(base_test_case_t, "Check get function", "[task]")
{
  memory_check_t memory_checker;
  {
    simple_thread_pool_t thread_pool;
    auto coro = []() -> cf::task<int> { co_return 2; };
    auto result = cf::sync_wait(coro(), &thread_pool);
    REQUIRE(result == 2);
    handle_error(std::move(thread_pool));
  }
  memory_checker.check();
}