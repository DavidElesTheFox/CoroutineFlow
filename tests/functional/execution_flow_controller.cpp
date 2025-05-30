#include <catch2/catch_test_macros.hpp>

#include <coroutine_flow/__details/scope_exit.hpp>
#include <coroutine_flow/__details/testing/execution_flow_controller.hpp>
#include <coroutine_flow/profiler.hpp>

#include <iostream>

namespace cf = coroutine_flow;

enum class test_points
{
  foo_before_do_something,
  foo_after_do_something,

  bar_before_do_something,
  bar_after_do_something
};
using execution_flow_controller =
    cf::__details::testing::execution_flow_controller<test_points>;
execution_flow_controller& get_controller()
{
  static execution_flow_controller instance;
  return instance;
}

struct Foo
{
    static inline uint32_t id_mask = 1 << 9;
    uint32_t call_id = 0;
    static bool is_my_id(uint32_t id) { return (id & id_mask) != 0; }

    template <typename callable_t>
    void operator()(callable_t&& something)
    {
      CF_PROFILE_SCOPE_N("foo()");
      get_controller().touch(test_points::foo_before_do_something, this);
      something((call_id++ | id_mask));
      get_controller().touch(test_points::foo_after_do_something, this);
    }
};

struct Bar
{
    static inline uint32_t id_mask = 1 << 10;
    uint32_t call_id = 0;
    static bool is_my_id(uint32_t id) { return (id & id_mask) != 0; }

    template <typename callable_t>
    void operator()(callable_t&& something)
    {
      CF_PROFILE_SCOPE_N("bar()");
      get_controller().touch(test_points::bar_before_do_something, this);
      something((call_id++ | id_mask));
      get_controller().touch(test_points::bar_after_do_something, this);
    }
};

constexpr const std::chrono::seconds c_test_case_timeout{ 3 };

SCENARIO("Foo waits Bar", "[execution_flow_controller]")
{
  GIVEN("One foo and bar objects")
  {
    CF_PROFILE_SCOPE_N("One foo and bar objects");
    Foo foo;
    Bar bar;
    std::mutex call_ids_mutex;
    std::vector<uint32_t> call_ids;
    auto push_call_id = [&](uint32_t id) mutable
    {
      std::lock_guard lock(call_ids_mutex);
      call_ids.push_back(id);
    };
    WHEN("Foo needs to wait until Bar finishes")
    {
      CF_PROFILE_SCOPE_N("Foo needs to wait until Bar finishes");

      get_controller().append(
          { { "bar called", test_points::bar_after_do_something, { &bar } },
            { "foo called", test_points::foo_before_do_something, { &foo } } });

      THEN("We expect the proper execution order")
      {
        cf::__details::scope_exit_t clean_up([]() noexcept
                                             { get_controller().clear(); });

        std::thread foo_thread([&] { foo(push_call_id); });

        std::thread bar_thread([&] { bar(push_call_id); });
        if (bar_thread.joinable())
        {
          bar_thread.join();
        }
        if (foo_thread.joinable())
        {
          foo_thread.join();
        }
        std::lock_guard lock(call_ids_mutex);

        REQUIRE(call_ids.size() == 2);
        REQUIRE(Bar::is_my_id(call_ids[0]));
        REQUIRE(Foo::is_my_id(call_ids[1]));
      }
    }
    WHEN("Foo needs to wait until Bar finishes two times")
    {
      CF_PROFILE_SCOPE_N("Foo needs to wait until Bar finishes two times");
      get_controller().append(
          { { "bar called 1st time",
              test_points::bar_after_do_something,
              &bar },
            { "bar called 2nd time",
              test_points::bar_after_do_something,
              &bar },
            { "foo called", test_points::foo_before_do_something, &foo } });

      THEN("We expect the proper execution order")
      {
        cf::__details::scope_exit_t clean_up([]() noexcept
                                             { get_controller().clear(); });

        std::thread foo_thread([&] { foo(push_call_id); });
        std::thread bar_thread(
            [&]
            {
              bar(push_call_id);
              bar(push_call_id);
            });
        if (bar_thread.joinable())
        {
          bar_thread.join();
        }
        if (foo_thread.joinable())
        {
          foo_thread.join();
        }

        REQUIRE(call_ids.size() == 3);
        REQUIRE(Bar::is_my_id(call_ids[0]));
        REQUIRE(Bar::is_my_id(call_ids[1]));
        REQUIRE(Foo::is_my_id(call_ids[2]));
      }
    }
    WHEN("Foo needs to wait until Bar finishes but Bar also needs to wait to "
         "Foo")
    {
      CF_PROFILE_SCOPE_N(
          "Foo needs to wait until Bar finishes but Bar also needs to wait to "
          "Foo");
      auto add_foo_dependency =
          [&p_bar = bar](execution_flow_controller::thread_safety_token_t token)
      {
        CF_PROFILE_SCOPE_N("callback::add_foo_dependency");
        get_controller().insert_after("foo called",
                                      { "bar called 3rd time",
                                        test_points::bar_before_do_something,
                                        &p_bar },
                                      std::move(token));
      };
      get_controller().append(
          { { "bar called 1st time",
              test_points::bar_after_do_something,
              &bar },
            { "bar called 2nd time",
              test_points::bar_after_do_something,
              &bar,
              add_foo_dependency },
            { "foo called", test_points::foo_before_do_something, &foo } },
          {});

      THEN("We expect the proper execution order")
      {
        cf::__details::scope_exit_t clean_up([]() noexcept
                                             { get_controller().clear(); });

        {
          std::thread foo_thread([&]() mutable { foo(push_call_id); });
          std::thread bar_thread(
              [&]() mutable
              {
                bar(push_call_id);
                bar(push_call_id);
                bar(push_call_id);
              });

          if (foo_thread.joinable())
          {
            foo_thread.join();
          }
          if (bar_thread.joinable())
          {
            bar_thread.join();
          }
        }

        REQUIRE(call_ids.size() == 4);
        REQUIRE(Bar::is_my_id(call_ids[0]));
        REQUIRE(Bar::is_my_id(call_ids[1]));
        REQUIRE(Foo::is_my_id(call_ids[2]));
        REQUIRE(Bar::is_my_id(call_ids[3]));
      }
    }
  }
}
