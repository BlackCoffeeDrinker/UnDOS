#pragma once

#include <__config.hpp>

#include <reverse_iterator.hpp>
#include <stddef.hpp>

namespace kstd {

template<class T, size_t N>
struct array {
  // types:
  using __self = array;
  using value_type = T;
  using reference = value_type &;
  using const_reference = const value_type &;
  using pointer = value_type *;
  using const_pointer = const value_type *;

  using iterator = pointer;
  using const_iterator = const_pointer;
  using size_type = size_t;
  using difference_type = ptrdiff_t;
  using reverse_iterator = kstd::reverse_iterator<iterator>;
  using const_reverse_iterator = kstd::reverse_iterator<const_iterator>;

  // The underlying data
  T elements[N];

  // Accessors
  T &operator[](size_t i) { return elements[i]; }
  const T &operator[](size_t i) const { return elements[i]; }

  // Iterators
  T *begin() { return elements; }
  T *end() { return elements + N; }

  // Size
  constexpr size_t size() const { return N; }
};

template<size_t _Ip, class _Tp, size_t _Size>
[[__nodiscard__]] inline constexpr _Tp &get(array<_Tp, _Size> &__a) noexcept {
  static_assert(_Ip < _Size, "Index out of bounds in std::get<> (std::array)");
  return __a.__elems_[_Ip];
}

}// namespace kstd
