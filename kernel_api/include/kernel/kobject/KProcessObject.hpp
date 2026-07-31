#pragma once

#include <list.hpp>

#include <kernel/__core.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/kobject/KThreadObject.hpp>
#include <kernel/kobject/ObjectType.hpp>
#include <kernel/virtual_memory.hpp>

namespace kernel {
struct KSubsystemObject;

enum class KHandle : uint32_t {};

struct ProcessHandleTable {
  enum class HandleAccess : uint32_t {
    None = 0,
    Read = 1 << 0,
    Write = 1 << 1,
    Execute = 1 << 2,
    Duplicate = 1 << 3,
    Close = 1 << 4,
    All = 0xFFFFFFFF
  };
  enum class HandleFlags : uint32_t {
    None = 0,
    Inheritable = 1 << 0,
    ProtectFromClose = 1 << 1,
  };
  struct Entry {
    KHandle handle;
    KObjectPtr<KObject> object;
    HandleAccess access;
    HandleFlags flags;
    uint32_t refcount;
  };

  kstd::list<Entry> entries;
  kstd::list<KHandle> free_list;
  uint32_t next_raw_handle{0};

  KHandle Allocate(KObjectPtr<KObject> obj,
                   HandleAccess access,
                   HandleFlags flags) {
    uint32_t raw;

    if (!free_list.empty()) {
      raw = static_cast<uint32_t>(free_list.front());
      free_list.pop_front();
    } else {
      raw = next_raw_handle++;
    }

    const auto h = static_cast<KHandle>(raw);

    entries.push_back(Entry{
        .handle = h,
        .object = obj,
        .access = access,
        .flags = flags,
        .refcount = 1});

    return h;
  }

  Entry *Lookup(KHandle h) {
    for (auto &e: entries)
      if (e.handle == h)
        return &e;
    return nullptr;
  }

  bool Close(KHandle h) {
    for (auto it = entries.begin(); it != entries.end(); ++it) {
      if (it->handle == h) {
        if (it->flags == HandleFlags::ProtectFromClose)
          return false;

        if (--it->refcount == 0) {
          free_list.push_back(h);
          entries.erase(it);
        }
        return true;
      }
    }
    return false;
  }
};

// Process Control Block (EPROCESS-style). Owns the isolated address space and
// the thread(s) running inside it. Platform-agnostic: all HAL-specific state
// lives inside vmm::AddressSpace / KThreadObject.
struct KProcessObject : KObjectT<KProcessObject, 1, TYPE_PROCESS> {
  enum class State : uint8_t { Running,
                               Terminated };

  vmm::AddressSpace address_space{};
  KObjectPtr<KThreadObject> main_thread;
  KObjectPtr<KSubsystemObject> subsystem;
  int32_t exit_code{0};
  State state{State::Running};

  ProcessHandleTable handles;
};

template<>
struct ObjectTypeOf<TYPE_PROCESS> {
  using type = KProcessObject;
};


inline ProcessHandleTable::HandleAccess operator|(ProcessHandleTable::HandleAccess a, ProcessHandleTable::HandleAccess b) {
  return static_cast<ProcessHandleTable::HandleAccess>(static_cast<uint32_t>(a) |
                                                       static_cast<uint32_t>(b));
}

inline bool operator&(ProcessHandleTable::HandleAccess a, ProcessHandleTable::HandleAccess b) {
  return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}


}// namespace kernel
