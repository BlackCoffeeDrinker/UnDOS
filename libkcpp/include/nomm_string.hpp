
#pragma once
#include <__config.hpp>

#include "array.hpp"
#include "stddef.hpp"
#include "string_interface.hpp"

namespace kstd {
template<typename CharT, class Traits>
struct basic_nomm_string : string_interface<CharT, Traits> {
  using size_type = typename string_interface<CharT, Traits>::size_type;

  template<size_t s>
  constexpr basic_nomm_string(char (&data)[s])
      : string_interface<CharT, Traits>(data, s - 1),
        _real_data(data),
        _capacity(s - 1) {}

  template<size_t s>
  constexpr basic_nomm_string(array<char, s> data)
      : string_interface<CharT, Traits>(data.data(), s - 1),
        _real_data(data.data()),
        _capacity(s - 1) {}

  constexpr basic_nomm_string(char *begin, char *end)
      : string_interface<CharT, Traits>(begin, static_cast<size_t>(end - begin - 1)),
        _real_data(begin),
        _capacity(static_cast<size_t>(end - begin - 1)) {
    if (begin >= end) {
      this->_len = 0;
      this->_capacity = 0;
    }
  }

  [[nodiscard]] constexpr size_type capacity() const noexcept { return this->_capacity; }

  ~basic_nomm_string() {
    null_terminate();
  }

  constexpr void append(char c) noexcept {
    if (this->_len >= this->_capacity) return;
    _real_data[this->_len++] = c;
    null_terminate();
  }

  constexpr void append(const string_view &sv) noexcept {
    if (sv.length() + this->_len >= this->_capacity) return;
    __builtin_memcpy(_real_data + this->_len, sv.data(), sv.length());
    this->_len += sv.length();
    null_terminate();
  }

  private:
  void null_terminate() const noexcept {
    if (this->_len >= this->_capacity) return;
    this->_data[this->_len] = '\0';
  }

  CharT *_real_data;
  size_type _capacity;
};

using nomm_string = basic_nomm_string<char, char_traits<char>>;
}// namespace kstd
