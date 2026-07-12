
#pragma once
#include <__config.hpp>

#include <__algo/min.hpp>
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

  constexpr basic_string_view() noexcept : _data{nullptr}, _len{0} {}
  constexpr basic_string_view(const basic_string_view &) noexcept = default;
  constexpr basic_string_view(const CharT *str) noexcept
      : _data{const_cast<pointer>(str)}, _len{str ? traits_type::length(str) : 0} {}
  constexpr basic_string_view(const CharT *str, size_type len) noexcept
      : _data{const_cast<pointer>(str)}, _len{len} {}
  constexpr basic_string_view(pointer str, size_type len) noexcept : _data{str}, _len{len} {}

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

  friend constexpr bool operator==(const basic_string_view &lhs, const_pointer rhs) noexcept {
    if (!rhs) return lhs.empty();
    size_type rhs_len = traits_type::length(rhs);
    if (lhs.length() != rhs_len) return false;
    return traits_type::compare(lhs.data(), rhs, lhs.length()) == 0;
  }

  friend constexpr bool operator<(const basic_string_view &lhs, const_pointer rhs) noexcept {
    if (!rhs) return false;
    size_type rhs_len = traits_type::length(rhs);
    size_type n = lhs.length() < rhs_len ? lhs.length() : rhs_len;
    if (const int res = traits_type::compare(lhs.data(), rhs, n);
        res != 0) {
      return res < 0;
    }
    return lhs.length() < rhs_len;
  }

  friend constexpr bool operator<(const_pointer lhs, const basic_string_view &rhs) noexcept {
    if (!lhs) return !rhs.empty();
    size_type lhs_len = traits_type::length(lhs);
    size_type n = lhs_len < rhs.length() ? lhs_len : rhs.length();
    if (const int res = traits_type::compare(lhs, rhs.data(), n);
        res != 0) {
      return res < 0;
    }
    return lhs_len < rhs.length();
  }


  friend constexpr bool operator==(const_pointer lhs, const basic_string_view &rhs) noexcept { return rhs == lhs; }
  friend constexpr bool operator!=(const basic_string_view &lhs, const_pointer rhs) noexcept { return !(lhs == rhs); }
  friend constexpr bool operator!=(const_pointer lhs, const basic_string_view &rhs) noexcept { return !(lhs == rhs); }
  friend constexpr bool operator>(const basic_string_view &lhs, const_pointer rhs) noexcept { return rhs < lhs; }
  friend constexpr bool operator>(const_pointer lhs, const basic_string_view &rhs) noexcept { return rhs < lhs; }
  friend constexpr bool operator>(const basic_string_view &lhs, const basic_string_view &rhs) noexcept { return rhs < lhs; }
  friend constexpr bool operator<=(const basic_string_view &lhs, const_pointer rhs) noexcept { return !(rhs < lhs); }
  friend constexpr bool operator<=(const_pointer lhs, const basic_string_view &rhs) noexcept { return !(rhs < lhs); }
  friend constexpr bool operator<=(const basic_string_view &lhs, const basic_string_view &rhs) noexcept { return !(rhs < lhs); }
  friend constexpr bool operator>=(const basic_string_view &lhs, const_pointer rhs) noexcept { return !(lhs < rhs); }
  friend constexpr bool operator>=(const_pointer lhs, const basic_string_view &rhs) noexcept { return !(lhs < rhs); }
  friend constexpr bool operator>=(const basic_string_view &lhs, const basic_string_view &rhs) noexcept { return !(lhs < rhs); }

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

    for (size_type i = pos; ; --i) {
      if (traits_type::compare(_data + i, v._data, v._len) == 0) return i;
      if (i == 0) break;
    }
    return npos;
  }

  [[nodiscard]] constexpr size_type rfind(CharT c, size_type pos = npos) const noexcept {
    size_type size = _len;
    if (size != 0) {
      for (size_type i = min(pos, size - 1); ; --i) {
        if (traits_type::eq(_data[i], c)) return i;
        if (i == 0) break;
      }
    }
    return npos;
  }

  [[nodiscard]] constexpr size_type rfind(const CharT* s, size_type pos, size_type count) const noexcept {
    return rfind(basic_string_view(s, count), pos);
  }

  [[nodiscard]] constexpr size_type rfind(const CharT* s, size_type pos = npos) const noexcept {
    return rfind(basic_string_view(s), pos);
  }

  [[nodiscard]] constexpr basic_string_view substr(size_type pos = 0, size_type n = npos) const noexcept {
    if (pos > this->_len) pos = this->_len;
    const size_type rlen = min<size_t>(n, this->_len - pos);
    return basic_string_view{this->_data + pos, rlen};
  }

  private:
  pointer _data = nullptr;
  size_type _len = 0;
};

using string_view = basic_string_view<char, char_traits<char>>;

}// namespace kstd
