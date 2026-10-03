#pragma once

#include <memory.hpp>
#include <new.hpp>
#include <type_traits.hpp>
#include <utility.hpp>


#if __STDC_HOSTED__
// On a hosted build (e.g. the host test binary), placement new comes from the
// standard library rather than libkcpp's freestanding new.hpp.
#include <new>
#endif

constexpr uint32_t __SYS_V1_MAGIC = 0xFEEDBEEF;
constexpr uint32_t __SYS_V2_MAGIC = 0xFEEDDEAD;

#include <Kernel.hpp>

namespace kernel::detail {
// kstd::unique_ptr's default_delete calls the global `delete` operator, which
// is only routed to KE_Free on freestanding builds (see the
// __STDC_HOSTED__ == 0 guard in vmm.cpp): a hosted build (e.g. the host test
// binary) keeps its own global operator new/delete backed by the host
// allocator, so relying on it here would free KE_Malloc'd memory with the
// wrong allocator. Always going through KE_Free explicitly keeps
// KE_CreateObject correct on every target.
template<typename T>
struct KMallocDeleter {
  void operator()(T *ptr) const noexcept {
    if (ptr) {
      ptr->~T();
      KE_Free(ptr);
    }
  }
};
}// namespace kernel::detail

template<typename T, typename... Args>
kstd::unique_ptr<T, kernel::detail::KMallocDeleter<T>> KE_CreateObject(Args &&...args) {
  if (void *p = KE_Malloc(sizeof(T))) {
    return kstd::unique_ptr<T, kernel::detail::KMallocDeleter<T>>(new (p) T(kstd::forward<Args>(args)...));
  }
  return nullptr;
}

template<typename T, typename... Args>
auto KE_CreateKPtrObject(Args &&...args) -> typename kernel::detail::KPtrTypeOf<T>::type {
  if (void *p = KE_Malloc(sizeof(T))) {
    T *obj = new (p) T(kstd::forward<Args>(args)...);
    return typename kernel::detail::KPtrTypeOf<T>::type(obj);
  }
  return nullptr;
}

UNDOS_KERNEL_PUBLIC_V1API(
    uint32_t,
    KE_SYSCALL_Dispatch,
    uint32_t number, uint32_t size, void *data, uint32_t flags);

namespace kernel {
using ::KE_CreateObject;

template<typename T, typename... Args>
  requires(kstd::is_base_of_v<KObject, T>)
KObjectPtr<T> CreateKObject(const kstd::static_string<64> &name, Args &&...args) {
  if (void *p = KE_Malloc(sizeof(T))) {
    KObjectPtr<T> res(new (p) T(name, kstd::forward<Args>(args)...));
    return res;
  }

  return nullptr;
}

template<typename T, typename... Args>
  requires(kstd::is_base_of_v<KObject, T>)
KObjectPtr<T> CreateAnonymousKObject(Args &&...args) {
  if (void *p = KE_Malloc(sizeof(T))) {
    KObjectPtr<T> res(new (p) T(kstd::forward<Args>(args)...));
    return res;
  }

  return nullptr;
}
}// namespace kernel
