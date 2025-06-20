#include <coroutine_flow/task.hpp>

#include <iostream>
#include <thread>

namespace cf = coroutine_flow;

class ThreadFactory
{
  public:
    void create_and_detach(std::function<void()> callback) const
    {
      std::thread([p_callback = std::move(callback)] { p_callback(); })
          .detach();
    }
};

void tag_invoke(cf::schedule_task_t&&,
                const ThreadFactory& factory,
                std::function<void()> callback)
{
  factory.create_and_detach(std::move(callback));
}

cf::task<int> foo()
{
  std::cout << "[foo()] return 3" << std::endl;
  co_return 3;
}

int main()
{
  std::cout << "[main] my_coro()" << std::endl;
  ThreadFactory scheduler;

  auto my_coro = []() -> cf::task<int>
  {
    std::cout << "[my_coro] co_await foo()" << std::endl;
    const int foo_result = co_await foo();
    std::cout << "[my_coro] return 42; // foo_result: " << foo_result
              << std::endl;
    co_return 42;
  };
  std::cout << "[main] run_async()" << std::endl;

  run_async(scheduler, my_coro());
  std::this_thread::sleep_for(std::chrono::seconds{ 3 });
  return 0;
}