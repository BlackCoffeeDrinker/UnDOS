
#pragma once
#include <__config.hpp>

#include <__algo/min.hpp>
#include <__type_traits/enable_if.hpp>
#include <__type_traits/is_pointer.hpp>
#include <__type_traits/remove_reference.hpp>
#include <char_traits.hpp>
#include <stddef.hpp>

namespace kstd {

template<class CharT, class Traits = char_traits<CharT>>
struct basic_string_view {
  // types
  using traits_type = Traits;
  using value_type = CharT;
  using pointer = CharT *;
  using const_pointer = const CharT *;
  using reference = CharT &;
  using const_reference = const CharT &;
  using const_iterator = const CharT *;
  using iterator = const_iterator;
  using size_type = size_t;

  static constexpr size_type npos = static_cast<size_type>(-1);

  constexpr basic_string_view() noexcept : _data{nullptr} {}
  constexpr basic_string_view(const basic_string_view &other) noexcept : _data{other._data}, _len{other._len} {}
  constexpr basic_string_view(const CharT *str) noexcept
      : _data{const_cast<pointer>(str)}, _len{str ? traits_type::length(str) : 0} {}
  constexpr basic_string_view(const CharT *str, size_type len) noexcept
      : _data{const_cast<pointer>(str)}, _len{len} {}
  constexpr basic_string_view(pointer str, size_type len) noexcept : _data{str}, _len{len} {}

  // Constructs a view over a fixed-size array (e.g. a raw on-disk field) using the array's
  // actual bound `N` as the maximum length, rather than treating it as a NUL-terminated
  // C string. If a NUL terminator is found within the first `N` characters, the view is
  // truncated there (matching normal string-literal behaviour); otherwise all `N` characters
  // are used, correctly handling fixed-size, non-terminated fields such as FAT short names.
  template<size_type N>
  constexpr basic_string_view(const CharT (&arr)[N]) noexcept
      : _data{const_cast<pointer>(arr)}, _len{array_bound_length(arr, N)} {}

  explicit constexpr operator const CharT *() const noexcept { return _data; }

  [[nodiscard]] constexpr pointer data() noexcept { return _data; }
  [[nodiscard]] constexpr const_pointer data() const noexcept { return _data; }
  [[nodiscard]] constexpr size_type length() const noexcept { return _len; }
  [[nodiscard]] constexpr size_type size() const noexcept { return _len; }
  [[nodiscard]] constexpr bool empty() const noexcept { return _len == 0; }
  constexpr void clear() noexcept { _len = 0; }

  [[nodiscard]] constexpr const_iterator begin() const noexcept { return _data; }
  [[nodiscard]] constexpr const_iterator end() const noexcept { return _data + _len; }
  [[nodiscard]] constexpr const_iterator cbegin() const noexcept { return _data; }
  [[nodiscard]] constexpr const_iterator cend() const noexcept { return _data + _len; }

  [[nodiscard]] constexpr pointer begin() noexcept { return _data; }
  [[nodiscard]] constexpr pointer end() noexcept { return _data + _len; }
  constexpr reference operator[](size_type pos) noexcept { return _data[pos]; }
  constexpr const_reference operator[](size_type pos) const noexcept { return _data[pos]; }

  constexpr reference back() noexcept { return _data[_len - 1]; }
  constexpr const_reference back() const noexcept { return _data[_len - 1]; }

  constexpr basic_string_view &operator=(const basic_string_view &other) noexcept {
    _data = other._data;
    _len = other._len;
    return *this;
  }

  friend constexpr bool operator==(const basic_string_view &lhs, const basic_string_view &rhs) noexcept {
    if (lhs.length() != rhs.length()) return false;
    if (lhs.data() == rhs.data()) return true;
    return traits_type::compare(lhs.data(), rhs.data(), lhs.length()) == 0;
  }

  friend constexpr bool operator!=(const basic_string_view &lhs, const basic_string_view &rhs) noexcept {
    return !(lhs == rhs);
  }

  friend constexpr bool operator<(const basic_string_view &lhs, const basic_string_view &rhs) noexcept {
    size_type n = lhs.length() < rhs.length() ? lhs.length() : rhs.length();
    if (const int res = traits_type::compare(lhs.data(), rhs.data(), n);
        res != 0) return res < 0;
    return lhs.length() < rhs.length();
  }

  friend constexpr bool operator>(const basic_string_view &lhs, const basic_string_view &rhs) noexcept { return rhs < lhs; }
  friend constexpr bool operator<=(const basic_string_view &lhs, const basic_string_view &rhs) noexcept { return !(rhs < lhs); }
  friend constexpr bool operator>=(const basic_string_view &lhs, const basic_string_view &rhs) noexcept { return !(lhs < rhs); }

  // Pointer overloads for real, dynamically-sized C strings (not fixed-size arrays). These take
  // the argument via a forwarding reference (`T&&`) rather than a plain pointer parameter, and
  // are SFINAE-constrained (via `is_array_sfinae_pointer`) to only participate when the
  // argument's un-decayed type is an actual pointer, not an array. This is essential: a plain
  // `const T*`/`const_pointer` parameter would always deduce/decay a `char[N]` argument down to
  // a pointer, making it indistinguishable (at the point of overload resolution) from the array
  // overloads below and causing ambiguous-overload errors. By deducing on the un-decayed
  // argument type first, arrays are cleanly excluded here and handled solely by the dedicated
  // array overloads, while genuine pointers (whose bound is unknown, so NUL-termination is the
  // only valid signal) are handled solely here.
  template<class T, class _RT = remove_reference_t<T>, class = enable_if_t<is_pointer_v<_RT>>>
  friend constexpr bool operator==(const basic_string_view &lhs, T &&rhs) noexcept {
    const_pointer p = rhs;
    if (!p) return lhs.empty();
    size_type rhs_len = traits_type::length(p);
    if (lhs.length() != rhs_len) return false;
    return traits_type::compare(lhs.data(), p, lhs.length()) == 0;
  }
  template<class T, class _RT = remove_reference_t<T>, class = enable_if_t<is_pointer_v<_RT>>>
  friend constexpr bool operator==(T &&lhs, const basic_string_view &rhs) noexcept { return rhs == static_cast<T &&>(lhs); }
  template<class T, class _RT = remove_reference_t<T>, class = enable_if_t<is_pointer_v<_RT>>>
  friend constexpr bool operator!=(const basic_string_view &lhs, T &&rhs) noexcept { return !(lhs == static_cast<T &&>(rhs)); }
  template<class T, class _RT = remove_reference_t<T>, class = enable_if_t<is_pointer_v<_RT>>>
  friend constexpr bool operator!=(T &&lhs, const basic_string_view &rhs) noexcept { return !(rhs == static_cast<T &&>(lhs)); }
  template<class T, class _RT = remove_reference_t<T>, class = enable_if_t<is_pointer_v<_RT>>>
  friend constexpr bool operator<(const basic_string_view &lhs, T &&rhs) noexcept {
    const_pointer p = rhs;
    if (!p) return false;
    size_type rhs_len = traits_type::length(p);
    size_type n = lhs.length() < rhs_len ? lhs.length() : rhs_len;
    if (const int res = traits_type::compare(lhs.data(), p, n);
        res != 0) {
      return res < 0;
    }
    return lhs.length() < rhs_len;
  }
  template<class T, class _RT = remove_reference_t<T>, class = enable_if_t<is_pointer_v<_RT>>>
  friend constexpr bool operator<(T &&lhs, const basic_string_view &rhs) noexcept {
    const_pointer p = lhs;
    if (!p) return !rhs.empty();
    size_type lhs_len = traits_type::length(p);
    size_type n = lhs_len < rhs.length() ? lhs_len : rhs.length();
    if (const int res = traits_type::compare(p, rhs.data(), n);
        res != 0) {
      return res < 0;
    }
    return lhs_len < rhs.length();
  }
  template<class T, class _RT = remove_reference_t<T>, class = enable_if_t<is_pointer_v<_RT>>>
  friend constexpr bool operator>(const basic_string_view &lhs, T &&rhs) noexcept { return static_cast<T &&>(rhs) < lhs; }
  template<class T, class _RT = remove_reference_t<T>, class = enable_if_t<is_pointer_v<_RT>>>
  friend constexpr bool operator>(T &&lhs, const basic_string_view &rhs) noexcept { return rhs < static_cast<T &&>(lhs); }
  template<class T, class _RT = remove_reference_t<T>, class = enable_if_t<is_pointer_v<_RT>>>
  friend constexpr bool operator<=(const basic_string_view &lhs, T &&rhs) noexcept { return !(static_cast<T &&>(rhs) < lhs); }
  template<class T, class _RT = remove_reference_t<T>, class = enable_if_t<is_pointer_v<_RT>>>
  friend constexpr bool operator<=(T &&lhs, const basic_string_view &rhs) noexcept { return !(rhs < static_cast<T &&>(lhs)); }
  template<class T, class _RT = remove_reference_t<T>, class = enable_if_t<is_pointer_v<_RT>>>
  friend constexpr bool operator>=(const basic_string_view &lhs, T &&rhs) noexcept { return !(lhs < static_cast<T &&>(rhs)); }
  template<class T, class _RT = remove_reference_t<T>, class = enable_if_t<is_pointer_v<_RT>>>
  friend constexpr bool operator>=(T &&lhs, const basic_string_view &rhs) noexcept { return !(static_cast<T &&>(lhs) < rhs); }

  // Fixed-size array overloads: these bind exactly (no array-to-pointer decay), so comparing
  // against a `char[N]` (e.g. a raw on-disk field) correctly uses `array_bound_length` instead
  // of a strlen-style scan that could read past the array.
  template<size_type N>
  friend constexpr bool operator==(const basic_string_view &lhs, const CharT (&rhs)[N]) noexcept { return lhs == basic_string_view(rhs); }
  template<size_type N>
  friend constexpr bool operator==(const CharT (&lhs)[N], const basic_string_view &rhs) noexcept { return basic_string_view(lhs) == rhs; }
  template<size_type N>
  friend constexpr bool operator!=(const basic_string_view &lhs, const CharT (&rhs)[N]) noexcept { return !(lhs == basic_string_view(rhs)); }
  template<size_type N>
  friend constexpr bool operator!=(const CharT (&lhs)[N], const basic_string_view &rhs) noexcept { return !(basic_string_view(lhs) == rhs); }
  template<size_type N>
  friend constexpr bool operator<(const basic_string_view &lhs, const CharT (&rhs)[N]) noexcept { return lhs < basic_string_view(rhs); }
  template<size_type N>
  friend constexpr bool operator<(const CharT (&lhs)[N], const basic_string_view &rhs) noexcept { return basic_string_view(lhs) < rhs; }
  template<size_type N>
  friend constexpr bool operator>(const basic_string_view &lhs, const CharT (&rhs)[N]) noexcept { return basic_string_view(rhs) < lhs; }
  template<size_type N>
  friend constexpr bool operator>(const CharT (&lhs)[N], const basic_string_view &rhs) noexcept { return rhs < basic_string_view(lhs); }
  template<size_type N>
  friend constexpr bool operator<=(const basic_string_view &lhs, const CharT (&rhs)[N]) noexcept { return !(basic_string_view(rhs) < lhs); }
  template<size_type N>
  friend constexpr bool operator<=(const CharT (&lhs)[N], const basic_string_view &rhs) noexcept { return !(rhs < basic_string_view(lhs)); }
  template<size_type N>
  friend constexpr bool operator>=(const basic_string_view &lhs, const CharT (&rhs)[N]) noexcept { return !(lhs < basic_string_view(rhs)); }
  template<size_type N>
  friend constexpr bool operator>=(const CharT (&lhs)[N], const basic_string_view &rhs) noexcept { return !(basic_string_view(lhs) < rhs); }

  [[nodiscard]] bool starts_with(const basic_string_view &rhs) const noexcept {
    return this->_len >= rhs._len && traits_type::compare(_data, rhs._data, rhs._len) == 0;
  }

  [[nodiscard]] constexpr size_type find(CharT c, size_type pos = 0) const noexcept {
    size_type ret = npos;
    if (pos < this->_len) {
      const size_type n = this->_len - pos;
      const CharT *p = traits_type::find(_data + pos, n, c);
      if (p)
        ret = static_cast<size_type>(p - _data);
    }
    return ret;
  }

  [[nodiscard]] constexpr size_type rfind(basic_string_view v, size_type pos = npos) const noexcept {
    if (v._len > _len) return npos;
    if (pos > _len - v._len) pos = _len - v._len;
    if (v._len == 0) return pos;

    for (size_type i = pos;; --i) {
      if (traits_type::compare(_data + i, v._data, v._len) == 0) return i;
      if (i == 0) break;
    }
    return npos;
  }

  [[nodiscard]] constexpr size_type rfind(CharT c, size_type pos = npos) const noexcept {
    size_type size = _len;
    if (size != 0) {
      for (size_type i = min(pos, size - 1);; --i) {
        if (traits_type::eq(_data[i], c)) return i;
        if (i == 0) break;
      }
    }
    return npos;
  }

  [[nodiscard]] constexpr size_type rfind(const CharT *s, size_type pos, size_type count) const noexcept {
    return rfind(basic_string_view(s, count), pos);
  }

  [[nodiscard]] constexpr size_type rfind(const CharT *s, size_type pos = npos) const noexcept {
    return rfind(basic_string_view(s), pos);
  }

  [[nodiscard]] constexpr basic_string_view substr(size_type pos = 0, size_type n = npos) const noexcept {
    if (pos > this->_len) pos = this->_len;
    const size_type rlen = min<size_t>(n, this->_len - pos);
    return basic_string_view{this->_data + pos, rlen};
  }

  private:
  static constexpr size_type array_bound_length(const CharT *s, size_type max_len) noexcept {
    size_type i = 0;
    while (i < max_len && !traits_type::eq(s[i], CharT())) ++i;
    return i;
  }

  pointer _data = nullptr;
  size_type _len = 0;
};

using string_view = basic_string_view<char, char_traits<char>>;

}// namespace kstd
