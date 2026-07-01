
#pragma once

#include <__config.hpp>
#include <__functional/invoke.hpp>
#include <__utility/forward.hpp>

namespace kstd {

// [concept.invocable]

template<class _Fn, class... _Args>
concept invocable = requires(_Fn &&__fn, _Args &&...__args) {
  invoke(forward<_Fn>(__fn), forward<_Args>(__args)...);// not required to be equality preserving
};

// [concept.regular.invocable]

template<class _Fn, class... _Args>
concept regular_invocable = invocable<_Fn, _Args...>;

}// namespace kstd
