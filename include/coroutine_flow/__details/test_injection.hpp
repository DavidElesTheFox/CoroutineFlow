#pragma once

#include <coroutine_flow/lib_config.hpp>

enum class TestInjectionPoints
{
  SuspendedHandle_Store_BeforeBarrier,
  SuspendedHandle_Store_AfterStored,

  SuspendedHandle_Read_BeforeBarrier,
  SuspendedHandle_Read_BeforeWait,
  SuspendedHandle_Read_AfterWait
}