
#pragma once

#include <stddef.h>
#include <stdint.h>

#define UNDOS_KERNEL_API extern "C" __attribute__((visibility("default"), used))
#define UNDOS_HAL_API extern "C" __attribute__((visibility("default"), used))

using PhysicalAddressT = uintptr_t;
using VirtualAddressT = uintptr_t;

namespace kernel {
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
