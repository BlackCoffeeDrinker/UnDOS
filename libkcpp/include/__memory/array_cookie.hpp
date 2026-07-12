
#pragma once

#include <__config.hpp>

#include <__memory/addressof.hpp>
#include <__type_traits/integral_constant.hpp>
#include <__type_traits/is_trivially_destructible.hpp>
#include <__type_traits/negation.hpp>

namespace kstd {
template<class _Tp>
struct __has_array_cookie : _Not<is_trivially_destructible<_Tp>> {};

struct __itanium_array_cookie {
  size_t __element_count;
};

template<class _Tp>
struct [[__gnu__::__aligned__(alignof(_Tp))]] __arm_array_cookie {
  size_t __element_size;
  size_t __element_count;
};

template<class _Tp>
// Avoid failures when -fsanitize-address-poison-custom-array-cookie is enabled
_KSTD_API size_t __get_array_cookie([[__maybe_unused__]] _Tp const *__ptr) {
  static_assert(
      __has_array_cookie<_Tp>::value, "Trying to access the array cookie of a type that is not guaranteed to have one");

#if defined(__i386__)
  using _ArrayCookie = __itanium_array_cookie;
#elif defined(__arm__) || (defined(__APPLE__) && defined(__aarch64__))
  using _ArrayCookie = __arm_array_cookie<_Tp>;
#else
  static_assert(false, "The array cookie layout is unknown on this ABI");
  struct _ArrayCookie {// dummy definition required to make the function parse
    size_t element_count;
  };
#endif

  char const *__array_cookie_start = reinterpret_cast<char const *>(__ptr) - sizeof(_ArrayCookie);
  _ArrayCookie __cookie;
  // This is necessary to avoid violating strict aliasing. It's valid because _ArrayCookie is an
  // implicit lifetime type.
  __builtin_memcpy(kstd::addressof(__cookie), __array_cookie_start, sizeof(_ArrayCookie));
  return __cookie.__element_count;
}
}// namespace kstd
