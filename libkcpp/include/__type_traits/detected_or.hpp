
#pragma once

#include <__config.hpp>
#include <__type_traits/void_t.hpp>


namespace kstd {

template<class _Default, class _Void, template<class...> class _Op, class... _Args>
struct __detector {
  using type = _Default;
};

template<class _Default, template<class...> class _Op, class... _Args>
struct __detector<_Default, __void_t<_Op<_Args...>>, _Op, _Args...> {
  using type = _Op<_Args...>;
};

template<class _Default, template<class...> class _Op, class... _Args>
using __detected_or_t = typename __detector<_Default, void, _Op, _Args...>::type;

}// namespace kstd
