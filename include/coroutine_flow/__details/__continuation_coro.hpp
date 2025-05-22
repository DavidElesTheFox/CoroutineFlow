#pragma once

#include <coroutine>

namespace coroutine_flow::__details
{
/*
class __continuation_coro
{
protected:
struct promise_t;

public:
using handle_t = std::coroutine_handle<promise_t>;

protected:
struct awaiter_t
{
    handle_t continuation_handle;
    bool await_ready() { return false; }
    void await_resume() {}

    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<> suspended_handle)
    {
      if (continuation_handle != nullptr)
      {
        continuation_handle.promise().next = suspended_handle;
        return continuation_handle;
      }
      else
      {
        return suspended_handle;
      }
    }
};

public:
explicit __continuation_coro(handle_t handle)
    : m_handle(std::move(handle))
{
}

awaiter_t operator co_await() { return { m_handle }; }

private:
handle_t m_handle;
};

struct __continuation_coro::promise_t
{
std::coroutine_handle<> next;
__continuation_coro get_return_object()
{
  return __continuation_coro(handle_t::from_promise(*this));
}

std::suspend_always initial_suspend() { return {}; }
awaiter_t final_suspend() { return { next }; }
void return_void() {}
};
*/
/*
template<typename T>
using my_task = cf::task_base<T, result_as_promise_t<T>>;

concept continuation_definition
template<typename T>
struct result_as_promise_t
{
   std::promise<T> result;
   __continuation_coro operator()(std::expected<T> result)
   {
      result.set_value...
   }
   T get() { result.feature().get();}
};

*/
} // namespace coroutine_flow::__details
