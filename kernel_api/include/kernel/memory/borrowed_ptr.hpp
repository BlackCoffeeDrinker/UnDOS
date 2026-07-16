
#pragma once

#include <span.hpp>
#include <string_view.hpp>
#include <type_traits.hpp>

namespace kernel {
/**
 * A borrowed pointer is a pointer that borrows a reference to an object from another address space.
 * 
 * @tparam T type
 */
template<typename T>
class borrowed_ptr final {
  using element_type = T;
  using pointer = kstd::remove_reference_t<T> *;

  pointer _ptr;
  size_t _size;
  vmm::AddressSpace _address_space;

  public:
  borrowed_ptr(nullptr_t) : _ptr(nullptr), _size(0), _address_space() {}
  borrowed_ptr(pointer ptr, size_t size, vmm::AddressSpace address_space) : _ptr(ptr), _size(size), _address_space(address_space) {}
  borrowed_ptr(borrowed_ptr &&other) noexcept : _ptr(other._ptr), _size(other._size), _address_space(other._address_space) {
    other._ptr = nullptr;
    other._size = 0;
  }

  borrowed_ptr &operator=(borrowed_ptr &&other) noexcept {
    if (this != &other) {
      reset();
      _ptr = other._ptr;
      _size = other._size;
      _address_space = other._address_space;
      other._ptr = nullptr;
      other._size = 0;
    }
    return *this;
  }

  borrowed_ptr(const borrowed_ptr &) = delete;
  borrowed_ptr &operator=(const borrowed_ptr &) = delete;

  ~borrowed_ptr() { reset(); }

  T &operator*() const { return *_ptr; }
  pointer operator->() const { return _ptr; }
  pointer get() const { return _ptr; }
  size_t size() const { return _size; }

  // Implicit conversion to a string_view (only meaningful when T is char)
  template<typename U = T, typename = kstd::enable_if_t<kstd::is_same_v<U, char>>>
  operator kstd::string_view() const { return kstd::string_view(_ptr, _size); }

  // Implicit conversion to a span (a non-owning view over the borrowed
  // memory); the borrowed_ptr itself keeps ownership/unmapping semantics.
  operator kstd::span<T>() const { return kstd::span<T>(_ptr, _size); }

  // Release ownership without unmapping
  pointer release() {
    pointer tmp = _ptr;
    _ptr = nullptr;
    _size = 0;
    return tmp;
  }

  // Reset the borrowed pointer
  void reset(pointer ptr = nullptr, size_t size = 0) {
    if (_ptr != nullptr) {
      // Unmap the borrowed memory from current address space
      KE_VMM_UnmapBorrowed(_ptr, _size, _address_space);
    }
    _ptr = ptr;
    _size = size;
  }

  explicit operator bool() const { return _ptr != nullptr; }
};
}// namespace kernel
