
#pragma once

#include <__config.hpp>

#include <__algo/min.hpp>
#include <__type_traits/enable_if.hpp>
#include <__type_traits/is_pointer.hpp>
#include <__type_traits/remove_reference.hpp>
#include <char_traits.hpp>
#include <new.hpp>
#include <string_view.hpp>
#include <utility.hpp>

namespace kstd {

_KSTD_API inline void *memcpy(void *dst, const void *src, size_t n) {
  return __builtin_memcpy(dst, src, n);
}

_KSTD_API inline void *memmove(void *dest, const void *src, size_t count) noexcept {
  auto *d = static_cast<unsigned char *>(dest);
  const auto *s = static_cast<const unsigned char *>(src);

  if (d == s || count == 0) {
    return dest;
  }

  if (d > s && d < s + count) {
    // Overlapping case: Copy backwards
    d += count;
    s += count;
    while (count--) {
      *(--d) = *(--s);
    }
  } else {
    // Non-overlapping or d < s: Copy forwards (can defer to memcpy)
    memcpy(dest, src, count);
  }

  return dest;
}

/**
 * A dynamically-sized, heap-owning string.
 *
 * @tparam CharT char type
 * @tparam Traits char traits
 */
template<typename CharT, class Traits = char_traits<CharT>>
struct basic_string {
  using traits_type = Traits;
  using value_type = CharT;
  using pointer = CharT *;
  using const_pointer = const CharT *;
  using reference = CharT &;
  using const_reference = const CharT &;
  using size_type = size_t;
  using const_iterator = const CharT *;
  using iterator = CharT *;

  static constexpr size_type npos = static_cast<size_type>(-1);

  // Number of characters (excluding the NUL terminator) that can be stored inline,
  // without any heap allocation. This is what makes short strings (such as the ones
  // used for well-known object-manager paths) usable in `constexpr` contexts: no
  // dynamic allocation ever happens, so the resulting value can be a core constant
  // expression.
  static constexpr size_type _sso_capacity = 22;

  constexpr basic_string() noexcept
      : _inline_buf{}, _data(_inline_buf), _len(0), _capacity(_sso_capacity + 1) {}

  constexpr basic_string(const CharT *str)
      : _inline_buf{}, _data(_inline_buf), _len(0), _capacity(_sso_capacity + 1) {
    append(basic_string_view<CharT, Traits>(str));
  }

  constexpr basic_string(const basic_string_view<CharT, Traits> &sv)
      : _inline_buf{}, _data(_inline_buf), _len(0), _capacity(_sso_capacity + 1) {
    append(sv);
  }

  constexpr basic_string(const basic_string &other)
      : _inline_buf{}, _data(_inline_buf), _len(0), _capacity(_sso_capacity + 1) {
    append(basic_string_view<CharT, Traits>(other._data, other._len));
  }

  constexpr basic_string(basic_string &&other) noexcept
      : _inline_buf{}, _data(_inline_buf), _len(0), _capacity(_sso_capacity + 1) {
    if (other._data == other._inline_buf) {
      if (other._len > 0) Traits::copy(this->_data, other._data, other._len);
      this->_len = other._len;
      null_terminate();
    } else {
      this->_data = other._data;
      this->_len = other._len;
      this->_capacity = other._capacity;
    }
    other._data = other._inline_buf;
    other._len = 0;
    other._capacity = _sso_capacity + 1;
    other.null_terminate();
  }

  constexpr ~basic_string() {
    if (_data != _inline_buf) {
      ::operator delete(_data);
    }
  }

  constexpr basic_string &operator=(const basic_string &other) {
    if (this == &other) return *this;
    this->_len = 0;
    append(basic_string_view<CharT, Traits>(other._data, other._len));
    return *this;
  }

  constexpr basic_string &operator=(basic_string &&other) noexcept {
    if (this == &other) return *this;
    if (_data != _inline_buf) {
      ::operator delete(_data);
    }
    if (other._data == other._inline_buf) {
      this->_data = _inline_buf;
      this->_capacity = _sso_capacity + 1;
      if (other._len > 0) Traits::copy(this->_data, other._data, other._len);
      this->_len = other._len;
      null_terminate();
    } else {
      this->_data = other._data;
      this->_len = other._len;
      this->_capacity = other._capacity;
    }
    other._data = other._inline_buf;
    other._len = 0;
    other._capacity = _sso_capacity + 1;
    other.null_terminate();
    return *this;
  }

  constexpr basic_string &operator=(const basic_string_view<CharT, Traits> &sv) {
    this->_len = 0;
    append(sv);
    return *this;
  }

  constexpr basic_string &operator=(const_pointer s) {
    this->_len = 0;
    append(basic_string_view<CharT, Traits>(s));
    return *this;
  }

  [[nodiscard]] constexpr pointer data() noexcept { return _data; }
  [[nodiscard]] constexpr const_pointer data() const noexcept { return _data; }
  [[nodiscard]] constexpr const_pointer c_str() const noexcept { return _data ? _data : &_empty_char; }
  [[nodiscard]] constexpr size_type length() const noexcept { return _len; }
  [[nodiscard]] constexpr size_type size() const noexcept { return _len; }
  [[nodiscard]] constexpr bool empty() const noexcept { return _len == 0; }
  constexpr void clear() noexcept {
    _len = 0;
    null_terminate();
  }

  [[nodiscard]] constexpr iterator begin() noexcept { return _data; }
  [[nodiscard]] constexpr iterator end() noexcept { return _data + _len; }
  [[nodiscard]] constexpr const_iterator begin() const noexcept { return _data; }
  [[nodiscard]] constexpr const_iterator end() const noexcept { return _data + _len; }
  [[nodiscard]] constexpr const_iterator cbegin() const noexcept { return _data; }
  [[nodiscard]] constexpr const_iterator cend() const noexcept { return _data + _len; }

  constexpr reference operator[](size_type pos) noexcept { return _data[pos]; }
  constexpr const_reference operator[](size_type pos) const noexcept { return _data[pos]; }

  constexpr reference back() noexcept { return _data[_len - 1]; }
  constexpr const_reference back() const noexcept { return _data[_len - 1]; }

  [[nodiscard]] constexpr size_type capacity() const noexcept {
    return this->_capacity > 0 ? this->_capacity - 1 : 0;
  }

  void reserve(size_type n) {
    if (n + 1 > this->_capacity) {
      __grow_to(n + 1);
    }
  }

  void resize(size_type count) {
    resize(count, CharT());
  }

  void resize(size_type count, CharT ch) {
    if (count > this->_len) {
      __ensure_capacity_for(count - this->_len);
      for (size_type i = this->_len; i < count; ++i) {
        this->_data[i] = ch;
      }
    }
    this->_len = count;
    null_terminate();
  }

  constexpr operator basic_string_view<CharT, Traits>() const noexcept {
    return basic_string_view<CharT, Traits>(_data, _len);
  }

  constexpr void append(CharT c) noexcept {
    __ensure_capacity_for(1);
    this->_data[this->_len++] = c;
    null_terminate();
  }

  constexpr void push_back(CharT c) noexcept {
    append(c);
  }

  constexpr void pop_back() noexcept {
    if (this->_len > 0) {
      this->_len--;
      null_terminate();
    }
  }

  constexpr void append(const CharT *str) noexcept {
    append(basic_string_view<CharT, Traits>(str));
  }

  constexpr void append(const basic_string_view<CharT, Traits> &sv) noexcept {
    if (sv.length() == 0) return;
    __ensure_capacity_for(sv.length());
    Traits::copy(this->_data + this->_len, sv.data(), sv.length());
    this->_len += sv.length();
    null_terminate();
  }

  void swap(basic_string &other) noexcept {
    // Note: a naive pointer swap would break the small-string-optimization case, where
    // `_data` may point into the object's own inline buffer.
    basic_string tmp(kstd::move(*this));
    *this = kstd::move(other);
    other = kstd::move(tmp);
  }

  [[nodiscard]] constexpr size_type find(CharT c, size_type pos = 0) const noexcept {
    size_type ret = npos;
    if (pos < _len) {
      const size_type n = _len - pos;
      const CharT *p = traits_type::find(_data + pos, n, c);
      if (p)
        ret = static_cast<size_type>(p - _data);
    }
    return ret;
  }

  [[nodiscard]] constexpr size_type rfind(basic_string_view<CharT, Traits> v, size_type pos = npos) const noexcept {
    return basic_string_view<CharT, Traits>(*this).rfind(v, pos);
  }

  [[nodiscard]] constexpr size_type rfind(CharT c, size_type pos = npos) const noexcept {
    return basic_string_view<CharT, Traits>(*this).rfind(c, pos);
  }

  [[nodiscard]] constexpr size_type rfind(const CharT *s, size_type pos, size_type count) const noexcept {
    return basic_string_view<CharT, Traits>(*this).rfind(s, pos, count);
  }

  [[nodiscard]] constexpr size_type rfind(const CharT *s, size_type pos = npos) const noexcept {
    return basic_string_view<CharT, Traits>(*this).rfind(s, pos);
  }

  [[nodiscard]] constexpr basic_string_view<CharT, Traits> substr(size_type pos = 0, size_type n = npos) const noexcept {
    if (pos > _len) pos = _len;
    const size_type rlen = min<size_t>(n, _len - pos);
    return basic_string_view<CharT, Traits>{_data + pos, rlen};
  }

  [[nodiscard]] constexpr int compare(basic_string_view<CharT, Traits> v) const noexcept {
    return basic_string_view<CharT, Traits>(*this).compare(v);
  }

  [[nodiscard]] constexpr int compare(size_type pos1, size_type count1, basic_string_view<CharT, Traits> v) const noexcept {
    return basic_string_view<CharT, Traits>(*this).compare(pos1, count1, v);
  }

  [[nodiscard]] constexpr int compare(size_type pos1, size_type count1, basic_string_view<CharT, Traits> v, size_type pos2, size_type count2) const noexcept {
    return basic_string_view<CharT, Traits>(*this).compare(pos1, count1, v, pos2, count2);
  }

  [[nodiscard]] constexpr int compare(const CharT *s) const noexcept {
    return basic_string_view<CharT, Traits>(*this).compare(s);
  }

  [[nodiscard]] constexpr int compare(size_type pos1, size_type count1, const CharT *s) const noexcept {
    return basic_string_view<CharT, Traits>(*this).compare(pos1, count1, s);
  }

  [[nodiscard]] constexpr int compare(size_type pos1, size_type count1, const CharT *s, size_type count2) const noexcept {
    return basic_string_view<CharT, Traits>(*this).compare(pos1, count1, s, count2);
  }

  // Comparison operators
  friend constexpr bool operator==(const basic_string &lhs, const basic_string &rhs) noexcept {
    return static_cast<basic_string_view<CharT, Traits>>(lhs) == static_cast<basic_string_view<CharT, Traits>>(rhs);
  }
  friend constexpr bool operator==(const basic_string &lhs, const basic_string_view<CharT, Traits> &rhs) noexcept {
    return static_cast<basic_string_view<CharT, Traits>>(lhs) == rhs;
  }
  friend constexpr bool operator==(const basic_string_view<CharT, Traits> &lhs, const basic_string &rhs) noexcept {
    return lhs == static_cast<basic_string_view<CharT, Traits>>(rhs);
  }

  friend constexpr bool operator!=(const basic_string &lhs, const basic_string &rhs) noexcept {
    return !(lhs == rhs);
  }
  friend constexpr bool operator!=(const basic_string &lhs, const basic_string_view<CharT, Traits> &rhs) noexcept {
    return !(lhs == rhs);
  }
  friend constexpr bool operator!=(const basic_string_view<CharT, Traits> &lhs, const basic_string &rhs) noexcept {
    return !(lhs == rhs);
  }

  friend constexpr bool operator<(const basic_string &lhs, const basic_string &rhs) noexcept {
    return static_cast<basic_string_view<CharT, Traits>>(lhs) < static_cast<basic_string_view<CharT, Traits>>(rhs);
  }
  friend constexpr bool operator<(const basic_string &lhs, const basic_string_view<CharT, Traits> &rhs) noexcept {
    return static_cast<basic_string_view<CharT, Traits>>(lhs) < rhs;
  }
  friend constexpr bool operator<(const basic_string_view<CharT, Traits> &lhs, const basic_string &rhs) noexcept {
    return lhs < static_cast<basic_string_view<CharT, Traits>>(rhs);
  }

  friend constexpr bool operator>(const basic_string &lhs, const basic_string &rhs) noexcept {
    return rhs < lhs;
  }
  friend constexpr bool operator>(const basic_string &lhs, const basic_string_view<CharT, Traits> &rhs) noexcept {
    return rhs < lhs;
  }
  friend constexpr bool operator>(const basic_string_view<CharT, Traits> &lhs, const basic_string &rhs) noexcept {
    return rhs < lhs;
  }

  friend constexpr bool operator<=(const basic_string &lhs, const basic_string &rhs) noexcept {
    return !(rhs < lhs);
  }
  friend constexpr bool operator<=(const basic_string &lhs, const basic_string_view<CharT, Traits> &rhs) noexcept {
    return !(rhs < lhs);
  }
  friend constexpr bool operator<=(const basic_string_view<CharT, Traits> &lhs, const basic_string &rhs) noexcept {
    return !(rhs < lhs);
  }

  friend constexpr bool operator>=(const basic_string &lhs, const basic_string &rhs) noexcept {
    return !(lhs < rhs);
  }
  friend constexpr bool operator>=(const basic_string &lhs, const basic_string_view<CharT, Traits> &rhs) noexcept {
    return !(lhs < rhs);
  }
  friend constexpr bool operator>=(const basic_string_view<CharT, Traits> &lhs, const basic_string &rhs) noexcept {
    return !(lhs < rhs);
  }

  // Pointer overloads for real, dynamically-sized C strings (not fixed-size arrays). Deduced via
  // a forwarding reference on the un-decayed argument type and SFINAE-constrained to pointer
  // types only (see the matching comment in string_view.hpp for the full rationale): this keeps
  // them from ever competing with the fixed-size array overloads below for a `char[M]` argument.
  template<class T, class _RT = remove_reference_t<T>, class = enable_if_t<is_pointer_v<_RT>>>
  friend constexpr bool operator==(const basic_string &lhs, T &&rhs) noexcept {
    return static_cast<basic_string_view<CharT, Traits>>(lhs) == static_cast<T &&>(rhs);
  }
  template<class T, class _RT = remove_reference_t<T>, class = enable_if_t<is_pointer_v<_RT>>>
  friend constexpr bool operator==(T &&lhs, const basic_string &rhs) noexcept {
    return static_cast<T &&>(lhs) == static_cast<basic_string_view<CharT, Traits>>(rhs);
  }
  template<class T, class _RT = remove_reference_t<T>, class = enable_if_t<is_pointer_v<_RT>>>
  friend constexpr bool operator!=(const basic_string &lhs, T &&rhs) noexcept {
    return !(lhs == static_cast<T &&>(rhs));
  }
  template<class T, class _RT = remove_reference_t<T>, class = enable_if_t<is_pointer_v<_RT>>>
  friend constexpr bool operator!=(T &&lhs, const basic_string &rhs) noexcept {
    return !(static_cast<T &&>(lhs) == rhs);
  }
  template<class T, class _RT = remove_reference_t<T>, class = enable_if_t<is_pointer_v<_RT>>>
  friend constexpr bool operator<(const basic_string &lhs, T &&rhs) noexcept {
    return static_cast<basic_string_view<CharT, Traits>>(lhs) < static_cast<T &&>(rhs);
  }
  template<class T, class _RT = remove_reference_t<T>, class = enable_if_t<is_pointer_v<_RT>>>
  friend constexpr bool operator<(T &&lhs, const basic_string &rhs) noexcept {
    return static_cast<T &&>(lhs) < static_cast<basic_string_view<CharT, Traits>>(rhs);
  }
  template<class T, class _RT = remove_reference_t<T>, class = enable_if_t<is_pointer_v<_RT>>>
  friend constexpr bool operator>(const basic_string &lhs, T &&rhs) noexcept {
    return static_cast<T &&>(rhs) < lhs;
  }
  template<class T, class _RT = remove_reference_t<T>, class = enable_if_t<is_pointer_v<_RT>>>
  friend constexpr bool operator>(T &&lhs, const basic_string &rhs) noexcept {
    return rhs < static_cast<T &&>(lhs);
  }
  template<class T, class _RT = remove_reference_t<T>, class = enable_if_t<is_pointer_v<_RT>>>
  friend constexpr bool operator<=(const basic_string &lhs, T &&rhs) noexcept {
    return !(static_cast<T &&>(rhs) < lhs);
  }
  template<class T, class _RT = remove_reference_t<T>, class = enable_if_t<is_pointer_v<_RT>>>
  friend constexpr bool operator<=(T &&lhs, const basic_string &rhs) noexcept {
    return !(rhs < static_cast<T &&>(lhs));
  }
  template<class T, class _RT = remove_reference_t<T>, class = enable_if_t<is_pointer_v<_RT>>>
  friend constexpr bool operator>=(const basic_string &lhs, T &&rhs) noexcept {
    return !(lhs < static_cast<T &&>(rhs));
  }
  template<class T, class _RT = remove_reference_t<T>, class = enable_if_t<is_pointer_v<_RT>>>
  friend constexpr bool operator>=(T &&lhs, const basic_string &rhs) noexcept {
    return !(static_cast<T &&>(lhs) < rhs);
  }

  // Fixed-size array overloads: bind exactly (no array-to-pointer decay), so comparing against
  // a `char[M]` (e.g. a raw on-disk field) correctly uses the array's bound instead of a
  // strlen-style scan that could read past a non-NUL-terminated array.
  template<size_t M>
  friend constexpr bool operator==(const basic_string &lhs, const CharT (&rhs)[M]) noexcept {
    return static_cast<basic_string_view<CharT, Traits>>(lhs) == basic_string_view<CharT, Traits>(rhs);
  }
  template<size_t M>
  friend constexpr bool operator==(const CharT (&lhs)[M], const basic_string &rhs) noexcept {
    return basic_string_view<CharT, Traits>(lhs) == static_cast<basic_string_view<CharT, Traits>>(rhs);
  }
  template<size_t M>
  friend constexpr bool operator!=(const basic_string &lhs, const CharT (&rhs)[M]) noexcept {
    return !(lhs == rhs);
  }
  template<size_t M>
  friend constexpr bool operator!=(const CharT (&lhs)[M], const basic_string &rhs) noexcept {
    return !(lhs == rhs);
  }
  template<size_t M>
  friend constexpr bool operator<(const basic_string &lhs, const CharT (&rhs)[M]) noexcept {
    return static_cast<basic_string_view<CharT, Traits>>(lhs) < basic_string_view<CharT, Traits>(rhs);
  }
  template<size_t M>
  friend constexpr bool operator<(const CharT (&lhs)[M], const basic_string &rhs) noexcept {
    return basic_string_view<CharT, Traits>(lhs) < static_cast<basic_string_view<CharT, Traits>>(rhs);
  }
  template<size_t M>
  friend constexpr bool operator>(const basic_string &lhs, const CharT (&rhs)[M]) noexcept {
    return rhs < lhs;
  }
  template<size_t M>
  friend constexpr bool operator>(const CharT (&lhs)[M], const basic_string &rhs) noexcept {
    return rhs < lhs;
  }
  template<size_t M>
  friend constexpr bool operator<=(const basic_string &lhs, const CharT (&rhs)[M]) noexcept {
    return !(rhs < lhs);
  }
  template<size_t M>
  friend constexpr bool operator<=(const CharT (&lhs)[M], const basic_string &rhs) noexcept {
    return !(rhs < lhs);
  }
  template<size_t M>
  friend constexpr bool operator>=(const basic_string &lhs, const CharT (&rhs)[M]) noexcept {
    return !(lhs < rhs);
  }
  template<size_t M>
  friend constexpr bool operator>=(const CharT (&lhs)[M], const basic_string &rhs) noexcept {
    return !(lhs < rhs);
  }

  private:
  static constexpr CharT _empty_char = CharT();

  constexpr void null_terminate() noexcept {
    if (this->_data && this->_len < this->_capacity) {
      this->_data[this->_len] = CharT();
    }
  }

  constexpr void __ensure_capacity_for(size_type extra) {
    const size_type needed = this->_len + extra + 1;
    if (needed <= this->_capacity) return;
    size_type new_capacity = this->_capacity == 0 ? 8 : this->_capacity * 2;
    if (new_capacity < needed) new_capacity = needed;
    __grow_to(new_capacity);
  }

  constexpr void __grow_to(size_type new_capacity) {
    auto *new_data = static_cast<pointer>(::operator new(new_capacity * sizeof(CharT)));
    if (this->_len > 0) {
      Traits::copy(new_data, this->_data, this->_len);
    }
    if (this->_data != _inline_buf) {
      ::operator delete(this->_data);
    }
    this->_data = new_data;
    this->_capacity = new_capacity;
    null_terminate();
  }

  CharT _inline_buf[_sso_capacity + 1];
  pointer _data;
  size_type _len;
  size_type _capacity;
};

template<typename CharT, class Traits>
constexpr basic_string<CharT, Traits> operator+(const basic_string<CharT, Traits> &lhs, const basic_string<CharT, Traits> &rhs) {
  basic_string<CharT, Traits> result(lhs);
  result.append(static_cast<basic_string_view<CharT, Traits>>(rhs));
  return result;
}

template<typename CharT, class Traits>
constexpr basic_string<CharT, Traits> operator+(const basic_string<CharT, Traits> &lhs, const CharT *rhs) {
  basic_string<CharT, Traits> result(lhs);
  result.append(rhs);
  return result;
}

template<typename CharT, class Traits>
constexpr basic_string<CharT, Traits> operator+(const CharT *lhs, const basic_string<CharT, Traits> &rhs) {
  basic_string<CharT, Traits> result(lhs);
  result.append(static_cast<basic_string_view<CharT, Traits>>(rhs));
  return result;
}

template<typename CharT, class Traits>
constexpr basic_string<CharT, Traits> operator+(const basic_string<CharT, Traits> &lhs, CharT rhs) {
  basic_string<CharT, Traits> result(lhs);
  result.append(rhs);
  return result;
}

template<typename CharT, class Traits>
constexpr basic_string<CharT, Traits> operator+(CharT lhs, const basic_string<CharT, Traits> &rhs) {
  basic_string<CharT, Traits> result;
  result.append(lhs);
  result.append(static_cast<basic_string_view<CharT, Traits>>(rhs));
  return result;
}

using string = basic_string<char, char_traits<char>>;

}// namespace kstd
