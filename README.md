# WIP CoroutineFlow - A Simple coroutine library 
Lightweight c++23 coroutine abstraction layer. If you are looking for a coroutine implementation that it not tight-coupled to a certain scheduler or a complex architecture.

## Overview 

### Simple integration
The goal is to have a coroutine library which requires really small amount of customization. Basically, having a one-liner function which allows the user to customaze a given thread pool behind the coroutine execution flow.

It is achieved via the [tag_invoke](https://open-std.org/JTC1/SC22/WG21/docs/papers/2019/p1895r0.pdf) concept. (The tag_invoke implementation is based on Eric's [prototype implementation](https://gist.github.com/ericniebler/056f5459cf259da526d9ea2279c386bb))
```c++
namespace cf = coroutine_flow;

// Customization code
void tag_invoke(cf::schedule_task_t&&,
                SomeThreadPool* thread_pool,
                std::function<void()> callback)
{
  thread_pool->enque(std::move(callback));
}
```
### Separation of Concerns
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

### Easy call
The 3rd aspect is the easy usage. When one calls a coroutine can decide on which scheduler should be used.
```c++
// Simple usage
int main()
{
    // Running in the background
    cf::run_async(coroutine(), thread_pool);

    // Running blocking
    int result = cf::sync_wait(coroutine(), thread_pool);
}
```

## Simple doesn't mean Stupid

WIP 

TODO:

Support of different 'extensions'. `std::future` or `stdexec::sender` or whatever.

### Tracy Integration

This library optionally integrates tracy. With this one can easily follow what really happens when coroutines are executed with a certain scheduler.

![](./documentation/tracy-example.png)

## Stability

Current Test Coverage: 78.8%. [See the llvm report](./documentation/coverage.report)

Currently 86 tests are defined to check
 - proper allocations/deallocations (promises and awaiters as well)
 - Different return types
 - Exception handlings
 - Execution flow (with injection to be able to simulate some edge cases)

Why not 100% Coverage?
Template execution causes some glitch in the measurements and right now only `std::promise` can be used as the result receiver. And `std::promise` doesn't support classes that are not moveable but copiable.
The other reason is about return type tests when a class throws. These create new
types and new template instantiations that has leaks according to the check. Similar glitch occurs during tag_invoke test.

The coverages per file:
Filename                              |  Regions |   Cover |  Functions | Executed  |     Lines |    Cover |   Branches |    Cover
--------------------------------------|----------|---------|------------|-----------|-----------|----------|------------|---------
__details/scope_exit.hpp              |        3 | 100.00% |          2 |  100.00%  |         4 |  100.00% |          0 |        -
__details/final_coroutine.hpp         |       71 |  54.93% |         22 |   68.18%  |        76 |   85.53% |         12 |   58.33%
__details/coroutine_chain.hpp         |       25 | 100.00% |          8 |  100.00%  |       112 |  100.00% |         14 |   92.86%
__details/task_awaiter.hpp            |       34 |  85.29% |          4 |  100.00%  |        83 |  100.00% |         14 |   78.57%
__details/testing/test_injection.hpp  |        5 | 100.00% |          4 |  100.00%  |        16 |  100.00% |          2 |  100.00%
__details/task_promise.hpp            |       46 |  78.26% |         17 |  100.00%  |        98 |  100.00% |         10 |   80.00%
__details/continuation_data.hpp       |        8 |  87.50% |          8 |   87.50%  |        38 |   84.21% |          0 |        -
__details/final_executor.hpp          |       12 | 100.00% |          8 |  100.00%  |        25 |  100.00% |          0 |        -
extensions/promise_extension.hpp      |       20 |  75.00% |          7 |  100.00%  |        20 |  100.00% |          4 |   75.00%
tag_invoke.hpp                        |        2 |  50.00% |          2 |   50.00%  |         6 |   50.00% |          0 |        -
task.hpp                              |       29 |  96.55% |         10 |  100.00%  |       186 |   97.31% |          8 |   87.50%
--
TOTAL                                 |      255 |  78.43% |         92 |   90.22%  |       664 |   96.23% |         64 |   79.69%