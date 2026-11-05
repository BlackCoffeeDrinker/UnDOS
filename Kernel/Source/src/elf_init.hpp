#pragma once

#include <kernel/kerror.hpp>
#include <stddef.h>
#include <stdint.h>

// Module constructor tables (kernel-loaded drivers and subsystems). Header
// only so the hosted tests can exercise validation and ordering without the
// VFS-backed loader. See Docs/KernelGlobalConstructors.md.
namespace kernel::elf {

// One relocated function-pointer table (.ctors or .init_array) inside a
// loaded image. `address` is the table's run-time address.
struct InitTable {
  uintptr_t address{0};
  size_t count{0};
};

// Index order is execution order: .ctors first, then .init_array.
inline constexpr size_t kInitTableCtors = 0;
inline constexpr size_t kInitTableInitArray = 1;
inline constexpr size_t kInitTableCount = 2;

struct InitTables {
  uintptr_t image_base{0};
  size_t image_size{0};
  bool user_mode{false};
  InitTable tables[kInitTableCount]{};
};

namespace detail {
inline bool InImage(const InitTables &t, uintptr_t address, size_t bytes) noexcept {
  if (address < t.image_base) return false;
  const uintptr_t offset = address - t.image_base;
  return offset <= t.image_size && bytes <= t.image_size - offset;
}

inline bool IsSentinel(uintptr_t value) noexcept {
  return value == 0 || value == static_cast<uintptr_t>(-1);
}
}// namespace detail

// Validates every table and entry, then runs them once in order. Nothing
// runs unless the whole set is valid. Never runs user-mode images: their
// constructors execute in ring 3 from their own start-up code.
inline kstd::error_code RunInitTables(const InitTables &t) noexcept {
  using Fn = void (*)();
  if (t.user_mode) return KernelError::InvalidArgument;

  for (const auto &table: t.tables) {
    if (table.count == 0) continue;
    if (table.count > SIZE_MAX / sizeof(Fn) ||
        (table.address % alignof(Fn)) != 0 ||
        !detail::InImage(t, table.address, table.count * sizeof(Fn))) {
      return KernelError::InvalidExecutable;
    }
    const auto *entries = reinterpret_cast<const Fn *>(table.address);
    for (size_t i = 0; i < table.count; ++i) {
      const auto value = reinterpret_cast<uintptr_t>(entries[i]);
      if (detail::IsSentinel(value)) continue;
      if (!detail::InImage(t, value, 1)) return KernelError::InvalidExecutable;
    }
  }

  for (const auto &table: t.tables) {
    const auto *entries = reinterpret_cast<const Fn *>(table.address);
    for (size_t i = 0; i < table.count; ++i) {
      if (detail::IsSentinel(reinterpret_cast<uintptr_t>(entries[i]))) continue;
      entries[i]();
    }
  }
  return {};
}

}// namespace kernel::elf
