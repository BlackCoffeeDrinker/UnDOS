
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
  // This entry's parent's key hash (0 for a top-level entry or the hive
  // root itself). keyHash alone is derived from the short name only (see
  // StaticIdentifier) and can therefore collide across different parents
  // (e.g. two drivers each having a "Start" child) - parentHash is what
  // actually disambiguates which entry this handle refers to.
  uint64_t parentHash{0};
  kstd::static_string<32> name;// This entry's own short name ("" for the hive root)
  ConfigurationValueType valueType{ConfigurationValueType::Invalid};// Invalid means this is a folder-like Key, not a value
  KConfigurationKeyAccess access;

  // Scratch storage backing the kstd::string_view returned by
  // KE_CM_EnumKey()/KE_CM_EnumValue(): the view needs somewhere stable to
  // point at, and this handle's own lifetime is a convenient place to keep
  // it, sized to match KConfigurationRoot::Root::name's capacity.
  kstd::static_string<32> enumNameScratch;

  // Own lifetime reference count for this handle object itself (distinct
  // from configRoot->configptr_references, which tracks how many open keys
  // reference the shared root). Managed by KConfigurationKeyPtrTraits; starts
  // at 0 since ownership is only granted once a KConfigurationKeyPtr adopts
  // the freshly-created object.
  kstd::atomic<uint32_t> handle_references{0};
};
}// namespace kernel
