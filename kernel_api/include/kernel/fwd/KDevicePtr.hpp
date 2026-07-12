
#pragma once

#include <kernel/fwd/KPtr.hpp>
#include <type_traits.hpp>

namespace kernel {
struct KDevice;

namespace detail {
struct KDevicePtrTraits {
  using base_type = KDevice;
  template<typename T>
  constexpr static void inc_ref(T *) {}
  template<typename T>
  constexpr static void dec_ref(T *) {}
  template<typename T>
  constexpr static T *release(T *ptr) { return ptr; }
  template<typename T, typename To>
  constexpr static bool convertible_to(T *) { return true; }
  template<typename T>
  constexpr static void type(T *) {}
};
}// namespace detail

template<typename T>
using KDevicePtr = detail::KPtr<T, detail::KDevicePtrTraits>;

}// namespace kernel
