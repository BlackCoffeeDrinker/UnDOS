
#pragma once

#include <kernel/__core.hpp>

namespace kernel {
enum class ObjectType : uint32_t {
  UNKNOWN = 0,
  THREAD,
  INTERRUPT,
  DEVICE,
  DRIVER
};

// The root header that prefixes EVERY object body in UnDOS
struct object_header_t {
  uint32_t reference_count;
  ObjectType type;
  uint32_t flags;
  // Future expansion: security descriptors, object names, etc.
};

// A base structural type for unified C++ tracking
struct kobject_t {
  protected:
  constexpr kobject_t() noexcept = default;

  public:
  // Helper to jump backward from the body pointer to the object header
  [[nodiscard]] inline object_header_t *get_header() noexcept {
    const auto body_addr = reinterpret_cast<uintptr_t>(this);
    return reinterpret_cast<object_header_t *>(body_addr - sizeof(object_header_t));
  }
};
}// namespace kernel
