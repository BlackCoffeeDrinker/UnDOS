
#pragma once
#include <__config.hpp>

namespace kstd {
template<typename T>
struct make_unsigned {
  using type = T;
};

template<>
struct make_unsigned<char> {
  using type = unsigned char;
};

template<>
struct make_unsigned<signed char> {
  using type = unsigned char;
};

template<>
struct make_unsigned<short> {
  using type = unsigned short;
};

template<>
struct make_unsigned<int> {
  using type = unsigned int;
};

template<>
struct make_unsigned<long> {
  using type = unsigned long;
};

template<>
struct make_unsigned<long long> {
  using type = unsigned long long;
};

template<typename T>
using make_unsigned_t = typename make_unsigned<T>::type;

}// namespace kstd
