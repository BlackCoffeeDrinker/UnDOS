#pragma once

#include <kernel/__core.hpp>

#include <atomic.hpp>
#include <static_string.hpp>
#include <string_view.hpp>

#include <kernel/common/DoubleListLink.hpp>
#include <kernel/kobject/ObjectType.hpp>
#include <kernel/virtual_memory.hpp>


namespace kernel {
struct ObjectNameTag {
  // Unique seed for Object Manager names to prevent cross-domain hash matches
  static constexpr uint64_t seed = 0xcbf29ce484222325ull;
};

using ObjectNameHash = StaticIdentifier<ObjectNameTag>;

constexpr ObjectNameHash operator""_objname(const char *str, size_t len) noexcept {
  return ObjectNameHash::from_string(kstd::string_view(str, len));
}

// A base structural type for unified C++ tracking
struct KObject : kernel::common::Link<KObject> {
  const ObjectType type;
  const kstd::static_string<64> name;
  const ObjectNameHash name_hash{0};// FNV-1a Hash

  // Starts at 0 rather than 1: ownership is only actually granted once the
  // freshly-constructed object is adopted by a KObjectPtr (its constructor
  // from a raw pointer always calls KObjectPtrTraits::inc_ref), so seeding
  // this at 1 here would double-count that first reference and the object
  // would never reach 0 (and thus never be freed) via a single KObjectPtr's
  // normal construction/destruction lifecycle.
  kstd::atomic<uint32_t> reference_count{0};
  uint32_t flags{0};

  KObject *parent{nullptr};

  virtual ~KObject() = default;

  bool operator<(const KObject &other) const noexcept { return name_hash < other.name_hash; }
  bool operator==(const KObject &other) const noexcept { return this == &other; }
  bool operator<(kstd::string_view other_name) const noexcept { return name_hash < ObjectNameHash::from_string(other_name); }
  bool operator==(kstd::string_view other_name) const noexcept { return name_hash == ObjectNameHash::from_string(other_name); }
  friend bool operator<(kstd::string_view other_name, const KObject &obj) noexcept { return ObjectNameHash::from_string(other_name) < obj.name_hash; }
  friend bool operator==(kstd::string_view other_name, const KObject &obj) noexcept { return ObjectNameHash::from_string(other_name) == obj.name_hash; }

  protected:
  constexpr KObject(const ObjectType type_, const kstd::remove_cv_t<decltype(name)> &name_) noexcept : type(type_), name(name_), name_hash(ObjectNameHash::from_string(name)) {}
  constexpr KObject(const ObjectType type_) noexcept : type(type_) {}
};

}// namespace kernel
