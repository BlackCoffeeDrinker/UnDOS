
#pragma once

#include <__config.hpp>
#include <__type_traits/is_same.hpp>

namespace kstd {

template<class _Tp, class _Up>
concept __same_as_impl = is_same<_Tp, _Up>::value;

template<class _Tp, class _Up>
concept same_as = __same_as_impl<_Tp, _Up> && __same_as_impl<_Up, _Tp>;

}// namespace kstd
