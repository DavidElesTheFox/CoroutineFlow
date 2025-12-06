#pragma once

#include <coroutine_flow/__details/final_coroutine.hpp>
#include <coroutine_flow/lib_config.hpp>
#include <coroutine_flow/result_wrapper.hpp>

namespace coroutine_flow::__details
{

template <typename T, typename Extension>
struct final_executor_t
{
#if CF_ENABLE_INJECTIONS
    final_executor_t()
    {
      CF_TEST_INJECTION(testing::test_injection_points_t::object__construct,
                        this);
    }
    ~final_executor_t()
    {
      CF_TEST_INJECTION(testing::test_injection_points_t::object__destruct,
                        this);
    }
#endif
    final_executor_t(final_executor_t&&) = default;
    final_executor_t& operator=(final_executor_t&&) = default;

    static final_coroutine_t execute_on(final_coroutine_t::fall_through_t _,
                                        final_executor_t executor,
                                        result_wrapper_t<T> result)
    {
      if constexpr (std::movable<T>)
      {
        executor.m_extension(std::move(result));
      }
      else
      {
        executor.m_extension(result);
      }
      co_return;
    }
    static final_coroutine_t execute_on(final_executor_t executor,
                                        result_wrapper_t<T> result)
    {
      if constexpr (std::movable<T>)
      {
        executor.m_extension(std::move(result));
      }
      else
      {
        executor.m_extension(result);
      }
      co_return;
    }
    static final_coroutine_t skip(final_executor_t extension)
    {
      CF_PROFILE_SCOPE();
      co_return;
    }
    Extension& get_extension() { return m_extension; }

  private:
    Extension m_extension;
};

} // namespace coroutine_flow::__details