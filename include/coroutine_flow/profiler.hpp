#pragma once

#include <coroutine_flow/lib_config.hpp>

#if CF_USE_TRACY
#include <sstream>
#include <tracy/Tracy.hpp>

template <typename... Args>
inline std::string format_profile_notes(Args&&... args)
{
  std::ostringstream os;
  ((os << std::forward<Args>(args) << " | "), ...);
  return os.str();
}

#define CF_PROFILE_SCOPE() ZoneScoped
#define CF_PROFILE_SCOPE_N(name) ZoneScopedN(name)
#define CF_PROFILE_ZONE(varname, name) ZoneNamedN(varname, name, true)
#define CF_PROFILE_MARK(name)                                                  \
  {                                                                            \
    CF_PROFILE_SCOPE_N(name);                                                  \
  }
#define CF_ATTACH_NOTE(...)                                                    \
  {                                                                            \
    std::string str = format_profile_notes(__VA_ARGS__);                       \
    ZoneText(str.c_str(), str.size());                                         \
  }
#define CF_PROFILER_ACTIVE 1
#else
#define CF_PROFILE_SCOPE()
#define CF_PROFILE_SCOPE_N(name)
#define CF_PROFILE_ZONE(varname, name)
#define CF_PROFILE_MARK(name)
#define CF_ATTACH_NOTE(...)
#define CF_PROFILER_ACTIVE 0

#endif