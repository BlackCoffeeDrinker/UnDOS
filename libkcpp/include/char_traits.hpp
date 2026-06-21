
#pragma once
#include <__config.hpp>

#include <stddef.hpp>

namespace kstd {
#define EOF (-1)


// 21.1
/**
 *  @brief  Basis for explicit traits specializations.
 *
*/
template<typename CharT>
struct char_traits {};

/// 21.1.3.1  char_traits specializations
template<>
struct char_traits<char> {
  typedef char char_type;
  typedef int int_type;

#if __cpp_lib_three_way_comparison
  using comparison_category = strong_ordering;
#endif

  static constexpr void assign(char_type &__c1, const char_type &__c2) noexcept {
    __c1 = __c2;
  }

  static constexpr bool
  eq(const char_type &__c1, const char_type &__c2) noexcept { return __c1 == __c2; }

  static constexpr bool
  lt(const char_type &__c1, const char_type &__c2) noexcept {
    // LWG 467.
    return (static_cast<unsigned char>(__c1) < static_cast<unsigned char>(__c2));
  }

  static constexpr int
  compare(const char_type *__s1, const char_type *__s2, size_t __n) {
    if (__n == 0)
      return 0;
#if __cplusplus >= 201703L
    if (is_constant_evaluated()) {
      for (size_t __i = 0; __i < __n; ++__i) {
        if (lt(__s1[__i], __s2[__i]))
          return -1;
        if (lt(__s2[__i], __s1[__i]))
          return 1;
      }
      return 0;
    }
#endif
    return __builtin_memcmp(__s1, __s2, __n);
  }

  static constexpr size_t
  length(const char_type *s) {
#if __cplusplus >= 201703L
    if (is_constant_evaluated()) {
      size_t i = 0;
      while (!eq(s[i], char_type()))
        ++i;
      return i;
    }
#endif

    auto count = s;
    for (; *count; ++count);
    return static_cast<size_t>(count - s);
  }

  static constexpr const char_type *
  find(const char_type *__s, size_t __n, const char_type &__a) {
    if (__n == 0)
      return nullptr;
#if __cplusplus >= 201703L
    if (is_constant_evaluated()) {
      for (size_t __i = 0; __i < __n; ++__i)
        if (eq(__s[__i], __a))
          return __s + __i;
      return nullptr;
    }
#endif

    const char *big = __s;
    size_t n = 0;

    for (n = 0; n < __n; n++)
      if (big[n] == __a)
        return &big[n];

    return nullptr;
  }

  static constexpr char_type *
  move(char_type *__s1, const char_type *__s2, size_t __n) {
    if (__n == 0)
      return __s1;
#if __cplusplus >= 202002L
    if (is_constant_evaluated()) {
      // Use __builtin_constant_p to avoid comparing unrelated pointers.
      if constexpr (__builtin_constant_p(__s2 < __s1) && __s1 > __s2 && __s1 < (__s2 + __n)) {
        do {
          --__n;
          assign(__s1[__n], __s2[__n]);
        } while (__n > 0);
      } else
        copy(__s1, __s2, __n);
      return __s1;
    }
#endif
    return static_cast<char_type *>(__builtin_memmove(__s1, __s2, __n));
  }

  static constexpr char_type *
  copy(char_type *__s1, const char_type *__s2, size_t __n) {
    if (__n == 0)
      return __s1;
#if __cplusplus >= 202002L
    if (is_constant_evaluated()) {
    }
#endif
    return static_cast<char_type *>(__builtin_memcpy(__s1, __s2, __n));
  }

  static constexpr char_type *
  assign(char_type *__s, size_t __n, char_type __a) {
    if (__n == 0)
      return __s;
#if __cplusplus >= 202002L
    if (is_constant_evaluated()) {
      for (size_t __i = 0; __i < __n; ++__i)
        __s[__i] = __a;
      return __s;
    }

    if constexpr (sizeof(char_type) == 1 && __is_trivial(char_type)) {
      if (__n) {
        unsigned char __c;
        __builtin_memcpy(&__c, __builtin_addressof(__a), 1);
        __builtin_memset(__s, __c, __n);
      }
    } else {
      for (size_t __i = 0; __i < __n; ++__i)
        __s[__i] = __a;
    }
    return __s;
#endif
    return static_cast<char_type *>(__builtin_memset(__s, __a, __n));
  }

  static constexpr char_type
  to_char_type(const int_type &__c) noexcept { return static_cast<char_type>(__c); }

  // To keep both the byte 0xff and the eof symbol 0xffffffff
  // from ending up as 0xffffffff.
  static constexpr int_type
  to_int_type(const char_type &__c) noexcept { return static_cast<int_type>(static_cast<unsigned char>(__c)); }

  static constexpr bool
  eq_int_type(const int_type &__c1, const int_type &__c2) noexcept { return __c1 == __c2; }
};

}// namespace kstd
