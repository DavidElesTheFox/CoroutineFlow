#include <coroutine_flow/extensions/promise_extension.hpp>
#include <coroutine_flow/task.hpp>

#include <iostream>
#include <thread>

namespace cf = coroutine_flow;

template <typename T>
using Task = cf::task_t<T, cf::extensions::promise_extension_t>;
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

Task<int> foo()
{
  co_return 3;
}

int main()
{
  ThreadFactory scheduler;

  auto my_coro = []() -> Task<int>
  {
    const int foo_result = co_await foo();
    co_return 42;
  };
  run_async(my_coro(), scheduler);
  std::this_thread::sleep_for(std::chrono::seconds{ 3 });
  return 0;
}