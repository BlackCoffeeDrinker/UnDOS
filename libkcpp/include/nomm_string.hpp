
#pragma once
#include <__config.hpp>

#include <__algo/min.hpp>
#include "array.hpp"
#include "char_traits.hpp"
#include "stddef.hpp"
#include "string_view.hpp"

namespace kstd {
/**
 * A string-like interface with non-owning memory
 * 
 * @tparam CharT char type
 * @tparam Traits char traits
 */
template<typename CharT, class Traits = char_traits<CharT>>
struct basic_nomm_string {
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

  template<size_t s>
  constexpr basic_nomm_string(CharT (&data)[s])
      : _data(data), _len(0), _capacity(s) {
    size_type len = Traits::length(data);
    this->_len = (len >= _capacity) ? (_capacity > 0 ? _capacity - 1 : 0) : len;
    null_terminate();
  }

  template<size_t s>
  constexpr basic_nomm_string(array<CharT, s> &data)
      : _data(data.data()), _len(0), _capacity(s) {
    size_type len = Traits::length(data.data());
    this->_len = (len >= _capacity) ? (_capacity > 0 ? _capacity - 1 : 0) : len;
    null_terminate();
  }

  constexpr basic_nomm_string(CharT *begin, CharT *end)
      : _data(begin), _len(0),
        _capacity((begin < end) ? static_cast<size_t>(end - begin) : 0) {
    if (this->_capacity > 0) {
      size_type len = Traits::length(begin);
      this->_len = (len >= _capacity) ? _capacity - 1 : len;
      null_terminate();
    }
  }

  [[nodiscard]] constexpr pointer data() noexcept { return _data; }
  [[nodiscard]] constexpr const_pointer data() const noexcept { return _data; }
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

  constexpr operator basic_string_view<CharT, Traits>() const noexcept {
    return basic_string_view<CharT, Traits>(_data, _len);
  }

  constexpr ~basic_nomm_string() {
    null_terminate();
  }

  constexpr void append(CharT c) noexcept {
    if (this->_capacity == 0 || this->_len >= this->_capacity - 1)
      return;
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

  constexpr void append(const basic_string_view<CharT, Traits> &sv) noexcept {
    if (this->_capacity == 0 || sv.length() + this->_len > this->_capacity - 1)
      return;
    Traits::copy(this->_data + this->_len, sv.data(), sv.length());
    this->_len += sv.length();
    null_terminate();
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

  [[nodiscard]] constexpr size_type rfind(const CharT* s, size_type pos, size_type count) const noexcept {
    return basic_string_view<CharT, Traits>(*this).rfind(s, pos, count);
  }

  [[nodiscard]] constexpr size_type rfind(const CharT* s, size_type pos = npos) const noexcept {
    return basic_string_view<CharT, Traits>(*this).rfind(s, pos);
  }

  [[nodiscard]] constexpr basic_string_view<CharT, Traits> substr(size_type pos = 0, size_type n = npos) const noexcept {
    if (pos > _len) pos = _len;
    const size_type rlen = min<size_t>(n, _len - pos);
    return basic_string_view<CharT, Traits>{_data + pos, rlen};
  }

  // Comparison operators
  friend constexpr bool operator==(const basic_nomm_string &lhs, const basic_nomm_string &rhs) noexcept {
    return static_cast<basic_string_view<CharT, Traits>>(lhs) == static_cast<basic_string_view<CharT, Traits>>(rhs);
  }
  friend constexpr bool operator==(const basic_nomm_string &lhs, const basic_string_view<CharT, Traits> &rhs) noexcept {
    return static_cast<basic_string_view<CharT, Traits>>(lhs) == rhs;
  }
  friend constexpr bool operator==(const basic_string_view<CharT, Traits> &lhs, const basic_nomm_string &rhs) noexcept {
    return lhs == static_cast<basic_string_view<CharT, Traits>>(rhs);
  }

  friend constexpr bool operator!=(const basic_nomm_string &lhs, const basic_nomm_string &rhs) noexcept {
    return !(lhs == rhs);
  }
  friend constexpr bool operator!=(const basic_nomm_string &lhs, const basic_string_view<CharT, Traits> &rhs) noexcept {
    return !(lhs == rhs);
  }
  friend constexpr bool operator!=(const basic_string_view<CharT, Traits> &lhs, const basic_nomm_string &rhs) noexcept {
    return !(lhs == rhs);
  }

  friend constexpr bool operator<(const basic_nomm_string &lhs, const basic_nomm_string &rhs) noexcept {
    return static_cast<basic_string_view<CharT, Traits>>(lhs) < static_cast<basic_string_view<CharT, Traits>>(rhs);
  }
  friend constexpr bool operator<(const basic_nomm_string &lhs, const basic_string_view<CharT, Traits> &rhs) noexcept {
    return static_cast<basic_string_view<CharT, Traits>>(lhs) < rhs;
  }
  friend constexpr bool operator<(const basic_string_view<CharT, Traits> &lhs, const basic_nomm_string &rhs) noexcept {
    return lhs < static_cast<basic_string_view<CharT, Traits>>(rhs);
  }

  friend constexpr bool operator>(const basic_nomm_string &lhs, const basic_nomm_string &rhs) noexcept {
    return rhs < lhs;
  }
  friend constexpr bool operator>(const basic_nomm_string &lhs, const basic_string_view<CharT, Traits> &rhs) noexcept {
    return rhs < lhs;
  }
  friend constexpr bool operator>(const basic_string_view<CharT, Traits> &lhs, const basic_nomm_string &rhs) noexcept {
    return rhs < lhs;
  }

  friend constexpr bool operator<=(const basic_nomm_string &lhs, const basic_nomm_string &rhs) noexcept {
    return !(rhs < lhs);
  }
  friend constexpr bool operator<=(const basic_nomm_string &lhs, const basic_string_view<CharT, Traits> &rhs) noexcept {
    return !(rhs < lhs);
  }
  friend constexpr bool operator<=(const basic_string_view<CharT, Traits> &lhs, const basic_nomm_string &rhs) noexcept {
    return !(rhs < lhs);
  }

  friend constexpr bool operator>=(const basic_nomm_string &lhs, const basic_nomm_string &rhs) noexcept {
    return !(lhs < rhs);
  }
  friend constexpr bool operator>=(const basic_nomm_string &lhs, const basic_string_view<CharT, Traits> &rhs) noexcept {
    return !(lhs < rhs);
  }
  friend constexpr bool operator>=(const basic_string_view<CharT, Traits> &lhs, const basic_nomm_string &rhs) noexcept {
    return !(lhs < rhs);
  }

  friend constexpr bool operator==(const basic_nomm_string &lhs, const_pointer rhs) noexcept {
    return static_cast<basic_string_view<CharT, Traits>>(lhs) == rhs;
  }
  friend constexpr bool operator==(const_pointer lhs, const basic_nomm_string &rhs) noexcept {
    return lhs == static_cast<basic_string_view<CharT, Traits>>(rhs);
  }
  friend constexpr bool operator!=(const basic_nomm_string &lhs, const_pointer rhs) noexcept {
    return !(lhs == rhs);
  }
  friend constexpr bool operator!=(const_pointer lhs, const basic_nomm_string &rhs) noexcept {
    return !(lhs == rhs);
  }
  friend constexpr bool operator<(const basic_nomm_string &lhs, const_pointer rhs) noexcept {
    return static_cast<basic_string_view<CharT, Traits>>(lhs) < rhs;
  }
  friend constexpr bool operator<(const_pointer lhs, const basic_nomm_string &rhs) noexcept {
    return lhs < static_cast<basic_string_view<CharT, Traits>>(rhs);
  }
  friend constexpr bool operator>(const basic_nomm_string &lhs, const_pointer rhs) noexcept {
    return rhs < lhs;
  }
  friend constexpr bool operator>(const_pointer lhs, const basic_nomm_string &rhs) noexcept {
    return rhs < lhs;
  }
  friend constexpr bool operator<=(const basic_nomm_string &lhs, const_pointer rhs) noexcept {
    return !(rhs < lhs);
  }
  friend constexpr bool operator<=(const_pointer lhs, const basic_nomm_string &rhs) noexcept {
    return !(rhs < lhs);
  }
  friend constexpr bool operator>=(const basic_nomm_string &lhs, const_pointer rhs) noexcept {
    return !(lhs < rhs);
  }
  friend constexpr bool operator>=(const_pointer lhs, const basic_nomm_string &rhs) noexcept {
    return !(lhs < rhs);
  }

  private:
  constexpr void null_terminate() noexcept {
    if (this->_data && this->_len < this->_capacity) {
      this->_data[this->_len] = CharT();
    }
  }

  pointer _data;
  size_type _len;
  size_type _capacity;
};

using nomm_string = basic_nomm_string<char, char_traits<char>>;
} // namespace kstd
