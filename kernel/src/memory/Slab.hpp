
#pragma once
#include <kernel/common/DoubleList.hpp>
#include "Layout.hpp"
#include "Storage.hpp"
#include <kernel/adt/avl_tree.hpp>
#include <span.hpp>

namespace kernel::memory {

class Cache;

class Slab : public common::Link<Slab> {
  common::DoubleList<Storage> freelist_;
  size_t allocated_ = 0;
  size_t layout_objects_;
  Cache *cache_;
  kstd::span<uint8_t> memory_;
  Storage *storage_base_;
  
  public:
  adt::AvlNode<Slab> avl_node;

  Slab(const Layout &layout, kstd::span<uint8_t> memory, Cache *cache) noexcept;

  void *allocate() noexcept;
  void free(void *ptr) noexcept;

  [[nodiscard]] bool empty() const noexcept { return allocated_ == 0; }
  [[nodiscard]] bool full() const noexcept { return allocated_ == layout_objects_; }
  [[nodiscard]] size_t allocated_count() const noexcept { return allocated_; }
  [[nodiscard]] kstd::span<uint8_t> memory() const noexcept { return memory_; }
  [[nodiscard]] Cache *cache() const noexcept { return cache_; }

  // Comparison for AVL tree (by address range)
  bool operator<(const Slab &other) const noexcept { return memory_.data() < other.memory_.data(); }
  bool operator==(const Slab &other) const noexcept { return memory_.data() == other.memory_.data(); }

  // Key lookup for AVL tree
  friend bool operator<(uintptr_t addr, const Slab &slab) noexcept {
    return addr < reinterpret_cast<uintptr_t>(slab.memory_.data());
  }
  friend bool operator>(uintptr_t addr, const Slab &slab) noexcept {
    return addr >= reinterpret_cast<uintptr_t>(slab.memory_.data()) + slab.memory_.size();
  }
  friend bool operator==(uintptr_t addr, const Slab &slab) noexcept {
    uintptr_t start = reinterpret_cast<uintptr_t>(slab.memory_.data());
    return addr >= start && addr < start + slab.memory_.size();
  }

};

}// namespace kernel::memory
