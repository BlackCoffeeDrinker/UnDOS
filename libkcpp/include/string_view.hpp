
#pragma once
#include <__config.hpp>

#include "__algo/min.hpp"


#include <char_traits.hpp>

namespace kstd {

template<class CharT, class Traits>
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

  constexpr basic_string_view() noexcept : _len{0}, _data{nullptr} {}
  constexpr basic_string_view(const basic_string_view &) noexcept = default;
  constexpr basic_string_view(const CharT *str) noexcept : _len{traits_type::length(str)}, _data{str} {}
  constexpr basic_string_view(const CharT *str, size_type len) noexcept : _len{len}, _data{str} {}

  [[nodiscard]] constexpr size_type length() const noexcept { return _len; }
  [[nodiscard]] constexpr size_type size() const noexcept { return _len; }
  [[nodiscard]] constexpr bool empty() const noexcept { return _len == 0; }

  [[nodiscard]] constexpr const_iterator begin() const noexcept { return _data; }
  [[nodiscard]] constexpr const_iterator end() const noexcept { return _data + _len; }
  [[nodiscard]] constexpr const_iterator cbegin() const noexcept { return _data; }
  [[nodiscard]] constexpr const_iterator cend() const noexcept { return _data + _len; }

  [[nodiscard]] constexpr const_reference operator[](size_type pos) const noexcept { return *(this->_data + pos); }
  [[nodiscard]] constexpr const_pointer data() const noexcept { return _data; }
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

  [[nodiscard]]
  constexpr basic_string_view substr(size_type pos = 0, size_type n = npos) const noexcept(false) {
    const size_type rlen = min<size_t>(n, _len - pos);
    return basic_string_view{_data + pos, rlen};
  }


  private:
  size_t _len;
  const CharT *_data;
};

using string_view = basic_string_view<char, char_traits<char>>;

}// namespace kstd
