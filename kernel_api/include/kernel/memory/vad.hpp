#pragma once

#include <kernel/__core.hpp>
#include <kernel/memory/address.hpp>
#include <kernel/adt/avl_tree.hpp>
#include <kernel/memory/protect.hpp>

#include <stdint.h>

namespace kernel::vmm {

struct VirtualAddressDescriptor {
  VirtualAddress start;
  VirtualAddress end;
  ProtectFlags flags;

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
