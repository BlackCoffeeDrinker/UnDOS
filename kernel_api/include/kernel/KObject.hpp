
#pragma once

#include <atomic.hpp>
#include <new.hpp>
#include <static_string.hpp>
#include <string_view.hpp>
#include <type_traits.hpp>
#include <utility.hpp>

#include <kernel/__core.hpp>
#include <kernel/adt/avl_tree.hpp>
#include <kernel/memory/virtual_memory.hpp>

namespace kernel {
struct KEvent;

struct ObjectType final {
  uint64_t value{0};

  constexpr ObjectType() noexcept = default;
  constexpr explicit ObjectType(uint64_t v) noexcept : value(v) {}
  constexpr bool operator==(ObjectType o) const noexcept { return value == o.value; }
  constexpr bool operator!=(ObjectType o) const noexcept { return value != o.value; }
  constexpr bool operator<(ObjectType o) const noexcept { return value < o.value; }

  template<size_t N>
  static consteval ObjectType from_literal(const char (&s)[N]) { return ObjectType{fnv1a64(s, N - 1)}; }
  static consteval ObjectType from_string(kstd::string_view sv) { return ObjectType{fnv1a64(sv.data(), sv.size())}; }

  private:
  static consteval uint64_t fnv1a64(const char *s, size_t n) {
    uint64_t h = 14695981039346656037ull;
    for (size_t i = 0; i < n; ++i) {
      if (s[i] != '\0') {
        h ^= static_cast<uint8_t>(s[i]);
        h *= 1099511628211ull;
      }
    }
    return h;
  }
};

consteval ObjectType operator""_type(const char *s, size_t n) {
  (void) n;
  return ObjectType::from_string(kstd::string_view{s});
}

constexpr auto TYPE_DRIVER = "driver"_type;
constexpr auto TYPE_DEVICE = "device"_type;
constexpr auto TYPE_DIRECTORY = "directory"_type;
constexpr auto TYPE_BUS = "bus"_type;
constexpr auto TYPE_VMM = "vmm"_type;

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

template<typename T, size_t Version, ObjectType ObjectTypeId>
struct KObjectT : KObject, Versioned<T, Version> {
  static constexpr ObjectType Type = ObjectTypeId;

  constexpr KObjectT() noexcept : KObject(Type) {}
};

template<typename T>
class KObjectPtr {
  T *_ptr;

  void retain() {
    if (_ptr) {
      static_assert(kstd::is_base_of_v<KObject, T>, "T must derive from KObject");
      _ptr->reference_count.fetch_add(1, kstd::memory_order_relaxed);
    }
  }

  void release() {
    if (_ptr) {
      static_assert(kstd::is_base_of_v<KObject, T>, "T must derive from KObject");
      _ptr->release();
    }
  }

  public:
  KObjectPtr() noexcept : _ptr{nullptr} {}
  KObjectPtr(decltype(nullptr)) noexcept : _ptr{nullptr} {}
  KObjectPtr(T *ptr) noexcept : _ptr{ptr} { retain(); }
  KObjectPtr(const KObjectPtr &other) noexcept : _ptr{other._ptr} { retain(); }
  KObjectPtr(KObjectPtr &&other) noexcept : _ptr{other._ptr} { other._ptr = nullptr; }

  template<typename U>
    requires(kstd::is_base_of_v<T, U>)
  KObjectPtr(const KObjectPtr<U> &other) noexcept : _ptr{other.get()} { retain(); }

  template<typename U>
    requires(kstd::is_base_of_v<T, U>)
  KObjectPtr(KObjectPtr<U> &&other) noexcept : _ptr{other.get()} { other._ptr = nullptr; }

  ~KObjectPtr() { release(); }

  KObjectPtr &operator=(const KObjectPtr &other) noexcept {
    if (this != &other) {
      release();
      _ptr = other._ptr;
      retain();
    }
    return *this;
  }

  KObjectPtr &operator=(KObjectPtr &&other) noexcept {
    if (this != &other) {
      release();
      _ptr = other._ptr;
      other._ptr = nullptr;
    }
    return *this;
  }

  KObjectPtr &operator=(decltype(nullptr)) noexcept {
    release();
    _ptr = nullptr;
    return *this;
  }

  T *get() const noexcept { return _ptr; }
  T &operator*() const noexcept { return *_ptr; }
  T *operator->() const noexcept { return _ptr; }

  explicit operator bool() const noexcept { return _ptr != nullptr; }

  bool operator==(const KObjectPtr &other) const noexcept { return _ptr == other._ptr; }
  bool operator!=(const KObjectPtr &other) const noexcept { return _ptr != other._ptr; }
  bool operator==(decltype(nullptr)) const noexcept { return _ptr == nullptr; }
  bool operator!=(decltype(nullptr)) const noexcept { return _ptr != nullptr; }

  template<typename U>
  friend class KObjectPtr;
};

struct KDirectoryObject : KObjectT<KDirectoryObject, 1, TYPE_DIRECTORY> {
  adt::AvlTree<KObject, &KObject::node> children;

  ~KDirectoryObject() override {
    children.clear([](KObject *obj) {
      if (obj) obj->release();
    });
  }
};

struct KDriverObject : KObjectT<KDriverObject, 1, TYPE_DRIVER> {
  cfunc<void(KObjectPtr<KDriverObject>, const KEvent &)> eventHandler;
  uintptr_t load_base{0};
  size_t total_size{0};
  cfunc<void(KObjectPtr<KDriverObject>&)> entry_point;
};

struct KBusObject : KObjectT<KBusObject, 1, TYPE_BUS> {
};

}// namespace kernel
