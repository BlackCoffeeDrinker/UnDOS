
#pragma once
#include <__config.hpp>
#include <__type_traits/copy_cv.hpp>
#include <__type_traits/add_reference.hpp>

namespace kstd {

template<class _From>
struct __copy_cvref {
  template<class _To>
  using __apply = __copy_cv_t<_From, _To>;
};

template<class _From>
struct __copy_cvref<_From &> {
  template<class _To>
  using __apply = __add_lvalue_reference_t<__copy_cv_t<_From, _To>>;
};

template<class _From>
struct __copy_cvref<_From &&> {
  template<class _To>
  using __apply = __add_rvalue_reference_t<__copy_cv_t<_From, _To>>;
};

template<class _From, class _To>
using __copy_cvref_t = typename __copy_cvref<_From>::template __apply<_To>;

}// namespace kstd
