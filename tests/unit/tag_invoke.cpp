#include <catch2/catch_test_macros.hpp>

#include <coroutine_flow/tag_invoke.hpp>

namespace cf = coroutine_flow;

// clang-format off
struct tag_01{};
// clang-format on

struct CustomClass01
{
    uint32_t call_count{ 0 };
};
struct CustomClass02
{
    uint32_t call_count{ 0 };
};
struct CustomClass03
{
    uint32_t call_count{ 0 };
};

void tag_invoke(tag_01&&, CustomClass01& obj)
{
  obj.call_count++;
}

void tag_invoke(tag_01&&, CustomClass03& obj) noexcept
{
  obj.call_count++;
}
TEST_CASE("run_tag_invoke", "[tag_invoke]")
{
  CustomClass01 test_object;
  tag_invoke(tag_01{}, test_object);
  REQUIRE(test_object.call_count == 1);
}

TEST_CASE("has_tag_invoke", "[tag_invoke]")
{
  constexpr bool has_tag = cf::is_tag_invocable_v<tag_01, CustomClass01&>;
  REQUIRE(has_tag);
}
TEST_CASE("has_no_tag_invoke", "[tag_invoke]")
{
  constexpr bool has_tag = cf::is_tag_invocable_v<tag_01, CustomClass01>;
  REQUIRE_FALSE(has_tag);
}

TEST_CASE("has_no_tag_invoke_at_all", "[tag_invoke]")
{
  constexpr bool has_tag = cf::is_tag_invocable_v<tag_01, CustomClass02>;
  REQUIRE_FALSE(has_tag);
}

TEST_CASE("has_no_except_tag_invoke", "[tag_invoke]")
{
  constexpr bool has_tag = cf::is_tag_invocable_v<tag_01, CustomClass03&>;
  REQUIRE(has_tag);
}

TEST_CASE("has_no_except_tag_invoke_of_no_except", "[tag_invoke]")
{
  constexpr bool has_tag =
      cf::is_noexcept_tag_invocable_v<tag_01, CustomClass03&>;
  REQUIRE(has_tag);
}

TEST_CASE("has_no_no_except_tag_invoke", "[tag_invoke]")
{
  constexpr bool has_tag =
      cf::is_noexcept_tag_invocable_v<tag_01, CustomClass01&>;
  REQUIRE_FALSE(has_tag);
}