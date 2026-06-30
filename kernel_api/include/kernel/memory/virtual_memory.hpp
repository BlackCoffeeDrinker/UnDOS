#pragma once
#include <kernel/__core.hpp>
#include <utility.hpp>

UNDOS_KERNEL_API [[nodiscard]] void *KE_Malloc(size_t size) noexcept;
UNDOS_KERNEL_API void KE_Free(void *ptr) noexcept;

namespace kernel {
template<typename T, typename... Args>
T *KE_CreateObject(Args &&...args) {
  void *p = KE_Malloc(sizeof(T));
  if (!p) return nullptr;
  return new (p) T(kstd::forward<Args>(args)...);
}

namespace vmm {
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


}// namespace vmm
}// namespace kernel
