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

constexpr const std::chrono::seconds c_test_case_timeout =
    cf::__details::testing::c_test_case_timeout;
struct NonDefaultConstructibleClass
{
    NonDefaultConstructibleClass() = delete;
    explicit NonDefaultConstructibleClass(int) {};
    NonDefaultConstructibleClass(NonDefaultConstructibleClass&&) = default;
    NonDefaultConstructibleClass(const NonDefaultConstructibleClass&) = default;

    NonDefaultConstructibleClass&
        operator=(NonDefaultConstructibleClass&&) = default;
    NonDefaultConstructibleClass&
        operator=(const NonDefaultConstructibleClass&) = default;
};

struct NonCopyableClass
{
    NonCopyableClass() = default;
    NonCopyableClass(NonCopyableClass&&) = default;
    NonCopyableClass(const NonCopyableClass&) = delete;

    NonCopyableClass& operator=(NonCopyableClass&&) = default;
    NonCopyableClass& operator=(const NonCopyableClass&) = delete;
};

struct NonMovableClass
{
    NonMovableClass() = default;
    NonMovableClass(NonMovableClass&&) = delete;
    NonMovableClass(const NonMovableClass&) = default;

    NonMovableClass& operator=(NonMovableClass&&) = delete;
    NonMovableClass& operator=(const NonMovableClass&) = default;
};

template <bool throw_at_copy_constructor, bool throw_at_copy_assign>
struct NonMovableThrowingClass
{
    NonMovableThrowingClass() = default;
    NonMovableThrowingClass(const NonMovableThrowingClass&)
    {
      if constexpr (throw_at_copy_constructor)
      {
        throw test_exception_t{};
      }
    }
    NonMovableThrowingClass(NonMovableThrowingClass&&) = delete;
    NonMovableThrowingClass& operator=(const NonMovableThrowingClass&)
    {
      if constexpr (throw_at_copy_assign)
      {
        throw test_exception_t{};
      }
      return *this;
    }
    NonMovableThrowingClass& operator=(NonMovableThrowingClass&&) = delete;
};
TEST_CASE_METHOD(base_test_case_t, "Non Copyable return type", "[task]")
{
  memory_check_t memory_checker;
  {
    simple_thread_pool_t thread_pool;

    auto coro_1 = []() -> cf::task<NonCopyableClass>
    { co_return NonCopyableClass{}; };

    auto coro_2 = [&]() -> cf::task<NonCopyableClass>
    {
      auto result = co_await coro_1();
      co_return result;
    };

    cf::sync_wait(coro_2(), &thread_pool);
    SUCCEED("This test needs to be only compiled");
    handle_error(std::move(thread_pool));
  }
  memory_checker.check();
}
/*
TODO Add this test case as a task<void> on top level, while std::future doesn't
support non movable classes

TEST_CASE("Non Moveable return type", "[task]")
{
  memory_check_t memory_checker;
  {
    simple_thread_pool_t thread_pool;

    auto coro_1 = []() -> cf::task<NonMovableClass>
    { co_return NonMovableClass{}; };

    auto coro_2 = [&]() -> cf::task<NonMovableClass>
    {
      auto result = co_await coro_1();
      co_return result;
    };

    cf::sync_wait(coro_2(), &thread_pool);
    SUCCEED("This test needs to be only compiled");
    handle_error(std::move(thread_pool));
  }
  memory_checker.check();
}
*/
TEST_CASE_METHOD(base_test_case_t, "Reference return type", "[task]")
{
  memory_check_t memory_checker;
  {
    simple_thread_pool_t thread_pool;
    int my_int = 0;
    auto [event, token] = event_t::create("coroutine finished");

    auto coro_1 = [&]() mutable -> cf::task<std::reference_wrapper<int>>
    { co_return my_int; };

    auto coro_2 = [&]() -> cf::task<int>
    {
      int& my_int_reference = co_await coro_1();
      my_int_reference = 1;
      event.trigger();
      co_return my_int_reference;
    };

    cf::sync_wait(coro_2(), &thread_pool);
    REQUIRE(token.is_triggered(c_test_case_timeout));
    REQUIRE(my_int == 1);
    SUCCEED("This test needs to be only compiled");
    handle_error(std::move(thread_pool));
  }
  memory_checker.check();
}

TEST_CASE_METHOD(base_test_case_t, "Pointer return type", "[task]")
{
  memory_check_t memory_checker;
  {
    simple_thread_pool_t thread_pool;
    int my_int = 0;
    auto [event, token] = event_t::create("coroutine finished");

    auto coro_1 = [&]() mutable -> cf::task<int*> { co_return &my_int; };

    auto coro_2 = [&]() -> cf::task<int>
    {
      int* my_int_ptr = co_await coro_1();
      *my_int_ptr = 1;
      event.trigger();
      co_return *my_int_ptr;
    };

    cf::sync_wait(coro_2(), &thread_pool);
    REQUIRE(token.is_triggered(c_test_case_timeout));
    REQUIRE(my_int == 1);
    SUCCEED("This test needs to be only compiled");
    handle_error(std::move(thread_pool));
  }
  memory_checker.check();
}

TEST_CASE_METHOD(base_test_case_t, "Tuple return type", "[task]")
{
  memory_check_t memory_checker;
  {
    simple_thread_pool_t thread_pool;
    auto [event, token] = event_t::create("coroutine finished");

    auto coro_1 = [&]() mutable -> cf::task<std::tuple<int, float, std::string>>
    { co_return std::tuple{ 1, 2.0f, "hi" }; };

    auto coro_2 = [&]() -> cf::task<int>
    {
      auto [my_int, my_float, my_string] = co_await coro_1();
      REQUIRE(my_int == 1);
      REQUIRE(my_float == 2.0f);
      REQUIRE(my_string == "hi");
      event.trigger();

      co_return 2;
    };

    cf::sync_wait(coro_2(), &thread_pool);
    REQUIRE(token.is_triggered(c_test_case_timeout));
    SUCCEED("This test needs to be only compiled");
    handle_error(std::move(thread_pool));
  }
  memory_checker.check();
}
TEST_CASE_METHOD(base_test_case_t,
                 "Non default constructible return type",
                 "[task]")
{
  memory_check_t memory_checker;
  {
    simple_thread_pool_t thread_pool;

    auto coro_1 = [&]() mutable -> cf::task<NonDefaultConstructibleClass>
    { co_return NonDefaultConstructibleClass{ 1 }; };

    auto coro_2 = [&]() -> cf::task<int>
    {
      auto non_default_constructible = co_await coro_1();
      co_return 2;
    };

    cf::sync_wait(coro_2(), &thread_pool);
    SUCCEED("This test needs to be only compiled");
    handle_error(std::move(thread_pool));
  }
  memory_checker.check();
}