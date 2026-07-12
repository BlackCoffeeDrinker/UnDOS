
#pragma once
#include <__config.hpp>

#include <stddef.hpp>

#include <__type_traits/is_constant_evaluated.hpp>
#include <__compare/three_way_comparable.hpp>

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

  using comparison_category = kstd::strong_ordering;

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
      for (size_t i = 0; i < __n; ++i) {
        if (lt(__s1[i], __s2[i]))
          return -1;
        if (lt(__s2[i], __s1[i]))
          return 1;
      }
      return 0;
    }
#endif

    // Optimized comparison using uint32_t
    const char_type *p1 = __s1;
    const char_type *p2 = __s2;
    size_t remaining = __n;

    // Check if both pointers are 4-byte aligned
    if ((reinterpret_cast<uintptr_t>(p1) & 3) == 0 &&
        (reinterpret_cast<uintptr_t>(p2) & 3) == 0) {
      // Process 4 bytes at a time
      while (remaining >= 4) {
        const uint32_t w1 = *reinterpret_cast<const uint32_t *>(p1);
        const uint32_t w2 = *reinterpret_cast<const uint32_t *>(p2);
        if (w1 != w2) {
          // Found difference, fall back to byte comparison
          break;
        }
        p1 += 4;
        p2 += 4;
        remaining -= 4;
      }
    }

    // Compare remaining bytes
    for (size_t i = 0; i < remaining; ++i) {
      if (lt(p1[i], p2[i]))
        return -1;
      if (lt(p2[i], p1[i]))
        return 1;
    }

    return 0;
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
    for (; *count; ++count) {}
    return static_cast<size_t>(count - s);
  }

  static constexpr const char_type *
  find(const char_type *__s, size_t __n, const char_type &__a) {
    if (__n == 0)
      return nullptr;
#if __cplusplus >= 201703L
    if (is_constant_evaluated()) {
      for (size_t i = 0; i < __n; ++i)
        if (eq(__s[i], __a))
          return __s + i;
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
