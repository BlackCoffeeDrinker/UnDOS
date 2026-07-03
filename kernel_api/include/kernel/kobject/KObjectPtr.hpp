#pragma once

#include <type_traits.hpp>
#include <kernel/kobject/KObject.hpp>

namespace kernel {

template<typename T>
class KObjectPtr {
  T *_ptr;

  void _inc_ref() {
    if (_ptr) {
      static_assert(kstd::is_base_of_v<KObject, T>, "T must derive from KObject");
      _ptr->reference_count.fetch_add(1, kstd::memory_order_relaxed);
    }
  }

  void _dec_ref() {
    if (_ptr) {
      static_assert(kstd::is_base_of_v<KObject, T>, "T must derive from KObject");
      _ptr->release();
    }
  }

  public:
  KObjectPtr() noexcept : _ptr{nullptr} {}
  KObjectPtr(decltype(nullptr)) noexcept : _ptr{nullptr} {}
  KObjectPtr(T *ptr) noexcept : _ptr{ptr} { _inc_ref(); }
  KObjectPtr(const KObjectPtr &other) noexcept : _ptr{other._ptr} { _inc_ref(); }
  KObjectPtr(KObjectPtr &&other) noexcept : _ptr{other._ptr} { other._ptr = nullptr; }

  template<typename U>
    requires(kstd::is_base_of_v<T, U>)
  KObjectPtr(const KObjectPtr<U> &other) noexcept : _ptr{other.get()} { _inc_ref(); }

  template<typename U>
    requires(kstd::is_base_of_v<T, U>)
  KObjectPtr(KObjectPtr<U> &&other) noexcept : _ptr{other.get()} { other._ptr = nullptr; }

  ~KObjectPtr() { _dec_ref(); }

  KObjectPtr &operator=(const KObjectPtr &other) noexcept {
    if (this != &other) {
      _dec_ref();
      _ptr = other._ptr;
      _inc_ref();
    }
    return *this;
  }

  KObjectPtr &operator=(KObjectPtr &&other) noexcept {
    if (this != &other) {
      _dec_ref();
      _ptr = other._ptr;
      other._ptr = nullptr;
    }
    return *this;
  }

  KObjectPtr &operator=(decltype(nullptr)) noexcept {
    _dec_ref();
    _ptr = nullptr;
    return *this;
  }

  T *release() noexcept {
    auto ptr = _ptr;
    _dec_ref();
    _ptr = nullptr;
    return ptr;
  }
  T *get() const noexcept { return _ptr; }
  T &operator*() const noexcept { return *_ptr; }
  T *operator->() const noexcept { return _ptr; }

  explicit operator KObject *() const noexcept { return _ptr; }
  explicit operator bool() const noexcept { return _ptr != nullptr; }

  bool operator==(const KObjectPtr &other) const noexcept { return _ptr == other._ptr; }
  bool operator!=(const KObjectPtr &other) const noexcept { return _ptr != other._ptr; }
  bool operator==(decltype(nullptr)) const noexcept { return _ptr == nullptr; }
  bool operator!=(decltype(nullptr)) const noexcept { return _ptr != nullptr; }

  template<typename U>
  KObjectPtr<U> As() const noexcept {
    if (U::Type == _ptr->type) {
      return KObjectPtr<U>(static_cast<U *>(_ptr));
    }
    return nullptr;
  }

  template<typename U>
  friend class KObjectPtr;
};

} // namespace kernel
