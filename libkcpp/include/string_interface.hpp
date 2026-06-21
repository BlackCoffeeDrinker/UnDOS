
#pragma once
#include <__config.hpp>

#include "stddef.hpp"
#include "string_view.hpp"

namespace kstd {
template<typename CharT, class Traits>
struct string_interface {
  using traits_type = Traits;
  using value_type = CharT;
  using pointer = CharT *;
  using const_pointer = const CharT *;
  using reference = CharT &;
  using const_reference = const CharT &;
  using size_type = size_t;

  static constexpr size_type npos = static_cast<size_type>(-1);

  constexpr operator string_view() const noexcept { return string_view{_data, _len}; }
  constexpr operator const char *() const noexcept { return _data; }

  [[nodiscard]] constexpr const char *data() const noexcept { return reinterpret_cast<const char *>(_data); }
  [[nodiscard]] constexpr size_type length() const noexcept { return _len; }

  constexpr char &operator[](size_type pos) noexcept {
    if (pos >= _len) return const_cast<char &>(reinterpret_cast<const char &>(_data[0]));
    return const_cast<char &>(reinterpret_cast<const char &>(_data[pos]));
  }
  constexpr const char &operator[](size_type pos) const noexcept {
    if (pos >= _len) return reinterpret_cast<const char &>(_data[0]);
    return reinterpret_cast<const char &>(_data[pos]);
  }

  [[nodiscard]] constexpr size_type find(CharT c, size_type pos = 0) const noexcept {
    size_type ret = npos;
    if (pos < this->_len) {
      const size_type n = this->_len - pos;
      const CharT *p = traits_type::find(_data + pos, n, c);
      if (p)
        ret = p - _data;
    }
    return ret;
  }

  protected:
  constexpr string_interface() noexcept : _data{nullptr} {}
  constexpr string_interface(const string_interface &) noexcept = default;
  constexpr string_interface(const CharT *str, size_type len) noexcept : _data{const_cast<CharT *>(str)}, _len{len} {}

  pointer _data = nullptr;// Data buffer for the string
  size_type _len = 0;     // Where the last valid character is
};
}// namespace kstd
