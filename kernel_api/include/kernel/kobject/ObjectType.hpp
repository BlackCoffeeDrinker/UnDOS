#pragma once

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
constexpr auto TYPE_BUS_DEVICE = "bus_device"_type;
constexpr auto TYPE_FUNCTIONAL_DEVICE = "functional_device"_type;
constexpr auto TYPE_DIRECTORY = "directory"_type;
constexpr auto TYPE_BUS = "bus"_type;
constexpr auto TYPE_VMM = "vmm"_type;
constexpr auto TYPE_MODULE = "module"_type;
constexpr auto TYPE_INTERRUPT = "interrupt"_type;

} // namespace kernel
