#include <catch2/catch_test_macros.hpp>

#include <coroutine_flow/__details/scope_exit.hpp>
#include <coroutine_flow/__details/testing/base_test_case.hpp>
#include <coroutine_flow/__details/testing/event.hpp>
#include <coroutine_flow/__details/testing/memory_check.hpp>
#include <coroutine_flow/__details/testing/simple_thread_pool.hpp>
#include <coroutine_flow/__details/testing/test_config.hpp>
#include <coroutine_flow/__details/testing/test_exception.hpp>

namespace cf = coroutine_flow;

using cf::__details::testing::base_test_case_t;
using cf::__details::testing::event_t;
using cf::__details::testing::memory_check_t;
using cf::__details::testing::simple_thread_pool_t;
using cf::__details::testing::test_exception_t;

constexpr const auto c_test_case_timeout =
    cf::__details::testing::c_test_case_timeout;

template <bool throw_at_copy_constructor,
          bool throw_at_copy_assign,
          bool throw_at_move_constructor,
          bool throw_at_move_assign>
struct ThrowingClass
{
    ThrowingClass() = default;
    ThrowingClass(const ThrowingClass&)
    {
      if constexpr (throw_at_copy_constructor)
      {
        throw test_exception_t{};
      }
    }
    ThrowingClass(ThrowingClass&&)
    {
      if constexpr (throw_at_move_constructor)
      {
        throw test_exception_t{};
      }
    }
    ThrowingClass& operator=(const ThrowingClass&)
    {
      if constexpr (throw_at_copy_assign)
      {
        throw test_exception_t{};
      }
      return *this;
    }
    ThrowingClass& operator=(ThrowingClass&&)
    {
      if constexpr (throw_at_move_assign)
      {
        throw test_exception_t{};
      }
      return *this;
    }
};

TEST_CASE_METHOD(base_test_case_t, "Coroutine with exception", "[task]")
{
  memory_check_t memory_checker;
  {
    simple_thread_pool_t thread_pool;
    auto coro = []() -> cf::task<int>
    {
      throw test_exception_t{};
      co_return 2;
    };

    REQUIRE_THROWS_AS(cf::sync_wait(coro(), &thread_pool), test_exception_t);
    handle_error(std::move(thread_pool));
  }
  memory_checker.check();
}

TEST_CASE_METHOD(base_test_case_t,
                 "Coroutine with exception 2nd level",
                 "[task]")
{
  memory_check_t memory_checker;
  {
    simple_thread_pool_t thread_pool;
    auto [exception_forwarded_event, exception_forwarded_token] =
        event_t::create("Exception forwarded");

    auto coro = []() -> cf::task<int>
    {
      throw test_exception_t{};
      co_return 2;
    };

    auto coro_2 = [&]() mutable -> cf::task<int>
    {
      try
      {
        int result = co_await coro();
        co_return result + 1;
      }
      catch (const test_exception_t& e)
      {
        exception_forwarded_event.trigger();
        throw e;
      }
      co_return -1;
    };

    REQUIRE_THROWS_AS(cf::sync_wait(coro_2(), &thread_pool), test_exception_t);
    REQUIRE(exception_forwarded_token.is_triggered(c_test_case_timeout));
    handle_error(std::move(thread_pool));
  }
  memory_checker.check();
}

TEST_CASE_METHOD(base_test_case_t,
                 "Coroutine with exception 3rd level",
                 "[task]")
{
  memory_check_t memory_checker;
  {
    simple_thread_pool_t thread_pool;
    auto [exception_forwarded_event, exception_forwarded_token] =
        event_t::create("Exception forwarded");
    auto [exception_forwarded_2_event, exception_forwarded_2_token] =
        event_t::create("Exception forwarded 2");
    auto coro = []() -> cf::task<int>
    {
      throw test_exception_t{};
      co_return 2;
    };

    auto coro_2 = [&]() mutable -> cf::task<int>
    {
      try
      {
        int result = co_await coro();
        co_return result + 1;
      }
      catch (const test_exception_t& e)
      {
        exception_forwarded_event.trigger();
        throw e;
      }
      co_return -1;
    };

    auto coro_3 = [&]() mutable -> cf::task<int>
    {
      try
      {
        int result = co_await coro_2();
        co_return result + 1;
      }
      catch (const test_exception_t& e)
      {
        exception_forwarded_2_event.trigger();
        throw e;
      }
      co_return -1;
    };

    REQUIRE_THROWS_AS(cf::sync_wait(coro_3(), &thread_pool), test_exception_t);
    REQUIRE(exception_forwarded_token.is_triggered(c_test_case_timeout));
    REQUIRE(exception_forwarded_2_token.is_triggered(c_test_case_timeout));
    handle_error(std::move(thread_pool));
  }
  memory_checker.check();
}

TEST_CASE_METHOD(base_test_case_t,
                 "Coroutine with exception after co_await",
                 "[task]")
{
  memory_check_t memory_checker;
  {
    simple_thread_pool_t thread_pool;
    auto [exception_forwarded_event, exception_forwarded_token] =
        event_t::create("Exception forwarded");

    auto coro = []() -> cf::task<int> { co_return 2; };

    auto coro_2 = [&]() mutable -> cf::task<int>
    {
      int result = co_await coro();
      throw test_exception_t{};

      co_return result;
    };

    REQUIRE_THROWS_AS(cf::sync_wait(coro_2(), &thread_pool), test_exception_t);
    handle_error(std::move(thread_pool));
  }
  memory_checker.check();
}

TEST_CASE_METHOD(base_test_case_t,
                 "Coroutine with exception after co_await 2nd level",
                 "[task]")
{
  CF_PROFILE_SCOPE();
  memory_check_t memory_checker;
  {
    simple_thread_pool_t thread_pool;
    auto [exception_forwarded_event, exception_forwarded_token] =
        event_t::create("Exception forwarded");

    auto coro = []() -> cf::task<int>
    {
      CF_PROFILE_MARK("coro");
      co_return 2;
    };

    auto coro_2 = [&]() mutable -> cf::task<int>
    {
      CF_PROFILE_MARK("coro_2");
      int result = co_await coro();
      CF_PROFILE_MARK("coro_2_2");

      throw test_exception_t{};

      co_return result;
    };
    auto coro_3 = [&]() mutable -> cf::task<int>
    {
      try
      {
        CF_PROFILE_MARK("coro_3");

        int result = co_await coro_2();
        CF_PROFILE_MARK("coro_3_1");

        co_return result + 1;
      }
      catch (const test_exception_t& e)
      {
        CF_PROFILE_MARK("coro_3_2");

        exception_forwarded_event.trigger();
        throw e;
      }
      co_return -1;
    };

    REQUIRE_THROWS_AS(cf::sync_wait(coro_3(), &thread_pool), test_exception_t);
    REQUIRE(exception_forwarded_token.is_triggered(c_test_case_timeout));
    handle_error(std::move(thread_pool));
  }
  memory_checker.check();
}

TEST_CASE_METHOD(base_test_case_t, "Exception during schedule", "[task]")
{
  memory_check_t memory_checker;
  {
    simple_thread_pool_t thread_pool;
    thread_pool.set_throw_at_schedule(0);

    auto coro = []() -> cf::task<int> { co_return 2; };

    REQUIRE_THROWS_AS(cf::sync_wait(coro(), &thread_pool), test_exception_t);
    handle_error(std::move(thread_pool));
  }
  memory_checker.check();
}

TEST_CASE_METHOD(base_test_case_t,
                 "Exception during schedule inside coroutine",
                 "[task]")
{
  memory_check_t memory_checker;
  {
    simple_thread_pool_t thread_pool;
    thread_pool.set_throw_at_schedule(1);

    auto coro = []() -> cf::task<int> { co_return 2; };
    auto coro_2 = [&]() -> cf::task<int>
    {
      int result = co_await coro();
      co_return result + 1;
    };

    REQUIRE_THROWS_AS(cf::sync_wait(coro_2(), &thread_pool), test_exception_t);
    handle_error(std::move(thread_pool));
  }
  memory_checker.check();
}

TEST_CASE_METHOD(base_test_case_t, "Exception during move", "[task]")
{
  memory_check_t memory_checker;
  {
    simple_thread_pool_t thread_pool;

    using CurrentThrowingClass = ThrowingClass<false, false, true, true>;

    auto coro = []() -> cf::task<CurrentThrowingClass>
    { co_return CurrentThrowingClass{}; };

    REQUIRE_THROWS_AS(cf::sync_wait(coro(), &thread_pool), test_exception_t);
    handle_error(std::move(thread_pool));
  }
  memory_checker.check();
}
TEST_CASE_METHOD(base_test_case_t, "Exception during move 2nd level", "[task]")
{
  memory_check_t memory_checker;
  {
    simple_thread_pool_t thread_pool;

    using CurrentThrowingClass = ThrowingClass<false, false, true, true>;

    auto coro = []() -> cf::task<CurrentThrowingClass>
    { co_return CurrentThrowingClass{}; };
    auto coro_2 = [&]() -> cf::task<int>
    {
      co_await coro();
      co_return 2;
    };

    REQUIRE_THROWS_AS(cf::sync_wait(coro_2(), &thread_pool), test_exception_t);
    handle_error(std::move(thread_pool));
  }
  memory_checker.check();
}
TEST_CASE_METHOD(base_test_case_t, "No Exception during move assign", "[task]")
{
  memory_check_t memory_checker;
  {
    simple_thread_pool_t thread_pool;

    using CurrentThrowingClass = ThrowingClass<false, false, false, true>;

    auto coro = []() -> cf::task<CurrentThrowingClass>
    { co_return CurrentThrowingClass{}; };

    cf::sync_wait(coro(), &thread_pool);
    SUCCEED("Test pass when no exception occurrs");
    handle_error(std::move(thread_pool));
  }
  memory_checker.check();
}

TEST_CASE_METHOD(base_test_case_t,
                 "No Exception during move assign 2nd level",
                 "[task]")
{
  memory_check_t memory_checker;
  {
    simple_thread_pool_t thread_pool;

    using CurrentThrowingClass = ThrowingClass<false, false, false, true>;

    auto coro = []() -> cf::task<CurrentThrowingClass>
    { co_return CurrentThrowingClass{}; };
    auto coro_2 = [&]() -> cf::task<int>
    {
      auto result = co_await coro();
      co_return 2;
    };

    cf::sync_wait(coro_2(), &thread_pool);
    SUCCEED("Test pass when no exception occurrs");
    handle_error(std::move(thread_pool));
  }
  memory_checker.check();
}
TEST_CASE_METHOD(base_test_case_t,
                 "No Exception during movable class during copy",
                 "[task]")
{
  memory_check_t memory_checker;
  {
    simple_thread_pool_t thread_pool;

    using CurrentThrowingClass = ThrowingClass<true, true, false, false>;

    auto coro = []() -> cf::task<CurrentThrowingClass>
    { co_return CurrentThrowingClass{}; };

    cf::sync_wait(coro(), &thread_pool);
    SUCCEED("Test pass when no exception occurrs");
    handle_error(std::move(thread_pool));
  }
  memory_checker.check();
}

TEST_CASE_METHOD(base_test_case_t,
                 "No Exception during movable class during copy 2nd level",
                 "[task]")
{
  memory_check_t memory_checker;
  {
    simple_thread_pool_t thread_pool;

    using CurrentThrowingClass = ThrowingClass<true, true, false, false>;

    auto coro = []() -> cf::task<CurrentThrowingClass>
    { co_return CurrentThrowingClass{}; };
    auto coro_2 = [&]() -> cf::task<int>
    {
      auto result = co_await coro();
      co_return 2;
    };

    cf::sync_wait(coro_2(), &thread_pool);
    SUCCEED("Test pass when no exception occurrs");
    handle_error(std::move(thread_pool));
  }
  memory_checker.check();
}
/* Future doesn't supports non movable class
TEST_CASE("Exception during copy in nonmovable class", "[task]")
{
  simple_thread_pool_t thread_pool;

  using CurrentThrowingClass = NonMovableThrowingClass<true, true>;

  auto coro = []() -> cf::task<CurrentThrowingClass>
  { co_return CurrentThrowingClass{}; };

  REQUIRE_THROWS_AS(coro().run_async(&thread_pool).sync_wait().h(),
                    test_exception_t);
}
TEST_CASE("Exception during copy in nonmovable class 2nd level", "[task]")
{
  simple_thread_pool_t thread_pool;

  using CurrentThrowingClass = NonMovableThrowingClass<true, true>;

  auto coro = []() -> cf::task<CurrentThrowingClass>
  { co_return CurrentThrowingClass{}; };
  auto coro_2 = [&]() -> cf::task<int>
  {
    co_await coro();
    co_return 2;
  };

  REQUIRE_THROWS_AS(coro_2().run_async(&thread_pool).sync_wait().get(),
                    test_exception_t);
}
*/