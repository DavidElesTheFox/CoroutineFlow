/*
 - check exception handling:
    during promise construction
    during result copy
    from 1st level
    from 2nd level
    from 3rd level
 - when suspenned called after object destroyed. (functional)
 - return types:
    reference
    pointer
    tuple
    non copyable (rvalue)
    non moveable (lvalue)
    has no default constructor
 - nothrow coroutine
*/
#include <catch2/catch_test_macros.hpp>

#include <coroutine_flow/profiler.hpp>
#include <coroutine_flow/task.hpp>

#include <future>
#include <list>
#include <thread>

namespace cf = coroutine_flow;
using namespace std::chrono_literals;

struct NonCopyableClass
{
    NonCopyableClass() = default;
    NonCopyableClass(NonCopyableClass&&) = default;
    NonCopyableClass(const NonCopyableClass&) = delete;

    NonCopyableClass& operator=(NonCopyableClass&&) = default;
    NonCopyableClass& operator=(const NonCopyableClass&) = delete;
};

struct NonMoveableClass
{
    NonMoveableClass() = default;
    NonMoveableClass(NonMoveableClass&&) = delete;
    NonMoveableClass(const NonMoveableClass&) = default;

    NonMoveableClass& operator=(NonMoveableClass&&) = delete;
    NonMoveableClass& operator=(const NonMoveableClass&) = default;
};

struct OnExit
{
    ~OnExit() { callback(); }
    std::function<void()> callback;
};

class SimpleThreadPool
{
  public:
    friend void tag_invoke(cf::schedule_task_t,
                           SimpleThreadPool* pool,
                           std::function<void()> callback)
    {
      pool->m_threads.emplace_back(
          [p_callback = callback](std::stop_token stop_token)
          {
            CF_PROFILE_SCOPE_N("SimpleThreadPool::tag_invoke::schedule");
            if (stop_token.stop_requested())
            {
              return;
            }
            std::cout << "[SimpleThreadPool::tag_invoke] thread start work "
                      << std::this_thread::get_id() << std::endl;
            p_callback();
            std::cout << "[SimpleThreadPool::tag_invoke] thread finished work "
                      << std::this_thread::get_id() << std::endl;
          });
    }
    SimpleThreadPool() = default;
    SimpleThreadPool(const SimpleThreadPool&) = delete;
    SimpleThreadPool(SimpleThreadPool&&) = default;

    SimpleThreadPool& operator=(const SimpleThreadPool&) = delete;
    SimpleThreadPool& operator=(SimpleThreadPool&&) = default;

    ~SimpleThreadPool()
    {
      for (auto& thread : m_threads)
      {
        thread.request_stop();
        if (thread.joinable())
        {
          thread.join();
        }
      }
    }

  private:
    std::list<std::jthread> m_threads;
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
      std::cout << "[Event::trigger] " << m_name << std::endl;

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

#pragma region Execution Tests

TEST_CASE("Neasted coroutine level 1", "[task]")
{
  SimpleThreadPool thread_pool;
  auto [called_event, called_token] = Event::create("coroutine is called");

  auto coro_1 = [p_called_event =
                     std::move(called_event)]() mutable -> cf::task<int>
  {
    p_called_event.trigger();
    co_return 1;
  };
  coro_1().run_async(&thread_pool);

  REQUIRE(called_token.is_triggered(c_test_case_timeout));
}

TEST_CASE("Neasted coroutine level 2", "[task]")
{
  SimpleThreadPool thread_pool;
  auto [called_event_1, called_token_1] =
      Event::create("coroutine 1 is called");

  auto coro_1 = [p_called_event =
                     std::move(called_event_1)]() mutable -> cf::task<int>
  {
    p_called_event.trigger();
    co_return 1;
  };

  auto [called_event_2, called_token_2] =
      Event::create("coroutine 2 is called");

  auto coro_2 = [p_called_event = std::move(called_event_2),
                 &coro_1]() mutable -> cf::task<int>
  {
    int result = co_await coro_1();
    REQUIRE(result == 1);
    p_called_event.trigger();
    co_return 2;
  };

  coro_2().run_async(&thread_pool);

  REQUIRE(called_token_1.is_triggered(c_test_case_timeout));
  REQUIRE(called_token_2.is_triggered(c_test_case_timeout));
}

TEST_CASE("Neasted coroutine level 3", "[task]")
{
  SimpleThreadPool thread_pool;

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
}

TEST_CASE("Neasted coroutine level 4", "[task]")
{
  SimpleThreadPool thread_pool;

  // Coroutine 1
  auto [called_event_1, called_token_1] =
      Event::create("coroutine 1 is called");

  auto coro_1 = [p_called_event =
                     std::move(called_event_1)]() mutable -> cf::task<int>
  {
    p_called_event.trigger();
    co_return 1;
  };

  // Coroutine 2
  auto [called_event_2, called_token_2] =
      Event::create("coroutine 2 is called");

  auto coro_2 = [p_called_event = std::move(called_event_2),
                 &coro_1]() mutable -> cf::task<int>
  {
    int result = co_await coro_1();
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
    int result = co_await coro_2();
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
    int result = co_await coro_3();
    REQUIRE(result == 3);
    p_called_event.trigger();
    co_return 4;
  };

  coro_4().run_async(&thread_pool);

  REQUIRE(called_token_1.is_triggered(c_test_case_timeout));
  REQUIRE(called_token_2.is_triggered(c_test_case_timeout));
  REQUIRE(called_token_3.is_triggered(c_test_case_timeout));
  REQUIRE(called_token_4.is_triggered(c_test_case_timeout));
}

TEST_CASE("Neasted coroutine level 2; waits 2", "[task]")
{
  SimpleThreadPool thread_pool;
  auto [called_event_1, called_token_1] =
      Event::create("coroutine 1 is called");
  std::atomic_uint32_t coro_1_call_count = 0;
  auto coro_1 = [p_called_event = &called_event_1,
                 &coro_1_call_count]() mutable -> cf::task<int>
  {
    p_called_event->trigger();
    coro_1_call_count++;
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
    p_called_event.trigger();
    p_coro_call_count++;
    co_return 2;
  };

  coro_2().run_async(&thread_pool);

  REQUIRE(called_token_1.is_triggered(c_test_case_timeout));
  REQUIRE(called_token_2.is_triggered(c_test_case_timeout));
  REQUIRE(coro_1_call_count == 2);
  REQUIRE(coro_2_call_count == 1);
}

TEST_CASE("Neasted coroutine level 3; waits 2", "[task]")
{
  CF_PROFILE_SCOPE();
  SimpleThreadPool thread_pool;

  // Coroutine 1
  auto [called_event_1, called_token_1] =
      Event::create("coroutine 1 is called");
  std::atomic_uint32_t coro_1_call_count = 0;

  auto coro_1 = [p_called_event = std::move(called_event_1),
                 &p_coro_call_count =
                     coro_1_call_count]() mutable -> cf::task<int>
  {
    CF_PROFILE_MARK("coro_1");
    p_called_event.trigger();
    p_coro_call_count++;
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
    p_called_event.trigger();
    p_coro_call_count++;
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
    p_called_event.trigger();
    p_coro_call_count++;

    co_return 3;
  };

  coro_3().run_async(&thread_pool);

  REQUIRE(called_token_1.is_triggered(c_test_case_timeout));
  REQUIRE(called_token_2.is_triggered(c_test_case_timeout));
  REQUIRE(called_token_3.is_triggered(c_test_case_timeout));
  REQUIRE(coro_1_call_count == 4);
  REQUIRE(coro_2_call_count == 2);
  REQUIRE(coro_3_call_count == 1);
}

TEST_CASE("Neasted coroutine level 4; waits 2", "[task]")
{
  CF_PROFILE_SCOPE();

  SimpleThreadPool thread_pool;

  // Coroutine 1
  auto [called_event_1, called_token_1] =
      Event::create("coroutine 1 is called");
  std::atomic_uint32_t coro_1_call_count = 0;

  auto coro_1 = [p_called_event = std::move(called_event_1),
                 &p_coro_call_count =
                     coro_1_call_count]() mutable -> cf::task<int>
  {
    CF_PROFILE_MARK("coro_1");
    p_called_event.trigger();
    p_coro_call_count++;
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
    p_called_event.trigger();
    p_coro_call_count++;
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
    p_called_event.trigger();
    p_coro_call_count++;
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
    p_called_event.trigger();
    p_coro_call_count++;
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
}

TEST_CASE("Neasted coroutine level 2; waits 3", "[task]")
{
  SimpleThreadPool thread_pool;
  auto [called_event_1, called_token_1] =
      Event::create("coroutine 1 is called");
  std::atomic_uint32_t coro_1_call_count = 0;
  auto coro_1 = [p_called_event = &called_event_1,
                 &coro_1_call_count]() mutable -> cf::task<int>
  {
    p_called_event->trigger();
    coro_1_call_count++;
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
    p_called_event.trigger();
    p_coro_call_count++;
    co_return 2;
  };

  coro_2().run_async(&thread_pool);

  REQUIRE(called_token_1.is_triggered(c_test_case_timeout));
  REQUIRE(called_token_2.is_triggered(c_test_case_timeout));
  REQUIRE(coro_1_call_count == 3);
  REQUIRE(coro_2_call_count == 1);
}

TEST_CASE("Neasted coroutine level 3; waits 3", "[task]")
{
  CF_PROFILE_SCOPE();
  SimpleThreadPool thread_pool;

  // Coroutine 1
  auto [called_event_1, called_token_1] =
      Event::create("coroutine 1 is called");
  std::atomic_uint32_t coro_1_call_count = 0;

  auto coro_1 = [p_called_event = std::move(called_event_1),
                 &p_coro_call_count =
                     coro_1_call_count]() mutable -> cf::task<int>
  {
    CF_PROFILE_MARK("coro_1");
    p_called_event.trigger();
    p_coro_call_count++;
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

    p_called_event.trigger();
    p_coro_call_count++;
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

    p_called_event.trigger();
    p_coro_call_count++;

    co_return 3;
  };

  coro_3().run_async(&thread_pool);

  REQUIRE(called_token_1.is_triggered(c_test_case_timeout));
  REQUIRE(called_token_2.is_triggered(c_test_case_timeout));
  REQUIRE(called_token_3.is_triggered(c_test_case_timeout));
  REQUIRE(coro_1_call_count == 9);
  REQUIRE(coro_2_call_count == 3);
  REQUIRE(coro_3_call_count == 1);
}

TEST_CASE("Neasted coroutine level 4; waits 3", "[task]")
{
  CF_PROFILE_SCOPE();

  SimpleThreadPool thread_pool;

  // Coroutine 1
  auto [called_event_1, called_token_1] =
      Event::create("coroutine 1 is called");
  std::atomic_uint32_t coro_1_call_count = 0;

  auto coro_1 = [p_called_event = std::move(called_event_1),
                 &p_coro_call_count =
                     coro_1_call_count]() mutable -> cf::task<int>
  {
    CF_PROFILE_MARK("coro_1");
    p_called_event.trigger();
    p_coro_call_count++;
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

    p_called_event.trigger();
    p_coro_call_count++;
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

    p_called_event.trigger();
    p_coro_call_count++;
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

    p_called_event.trigger();
    p_coro_call_count++;
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
}

TEST_CASE("Mixed data", "[task]")
{
  CF_PROFILE_SCOPE();

  SimpleThreadPool thread_pool;
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
}
#pragma endregion
