#pragma once

#include <coroutine_flow/__details/testing/memory_sentinel.hpp>
#include <coroutine_flow/__details/testing/test_injection.hpp>

#include <catch2/catch_test_macros.hpp>

#include <sstream>

namespace coroutine_flow::__details::testing
{
class memory_check_t
{
  public:
    memory_check_t()
    {
      test_injection_dispatcher_t::instance().register_callback(
          test_injection_points_t::object__construct,
          [&](void* object) { m_memory_sentinel.on_construct(object); });
      test_injection_dispatcher_t::instance().register_callback(
          test_injection_points_t::object__destruct,
          [&](void* object) { m_memory_sentinel.on_destruct(object); });
    }
    void check()
    {
      test_injection_dispatcher_t::instance().clear();
      auto leaks = m_memory_sentinel.collect_memory_leaks();
      std::ostringstream os;
      if (leaks.empty() == false)
      {
        for (const auto& [object, location] : leaks)
        {
          os << "object: " << object << " at: \n" << location << std::endl;
        }
        FAIL("Memory leak found. Errors:\n" + os.str());
      }
    }

  private:
    memory_sentinel_t m_memory_sentinel;
};
} // namespace coroutine_flow::__details::testing