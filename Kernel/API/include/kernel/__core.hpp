#pragma once

#include <stddef.h>
#include <stdint.h>

#include <kernel/__api.hpp>

#include <kernel/fwd/KConfigurationKey.hpp>

#define UNDOS_DRIVER_ENTRY \
  extern "C" void DriverEntry(kernel::KObjectPtr<kernel::KDriverObject> &driver, \
                               const kernel::KConfigurationKeyPtr &driverConfig)

namespace kernel {
template<typename ThingToVersion, size_t Version>
struct Versioned {
  uint16_t version = Version;
  size_t size = sizeof(ThingToVersion);
};
}// namespace kernel
