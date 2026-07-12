#pragma once

#include <kernel/__core.hpp>

#include <atomic.hpp>
#include <static_string.hpp>
#include <string_view.hpp>

#include <kernel/adt/avl_tree.hpp>
#include <kernel/kobject/ObjectType.hpp>
#include <kernel/virtual_memory.hpp>

namespace kernel {
struct KObject;
}


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

  bool operator<(const KObject &other) const noexcept { return kstd::string_view(name) < kstd::string_view(other.name); }
  bool operator==(const KObject &other) const noexcept { return kstd::string_view(name) == kstd::string_view(other.name); }
  bool operator<(kstd::string_view other_name) const noexcept { return kstd::string_view(name) < other_name; }
  bool operator==(kstd::string_view other_name) const noexcept { return kstd::string_view(name) == other_name; }
  friend bool operator<(kstd::string_view other_name, const KObject &obj) noexcept { return other_name < kstd::string_view(obj.name); }
  friend bool operator==(kstd::string_view other_name, const KObject &obj) noexcept { return other_name == kstd::string_view(obj.name); }

  protected:
  constexpr KObject(const ObjectType type_) noexcept : type(type_) {}
  constexpr KObject(const ObjectType type_, const kstd::static_string<64> &name_) noexcept : type(type_), name(name_) {}
};

}// namespace kernel
