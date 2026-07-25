
#pragma once

#include <kernel/__core.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/mutex.hpp>

#include <list.hpp>

namespace kernel {
enum class ConfigurationValueType;

struct KConfigurationRoot : KObjectT<KConfigurationRoot, 1, TYPE_CONFIGROOT_SECTION> {
  struct Root {
    uint64_t keyHash{0};
    ConfigurationValueType valueType;
    uint32_t offset;
    uint16_t length;
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
