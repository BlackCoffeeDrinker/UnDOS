#pragma once

#include <kernel/__core.hpp>
#include <kernel/StaticIdentifier.hpp>

namespace kernel {

struct ObjectTypeTag {
  // Unique seed for ObjectType to minimize collisions with other potential StaticIdentifiers
  static constexpr uint64_t seed = 0x84222325cbf1291bull;
};

using ObjectType = StaticIdentifier<ObjectTypeTag>;

consteval ObjectType operator""_type(const char *s, size_t n) {
  (void) n;
  return ObjectType::from_string(kstd::string_view{s});
}

constexpr auto TYPE_DRIVER = "driver"_type;
constexpr auto TYPE_DEVICE = "device"_type;
constexpr auto TYPE_BUS = "device_bus"_type;
constexpr auto TYPE_DIRECTORY = "directory"_type;
constexpr auto TYPE_VMM = "vmm"_type;
constexpr auto TYPE_MODULE = "module"_type;
constexpr auto TYPE_INTERRUPT = "interrupt"_type;
constexpr auto TYPE_THREAD = "thread"_type;
constexpr auto TYPE_PROCESS = "process"_type;
constexpr auto TYPE_VOLUME_MOUNT = "volume_mount"_type;
constexpr auto TYPE_FILE_SYSTEM = "file_system"_type;
constexpr auto TYPE_FILE = "file"_type;
constexpr auto TYPE_SUBSYSTEM = "subsystem"_type;

template<ObjectType T>
struct ObjectTypeOf {};


}// namespace kernel
