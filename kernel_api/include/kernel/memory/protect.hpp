#pragma once
#include <kernel/__core.hpp>

namespace kernel::vmm {

// Platform-agnostic protection flags
enum class ProtectFlags : uint32_t {
  NONE = 0,
  READ = 1 << 0,
  WRITE = 1 << 1,
  EXECUTE = 1 << 2,
  USER = 1 << 3,
  NOCACHE = 1 << 4
};

constexpr ProtectFlags operator|(ProtectFlags a, ProtectFlags b) noexcept { return static_cast<ProtectFlags>(kstd::to_underlying(a) | kstd::to_underlying(b)); }
constexpr ProtectFlags operator&(ProtectFlags a, ProtectFlags b) noexcept { return static_cast<ProtectFlags>(kstd::to_underlying(a) & kstd::to_underlying(b)); }
constexpr ProtectFlags operator~(ProtectFlags a) noexcept { return static_cast<ProtectFlags>(~kstd::to_underlying(a)); }
constexpr bool has_flag(ProtectFlags mask, ProtectFlags flag) noexcept { return (mask & flag) == flag; }

} // namespace kernel::vmm
