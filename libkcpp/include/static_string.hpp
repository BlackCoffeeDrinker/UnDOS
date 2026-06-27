
#pragma once

#include <__config.hpp>
#include <char_traits.hpp>
#include <string_interface.hpp>
#include <string_view.hpp>

namespace kstd {

/**
 * Static compile time string
 * 
 * @tparam N Number of characters to allocate
 */
template<size_t N, typename CharT = char, typename Traits = char_traits<CharT>>
struct basic_static_string : string_interface<CharT, Traits> {
  using traits_type = Traits;
  using value_type = CharT;
  using pointer = CharT *;
  using const_pointer = const CharT *;
  using reference = CharT &;
  using const_reference = const CharT &;
  using size_type = size_t;
  using iterator = pointer;
  using const_iterator = const_pointer;

  constexpr basic_static_string() noexcept : string_interface<CharT, Traits>(_buffer, 0), _buffer{} {
    _buffer[0] = CharT();
  }

  constexpr basic_static_string(const CharT *str) noexcept : string_interface<CharT, Traits>(_buffer, 0), _buffer{} {
    append(str);
  }

  constexpr basic_static_string(const basic_string_view<CharT, Traits> &sv) noexcept
      : string_interface<CharT, Traits>(_buffer, 0), _buffer{} {
    append(sv);
  }

  constexpr basic_static_string(const basic_static_string &other) noexcept
      : string_interface<CharT, Traits>(_buffer, other._len), _buffer{} {
    for (size_type i = 0; i <= other._len; ++i) {
      _buffer[i] = other._buffer[i];
    }
  }

  constexpr basic_static_string &operator=(const basic_static_string &other) noexcept {
    if (this != &other) {
      this->_len = other._len;
      for (size_type i = 0; i <= other._len; ++i) {
        _buffer[i] = other._buffer[i];
      }
    }
    return *this;
  }

  constexpr basic_static_string &operator=(const basic_string_view<CharT, Traits> &sv) noexcept {
    this->_len = 0;
    append(sv);
    return *this;
  }

  [[nodiscard]] constexpr size_type capacity() const noexcept { return N; }

  constexpr operator const CharT *() const noexcept { return _buffer; }
  [[nodiscard]] constexpr pointer data() noexcept { return _buffer; }
  [[nodiscard]] constexpr const_pointer data() const noexcept { return _buffer; }

  [[nodiscard]] constexpr iterator begin() noexcept { return _buffer; }
  [[nodiscard]] constexpr iterator end() noexcept { return _buffer + this->_len; }
  [[nodiscard]] constexpr const_iterator begin() const noexcept { return _buffer; }
  [[nodiscard]] constexpr const_iterator end() const noexcept { return _buffer + this->_len; }
  [[nodiscard]] constexpr const_iterator cbegin() const noexcept { return _buffer; }
  [[nodiscard]] constexpr const_iterator cend() const noexcept { return _buffer + this->_len; }

  constexpr reference operator[](size_type pos) noexcept { return _buffer[pos]; }
  constexpr const_reference operator[](size_type pos) const noexcept { return _buffer[pos]; }

  [[nodiscard]] constexpr size_type find(CharT c, size_type pos = 0) const noexcept {
    size_type ret = this->npos;
    if (pos < this->_len) {
      const size_type n = this->_len - pos;
      const CharT *p = traits_type::find(_buffer + pos, n, c);
      if (p)
        ret = static_cast<size_type>(p - _buffer);
    }
    return ret;
  }

  constexpr void append(CharT c) noexcept {
    if (this->_len >= N)
      return;
    _buffer[this->_len++] = c;
    _buffer[this->_len] = CharT();
  }

  constexpr void append(const CharT *str) noexcept {
    append(basic_string_view<CharT, Traits>(str));
  }

  constexpr void append(const basic_string_view<CharT, Traits> &sv) noexcept {
    size_type to_copy = sv.length();
    if (this->_len + to_copy > N) {
      to_copy = N - this->_len;
    }
    for (size_type i = 0; i < to_copy; ++i) {
      _buffer[this->_len + i] = sv[i];
    }
    this->_len += to_copy;
    _buffer[this->_len] = CharT();
  }

  constexpr basic_static_string &operator=(const_pointer sv) noexcept {
    this->_len = 0;
    append(sv);
    return *this;
  }

  constexpr operator basic_string_view<CharT, Traits>() const noexcept {
    return basic_string_view<CharT, Traits>(this->data(), this->length());
  }

  private:
  CharT _buffer[N + 1];
};

template<size_t N>
using static_string = basic_static_string<N, char, char_traits<char>>;

}// namespace kstd
