
#pragma once

#include <kernel/__core.hpp>
#include <kernel/memory/address.hpp>
#include <kernel/virtual_memory.hpp>

namespace kernel {
/**
 * @brief Wrapper for a raw data buffer (pointer + size).
 */
struct DataBuffer {
  using Destructor = void (*)(void *);

  VirtualAddress ptr{0};
  size_t size = 0;
  Destructor destroy{};

  static DataBuffer Alloc(size_t size) {
    return {VirtualAddress::from_ptr(KE_Malloc(size)), size, nullptr};
  }

  template<typename T, typename... Args>
  static DataBuffer Create(Args &&...args) {
    auto ret = Alloc(sizeof(T));
    new (ret.ptr.as_ptr<T>()) T(kstd::forward<Args>(args)...);
    return ret;
  }

  constexpr DataBuffer() = default;
  DataBuffer(const DataBuffer &) = delete;
  constexpr DataBuffer(DataBuffer &&rhs) noexcept
      : ptr(rhs.ptr), size(rhs.size), destroy(rhs.destroy) {
    rhs.ptr = nullptr;
    rhs.size = 0;
    rhs.destroy = nullptr;
  }

  constexpr DataBuffer(VirtualAddress p, size_t s, Destructor destructor = nullptr)
      : ptr(p), size(s), destroy(destructor) {}
  ~DataBuffer() { *this = nullptr; }

  DataBuffer &operator=(const DataBuffer &) = delete;
  DataBuffer &operator=(DataBuffer &&rhs) noexcept {
    *this = nullptr;

    this->ptr = rhs.ptr;
    this->size = rhs.size;
    this->destroy = rhs.destroy;

    rhs.ptr = nullptr;
    rhs.size = 0;
    rhs.destroy = nullptr;

    return *this;
  }

  DataBuffer &operator=(nullptr_t) {
    if (ptr && destroy) destroy(ptr.as_ptr<void>());
    KE_Free(ptr.as_ptr<>());
    ptr = nullptr;
    size = 0;
    destroy = nullptr;
    return *this;
  }

  template<typename T>
  [[nodiscard]] T *as() const { return ptr.as_ptr<T>(); }

  explicit constexpr operator bool() const { return ptr.value != 0; }
};

}// namespace kernel
