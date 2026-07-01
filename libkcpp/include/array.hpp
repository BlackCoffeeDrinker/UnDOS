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

  // iterators:
  [[nodiscard]] constexpr iterator begin() noexcept {
    return iterator(data());
  }
  [[nodiscard]] constexpr const_iterator begin() const noexcept {
    return const_iterator(data());
  }
  [[nodiscard]] constexpr iterator end() noexcept {
    return iterator(data() + N);
  }
  [[nodiscard]] constexpr const_iterator end() const noexcept {
    return const_iterator(data() + N);
  }

  [[nodiscard]] constexpr value_type *data() noexcept {
    return elements;
  }
  [[nodiscard]] constexpr const value_type *data() const noexcept {
    return elements;
  }

  // Size
  constexpr size_t size() const { return N; }
};

template<class T>
struct array<T, 0> {
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

  // Accessors
  T &operator[](size_t) { return *data(); }
  const T &operator[](size_t) const { return *data(); }

  // iterators:
  [[nodiscard]] constexpr iterator begin() noexcept {
    return nullptr;
  }
  [[nodiscard]] constexpr const_iterator begin() const noexcept {
    return nullptr;
  }
  [[nodiscard]] constexpr iterator end() noexcept {
    return nullptr;
  }
  [[nodiscard]] constexpr const_iterator end() const noexcept {
    return nullptr;
  }

  [[nodiscard]] constexpr value_type *data() noexcept {
    return nullptr;
  }
  [[nodiscard]] constexpr const value_type *data() const noexcept {
    return nullptr;
  }

  // Size
  constexpr size_t size() const { return 0; }
};

template<size_t _Ip, class _Tp, size_t _Size>
[[nodiscard]] inline constexpr _Tp &get(array<_Tp, _Size> &__a) noexcept {
  static_assert(_Ip < _Size, "Index out of bounds in std::get<> (std::array)");
  return __a.elements[_Ip];
}

template<size_t _Ip, class _Tp, size_t _Size>
[[nodiscard]] inline constexpr const _Tp &get(const array<_Tp, _Size> &__a) noexcept {
  static_assert(_Ip < _Size, "Index out of bounds in std::get<> (std::array)");
  return __a.elements[_Ip];
}

}// namespace kstd
