
#pragma once

#include <__config.hpp>

namespace kstd {

// Let COPYCV(FROM, TO) be an alias for type TO with the addition of FROM's
// top-level cv-qualifiers.
template<class _From>
struct __copy_cv {
  template<class _To>
  using __apply = _To;
};

template<class _From>
struct __copy_cv<const _From> {
  template<class _To>
  using __apply = const _To;
};

template<class _From>
struct __copy_cv<volatile _From> {
  template<class _To>
  using __apply = volatile _To;
};

template<class _From>
struct __copy_cv<const volatile _From> {
  template<class _To>
  using __apply = const volatile _To;
};

template<class _From, class _To>
using __copy_cv_t = typename __copy_cv<_From>::template __apply<_To>;

}// namespace kstd
