/*

 - Memory tests
*/
#include <catch2/catch_test_macros.hpp>

#include <coroutine_flow/simple_thread_pool.hpp>

#include <coroutine_flow/profiler.hpp>
#include <coroutine_flow/task.hpp>

#include <future>
#include <iostream>
#include <list>
#include <thread>

namespace cf = coroutine_flow;
using namespace std::chrono_literals;

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

struct OnExit
{
    ~OnExit() { callback(); }
    std::function<void()> callback;
};

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

class Event
{
  public:
    class Token
    {
      public:
        explicit Token(Event& event)
            : m_triggered_future(event.m_triggered.get_future())
        {
        }

        bool is_triggered(const std::chrono::milliseconds& timeout) const
        {
          if (const auto future_status = m_triggered_future.wait_for(timeout);
              future_status == std::future_status::ready)
          {
            return true;
          }
          else
          {
            return false;
          }
        }

      private:
        std::future<bool> m_triggered_future;
    };

    static std::tuple<Event, Event::Token> create(std::string_view event_name)
    {
      Event result{ event_name };
      auto event_token = result.get_token();
      return { std::move(result), std::move(event_token) };
    }
    explicit Event(std::string_view name)
        : m_name(name)
    {
    }

    Token get_token() { return Token{ *this }; }

    void trigger()
    {
      if (std::exchange(m_once_triggered, true) == false)
      {
        m_triggered.set_value(true);
      }
    }

  private:
    std::string m_name;
    std::promise<bool> m_triggered;
    bool m_once_triggered{ false };
};

void handle_error(simple_thread_pool&& thread_pool)
{
  std::vector<std::exception_ptr> errors = thread_pool.clear_errors();
  if (errors.empty())
  {
    return;
  }
  std::cout << "Error count: " << errors.size() << std::endl;
  for (std::exception_ptr ex : errors)
  {
    try
    {
      std::rethrow_exception(ex);
    }
    catch (const std::exception& e)
    {
      std::cout << "Error: " << e.what() << std::endl;
    }
  }
  FAIL("Error occurred in thread pool");
}

constexpr const std::chrono::duration c_test_case_timeout = 1s;
TEST_CASE("Check Destructor when not scheduled", "[task]")
{
  bool coroutine_state_destroyed = false;
  {
    auto coro = [](OnExit&& checker) -> cf::task<int> { co_return 2; };
    coro({ [&] { coroutine_state_destroyed = true; } });
  }
  REQUIRE(coroutine_state_destroyed);
}

TEST_CASE("Check Destructor when scheduled", "[task]")
{
  CF_PROFILE_SCOPE();
  bool coroutine_state_destroyed = false;
  {
    CF_PROFILE_SCOPE_N("coro owner_scope");
    simple_thread_pool thread_pool;
    auto coro = [](OnExit&& checker) -> cf::task<int>
    {
      CF_PROFILE_SCOPE_N("coro");
      co_return 2;
    };
    coro({ [&] { coroutine_state_destroyed = true; } }).run_async(&thread_pool);
    handle_error(std::move(thread_pool));
  }
  REQUIRE(coroutine_state_destroyed);
}

#pragma region Execution Tests

TEST_CASE("Neasted coroutine level 1", "[task]")
{
  simple_thread_pool thread_pool;
  auto [called_event, called_token] = Event::create("coroutine is called");

  auto coro_1 = [p_called_event =
                     std::move(called_event)]() mutable -> cf::task<int>
  {
    p_called_event.trigger();
    co_return 1;
  };
  coro_1().run_async(&thread_pool);

  REQUIRE(called_token.is_triggered(c_test_case_timeout));
  handle_error(std::move(thread_pool));
}

TEST_CASE("Neasted coroutine level 2", "[task]")
{
  simple_thread_pool thread_pool;
  auto [called_event_1, called_token_1] =
      Event::create("coroutine 1 is called");

  auto coro_1 = [p_called_event =
                     std::move(called_event_1)]() mutable -> cf::task<int>
  {
    CF_PROFILE_MARK("coro_1");
    p_called_event.trigger();
    co_return 1;
  };

  auto [called_event_2, called_token_2] =
      Event::create("coroutine 2 is called");

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

  coro_2().run_async(&thread_pool);

  REQUIRE(called_token_1.is_triggered(c_test_case_timeout));
  REQUIRE(called_token_2.is_triggered(c_test_case_timeout));
  handle_error(std::move(thread_pool));
}

TEST_CASE("Neasted coroutine level 3", "[task]")
{
  simple_thread_pool thread_pool;

  // Coroutine 1
  auto [called_event_1, called_token_1] =
      Event::create("coroutine 1 is called");

  auto coro_1 = [p_called_event =
                     std::move(called_event_1)]() mutable -> cf::task<int>
  {
    CF_PROFILE_MARK("coro_1");

    p_called_event.trigger();
    co_return 1;
  };

  // Coroutine 2
  auto [called_event_2, called_token_2] =
      Event::create("coroutine 2 is called");

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
      Event::create("coroutine 3 is called");

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

  coro_3().run_async(&thread_pool);

  REQUIRE(called_token_1.is_triggered(c_test_case_timeout));
  REQUIRE(called_token_2.is_triggered(c_test_case_timeout));
  REQUIRE(called_token_3.is_triggered(c_test_case_timeout));
  handle_error(std::move(thread_pool));
}

TEST_CASE("Neasted coroutine level 4", "[task]")
{
  simple_thread_pool thread_pool;

  // Coroutine 1
  auto [called_event_1, called_token_1] =
      Event::create("coroutine 1 is called");

  auto coro_1 = [p_called_event =
                     std::move(called_event_1)]() mutable -> cf::task<int>
  {
    CF_PROFILE_MARK("coro_1");
    p_called_event.trigger();
    co_return 1;
  };

  // Coroutine 2
  auto [called_event_2, called_token_2] =
      Event::create("coroutine 2 is called");

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
      Event::create("coroutine 3 is called");

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
      Event::create("coroutine 4 is called");
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

  coro_4().run_async(&thread_pool);

  REQUIRE(called_token_1.is_triggered(c_test_case_timeout));
  REQUIRE(called_token_2.is_triggered(c_test_case_timeout));
  REQUIRE(called_token_3.is_triggered(c_test_case_timeout));
  REQUIRE(called_token_4.is_triggered(c_test_case_timeout));
  handle_error(std::move(thread_pool));
}

TEST_CASE("Neasted coroutine level 2; waits 2", "[task]")
{
  CF_PROFILE_SCOPE();
  simple_thread_pool thread_pool;
  auto [called_event_1, called_token_1] =
      Event::create("coroutine 1 is called");
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
      Event::create("coroutine 2 is called");
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

  coro_2().run_async(&thread_pool);

  REQUIRE(called_token_1.is_triggered(c_test_case_timeout));
  REQUIRE(called_token_2.is_triggered(c_test_case_timeout));
  REQUIRE(coro_1_call_count == 2);
  REQUIRE(coro_2_call_count == 1);
  handle_error(std::move(thread_pool));
}

TEST_CASE("Neasted coroutine level 3; waits 2", "[task]")
{
  CF_PROFILE_SCOPE();
  simple_thread_pool thread_pool;

  // Coroutine 1
  auto [called_event_1, called_token_1] =
      Event::create("coroutine 1 is called");
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
      Event::create("coroutine 2 is called");
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
      Event::create("coroutine 3 is called");
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

  coro_3().run_async(&thread_pool);

  REQUIRE(called_token_1.is_triggered(c_test_case_timeout));
  REQUIRE(called_token_2.is_triggered(c_test_case_timeout));
  REQUIRE(called_token_3.is_triggered(c_test_case_timeout));
  REQUIRE(coro_1_call_count == 4);
  REQUIRE(coro_2_call_count == 2);
  REQUIRE(coro_3_call_count == 1);
  handle_error(std::move(thread_pool));
}

TEST_CASE("Neasted coroutine level 4; waits 2", "[task]")
{
  CF_PROFILE_SCOPE();

  simple_thread_pool thread_pool;

  // Coroutine 1
  auto [called_event_1, called_token_1] =
      Event::create("coroutine 1 is called");
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
      Event::create("coroutine 2 is called");
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
      Event::create("coroutine 3 is called");
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
      Event::create("coroutine 4 is called");
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

  coro_4().run_async(&thread_pool);

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

TEST_CASE("Neasted coroutine level 2; waits 3", "[task]")
{
  simple_thread_pool thread_pool;
  auto [called_event_1, called_token_1] =
      Event::create("coroutine 1 is called");
  std::atomic_uint32_t coro_1_call_count = 0;
  auto coro_1 = [p_called_event = &called_event_1,
                 &coro_1_call_count]() mutable -> cf::task<int>
  {
    coro_1_call_count++;
    p_called_event->trigger();
    co_return 1;
  };

  auto [called_event_2, called_token_2] =
      Event::create("coroutine 2 is called");
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

  coro_2().run_async(&thread_pool);

  REQUIRE(called_token_1.is_triggered(c_test_case_timeout));
  REQUIRE(called_token_2.is_triggered(c_test_case_timeout));
  REQUIRE(coro_1_call_count == 3);
  REQUIRE(coro_2_call_count == 1);
  handle_error(std::move(thread_pool));
}

TEST_CASE("Neasted coroutine level 3; waits 3", "[task]")
{
  CF_PROFILE_SCOPE();
  simple_thread_pool thread_pool;

  // Coroutine 1
  auto [called_event_1, called_token_1] =
      Event::create("coroutine 1 is called");
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
      Event::create("coroutine 2 is called");
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
      Event::create("coroutine 3 is called");
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

  coro_3().run_async(&thread_pool);

  REQUIRE(called_token_1.is_triggered(c_test_case_timeout));
  REQUIRE(called_token_2.is_triggered(c_test_case_timeout));
  REQUIRE(called_token_3.is_triggered(c_test_case_timeout));
  REQUIRE(coro_1_call_count == 9);
  REQUIRE(coro_2_call_count == 3);
  REQUIRE(coro_3_call_count == 1);
  handle_error(std::move(thread_pool));
}

TEST_CASE("Neasted coroutine level 4; waits 3", "[task]")
{
  CF_PROFILE_SCOPE();

  simple_thread_pool thread_pool;

  // Coroutine 1
  auto [called_event_1, called_token_1] =
      Event::create("coroutine 1 is called");
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
      Event::create("coroutine 2 is called");
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
      Event::create("coroutine 3 is called");
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
      Event::create("coroutine 4 is called");
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

  coro_4().run_async(&thread_pool);

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

TEST_CASE("Mixed data", "[task]")
{
  CF_PROFILE_SCOPE();

  simple_thread_pool thread_pool;
  auto [called_event_1, called_token_1] =
      Event::create("coroutine 1 is called");
  auto [called_event_2, called_token_2] =
      Event::create("coroutine 2 is called");
  auto [called_event_3, called_token_3] =
      Event::create("coroutine 3 is called");
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

  coro_3().run_async(&thread_pool);
  REQUIRE(called_token_1.is_triggered(c_test_case_timeout));
  REQUIRE(called_token_2.is_triggered(c_test_case_timeout));
  REQUIRE(called_token_3.is_triggered(c_test_case_timeout));
  handle_error(std::move(thread_pool));
}
#pragma endregion

#pragma region Return Type test

TEST_CASE("Non Copyable return type", "[task]")
{
  simple_thread_pool thread_pool;

  auto coro_1 = []() -> cf::task<NonCopyableClass>
  { co_return NonCopyableClass{}; };

  auto coro_2 = [&]() -> cf::task<NonCopyableClass>
  {
    auto result = co_await coro_1();
    co_return result;
  };

  coro_2().run_async(&thread_pool);
  SUCCEED("This test needs to be only compiled");
  handle_error(std::move(thread_pool));
}

TEST_CASE("Non Moveable return type", "[task]")
{
  simple_thread_pool thread_pool;

  auto coro_1 = []() -> cf::task<NonMovableClass>
  { co_return NonMovableClass{}; };

  auto coro_2 = [&]() -> cf::task<NonMovableClass>
  {
    auto result = co_await coro_1();
    co_return result;
  };

  coro_2().run_async(&thread_pool);
  SUCCEED("This test needs to be only compiled");
  handle_error(std::move(thread_pool));
}

TEST_CASE("Reference return type", "[task]")
{
  simple_thread_pool thread_pool;
  int my_int = 0;
  auto [event, token] = Event::create("coroutine finished");

  auto coro_1 = [&]() mutable -> cf::task<std::reference_wrapper<int>>
  { co_return my_int; };

  auto coro_2 = [&]() -> cf::task<int>
  {
    int& my_int_reference = co_await coro_1();
    my_int_reference = 1;
    event.trigger();
    co_return my_int_reference;
  };

  coro_2().run_async(&thread_pool);
  REQUIRE(token.is_triggered(c_test_case_timeout));
  REQUIRE(my_int == 1);
  SUCCEED("This test needs to be only compiled");
  handle_error(std::move(thread_pool));
}

TEST_CASE("Pointer return type", "[task]")
{
  simple_thread_pool thread_pool;
  int my_int = 0;
  auto [event, token] = Event::create("coroutine finished");

  auto coro_1 = [&]() mutable -> cf::task<int*> { co_return &my_int; };

  auto coro_2 = [&]() -> cf::task<int>
  {
    int* my_int_ptr = co_await coro_1();
    *my_int_ptr = 1;
    event.trigger();
    co_return *my_int_ptr;
  };

  coro_2().run_async(&thread_pool);
  REQUIRE(token.is_triggered(c_test_case_timeout));
  REQUIRE(my_int == 1);
  SUCCEED("This test needs to be only compiled");
  handle_error(std::move(thread_pool));
}

TEST_CASE("Tuple return type", "[task]")
{
  simple_thread_pool thread_pool;
  auto [event, token] = Event::create("coroutine finished");

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

  coro_2().run_async(&thread_pool);
  REQUIRE(token.is_triggered(c_test_case_timeout));
  SUCCEED("This test needs to be only compiled");
  handle_error(std::move(thread_pool));
}
TEST_CASE("Non default constructible return type", "[task]")
{
  simple_thread_pool thread_pool;

  auto coro_1 = [&]() mutable -> cf::task<NonDefaultConstructibleClass>
  { co_return NonDefaultConstructibleClass{ 1 }; };

  auto coro_2 = [&]() -> cf::task<int>
  {
    auto non_default_constructible = co_await coro_1();
    co_return 2;
  };

  coro_2().run_async(&thread_pool);
  SUCCEED("This test needs to be only compiled");
  handle_error(std::move(thread_pool));
}
#pragma endregion

#pragma region Exception Tests

TEST_CASE("Coroutine with exception", "[task]")
{
  simple_thread_pool thread_pool;
  auto coro = []() -> cf::task<int>
  {
    throw test_exception_t{};
    co_return 2;
  };

  REQUIRE_THROWS_AS(coro().sync_wait(&thread_pool), test_exception_t);
  handle_error(std::move(thread_pool));
}

TEST_CASE("Coroutine with exception 2nd level", "[task]")
{
  simple_thread_pool thread_pool;
  auto [exception_forwarded_event, exception_forwarded_token] =
      Event::create("Exception forwarded");

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

  REQUIRE_THROWS_AS(coro_2().sync_wait(&thread_pool), test_exception_t);
  REQUIRE(exception_forwarded_token.is_triggered(c_test_case_timeout));
  handle_error(std::move(thread_pool));
}

TEST_CASE("Coroutine with exception 3rd level", "[task]")
{
  simple_thread_pool thread_pool;
  auto [exception_forwarded_event, exception_forwarded_token] =
      Event::create("Exception forwarded");
  auto [exception_forwarded_2_event, exception_forwarded_2_token] =
      Event::create("Exception forwarded 2");
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

  REQUIRE_THROWS_AS(coro_3().sync_wait(&thread_pool), test_exception_t);
  REQUIRE(exception_forwarded_token.is_triggered(c_test_case_timeout));
  REQUIRE(exception_forwarded_2_token.is_triggered(c_test_case_timeout));
  handle_error(std::move(thread_pool));
}

TEST_CASE("Coroutine with exception after co_await", "[task]")
{
  simple_thread_pool thread_pool;
  auto [exception_forwarded_event, exception_forwarded_token] =
      Event::create("Exception forwarded");

  auto coro = []() -> cf::task<int> { co_return 2; };

  auto coro_2 = [&]() mutable -> cf::task<int>
  {
    int result = co_await coro();
    throw test_exception_t{};

    co_return result;
  };

  REQUIRE_THROWS_AS(coro_2().sync_wait(&thread_pool), test_exception_t);
  handle_error(std::move(thread_pool));
}

TEST_CASE("Coroutine with exception after co_await 2nd level", "[task]")
{
  CF_PROFILE_SCOPE();
  simple_thread_pool thread_pool;
  auto [exception_forwarded_event, exception_forwarded_token] =
      Event::create("Exception forwarded");

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

  REQUIRE_THROWS_AS(coro_3().sync_wait(&thread_pool), test_exception_t);
  REQUIRE(exception_forwarded_token.is_triggered(c_test_case_timeout));
  handle_error(std::move(thread_pool));
}

TEST_CASE("Exception during schedule", "[task]")
{
  simple_thread_pool thread_pool;
  thread_pool.set_throw_at_schedule(0);

  auto coro = []() -> cf::task<int> { co_return 2; };

  REQUIRE_THROWS_AS(coro().sync_wait(&thread_pool), test_exception_t);
  handle_error(std::move(thread_pool));
}

TEST_CASE("Exception during schedule inside coroutine", "[task]")
{
  simple_thread_pool thread_pool;
  thread_pool.set_throw_at_schedule(1);

  auto coro = []() -> cf::task<int> { co_return 2; };
  auto coro_2 = [&]() -> cf::task<int>
  {
    int result = co_await coro();
    co_return result + 1;
  };

  REQUIRE_THROWS_AS(coro_2().sync_wait(&thread_pool), test_exception_t);
  handle_error(std::move(thread_pool));
}

TEST_CASE("Exception during move", "[task]")
{
  simple_thread_pool thread_pool;

  using CurrentThrowingClass = ThrowingClass<false, false, true, true>;

  auto coro = []() -> cf::task<CurrentThrowingClass>
  { co_return CurrentThrowingClass{}; };

  REQUIRE_THROWS_AS(coro().sync_wait(&thread_pool), test_exception_t);
  handle_error(std::move(thread_pool));
}
TEST_CASE("Exception during move 2nd level", "[task]")
{
  simple_thread_pool thread_pool;

  using CurrentThrowingClass = ThrowingClass<false, false, true, true>;

  auto coro = []() -> cf::task<CurrentThrowingClass>
  { co_return CurrentThrowingClass{}; };
  auto coro_2 = [&]() -> cf::task<int>
  {
    co_await coro();
    co_return 2;
  };

  REQUIRE_THROWS_AS(coro_2().sync_wait(&thread_pool), test_exception_t);
  handle_error(std::move(thread_pool));
}
TEST_CASE("No Exception during move assign", "[task]")
{
  simple_thread_pool thread_pool;

  using CurrentThrowingClass = ThrowingClass<false, false, false, true>;

  auto coro = []() -> cf::task<CurrentThrowingClass>
  { co_return CurrentThrowingClass{}; };

  coro().sync_wait(&thread_pool);
  SUCCEED("Test pass when no exception occurrs");
  handle_error(std::move(thread_pool));
}

TEST_CASE("No Exception during move assign 2nd level", "[task]")
{
  simple_thread_pool thread_pool;

  using CurrentThrowingClass = ThrowingClass<false, false, false, true>;

  auto coro = []() -> cf::task<CurrentThrowingClass>
  { co_return CurrentThrowingClass{}; };
  auto coro_2 = [&]() -> cf::task<int>
  {
    auto result = co_await coro();
    co_return 2;
  };

  coro_2().sync_wait(&thread_pool);
  SUCCEED("Test pass when no exception occurrs");
  handle_error(std::move(thread_pool));
}
TEST_CASE("No Exception during movable class during copy", "[task]")
{
  simple_thread_pool thread_pool;

  using CurrentThrowingClass = ThrowingClass<true, true, false, false>;

  auto coro = []() -> cf::task<CurrentThrowingClass>
  { co_return CurrentThrowingClass{}; };

  coro().sync_wait(&thread_pool);
  SUCCEED("Test pass when no exception occurrs");
  handle_error(std::move(thread_pool));
}

TEST_CASE("No Exception during movable class during copy 2nd level", "[task]")
{
  simple_thread_pool thread_pool;

  using CurrentThrowingClass = ThrowingClass<true, true, false, false>;

  auto coro = []() -> cf::task<CurrentThrowingClass>
  { co_return CurrentThrowingClass{}; };
  auto coro_2 = [&]() -> cf::task<int>
  {
    auto result = co_await coro();
    co_return 2;
  };

  coro_2().sync_wait(&thread_pool);
  SUCCEED("Test pass when no exception occurrs");
  handle_error(std::move(thread_pool));
}
/* Future doesn't supports non movable class
TEST_CASE("Exception during copy in nonmovable class", "[task]")
{
  simple_thread_pool thread_pool;

  using CurrentThrowingClass = NonMovableThrowingClass<true, true>;

  auto coro = []() -> cf::task<CurrentThrowingClass>
  { co_return CurrentThrowingClass{}; };

  REQUIRE_THROWS_AS(coro().run_async(&thread_pool).sync_wait().h(),
                    test_exception_t);
}
TEST_CASE("Exception during copy in nonmovable class 2nd level", "[task]")
{
  simple_thread_pool thread_pool;

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
#pragma endregion

TEST_CASE("Check get function", "[task]")
{
  simple_thread_pool thread_pool;
  auto coro = []() -> cf::task<int> { co_return 2; };
  auto result = coro().sync_wait(&thread_pool);
  REQUIRE(result == 2);
  handle_error(std::move(thread_pool));
}
