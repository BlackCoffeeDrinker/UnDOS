
#pragma once
#include <__config.hpp>

#include "stddef.hpp"

namespace kstd {

template<typename CharT, class Traits>
struct string_interface {
  using traits_type = Traits;
  using value_type = CharT;
  using pointer = CharT *;
  using const_pointer = const CharT *;
  using reference = CharT &;
  using const_reference = const CharT &;
  using const_iterator = const CharT *;
  using iterator = pointer;
  using size_type = size_t;

  static constexpr size_type npos = static_cast<size_type>(-1);

  explicit constexpr operator const CharT *() const noexcept { return _data; }

  [[nodiscard]] constexpr pointer data() noexcept { return _data; }
  [[nodiscard]] constexpr const_pointer data() const noexcept { return _data; }
  [[nodiscard]] constexpr size_type length() const noexcept { return _len; }
  [[nodiscard]] constexpr size_type size() const noexcept { return _len; }
  [[nodiscard]] constexpr bool empty() const noexcept { return _len == 0; }

  [[nodiscard]] constexpr iterator begin() noexcept { return _data; }
  [[nodiscard]] constexpr iterator end() noexcept { return _data + _len; }
  [[nodiscard]] constexpr const_iterator begin() const noexcept { return _data; }
  [[nodiscard]] constexpr const_iterator end() const noexcept { return _data + _len; }
  [[nodiscard]] constexpr const_iterator cbegin() const noexcept { return _data; }
  [[nodiscard]] constexpr const_iterator cend() const noexcept { return _data + _len; }

  constexpr reference operator[](size_type pos) noexcept {
    return _data[pos];
  }
  constexpr const_reference operator[](size_type pos) const noexcept {
    return _data[pos];
  }
  
  friend constexpr bool operator==(const string_interface &lhs, const string_interface &rhs) noexcept {
    if (lhs.length() != rhs.length()) return false;
    if (lhs.data() == rhs.data()) return true;
    return traits_type::compare(lhs.data(), rhs.data(), lhs.length()) == 0;
  }

  friend constexpr bool operator!=(const string_interface &lhs, const string_interface &rhs) noexcept {
    return !(lhs == rhs);
  }

  friend constexpr bool operator<(const string_interface &lhs, const string_interface &rhs) noexcept {
    size_type n = lhs.length() < rhs.length() ? lhs.length() : rhs.length();
    int res = traits_type::compare(lhs.data(), rhs.data(), n);
    if (res != 0) return res < 0;
    return lhs.length() < rhs.length();
  }

  friend constexpr bool operator>(const string_interface &lhs, const string_interface &rhs) noexcept {
    return rhs < lhs;
  }

  friend constexpr bool operator<=(const string_interface &lhs, const string_interface &rhs) noexcept {
    return !(rhs < lhs);
  }

  friend constexpr bool operator>=(const string_interface &lhs, const string_interface &rhs) noexcept {
    return !(lhs < rhs);
  }

  friend constexpr bool operator==(const string_interface &lhs, const_pointer rhs) noexcept {
    if (!rhs) return lhs.empty();
    size_type rhs_len = traits_type::length(rhs);
    if (lhs.length() != rhs_len) return false;
    return traits_type::compare(lhs.data(), rhs, lhs.length()) == 0;
  }
  friend constexpr bool operator==(const_pointer lhs, const string_interface &rhs) noexcept {
    return rhs == lhs;
  }
  friend constexpr bool operator!=(const string_interface &lhs, const_pointer rhs) noexcept {
    return !(lhs == rhs);
  }
  friend constexpr bool operator!=(const_pointer lhs, const string_interface &rhs) noexcept {
    return !(lhs == rhs);
  }

  friend constexpr bool operator<(const string_interface &lhs, const_pointer rhs) noexcept {
    if (!rhs) return false;
    size_type rhs_len = traits_type::length(rhs);
    size_type n = lhs.length() < rhs_len ? lhs.length() : rhs_len;
    int res = traits_type::compare(lhs.data(), rhs, n);
    if (res != 0) return res < 0;
    return lhs.length() < rhs_len;
  }
  
  friend constexpr bool operator<(const_pointer lhs, const string_interface &rhs) noexcept {
    if (!lhs) return !rhs.empty();
    size_type lhs_len = traits_type::length(lhs);
    size_type n = lhs_len < rhs.length() ? lhs_len : rhs.length();
    int res = traits_type::compare(lhs, rhs.data(), n);
    if (res != 0) return res < 0;
    return lhs_len < rhs.length();
  }
  
  friend constexpr bool operator>(const string_interface &lhs, const_pointer rhs) noexcept {
    return rhs < lhs;
  }
  
  friend constexpr bool operator>(const_pointer lhs, const string_interface &rhs) noexcept {
    return rhs < lhs;
  }
  friend constexpr bool operator<=(const string_interface &lhs, const_pointer rhs) noexcept {
    return !(rhs < lhs);
  }
  friend constexpr bool operator<=(const_pointer lhs, const string_interface &rhs) noexcept {
    return !(rhs < lhs);
  }
  friend constexpr bool operator>=(const string_interface &lhs, const_pointer rhs) noexcept {
    return !(lhs < rhs);
  }
  friend constexpr bool operator>=(const_pointer lhs, const string_interface &rhs) noexcept {
    return !(lhs < rhs);
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

  protected:
  constexpr string_interface() noexcept : _data{nullptr}, _len{0} {}
  constexpr string_interface(const string_interface &) noexcept = default;
  constexpr string_interface(pointer str, size_type len) noexcept : _data{str}, _len{len} {}
  constexpr string_interface(const_pointer str, size_type len) noexcept : _data{const_cast<pointer>(str)}, _len{len} {}

  pointer _data = nullptr;// Data buffer for the string
  size_type _len = 0;     // Where the last valid character is
};
}// namespace kstd
