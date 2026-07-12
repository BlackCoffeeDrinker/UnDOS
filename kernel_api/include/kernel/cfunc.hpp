
#pragma once
#include <kernel/__core.hpp>

namespace kernel {
/**
 * @brief Wrapper for a C-style function pointer.
 *
 * `cfunc<R, Args...>` stores a raw function pointer of type `R(*)(Args...)`
 * and exposes a callable interface. This allows the function pointer to be
 * passed around as a lightweight object while retaining ABI-stable C linkage.
 *
 * @tparam R    Return type of the function.
 * @tparam Args Parameter types of the function.
 */
template<class R, class... Args>
struct cfunc {
  using pointer = R (*)(Args...);
  pointer fn = nullptr;

  constexpr cfunc() = default;
  constexpr explicit cfunc(pointer p) : fn(p) {}
  constexpr R operator()(Args... args) const { return fn(args...); }
  explicit constexpr operator bool() const { return fn != nullptr; }
  constexpr bool operator==(pointer p) const { return fn == p; }
  [[nodiscard]] constexpr bool valid() const noexcept { return fn != nullptr; }

  constexpr cfunc &operator=(pointer p) {
    fn = p;
    return *this;
  }
};

template<class R, class... Args>
struct cfunc<R(Args...)> : cfunc<R, Args...> {
  using cfunc<R, Args...>::cfunc;
  using cfunc<R, Args...>::operator=;
};

}// namespace kernel
