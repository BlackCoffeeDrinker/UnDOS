
#pragma once
#include <__config.hpp>

#include "array.hpp"
#include "char_traits.hpp"
#include "stddef.hpp"
#include "string_interface.hpp"
#include "string_view.hpp"

namespace kstd {
/**
 * A string-like interface with non-owning memory
 * 
 * @tparam CharT char type
 * @tparam Traits char traits
 */
template<typename CharT, class Traits = char_traits<CharT>>
struct basic_nomm_string : string_interface<CharT, Traits> {
  using size_type = typename string_interface<CharT, Traits>::size_type;
  using pointer = typename string_interface<CharT, Traits>::pointer;

  template<size_t s>
  constexpr basic_nomm_string(CharT (&data)[s])
      : string_interface<CharT, Traits>(data, 0),
        _capacity(s) {
    size_type len = Traits::length(data);
    this->_len = (len >= _capacity) ? (_capacity > 0 ? _capacity - 1 : 0) : len;
    null_terminate();
  }

  template<size_t s>
  constexpr basic_nomm_string(array<CharT, s> &data)
      : string_interface<CharT, Traits>(data.data(), 0),
        _capacity(s) {
    size_type len = Traits::length(data.data());
    this->_len = (len >= _capacity) ? (_capacity > 0 ? _capacity - 1 : 0) : len;
    null_terminate();
  }

  constexpr basic_nomm_string(CharT *begin, CharT *end)
      : string_interface<CharT, Traits>(begin, 0),
        _capacity((begin < end) ? static_cast<size_t>(end - begin) : 0) {
    if (this->_capacity > 0) {
      size_type len = Traits::length(begin);
      this->_len = (len >= _capacity) ? _capacity - 1 : len;
      null_terminate();
    }
  }

  [[nodiscard]] constexpr size_type capacity() const noexcept {
    return this->_capacity > 0 ? this->_capacity - 1 : 0;
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

  constexpr void append(const basic_string_view<CharT, Traits> &sv) noexcept {
    if (this->_capacity == 0 || sv.length() + this->_len > this->_capacity - 1)
      return;
    Traits::copy(this->_data + this->_len, sv.data(), sv.length());
    this->_len += sv.length();
    null_terminate();
  }

  constexpr operator basic_string_view<CharT, Traits>() const noexcept {
    return basic_string_view<CharT, Traits>(this->data(), this->length());
  }

  private:
  constexpr void null_terminate() noexcept {
    if (this->_data && this->_len < this->_capacity) {
      this->_data[this->_len] = CharT();
    }
  }

  size_type _capacity;
};

using nomm_string = basic_nomm_string<char, char_traits<char>>;
}// namespace kstd
