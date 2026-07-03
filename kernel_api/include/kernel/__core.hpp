
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <memory.hpp>
#include <type_traits.hpp>
#include <utility.hpp>

#define UNDOS_KERNEL_API extern "C" __attribute__((visibility("default"), used))
#define UNDOS_KERNEL_CPP_API __attribute__((visibility("default"), used))
#define UNDOS_HAL_API extern "C" __attribute__((visibility("default"), used))


using PhysicalAddressT = uintptr_t;
using VirtualAddressT = uintptr_t;

namespace kernel {

template<typename ThingToVersion, size_t Version>
struct Versioned {
  uint16_t version = Version;
  size_t size = sizeof(ThingToVersion);
};

/**
 * @brief Wrapper for a C-style function pointer.
 *
 * `cfunc<R, Args...>` stores a raw function pointer of type `R(*)(Args...)`
 * and exposes a callable interface. This allows the function pointer to be
 * passed around as a lightweight object while retaining ABI-stable C linkage.
 *
 * @tparam R    Return type of the function.
 * @tparam Args Parameter types of the function.
 */
template<class R, class... Args>
struct cfunc {
  using pointer = R (*)(Args...);
  pointer fn = nullptr;

  constexpr cfunc() = default;
  constexpr explicit cfunc(pointer p) : fn(p) {}
  constexpr R operator()(Args... args) const { return fn(args...); }
  explicit constexpr operator bool() const { return fn != nullptr; }
  constexpr bool operator==(pointer p) const { return fn == p; }
  [[nodiscard]] constexpr bool valid() const noexcept { return fn != nullptr; }

  constexpr cfunc &operator=(pointer p) {
    fn = p;
    return *this;
  }
};

template<class R, class... Args>
struct cfunc<R(Args...)> : cfunc<R, Args...> {
  using cfunc<R, Args...>::cfunc;
  using cfunc<R, Args...>::operator=;
};

/**
 * @brief Wrapper for a raw data buffer (pointer + size).
 */
struct data_buffer {
  void *ptr = nullptr;
  size_t size = 0;

  constexpr data_buffer() = default;
  constexpr data_buffer(void *p, size_t s) : ptr(p), size(s) {}

  template<typename T>
  [[nodiscard]] T *as() const { return reinterpret_cast<T *>(ptr); }

  explicit constexpr operator bool() const { return ptr != nullptr; }
};

template<typename Tag, typename T>
struct Address {
  using type = T;
  using tag = Tag;
  type value;

  constexpr Address(type _value) : value(_value) {}
  constexpr Address() : value(0) {}
  constexpr Address(const Address &other) = default;
  constexpr Address(Address &&other) noexcept = default;

  explicit constexpr operator type() const { return value; }
  explicit constexpr operator bool() const { return value != 0; }
  constexpr bool operator!() const { return value == 0; }

  constexpr Address operator+(type offset) const { return Address(value + offset); }
  constexpr Address operator-(type offset) const { return Address(value - offset); }
  constexpr Address operator+(const Address &other) const { return Address(value + other.value); }
  constexpr Address operator-(const Address &other) const { return Address(value - other.value); }
  constexpr Address &operator+=(type offset) {
    value += offset;
    return *this;
  }
  constexpr Address &operator-=(type offset) {
    value -= offset;
    return *this;
  }

  constexpr Address &operator=(const Address &other) = default;
  constexpr Address &operator=(type other) {
    value = other;
    return *this;
  }
  constexpr Address &operator=(Address &&other) noexcept = default;

  constexpr bool operator==(const Address &other) const { return value == other.value; }
  constexpr bool operator!=(const Address &other) const { return value != other.value; }
  constexpr bool operator<(const Address &other) const { return value < other.value; }
  constexpr bool operator>(const Address &other) const { return value > other.value; }
  constexpr bool operator<=(const Address &other) const { return value <= other.value; }
  constexpr bool operator>=(const Address &other) const { return value >= other.value; }

  constexpr bool operator==(type other) const { return value == other; }
  constexpr bool operator!=(type other) const { return value != other; }
  constexpr bool operator<(type other) const { return value < other; }
  constexpr bool operator>(type other) const { return value > other; }
  constexpr bool operator<=(type other) const { return value <= other; }
  constexpr bool operator>=(type other) const { return value >= other; }

  constexpr Address operator&(type mask) const { return Address(value & mask); }
  constexpr Address operator|(type mask) const { return Address(value | mask); }
  constexpr Address operator>>(type shift) const { return Address(value >> shift); }
  constexpr Address operator<<(type shift) const { return Address(value << shift); }
  constexpr Address operator~() const { return Address(~value); }

  constexpr bool is_aligned(type alignment) const { return (value % alignment) == 0; }
  constexpr Address align_up(type alignment) const { return Address((value + alignment - 1) & ~(alignment - 1)); }
  constexpr Address align_down(type alignment) const { return Address(value & ~(alignment - 1)); }

  template<typename P = void>
  P *as_ptr() const { return reinterpret_cast<P *>(value); }

  static constexpr Address from_ptr(const void *ptr) { return Address(reinterpret_cast<type>(ptr)); }
};

struct PhysicalTag {};
struct VirtualTag {};

using PhysicalAddress = Address<PhysicalTag, PhysicalAddressT>;
using VirtualAddress = Address<VirtualTag, VirtualAddressT>;

}// namespace kernel
