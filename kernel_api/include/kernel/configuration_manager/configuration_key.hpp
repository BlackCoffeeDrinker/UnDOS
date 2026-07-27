
#pragma once

#include <atomic.hpp>
#include <static_string.hpp>
#include <stdint.h>

#include <kernel/configuration_manager/configuration_key_access.hpp>
#include <kernel/configuration_manager/configuration_value_type.hpp>
#include <kernel/kobject/KConfigurationRoot.hpp>

namespace kernel {
struct KConfigurationKey {
  KObjectPtr<KConfigurationRoot> configRoot;
  uint64_t keyHash{0};// This entry's own key hash; 0 means the hive root itself
  kstd::static_string<32> name;// This entry's own short name ("" for the hive root)
  ConfigurationValueType valueType{ConfigurationValueType::Invalid};// Invalid means this is a folder-like Key, not a value
  KConfigurationKeyAccess access;

  // Scratch storage backing the kstd::string_view returned by
  // KE_CM_GetKeyValueString(): the view needs somewhere stable to point at,
  // and this handle's own lifetime is a convenient place to keep it.
  kstd::static_string<128> stringValueScratch;

  // Own lifetime reference count for this handle object itself (distinct
  // from configRoot->configptr_references, which tracks how many open keys
  // reference the shared root). Managed by KConfigurationKeyPtrTraits; starts
  // at 0 since ownership is only granted once a KConfigurationKeyPtr adopts
  // the freshly-created object.
  kstd::atomic<uint32_t> handle_references{0};
};
}// namespace kernel
