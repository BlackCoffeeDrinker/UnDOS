#pragma once

#include <new.hpp>
#include <type_traits.hpp>
#include <utility.hpp>

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
KObjectPtr<T> CreateKObject(Args &&...args) {
  auto *ptr = KE_CreateObject<T>(kstd::forward<Args>(args)...);
  if (!ptr) return nullptr;
  KObjectPtr<T> res(ptr);
  ptr->release();
  return res;
}

}// namespace kernel
