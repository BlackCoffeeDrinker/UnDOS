
#pragma once

#include <atomic.hpp>
#include <string_view.hpp>
#include <type_traits.hpp>

namespace kernel {
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

// A base structural type for unified C++ tracking
struct KObject {
  const ObjectType type;
  kstd::atomic<uint32_t> reference_count{1};
  uint32_t flags{0};

  virtual ~KObject() = default;

  protected:
  constexpr KObject(const ObjectType type_) noexcept : type(type_) {}
};

struct KDriverObject : KObject {
  KDriverObject() noexcept : KObject{TYPE_DRIVER} {}
};

template<typename T>
class KObjectPtr {
  static_assert(kstd::is_base_of_v<KObject, T>, "T must derive from KObject");

  T *_ptr;

  void retain() {
    if (_ptr) {
      _ptr->reference_count.fetch_add(1, kstd::memory_order_relaxed);
    }
  }

  void release() {
    if (_ptr) {
      if (_ptr->reference_count.fetch_sub(1, kstd::memory_order_acq_rel) == 1) {
        kstd::atomic_thread_fence(kstd::memory_order_acquire);
        // TODO
      }
    }
  }

  public:
  KObjectPtr() noexcept : _ptr{nullptr} {}
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

  T *get() const noexcept { return _ptr; }
  T &operator*() const noexcept { return *_ptr; }
  T *operator->() const noexcept { return _ptr; }

  explicit operator bool() const noexcept { return _ptr != nullptr; }

  bool operator==(const KObjectPtr &other) const noexcept { return _ptr == other._ptr; }
  bool operator!=(const KObjectPtr &other) const noexcept { return _ptr != other._ptr; }

  template<typename U>
  friend class KObjectPtr;
};

}// namespace kernel
