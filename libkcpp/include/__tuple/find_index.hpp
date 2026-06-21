
#pragma once

#include <__config.hpp>
#include <__type_traits/is_same.hpp>

namespace kstd {

namespace __find_detail {

static constexpr size_t __not_found = static_cast<size_t>(-1);
static constexpr size_t __ambiguous = __not_found - 1;

inline constexpr size_t __find_idx_return(size_t __curr_i, size_t __res, bool __matches) {
  return !__matches ? __res : (__res == __not_found ? __curr_i : __ambiguous);
}

template<size_t _Nx>
inline constexpr size_t __find_idx(size_t __i, const bool (&__matches)[_Nx]) {
  return __i == _Nx
             ? __not_found
             : __find_detail::__find_idx_return(__i, __find_detail::__find_idx(__i + 1, __matches), __matches[__i]);
}

template<class _T1, class... _Args>
struct __find_exactly_one_checked {
  static constexpr bool __matches[sizeof...(_Args)] = {is_same<_T1, _Args>::value...};
  static constexpr size_t value = __find_detail::__find_idx(0, __matches);
  static_assert(value != __not_found, "type not found in type list");
  static_assert(value != __ambiguous, "type occurs more than once in type list");
};

template<class _T1>
struct __find_exactly_one_checked<_T1> {
  static_assert(!is_same<_T1, _T1>::value, "type not in empty type list");
};

}// namespace __find_detail

template<typename _T1, typename... _Args>
struct __find_exactly_one_t : public __find_detail::__find_exactly_one_checked<_T1, _Args...> {};

}// namespace kstd
