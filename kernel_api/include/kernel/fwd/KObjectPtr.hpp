#pragma once

#include <kernel/__core.hpp>
#include <kernel/fwd/KPtr.hpp>

#include <atomic.hpp>
#include <type_traits.hpp>

namespace kernel {
struct KObject;
}

UNDOS_KERNEL_PUBLIC_V1API(void, KE_OB_Retain, kernel::KObject *obj);
UNDOS_KERNEL_PUBLIC_V1API(void, KE_OB_Release, kernel::KObject *obj);

namespace kernel {
namespace detail {
struct KObjectPtrTraits {
  using base_type = KObject;
  template<typename T>
  constexpr static void inc_ref(T *ref) { KE_OB_Retain(ref); }
  template<typename T>
  constexpr static void dec_ref(T *ref) { KE_OB_Release(ref); }
  template<typename T>
  constexpr static T *release(T *ptr) { return ptr; }
  template<typename T, typename U>
  constexpr static bool convertible_to(T *ptr) { return (U::Type == ptr->type); }
};
}// namespace detail

template<typename T>
using KObjectPtr = detail::KPtr<T, detail::KObjectPtrTraits>;

}// namespace kernel
