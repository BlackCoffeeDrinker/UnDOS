
#pragma once
#include <__config.hpp>

#include <__algo/min.hpp>
#include <char_traits.hpp>
#include <string_interface.hpp>

namespace kstd {

template<class CharT, class Traits = char_traits<CharT>>
struct basic_string_view : string_interface<CharT, Traits> {
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

  constexpr basic_string_view() noexcept : string_interface<CharT, Traits>() {}
  constexpr basic_string_view(const basic_string_view &) noexcept = default;
  constexpr basic_string_view(const CharT *str) noexcept
      : string_interface<CharT, Traits>(str, str ? traits_type::length(str) : 0) {}
  constexpr basic_string_view(const CharT *str, size_type len) noexcept
      : string_interface<CharT, Traits>(str, len) {}

  [[nodiscard]] constexpr basic_string_view substr(size_type pos = 0, size_type n = npos) const noexcept {
    if (pos > this->_len) pos = this->_len;
    const size_type rlen = min<size_t>(n, this->_len - pos);
    return basic_string_view{this->_data + pos, rlen};
  }
};

using string_view = basic_string_view<char, char_traits<char>>;

}// namespace kstd
