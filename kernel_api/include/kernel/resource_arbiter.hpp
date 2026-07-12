
#pragma once

#include <kernel/resource.hpp>

// Allocation-agnostic reservation/conflict core. Everything here operates over
// already-built model objects and a caller-supplied node allocator, so the same
// logic is exercised by the hosted unit tests (array/stack nodes) and by the
// kernel resource manager (KE_Malloc'd nodes).
namespace kernel::resource {

// Inclusive-end overlap test: do [aStart,aEnd] and [bStart,bEnd] intersect?
constexpr bool RangesOverlap(uint32_t aStart, uint32_t aEnd, uint32_t bStart, uint32_t bEnd) {
  return aStart <= bEnd && bStart <= aEnd;
}

// Inclusive end of a [start,length] span (length 0 is treated as a single unit).
constexpr uint32_t SpanEnd(uint32_t start, uint32_t length) {
  return start + (length ? length - 1 : 0);
}

// A requirement conflicts with the reserved set when some already-reserved
// descriptor has the same ResourceType, an overlapping range, and the two are
// not both Shared. `reserved` is any range-for-iterable of descriptor-like
// items exposing `type`, `share`, `start`, and `length`.
template<typename Reserved>
bool ConflictsWithReserved(const IoResourceDescriptor &desc, const Reserved &reserved) {
  const uint32_t descEnd = SpanEnd(desc.start, desc.length);
  for (const auto &r: reserved) {
    if (r.type != desc.type) continue;
    if (!RangesOverlap(desc.start, descEnd, r.start, SpanEnd(r.start, r.length))) continue;
    const bool bothShared = desc.share == ResourceShare::Shared && r.share == ResourceShare::Shared;
    if (!bothShared) return true;
  }
  return false;
}

// Attempt to reserve every descriptor of a single alternative (fixed-only): all
// descriptors must be free of conflicts. On success emits one
// CmPartialResourceDescriptor per descriptor into `out` via `alloc` (a functor
// returning CmPartialResourceDescriptor* or nullptr on failure) and returns
// true; on any conflict returns false without emitting anything.
template<typename Reserved, typename Allocator>
bool TryReserveAlternative(const IoResourceList &alt, const Reserved &reserved, CmResourceList &out, Allocator alloc) {
  for (const auto &desc: alt.descriptors) {
    if (ConflictsWithReserved(desc, reserved)) return false;
  }
  for (const auto &desc: alt.descriptors) {
    CmPartialResourceDescriptor *node = alloc();
    if (!node) return false;
    node->type = desc.type;
    node->share = desc.share;
    node->flags = 0;
    node->start = desc.start;
    node->length = desc.length ? desc.length : 1;
    out.descriptors.push_back(node);
  }
  return true;
}

}// namespace kernel::resource
