#pragma once

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

template<typename T, typename... Args>
T *KE_CreateObject(Args &&...args) {
  void *p = KE_Malloc(sizeof(T));
  if (!p) return nullptr;
  return new (p) T(kstd::forward<Args>(args)...);
}

namespace kernel {
using ::KE_CreateObject;

template<typename T, typename... Args>
  requires(kstd::is_base_of_v<KObject, T>)
KObjectPtr<T> CreateKObject(const kstd::static_string<64> &name, Args &&...args) {
  auto *ptr = KE_CreateObject<T>(kstd::forward<Args>(args)...);
  if (!ptr) return nullptr;
  ptr->name = name;
  KObjectPtr<T> res(ptr);
  KE_OB_Release(ptr);
  return res;
}
}// namespace kernel
