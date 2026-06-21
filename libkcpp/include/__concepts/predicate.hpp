
#pragma once
#include <__config.hpp>

#include <__concepts/boolean_testable.hpp>
#include <__concepts/invocable.hpp>
#include <__type_traits/invoke.hpp>

namespace kstd {

template<class _Fn, class... _Args>
concept predicate = regular_invocable<_Fn, _Args...> && __boolean_testable<invoke_result_t<_Fn, _Args...>>;


}
