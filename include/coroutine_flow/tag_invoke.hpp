#pragma once

#include <type_traits>

namespace coroutine_flow
{
namespace _tag_invoke
{
  void tag_invoke();

  struct func
  {
      template <typename Cpo, typename... Args>
      constexpr auto operator()(Cpo cpo, Args&&... args) const noexcept
      {
        return tag_invoke(std::forward<Cpo>(cpo), std::forward<Args>(args)...);
      }
  };

  template <typename Cpo, typename... Args>
  using tag_invoke_result_t =
      decltype(tag_invoke(std::declval<Cpo>(), std::declval<Args>()...));

  template <typename Cpo, typename... Args>
  constexpr auto try_tag_invoke(int) noexcept(
      noexcept(tag_invoke(std::declval<Cpo>(), std::declval<Args>()...)))
      -> decltype(static_cast<void>(tag_invoke(std::declval<Cpo>(),
                                               std::declval<Args>()...)),
                  std::true_type{})
  {
    return {};
  }

  template <typename Cpo, typename... Args>
  constexpr std::false_type try_tag_invoke(...) noexcept(false)
  {
    return {};
  }

} // namespace _tag_invoke
inline constexpr _tag_invoke::func tag_invoke;

template <typename Cpo>
using tag_t = std::decay_t<Cpo>;

template <typename Cpo, typename... Args>
inline constexpr bool is_tag_invocable_v =
    _tag_invoke::try_tag_invoke<Cpo, Args...>(0)();

template <typename Cpo, typename... Args>
inline constexpr bool is_noexcept_tag_invocable_v =
    noexcept(_tag_invoke::try_tag_invoke<Cpo, Args...>(0)());

template <typename Cpo, typename... Args>
using is_tag_invocable = std::bool_constant<is_tag_invocable_v<Cpo, Args...>>;

template <typename Cpo, typename... Args>
using is_noexcept_tag_invocable =
    std::bool_constant<is_noexcept_tag_invocable_v<Cpo, Args...>>;
} // namespace coroutine_flow