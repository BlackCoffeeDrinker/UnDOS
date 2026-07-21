#pragma once

#include <kernel/__core.hpp>
#include <kernel/memory/address.hpp>
#include <kernel/adt/avl_tree.hpp>
#include <kernel/memory/protect.hpp>

#include <stdint.h>

namespace kernel::vmm {

// Describes what physically backs the pages covered by a VAD once they are
// (or need to be) faulted in.
enum class BackingType : uint8_t {
  // Ordinary, demand-zeroed/demand-paged memory backed by physical frames
  // allocated on first touch (e.g. heap, user data/stack).
  ANONYMOUS,
  // Backed by the pagefile: the page's contents live on disk and must be
  // read back in on fault.
  SWAPPED,
  // Never allowed to fault (e.g. permanently resident kernel structures);
  // any page fault against such a region is a bug and must be fatal.
  PINNED,
};

struct VirtualAddressDescriptor {
  VirtualAddress start;
  VirtualAddress end;
  ProtectFlags flags;
  BackingType backing = BackingType::ANONYMOUS;

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
