
#pragma once

#include <__config.hpp>
#include <__algo/min.hpp>
#include <__type_traits/enable_if.hpp>
#include <__type_traits/is_pointer.hpp>
#include <__type_traits/remove_reference.hpp>
#include <char_traits.hpp>
#include <string_view.hpp>

namespace kstd {

/**
 * Static compile time string
 * 
 * @tparam N Number of characters to allocate
 */
template<size_t N, typename CharT = char, typename Traits = char_traits<CharT>>
struct basic_static_string {
  using traits_type = Traits;
  using value_type = CharT;
  using pointer = CharT *;
  using const_pointer = const CharT *;
  using reference = CharT &;
  using const_reference = const CharT &;
  using size_type = size_t;
  using iterator = pointer;
  using const_iterator = const_pointer;

  static constexpr size_type npos = static_cast<size_type>(-1);

  constexpr basic_static_string() noexcept : _len(0), _buffer{} {
    if constexpr (N > 0) {
      _buffer[0] = CharT();
    }
  }

  constexpr basic_static_string(const CharT *str) noexcept : _len(0), _buffer{} {
    if constexpr (N > 0) {
      _buffer[0] = CharT();
    }
    append(str);
  }

  constexpr basic_static_string(const basic_string_view<CharT, Traits> &sv) noexcept
      : _len(0), _buffer{} {
    if constexpr (N > 0) {
      _buffer[0] = CharT();
    }
    append(sv);
  }

  constexpr basic_static_string(const basic_static_string &other) noexcept
      : _len(other._len), _buffer{} {
    if constexpr (N > 0) {
      for (size_type i = 0; i <= other._len; ++i) {
        _buffer[i] = other._buffer[i];
      }
    }
  }

  constexpr basic_static_string &operator=(const basic_static_string &other) noexcept {
    if (this != &other) {
      this->_len = other._len;
      if constexpr (N > 0) {
        for (size_type i = 0; i <= other._len; ++i) {
          _buffer[i] = other._buffer[i];
        }
      }
    }
    return *this;
  }

  constexpr basic_static_string &operator=(const basic_string_view<CharT, Traits> &sv) noexcept {
    this->_len = 0;
    append(sv);
    return *this;
  }

  constexpr basic_static_string &operator=(const_pointer sv) noexcept {
    this->_len = 0;
    append(sv);
    return *this;
  }

  [[nodiscard]] constexpr pointer data() noexcept { return _buffer; }
  [[nodiscard]] constexpr const_pointer data() const noexcept { return _buffer; }
  [[nodiscard]] constexpr size_type length() const noexcept { return _len; }
  [[nodiscard]] constexpr size_type size() const noexcept { return _len; }
  [[nodiscard]] constexpr bool empty() const noexcept { return _len == 0; }
  constexpr void clear() noexcept {
    _len = 0;
    if constexpr (N > 0) {
      _buffer[0] = CharT();
    }
  }

  [[nodiscard]] constexpr iterator begin() noexcept { return _buffer; }
  [[nodiscard]] constexpr iterator end() noexcept { return _buffer + _len; }
  [[nodiscard]] constexpr const_iterator begin() const noexcept { return _buffer; }
  [[nodiscard]] constexpr const_iterator end() const noexcept { return _buffer + _len; }
  [[nodiscard]] constexpr const_iterator cbegin() const noexcept { return _buffer; }
  [[nodiscard]] constexpr const_iterator cend() const noexcept { return _buffer + _len; }

  constexpr reference operator[](size_type pos) noexcept { return _buffer[pos]; }
  constexpr const_reference operator[](size_type pos) const noexcept { return _buffer[pos]; }

  constexpr reference back() noexcept { return _buffer[_len - 1]; }
  constexpr const_reference back() const noexcept { return _buffer[_len - 1]; }

  [[nodiscard]] static constexpr size_type capacity() noexcept { return N > 0 ? N - 1 : 0; }

  constexpr operator basic_string_view<CharT, Traits>() const noexcept {
    return basic_string_view<CharT, Traits>(_buffer, _len);
  }

  constexpr void append(CharT c) noexcept {
    if constexpr (N == 0) return;
    if (this->_len >= N - 1)
      return;
    _buffer[this->_len++] = c;
    _buffer[this->_len] = CharT();
  }

  constexpr void push_back(CharT c) noexcept {
    append(c);
  }

  constexpr void pop_back() noexcept {
    if (_len > 0) {
      --_len;
      if constexpr (N > 0) {
        _buffer[_len] = CharT();
      }
    }
  }

  constexpr void append(const CharT *str) noexcept {
    append(basic_string_view<CharT, Traits>(str));
  }

  constexpr void append(const basic_string_view<CharT, Traits> &sv) noexcept {
    if constexpr (N == 0) return;
    size_type to_copy = sv.length();
    if (this->_len + to_copy > N - 1) {
      to_copy = (N - 1) - this->_len;
    }
    for (size_type i = 0; i < to_copy; ++i) {
      _buffer[this->_len + i] = sv[i];
    }
    this->_len += to_copy;
    _buffer[this->_len] = CharT();
  }

  [[nodiscard]] constexpr size_type find(CharT c, size_type pos = 0) const noexcept {
    size_type ret = npos;
    if (pos < _len) {
      const size_type n = _len - pos;
      const CharT *p = traits_type::find(_buffer + pos, n, c);
      if (p)
        ret = static_cast<size_type>(p - _buffer);
    }
    return ret;
  }

  [[nodiscard]] constexpr size_type rfind(basic_string_view<CharT, Traits> v, size_type pos = npos) const noexcept {
    return basic_string_view<CharT, Traits>(*this).rfind(v, pos);
  }

  [[nodiscard]] constexpr size_type rfind(CharT c, size_type pos = npos) const noexcept {
    return basic_string_view<CharT, Traits>(*this).rfind(c, pos);
  }

  [[nodiscard]] constexpr size_type rfind(const CharT* s, size_type pos, size_type count) const noexcept {
    return basic_string_view<CharT, Traits>(*this).rfind(s, pos, count);
  }

  [[nodiscard]] constexpr size_type rfind(const CharT* s, size_type pos = npos) const noexcept {
    return basic_string_view<CharT, Traits>(*this).rfind(s, pos);
  }

  [[nodiscard]] constexpr basic_string_view<CharT, Traits> substr(size_type pos = 0, size_type n = npos) const noexcept {
    if (pos > _len) pos = _len;
    const size_type rlen = min<size_t>(n, _len - pos);
    return basic_string_view<CharT, Traits>{_buffer + pos, rlen};
  }

  // Comparison operators
  friend constexpr bool operator==(const basic_static_string &lhs, const basic_static_string &rhs) noexcept {
    return static_cast<basic_string_view<CharT, Traits>>(lhs) == static_cast<basic_string_view<CharT, Traits>>(rhs);
  }
  friend constexpr bool operator==(const basic_static_string &lhs, const basic_string_view<CharT, Traits> &rhs) noexcept {
    return static_cast<basic_string_view<CharT, Traits>>(lhs) == rhs;
  }
  friend constexpr bool operator==(const basic_string_view<CharT, Traits> &lhs, const basic_static_string &rhs) noexcept {
    return lhs == static_cast<basic_string_view<CharT, Traits>>(rhs);
  }

  friend constexpr bool operator!=(const basic_static_string &lhs, const basic_static_string &rhs) noexcept {
    return !(lhs == rhs);
  }
  friend constexpr bool operator!=(const basic_static_string &lhs, const basic_string_view<CharT, Traits> &rhs) noexcept {
    return !(lhs == rhs);
  }
  friend constexpr bool operator!=(const basic_string_view<CharT, Traits> &lhs, const basic_static_string &rhs) noexcept {
    return !(lhs == rhs);
  }

  friend constexpr bool operator<(const basic_static_string &lhs, const basic_static_string &rhs) noexcept {
    return static_cast<basic_string_view<CharT, Traits>>(lhs) < static_cast<basic_string_view<CharT, Traits>>(rhs);
  }
  friend constexpr bool operator<(const basic_static_string &lhs, const basic_string_view<CharT, Traits> &rhs) noexcept {
    return static_cast<basic_string_view<CharT, Traits>>(lhs) < rhs;
  }
  friend constexpr bool operator<(const basic_string_view<CharT, Traits> &lhs, const basic_static_string &rhs) noexcept {
    return lhs < static_cast<basic_string_view<CharT, Traits>>(rhs);
  }

  friend constexpr bool operator>(const basic_static_string &lhs, const basic_static_string &rhs) noexcept {
    return rhs < lhs;
  }
  friend constexpr bool operator>(const basic_static_string &lhs, const basic_string_view<CharT, Traits> &rhs) noexcept {
    return rhs < lhs;
  }
  friend constexpr bool operator>(const basic_string_view<CharT, Traits> &lhs, const basic_static_string &rhs) noexcept {
    return rhs < lhs;
  }

  friend constexpr bool operator<=(const basic_static_string &lhs, const basic_static_string &rhs) noexcept {
    return !(rhs < lhs);
  }
  friend constexpr bool operator<=(const basic_static_string &lhs, const basic_string_view<CharT, Traits> &rhs) noexcept {
    return !(rhs < lhs);
  }
  friend constexpr bool operator<=(const basic_string_view<CharT, Traits> &lhs, const basic_static_string &rhs) noexcept {
    return !(rhs < lhs);
  }

  friend constexpr bool operator>=(const basic_static_string &lhs, const basic_static_string &rhs) noexcept {
    return !(lhs < rhs);
  }
  friend constexpr bool operator>=(const basic_static_string &lhs, const basic_string_view<CharT, Traits> &rhs) noexcept {
    return !(lhs < rhs);
  }
  friend constexpr bool operator>=(const basic_string_view<CharT, Traits> &lhs, const basic_static_string &rhs) noexcept {
    return !(lhs < rhs);
  }

  // Pointer overloads for real, dynamically-sized C strings (not fixed-size arrays). Deduced via
  // a forwarding reference on the un-decayed argument type and SFINAE-constrained to pointer
  // types only (see the matching comment in string_view.hpp for the full rationale): this keeps
  // them from ever competing with the fixed-size array overloads below for a `char[M]` argument.
  template<class T, class _RT = remove_reference_t<T>, class = enable_if_t<is_pointer_v<_RT>>>
  friend constexpr bool operator==(const basic_static_string &lhs, T &&rhs) noexcept {
    return static_cast<basic_string_view<CharT, Traits>>(lhs) == static_cast<T &&>(rhs);
  }
  template<class T, class _RT = remove_reference_t<T>, class = enable_if_t<is_pointer_v<_RT>>>
  friend constexpr bool operator==(T &&lhs, const basic_static_string &rhs) noexcept {
    return static_cast<T &&>(lhs) == static_cast<basic_string_view<CharT, Traits>>(rhs);
  }
  template<class T, class _RT = remove_reference_t<T>, class = enable_if_t<is_pointer_v<_RT>>>
  friend constexpr bool operator!=(const basic_static_string &lhs, T &&rhs) noexcept {
    return !(lhs == static_cast<T &&>(rhs));
  }
  template<class T, class _RT = remove_reference_t<T>, class = enable_if_t<is_pointer_v<_RT>>>
  friend constexpr bool operator!=(T &&lhs, const basic_static_string &rhs) noexcept {
    return !(static_cast<T &&>(lhs) == rhs);
  }
  template<class T, class _RT = remove_reference_t<T>, class = enable_if_t<is_pointer_v<_RT>>>
  friend constexpr bool operator<(const basic_static_string &lhs, T &&rhs) noexcept {
    return static_cast<basic_string_view<CharT, Traits>>(lhs) < static_cast<T &&>(rhs);
  }
  template<class T, class _RT = remove_reference_t<T>, class = enable_if_t<is_pointer_v<_RT>>>
  friend constexpr bool operator<(T &&lhs, const basic_static_string &rhs) noexcept {
    return static_cast<T &&>(lhs) < static_cast<basic_string_view<CharT, Traits>>(rhs);
  }
  template<class T, class _RT = remove_reference_t<T>, class = enable_if_t<is_pointer_v<_RT>>>
  friend constexpr bool operator>(const basic_static_string &lhs, T &&rhs) noexcept {
    return static_cast<T &&>(rhs) < lhs;
  }
  template<class T, class _RT = remove_reference_t<T>, class = enable_if_t<is_pointer_v<_RT>>>
  friend constexpr bool operator>(T &&lhs, const basic_static_string &rhs) noexcept {
    return rhs < static_cast<T &&>(lhs);
  }
  template<class T, class _RT = remove_reference_t<T>, class = enable_if_t<is_pointer_v<_RT>>>
  friend constexpr bool operator<=(const basic_static_string &lhs, T &&rhs) noexcept {
    return !(static_cast<T &&>(rhs) < lhs);
  }
  template<class T, class _RT = remove_reference_t<T>, class = enable_if_t<is_pointer_v<_RT>>>
  friend constexpr bool operator<=(T &&lhs, const basic_static_string &rhs) noexcept {
    return !(rhs < static_cast<T &&>(lhs));
  }
  template<class T, class _RT = remove_reference_t<T>, class = enable_if_t<is_pointer_v<_RT>>>
  friend constexpr bool operator>=(const basic_static_string &lhs, T &&rhs) noexcept {
    return !(lhs < static_cast<T &&>(rhs));
  }
  template<class T, class _RT = remove_reference_t<T>, class = enable_if_t<is_pointer_v<_RT>>>
  friend constexpr bool operator>=(T &&lhs, const basic_static_string &rhs) noexcept {
    return !(static_cast<T &&>(lhs) < rhs);
  }

  // Fixed-size array overloads: bind exactly (no array-to-pointer decay), so comparing against
  // a `char[M]` (e.g. a raw on-disk field) correctly uses the array's bound instead of a
  // strlen-style scan that could read past a non-NUL-terminated array.
  template<size_t M>
  friend constexpr bool operator==(const basic_static_string &lhs, const CharT (&rhs)[M]) noexcept {
    return static_cast<basic_string_view<CharT, Traits>>(lhs) == basic_string_view<CharT, Traits>(rhs);
  }
  template<size_t M>
  friend constexpr bool operator==(const CharT (&lhs)[M], const basic_static_string &rhs) noexcept {
    return basic_string_view<CharT, Traits>(lhs) == static_cast<basic_string_view<CharT, Traits>>(rhs);
  }
  template<size_t M>
  friend constexpr bool operator!=(const basic_static_string &lhs, const CharT (&rhs)[M]) noexcept {
    return !(lhs == rhs);
  }
  template<size_t M>
  friend constexpr bool operator!=(const CharT (&lhs)[M], const basic_static_string &rhs) noexcept {
    return !(lhs == rhs);
  }
  template<size_t M>
  friend constexpr bool operator<(const basic_static_string &lhs, const CharT (&rhs)[M]) noexcept {
    return static_cast<basic_string_view<CharT, Traits>>(lhs) < basic_string_view<CharT, Traits>(rhs);
  }
  template<size_t M>
  friend constexpr bool operator<(const CharT (&lhs)[M], const basic_static_string &rhs) noexcept {
    return basic_string_view<CharT, Traits>(lhs) < static_cast<basic_string_view<CharT, Traits>>(rhs);
  }
  template<size_t M>
  friend constexpr bool operator>(const basic_static_string &lhs, const CharT (&rhs)[M]) noexcept {
    return rhs < lhs;
  }
  template<size_t M>
  friend constexpr bool operator>(const CharT (&lhs)[M], const basic_static_string &rhs) noexcept {
    return rhs < lhs;
  }
  template<size_t M>
  friend constexpr bool operator<=(const basic_static_string &lhs, const CharT (&rhs)[M]) noexcept {
    return !(rhs < lhs);
  }
  template<size_t M>
  friend constexpr bool operator<=(const CharT (&lhs)[M], const basic_static_string &rhs) noexcept {
    return !(rhs < lhs);
  }
  template<size_t M>
  friend constexpr bool operator>=(const basic_static_string &lhs, const CharT (&rhs)[M]) noexcept {
    return !(lhs < rhs);
  }
  template<size_t M>
  friend constexpr bool operator>=(const CharT (&lhs)[M], const basic_static_string &rhs) noexcept {
    return !(lhs < rhs);
  }

  private:
  size_type _len = 0;
  CharT _buffer[N];
};

template<size_t N>
using static_string = basic_static_string<N, char, char_traits<char>>;

} // namespace kstd
