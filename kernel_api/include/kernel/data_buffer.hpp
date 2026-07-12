
#pragma once

#include <kernel/__core.hpp>
#include <kernel/memory/address.hpp>
#include <kernel/virtual_memory.hpp>

#include <stdint.h>

namespace kernel {
/**
 * @brief Wrapper for a raw data buffer (pointer + size).
 */
struct DataBuffer {
  VirtualAddress ptr{0};
  size_t size = 0;

  static DataBuffer Alloc(size_t size) { return {VirtualAddress::from_ptr(KE_Malloc(size)), size}; }

  template<typename T, typename... Args>
  static DataBuffer Create(Args &&...args) {
    auto ret = Alloc(sizeof(T));
    new (ret.ptr.as_ptr<T>()) T(kstd::forward<Args>(args)...);
    return ret;
  }

  constexpr DataBuffer() = default;
  DataBuffer(const DataBuffer &) = delete;
  constexpr DataBuffer(DataBuffer &&rhs) noexcept : ptr(rhs.ptr), size(rhs.size) {
    rhs.ptr = nullptr;
    rhs.size = 0;
  }

  constexpr DataBuffer(VirtualAddress p, size_t s) : ptr(p), size(s) {}
  ~DataBuffer() {
    *this = nullptr;
  }

  DataBuffer &operator=(const DataBuffer &) = delete;
  DataBuffer &operator=(DataBuffer &&rhs) noexcept {
    *this = nullptr;

    this->ptr = rhs.ptr;
    this->size = rhs.size;

    rhs.ptr = nullptr;
    rhs.size = 0;

    return *this;
  }

  DataBuffer &operator=(nullptr_t) {
    KE_Free(ptr.as_ptr<>());
    ptr = nullptr;
    size = 0;
    return *this;
  }

  template<typename T>
  [[nodiscard]] T *as() const { return ptr.as_ptr<T>(); }

  explicit constexpr operator bool() const { return ptr.value != 0; }
};

}// namespace kernel
