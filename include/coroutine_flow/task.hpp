#pragma once

#include <coroutine_flow/tag_invoke.hpp>

namespace coroutine_flow
{

template <typename T>
class task
{
  protected:
  public:
    void sync_wait();
    void run_async();
};
} // namespace coroutine_flow