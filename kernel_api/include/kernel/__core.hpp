#pragma once

#include <stddef.h>
#include <stdint.h>

#define UNDOS_KERNEL_API_DEF __attribute__((visibility("default"), used))
#define UNDOS_HAL_API_DEF __attribute__((visibility("default"), used))
#define UNDOS_KERNEL_CPP_API __attribute__((visibility("default"), used))

#define UNDOS_KERNEL_PUBLIC_V1API(return_type, func_name, ...) \
  UNDOS_KERNEL_API_DEF return_type func_name(__VA_ARGS__) noexcept asm(#func_name)

#define UNDOS_HAL_PUBLIC_V1API(return_type, func_name, ...) \
  UNDOS_HAL_API_DEF return_type func_name(__VA_ARGS__) noexcept asm(#func_name)

#define UNDOS_DRIVER_ENTRY extern "C" void DriverEntry(kernel::KObjectPtr<kernel::KDriverObject> &driver)

namespace kernel {
template<typename ThingToVersion, size_t Version>
struct Versioned {
  uint16_t version = Version;
  size_t size = sizeof(ThingToVersion);
};
}// namespace kernel
