#pragma once

#include <format>
#include <mutex>
#include <shared_mutex>
#include <stacktrace>
#include <unordered_map>
#include <vector>

namespace coroutine_flow::__details::testing
{
class error_report_t
{
  public:
    error_report_t(std::string_view message,
                   std::stacktrace construction_location,
                   std::stacktrace error_location)
        : m_message(message.data())
        , m_construction_location(std::move(construction_location))
        , m_error_location(std::move(error_location))
    {
    }

  private:
    std::string m_message;
    std::stacktrace m_construction_location;
    std::stacktrace m_error_location;
};
/**
 * This class suited to detect not destructed object.
 */
class memory_sentinel_t
{
  private:
    struct entry_t
    {
        std::stacktrace construction_info;
        std::optional<std::stacktrace> destruction_info;
    };

  public:
    class error_report_t
    {
      public:
        error_report_t(std::string_view message,
                       std::stacktrace construction_location,
                       std::stacktrace error_location)
            : m_message(message.data())
            , m_construction_location(std::move(construction_location))
            , m_error_location(std::move(error_location))
        {
        }
        const std::string& get_message() const { return m_message; }
        const std::stacktrace& get_construction_location() const
        {
          return m_construction_location;
        }

        const std::stacktrace& get_error_location() const
        {
          return m_error_location;
        }

      private:
        std::string m_message;
        std::stacktrace m_construction_location;
        std::stacktrace m_error_location;
    };
    void on_construct(void* object,
                      std::stacktrace location = std::stacktrace::current())
    {
      std::unique_lock lock{ m_memory_map_mutex };
      auto& entries = m_memory_map[object];
      entry_t new_entry = { .construction_info = std::move(location),
                            .destruction_info = std::nullopt };
      entries.push_back(std::move(new_entry));
    }

    void on_destruct(void* object,
                     std::stacktrace location = std::stacktrace::current())
    {
      std::unique_lock lock{ m_memory_map_mutex };
      auto& entries = m_memory_map[object];
      if (entries.empty())
      {
        return; // During move it might happen
      }
      auto& entry = entries.back();

      entry.destruction_info = std::move(location);
    }

    std::vector<std::tuple<void*, std::stacktrace>> collect_memory_leaks() const
    {
      std::shared_lock lock{ m_memory_map_mutex };
      std::vector<std::tuple<void*, std::stacktrace>> result;
      for (const auto& [object, entries] : m_memory_map)
      {
        if (entries.empty())
        {
          continue;
        }
        if (entries.back().destruction_info == std::nullopt)
        {
          result.emplace_back(object, entries.back().construction_info);
        }
      }
      return result;
    }

  private:
    std::unordered_map<void*, std::vector<entry_t>> m_memory_map;
    mutable std::shared_mutex m_memory_map_mutex;
};
} // namespace coroutine_flow::__details::testing