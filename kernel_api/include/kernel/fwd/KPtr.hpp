
#pragma once

#include <type_traits.hpp>

namespace kernel::detail {
template<typename T, typename Traits>
class KPtr {
  using pointer = T *;
  using reference = T &;
  using const_pointer = const pointer;
  using const_reference = const T &;
  using base_type = typename Traits::base_type;

  pointer _ptr;

  public:
  constexpr KPtr() noexcept : _ptr{nullptr} {}
  constexpr KPtr(decltype(nullptr)) noexcept : _ptr{nullptr} {}
  constexpr KPtr(pointer ptr) noexcept : _ptr{ptr} { Traits::template inc_ref<T>(_ptr); }
  constexpr KPtr(const_reference other) noexcept : _ptr{other._ptr} { Traits::template inc_ref<T>(_ptr); }
  constexpr KPtr(const KPtr &other) noexcept : _ptr{other._ptr} { Traits::template inc_ref<T>(_ptr); }
  constexpr KPtr(KPtr &&other) noexcept : _ptr{other._ptr} { other._ptr = nullptr; }

  template<typename U>
    requires(kstd::is_base_of_v<T, U>)
  constexpr KPtr(const KPtr<U, Traits> &other) noexcept : _ptr{other.get()} { Traits::template inc_ref<T>(_ptr); }

  template<typename U>
    requires(kstd::is_base_of_v<T, U>)
  constexpr KPtr(KPtr<U, Traits> &&other) noexcept : _ptr{other.get()} { other._ptr = nullptr; }

  ~KPtr() { Traits::template dec_ref<T>(_ptr); }

  KPtr &operator=(const KPtr &other) noexcept {
    if (this != &other) {
      Traits::template dec_ref<T>(_ptr);
      _ptr = other._ptr;
      Traits::template inc_ref<T>(_ptr);
    }
    return *this;
  }

  KPtr &operator=(KPtr &&other) noexcept {
    if (this != &other) {
      Traits::template dec_ref<T>(_ptr);
      _ptr = other._ptr;
      other._ptr = nullptr;
    }
    return *this;
  }

  KPtr &operator=(decltype(nullptr)) noexcept {
    Traits::template dec_ref<T>(_ptr);
    _ptr = nullptr;
    return *this;
  }

  pointer release() noexcept {
    auto ptr = Traits::template release<T>(_ptr);
    _ptr = nullptr;
    return ptr;
  }

  pointer get() const noexcept { return _ptr; }
  reference operator*() const noexcept { return *_ptr; }
  pointer operator->() const noexcept { return _ptr; }
  explicit operator base_type *() const noexcept { return _ptr; }
  explicit operator bool() const noexcept { return _ptr != nullptr; }
  bool operator==(const KPtr &other) const noexcept { return _ptr == other._ptr; }
  bool operator!=(const KPtr &other) const noexcept { return _ptr != other._ptr; }
  bool operator==(decltype(nullptr)) const noexcept { return _ptr == nullptr; }
  bool operator!=(decltype(nullptr)) const noexcept { return _ptr != nullptr; }

  template<typename U>
  KPtr<U, Traits> As() const noexcept {
    if (Traits::template convertible_to<T, U>(_ptr)) {
      return KPtr<U, Traits>(static_cast<U *>(_ptr));
    }
    return nullptr;
  }

  auto type() { return Traits::template type<T>(_ptr); }

  template<typename U, typename Z>
  friend class KPtr;
};

}// namespace kernel::detail
