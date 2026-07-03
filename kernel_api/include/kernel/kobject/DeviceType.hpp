#pragma once

#include <kernel/StaticIdentifier.hpp>

namespace kernel {

struct DeviceTypeTag {
    // Unique seed for DeviceType to minimize collisions
    static constexpr uint64_t seed = 0x5a1e7b1d9c2f4a3bull;
};

using DeviceType = StaticIdentifier<DeviceTypeTag>;

consteval DeviceType operator""_dev(const char *s, size_t n) {
  return DeviceType::from_string(kstd::string_view{s, n});
}

namespace device_type {
constexpr auto Unknown = DeviceType{0};
constexpr auto Bus = "bus"_dev;
constexpr auto Disk = "disk"_dev;
constexpr auto Keyboard = "keyboard"_dev;
constexpr auto Mouse = "mouse"_dev;
constexpr auto Video = "video"_dev;
constexpr auto Serial = "serial"_dev;
constexpr auto Parallel = "parallel"_dev;
} // namespace device_type

} // namespace kernel
