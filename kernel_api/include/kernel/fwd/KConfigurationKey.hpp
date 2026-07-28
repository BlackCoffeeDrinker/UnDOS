
#pragma once

#include <atomic.hpp>
#include <kernel/fwd/KPtr.hpp>

namespace kernel {
struct KConfigurationKey;
}

// region Ref Count
UNDOS_KERNEL_PUBLIC_V1API(bool, KE_CM_IncrementConfigRootReferenceCount, const kernel::KConfigurationKey *key);
UNDOS_KERNEL_PUBLIC_V1API(bool, KE_CM_DecrementConfigRootReferenceCount, const kernel::KConfigurationKey *key);
// endregion

// Forward declaration so KConfigurationKeyPtrTraits::dec_ref() below can call
// it: the full declaration normally comes from kernel/virtual_memory.hpp, but
// this header is now pulled in very early (via kernel/__core.hpp, for
// UNDOS_DRIVER_ENTRY), before that header is otherwise reached.
UNDOS_KERNEL_PUBLIC_V1API(void, KE_Free, void *ptr);

namespace kernel {
namespace detail {
struct KConfigurationKeyPtrTraits {
  using base_type = KConfigurationKey;
  // inc_ref/dec_ref serve two independent purposes here: they keep the
  // shared KConfigurationRoot's configptr_references count in sync (via
  // KE_CM_Increment/DecrementConfigRootReferenceCount), and they own the
  // lifetime of the KConfigurationKey handle object itself (via
  // handle_references), freeing it with KE_Free once the last
  // KConfigurationKeyPtr referencing it goes away.
  template<typename T>
  static void inc_ref(T *item) {
    if (!item) return;
    item->handle_references.fetch_add(1, kstd::memory_order_relaxed);
    KE_CM_IncrementConfigRootReferenceCount(item);
  }
  template<typename T>
  static void dec_ref(T *item) {
    if (!item) return;
    KE_CM_DecrementConfigRootReferenceCount(item);
    if (item->handle_references.fetch_sub(1, kstd::memory_order_acq_rel) == 1) {
      item->~T();
      KE_Free(item);
    }
  }
  template<typename T>
  constexpr static T *release(T *ptr) { return ptr; }
  template<typename T, typename To>
  constexpr static bool convertible_to(T *) { return true; }
  template<typename T>
  constexpr static void type(T *) {}
};
}// namespace detail

using KConfigurationKeyPtr = detail::KPtr<KConfigurationKey, detail::KConfigurationKeyPtrTraits>;

template<>
struct detail::KPtrTypeOf<KConfigurationKey> {
  using type = KConfigurationKeyPtr;
};


}// namespace kernel
