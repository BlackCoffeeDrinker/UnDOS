#pragma once

#include "kernel/memory/virtual_memory.hpp"


#include <atomic.hpp>
#include <static_string.hpp>
#include <string_view.hpp>

#include <kernel/adt/avl_tree.hpp>
#include <kernel/kobject/ObjectType.hpp>

namespace kernel {

// A base structural type for unified C++ tracking
struct KObject {
  const ObjectType type;
  kstd::atomic<uint32_t> reference_count{1};
  uint32_t flags{0};

  kstd::static_string<64> name;
  KObject *parent{nullptr};
  adt::AvlNode<KObject> node;

  virtual ~KObject() = default;

  void retain() {
    reference_count.fetch_add(1, kstd::memory_order_relaxed);
  }

  void release() {
    if (reference_count.fetch_sub(1, kstd::memory_order_acq_rel) == 1) {
      kstd::atomic_thread_fence(kstd::memory_order_acquire);
      this->~KObject();
      KE_Free(this);
    }
  }

  bool operator<(const KObject &other) const noexcept {
    return kstd::string_view(name) < kstd::string_view(other.name);
  }

  bool operator==(const KObject &other) const noexcept {
    return kstd::string_view(name) == kstd::string_view(other.name);
  }

  bool operator<(kstd::string_view other_name) const noexcept {
    return kstd::string_view(name) < other_name;
  }

  bool operator==(kstd::string_view other_name) const noexcept {
    return kstd::string_view(name) == other_name;
  }

  friend bool operator<(kstd::string_view other_name, const KObject &obj) noexcept {
    return other_name < kstd::string_view(obj.name);
  }

  friend bool operator==(kstd::string_view other_name, const KObject &obj) noexcept {
    return other_name == kstd::string_view(obj.name);
  }

  protected:
  constexpr KObject(const ObjectType type_) noexcept : type(type_) {}
};

}// namespace kernel
