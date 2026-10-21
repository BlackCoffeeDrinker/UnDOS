
#pragma once

#include <__config.hpp>
#include <__type_traits/invoke.hpp>
#include <__type_traits/is_void.hpp>
#include <__utility/forward.hpp>

namespace kstd {

template<class _Fn, class... _Args>
constexpr invoke_result_t<_Fn, _Args...>
invoke(_Fn &&__f, _Args &&...__args) noexcept(is_nothrow_invocable_v<_Fn, _Args...>) {
  return kstd::__invoke(kstd::forward<_Fn>(__f), kstd::forward<_Args>(__args)...);
}

template<class _Result, class _Fn, class... _Args>
  requires is_invocable_r_v<_Result, _Fn, _Args...>
constexpr _Result
invoke_r(_Fn &&__f, _Args &&...__args) noexcept(is_nothrow_invocable_r_v<_Result, _Fn, _Args...>) {
  if constexpr (is_void_v<_Result>) {
    static_cast<void>(kstd::invoke(kstd::forward<_Fn>(__f), kstd::forward<_Args>(__args)...));
  } else {
    // TODO: Use reference_converts_from_temporary_v once implemented
    // using _ImplicitInvokeResult = invoke_result_t<_Fn, _Args...>;
    // static_assert(!reference_converts_from_temporary_v<_Result, _ImplicitInvokeResult>,
    static_assert(true,
                  "Returning from invoke_r would bind a temporary object to the reference return type, "
                  "which would result in a dangling reference.");
    return kstd::invoke(kstd::forward<_Fn>(__f), kstd::forward<_Args>(__args)...);
  }
}

}// namespace kstd
