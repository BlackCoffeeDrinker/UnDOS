
#pragma once
#include <memory.hpp>

#include <kernel/__core.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/kobject/KVolumeMountObject.hpp>

namespace kernel {
struct KVFSNode;
struct KFilesystemObject;

struct KFileObject : KObjectT<KFileObject, 1, TYPE_FILE> {
  enum class OpenMode : uint32_t {
    Read = 0x01,
    Write = 0x02,
    ReadWrite = Read | Write,
    Append = 0x04,
    Truncate = 0x08,
    Create = 0x10,   // Create if not exists
    Exclusive = 0x20,// Fail if exists
    Directory = 0x40,// Opening a directory
  };

  kstd::static_string<255> actualPath;
  KObjectPtr<KVolumeMountObject> mountPoint;
  kstd::unique_ptr<KVFSNode> vfsNode;

  const KObjectPtr<KFilesystemObject> &FileSystem() const noexcept { return mountPoint->fileSystem; }

  OpenMode mode;
  uint64_t offset;
  uint32_t flags;
};

template<>
struct ObjectTypeOf<TYPE_FILE> {
  using type = KFileObject;
};

inline KFileObject::OpenMode operator|(KFileObject::OpenMode a, KFileObject::OpenMode b) {
  return static_cast<KFileObject::OpenMode>(static_cast<uint32_t>(a) |
                                            static_cast<uint32_t>(b));
}

inline bool operator&(KFileObject::OpenMode a, KFileObject::OpenMode b) {
  return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

}// namespace kernel
