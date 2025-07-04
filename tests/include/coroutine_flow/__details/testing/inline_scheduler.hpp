#pragma once

#include <coroutine_flow/task.hpp>

namespace coroutine_flow::__details::testing
{
struct inline_scheduler_t
{
};

void tag_invoke(coroutine_flow::schedule_task_t,
                inline_scheduler_t*,
                std::function<void()> callback)
{
  callback();
}
void handle_error(inline_scheduler_t&&)
{
}
} // namespace coroutine_flow::__details::testing