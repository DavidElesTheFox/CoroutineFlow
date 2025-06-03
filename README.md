# WIP CoroutineFlow - A Simple coroutine library 
Lightweight c++23 coroutine abstraction layer. 

## Overview - Simple

WIP

First of all the goal is to have a coroutine library which requires really small amount of customization.
```c++
namespace cf = coroutine_flow;

// Customization code
void tag_invoke(cf::schedule_task_t&&,
                SomeThreadPool* thread_pool,
                std::function<void()> callback)
{
  thread_pool.enqueu(std::move(callback));
}
```

The other key aspect is the where to run the coroutine. A couroutine doesn't defines the scheduler or thread where it runs by default. It makes easier to separate the different concerns.
```c++
// Coroutine definitions
cf::task<> some_concurrent_task()
{
    co_return;
}
cf::task<int> coroutine()
{
    // No scheduler/thread_pool magic
    co_await some_concurrent_task();
    co_return 2;
}
```

The 3rd aspect is the easy usage. When one calls a coroutine can decide on which scheduler should be used.
```c++
// Simple usage
int main()
{
    // Running in the background
    coroutine().run_async(thread_pool);

    int result = coroutine().sync_wait(thread_pool).get();
}
```

## Simple doesn't mean Stupid

WIP 

TODO:

Support of different 'extensions'. `std::future` or `stdexec::sender` or whatever.

## Stability

WIP

Tests are running with: 
- thread sanitizer
- memory sanitizer

Test covarage with:
 - Unit tests
 - Functional tests
 - Stress tests
 - Benchmarks