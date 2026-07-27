
#pragma once

#include <kernel/__core.hpp>
#include <kernel/configuration_manager/configuration_value_type.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/mutex.hpp>

#include <list.hpp>
#include <static_string.hpp>

namespace kernel {
struct KConfigurationRoot : KObjectT<KConfigurationRoot, 1, TYPE_CONFIGROOT_SECTION> {
  // A single INDEX TABLE entry, kept resident in memory while the hive is
  // open. `keyHash` uniquely identifies this entry (it is derived from both
  // the entry's own short `name` and its `parentHash`, so it doubles as a
  // path hash without needing to store the full path). `parentHash` is the
  // `keyHash` of the entry's immediate parent (0 for a top-level entry,
  // directly under the hive root) and is what lets EnumKey/EnumValue find
  // an entry's children.
  struct Root {
    uint64_t keyHash{0};
    uint64_t parentHash{0};
    ConfigurationValueType valueType{ConfigurationValueType::Invalid};
    uint32_t offset{0};
    uint16_t length{0};
    kstd::static_string<32> name;

    // Bookkeeping needed to navigate the chained-groups on-disk layout: not
    // part of the 24-byte on-disk index entry itself.
    // `indexEntryOffset` is the absolute file offset of this entry's own
    // 24-byte slot (so it can be persisted back in place), and `groupOffset`
    // is the absolute file offset of the start of the INDEX TABLE group this
    // entry belongs to (so the matching DATA REGION - right after that
    // group's fixed-size index table and next-group pointer - can be found).
    uint32_t indexEntryOffset{0};
    uint32_t groupOffset{0};
  };

  kstd::static_string<255> filePath;//< The path to the configroot file
  KObjectPtr<KFileObject> file;     //< If the configroot is currently open, this is its backing file (only valid if references != 0)
  uint32_t configptr_references = 0;//< Number of KConfigurationKey opened, once it's 0, the configroot is closed

  Mutex mtx;             //< Lock for reading and writing to the configroot
  kstd::list<Root> roots;//< List of folders in this configroot, if references != 0, empty otherwise
  size_t numRoots;
};

template<>
struct ObjectTypeOf<TYPE_CONFIGROOT_SECTION> {
  using type = KConfigurationRoot;
};

}// namespace kernel
