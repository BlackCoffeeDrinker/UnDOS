
#pragma once

#include <stddef.h>
#include <stdint.h>

#define _ALWAYS_INLINE __attribute__((__always_inline__))
#define _HIDDEN __attribute__((__visibility__("hidden")))

#  if __has_attribute(exclude_from_explicit_instantiation)
#    define _EXCLUDE_FROM_EXPLICIT_INSTANTIATION __attribute__((__exclude_from_explicit_instantiation__))
#  else
#    define _EXCLUDE_FROM_EXPLICIT_INSTANTIATION _ALWAYS_INLINE
#  endif

#define _KSTD_API _HIDDEN _EXCLUDE_FROM_EXPLICIT_INSTANTIATION  

#if __has_attribute(__no_sanitize__) && !defined(__GNUC__)
#define _NO_CFI __attribute__((__no_sanitize__("cfi")))
#else
#define _NO_CFI
#endif

#if defined(__clang__)
#define _CTAD_SUPPORTED_FOR_TYPE(_ClassName) \
  template<class... _Tag>                           \
  [[maybe_unused]] _ClassName(typename _Tag::__allow_ctad...)->_ClassName<_Tag...>
#else
#define _CTAD_SUPPORTED_FOR_TYPE(ClassName) \
  template<class... _Tag>                          \
  ClassName(typename _Tag::__allow_ctad...)->ClassName<_Tag...>
#endif
