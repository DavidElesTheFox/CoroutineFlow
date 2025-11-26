#include <iostream>

#include <coroutine_flow/tag_invoke.hpp>

namespace cf = coroutine_flow;

struct MyCpo
{
};

class SomeLogic
{
  public:
    template <typename T>
    int operator()(int a, const T& object) const
    {
      int x = 2 * a;
      if constexpr (cf::is_tag_invocable_v<MyCpo, cf::tag_t<T>>)
      {
        x = cf::tag_invoke(MyCpo{}, object);
      }
      return x;
    }
};

class Foo
{
  public:
    friend int tag_invoke(MyCpo&&, const Foo& f) { return 5; }
};

int main()
{
  SomeLogic logic;
  const int result = logic(20, Foo{});
  const int result_without_tag = logic(20, 4);
  return 0;
}