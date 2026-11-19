#pragma once

#include <stdint.h>
#include <kernel/fwd/KObjectPtr.hpp>

namespace kernel {
struct KSurface;

// GDI-style device context handle: bits 0-15 are the slot index, bits 16-31
// are the slot's generation tag (bumped on every Close(), rejecting stale
// handles that alias a reused index).
using HDC = uint32_t;
constexpr HDC INVALID_HDC = 0;

// Owners moved out of a closed slot. Drop it after the USER scene lock is
// released: the last release can free heap memory and wait on the arena.
struct DetachedDC {
  KObjectPtr<KSurface> target;
  KObjectPtr<KObject> font;
};
} // namespace kernel
