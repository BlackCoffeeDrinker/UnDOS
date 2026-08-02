#pragma once

#include <kernel/__core.hpp>
#include <kernel/kobject/KObjectT.hpp>

namespace kernel {
struct KFileObject;

struct KMemoryBackingObject : KObjectT<KMemoryBackingObject, 1, TYPE_MEMORY_SECTION> {
  using KObjectT::KObjectT;
  enum class Kind {
    Anonymous,  //< anonymous memory (heap/stack)
    File,       //< file-backed mapping (mmap / module image)
    Shared,     //< named/shared memory between processes
    KernelModule//< kernel module image (file-backed, special semantics)
  };

  // The logical kind of this backing object. Determines how pages are populated
  // on fault (zero-fill, read from file, shared semantics, etc.).
  Kind kind = Kind::Anonymous;

  // For File or KernelModule kinds: a pointer to the open file object that
  // supplies the contents. May be null for Anonymous/Shared kinds.
  KObjectPtr<KFileObject> file;

  // Base offset (in bytes) inside `file` that corresponds to backing->start.
  // Page i of the mapping is read from file at (file_offset + i * PAGE_SIZE).
  // Ignored for Anonymous and Shared kinds.
  uint64_t file_offset = 0;

  // Total size (in bytes) of the backing object. Used for bounds checks,
  // partial-page handling at EOF, and for reporting/inspection via the Object Manager.
  size_t size = 0;
};

template<>
struct ObjectTypeOf<TYPE_MEMORY_SECTION> {
  using type = KMemoryBackingObject;
};
}// namespace kernel
