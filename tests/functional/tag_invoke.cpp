#include <catch2/catch_test_macros.hpp>

#include <coroutine_flow/tag_invoke.hpp>

namespace cf = coroutine_flow;

// clang-format off
struct get_algo_t{};
// clang-format on

struct Algo01
{
    uint32_t call_count{ 0 };
};

struct Algo02
{
    uint32_t call_count{ 0 };
};

struct UsesAlgo01
{
    Algo01 algorithm;
    friend Algo01& tag_invoke(get_algo_t&&, UsesAlgo01& obj)
    {
      return obj.algorithm;
    }
};

struct UsesAlgo02
{
    Algo02 algorithm;
};

Algo02& tag_invoke(get_algo_t&&, UsesAlgo02& obj)
{
  return obj.algorithm;
}

struct MainLogic
{
    template <typename T>
    void operator()(T&& dependency)
    {
      if constexpr (cf::is_tag_invocable_v<get_algo_t, T>)
      {
        cf::tag_invoke(get_algo_t{}, dependency).call_count++;
      }
    }
};

SCENARIO("Using object with tag", "[tag_invoke]")
{
  MainLogic logic;
  GIVEN("Two dependecy with different algorithms")
  {
    UsesAlgo01 algo_01_dependency;
    UsesAlgo02 algo_02_dependency;

    WHEN("Calling the first algorithm dependency")
    {
      logic(algo_01_dependency);
      THEN("Algorithm 01 should be called")
      {
        REQUIRE(algo_01_dependency.algorithm.call_count == 1);
        REQUIRE(algo_02_dependency.algorithm.call_count == 0);
      }
    }
    WHEN("Calling the second algorithm dependency")
    {
      logic(algo_02_dependency);
      THEN("Algorithm 02 should be called")
      {
        REQUIRE(algo_01_dependency.algorithm.call_count == 0);
        REQUIRE(algo_02_dependency.algorithm.call_count == 1);
      }
    }
    WHEN("Calling it with a type that has no algorithm")
    {
      logic(42);
      THEN("Nothing to be called")
      {
        REQUIRE(algo_01_dependency.algorithm.call_count == 0);
        REQUIRE(algo_02_dependency.algorithm.call_count == 0);
      }
    }
  }
}