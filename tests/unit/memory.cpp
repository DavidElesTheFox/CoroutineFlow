#include <catch2/catch_test_macros.hpp>

#include <coroutine_flow/__details/scope_exit.hpp>
#include <coroutine_flow/__details/testing/base_test_case.hpp>
#include <coroutine_flow/__details/testing/memory_check.hpp>
#include <coroutine_flow/__details/testing/simple_thread_pool.hpp>
#include <coroutine_flow/__details/testing/test_config.hpp>

namespace cf = coroutine_flow;

using cf::__details::testing::base_test_case_t;
using cf::__details::testing::memory_check_t;
using cf::__details::testing::simple_thread_pool_t;

constexpr const auto c_test_case_timeout =
    cf::__details::testing::c_test_case_timeout;
struct on_exit_t
{
    ~on_exit_t() { callback(); }
    std::function<void()> callback;
};
TEST_CASE_METHOD(base_test_case_t,
                 "Check Destructor when not scheduled",
                 "[task]")
{
  memory_check_t memory_checker;
  {
    bool coroutine_state_destroyed = false;
    {
      auto coro = [](on_exit_t&& checker) -> cf::task<int> { co_return 2; };
      coro({ [&] { coroutine_state_destroyed = true; } });
    }
    REQUIRE(coroutine_state_destroyed);
  }
  memory_checker.check();
}

TEST_CASE_METHOD(base_test_case_t, "Check Destructor when scheduled", "[task]")
{
  try
  {

    memory_check_t memory_checker;
    {
      CF_PROFILE_SCOPE();
      bool coroutine_state_destroyed = false;
      {
        CF_PROFILE_SCOPE_N("coro owner_scope");
        simple_thread_pool_t thread_pool;
        auto coro = [](on_exit_t&& checker) -> cf::task<int>
        {
          CF_PROFILE_SCOPE_N("coro");
          co_return 2;
        };
        cf::sync_wait(coro({ [&] { coroutine_state_destroyed = true; } }),
                      &thread_pool);
        handle_error(std::move(thread_pool));
      }
      REQUIRE(coroutine_state_destroyed);
    }
    memory_checker.check();
  }
  catch (const std::exception& e)
  {

    FAIL("error occured");
  }
}