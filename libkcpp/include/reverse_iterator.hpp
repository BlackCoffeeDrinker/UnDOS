
#pragma once

#include <__config.hpp>
#include <__iterator/iterator_traits.hpp>
#include <type_traits.hpp>

namespace kstd {

template <class _Iter>
class reverse_iterator {
protected:
  _Iter __t;

public:
  using iterator_type = _Iter;
  using iterator_category = typename iterator_traits<_Iter>::iterator_category;
  using value_type = typename iterator_traits<_Iter>::value_type;
  using difference_type = typename iterator_traits<_Iter>::difference_type;
  using pointer = typename iterator_traits<_Iter>::pointer;
  using reference = typename iterator_traits<_Iter>::reference;

  constexpr reverse_iterator() : __t() {}
  explicit constexpr reverse_iterator(_Iter __x) : __t(__x) {}
  template <class _Up>
  requires(!is_same_v<_Up, _Iter> && is_convertible_v<_Up, _Iter>)
  constexpr reverse_iterator(const reverse_iterator<_Up> &__u) : __t(__u.base()) {}

  constexpr _Iter base() const { return __t; }

  constexpr reference operator*() const {
    _Iter __tmp = __t;
    return *--__tmp;
  }

  constexpr pointer operator->() const {
    _Iter __tmp = __t;
    --__tmp;
    return __tmp;
  }

  constexpr reverse_iterator &operator++() {
    --__t;
    return *this;
  }

  constexpr reverse_iterator operator++(int) {
    reverse_iterator __tmp = *this;
    --__t;
    return __tmp;
  }

  constexpr reverse_iterator &operator--() {
    ++__t;
    return *this;
  }

  constexpr reverse_iterator operator--(int) {
    reverse_iterator __tmp = *this;
    ++__t;
    return __tmp;
  }

  constexpr reverse_iterator operator+(difference_type __n) const { return reverse_iterator(__t - __n); }

  constexpr reverse_iterator &operator+=(difference_type __n) {
    __t -= __n;
    return *this;
  }

  constexpr reverse_iterator operator-(difference_type __n) const { return reverse_iterator(__t + __n); }

  constexpr reverse_iterator &operator-=(difference_type __n) {
    __t += __n;
    return *this;
  }

  constexpr reference operator[](difference_type __n) const { return *(*this + __n); }
};

template <class _Iter1, class _Iter2>
inline constexpr bool operator==(const reverse_iterator<_Iter1> &__x, const reverse_iterator<_Iter2> &__y) {
  return __x.base() == __y.base();
}

template <class _Iter1, class _Iter2>
inline constexpr bool operator!=(const reverse_iterator<_Iter1> &__x, const reverse_iterator<_Iter2> &__y) {
  return __x.base() != __y.base();
}

template <class _Iter1, class _Iter2>
inline constexpr bool operator<(const reverse_iterator<_Iter1> &__x, const reverse_iterator<_Iter2> &__y) {
  return __x.base() > __y.base();
}

template <class _Iter1, class _Iter2>
inline constexpr bool operator<=(const reverse_iterator<_Iter1> &__x, const reverse_iterator<_Iter2> &__y) {
  return __x.base() >= __y.base();
}

template <class _Iter1, class _Iter2>
inline constexpr bool operator>(const reverse_iterator<_Iter1> &__x, const reverse_iterator<_Iter2> &__y) {
  return __x.base() < __y.base();
}

template <class _Iter1, class _Iter2>
inline constexpr bool operator>=(const reverse_iterator<_Iter1> &__x, const reverse_iterator<_Iter2> &__y) {
  return __x.base() <= __y.base();
}

template <class _Iter1, class _Iter2>
inline constexpr auto operator-(const reverse_iterator<_Iter1> &__x, const reverse_iterator<_Iter2> &__y)
    -> decltype(__y.base() - __x.base()) {
  return __y.base() - __x.base();
}

template <class _Iter>
inline constexpr reverse_iterator<_Iter> operator+(typename reverse_iterator<_Iter>::difference_type __n,
                                                   const reverse_iterator<_Iter> &__x) {
  return reverse_iterator<_Iter>(__x.base() - __n);
}

} // namespace kstd
