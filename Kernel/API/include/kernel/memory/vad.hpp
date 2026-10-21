#pragma once

#include <kernel/__core.hpp>
#include <kernel/adt/avl_tree.hpp>
#include <kernel/fwd/KObjectPtr.hpp>
#include <kernel/memory/address.hpp>
#include <kernel/memory/protect.hpp>

#include <stdint.h>

namespace kernel {
// Forward-declared to avoid pulling in the full KObject/KObjectT include
// chain here: vad.hpp is transitively included from kernel/kobject/KObject.hpp
// (via virtual_memory.hpp), so a full include here would create a cycle.
struct KMemoryBackingObject;
}// namespace kernel

namespace kernel::vmm {

struct VirtualAddressDescriptor {
  VirtualAddress start;
  VirtualAddress end;
  ProtectFlags flags;
  // The backing object describing what physically backs the pages covered
  // by this VAD once they are (or need to be) faulted in. Kept alive for as
  // long as this VAD references it. May be null for
  // lightweight/kernel-internal VADs that don't have a distinct backing
  // object (e.g. scratch mappings) -- such VADs are treated as anonymous.
  KObjectPtr<KMemoryBackingObject> backing;
  uint16_t lock_count = 0;

  adt::AvlNode<VirtualAddressDescriptor> node;

  bool operator<(const VirtualAddressDescriptor &other) const noexcept { return start < other.start; }
  bool operator==(const VirtualAddressDescriptor &other) const noexcept { return start == other.start; }

  // For key-based lookup
  bool operator<(const VirtualAddress addr) const noexcept { return start < addr; }
  friend bool operator<(const VirtualAddress addr, const VirtualAddressDescriptor &vad) noexcept { return addr < vad.start; }
  friend bool operator==(const VirtualAddress addr, const VirtualAddressDescriptor &vad) noexcept { return addr >= vad.start && addr < vad.end; }
};

using VadTree = adt::AvlTree<VirtualAddressDescriptor, &VirtualAddressDescriptor::node>;
}// namespace kernel::vmm
