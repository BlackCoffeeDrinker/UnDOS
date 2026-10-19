#pragma once

#include <list.hpp>

#include <kernel/__core.hpp>
#include <kernel/gfx/KDeviceContextTable.hpp>
#include <kernel/gfx/KFontResourceTable.hpp>
#include <kernel/kobject/KDispatcherHeader.hpp>
#include <kernel/kobject/KEventObject.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/kobject/KThreadObject.hpp>
#include <kernel/kobject/ObjectType.hpp>
#include <kernel/virtual_memory.hpp>

namespace kernel {
struct KSubsystemObject;

enum class KHandle : uint32_t {};
enum class StandardStream : uint32_t {
  Stdin,
  Stdout,
  Stderr,
};

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
  struct StandardStreamHandle {
    KHandle value{};
    bool available{false};
  };
  StandardStreamHandle standard_streams[3]{};

  bool GetStandardHandle(StandardStream stream, KHandle &handle) const {
    const auto index = static_cast<uint32_t>(stream);
    if (index >= 3 || !standard_streams[index].available) return false;
    for (const auto &entry : entries) {
      if (entry.handle == standard_streams[index].value && entry.object) {
        handle = entry.handle;
        return true;
      }
    }
    return false;
  }

  bool SetStandardHandle(StandardStream stream, KHandle handle) {
    const auto index = static_cast<uint32_t>(stream);
    if (index >= 3) return false;
    const auto *entry = Lookup(handle);
    if (!entry || !entry->object) return false;
    standard_streams[index] = {handle, true};
    return true;
  }

  bool AllocateStandardHandle(StandardStream stream, const KObjectPtr<KObject> &object,
                              HandleAccess access) {
    if (!object || static_cast<uint32_t>(stream) >= 3) return false;
    const auto count = entries.size();
    const auto handle = Allocate(object, access, HandleFlags::Inheritable);
    if (entries.size() != count + 1) return false;
    return SetStandardHandle(stream, handle);
  }

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

  bool HasAccess(KHandle h, HandleAccess required) {
    const auto *entry = Lookup(h);
    return entry && (static_cast<uint32_t>(entry->access) & static_cast<uint32_t>(required)) == static_cast<uint32_t>(required);
  }

  bool Close(KHandle h) {
    for (auto it = entries.begin(); it != entries.end(); ++it) {
      if (it->handle == h) {
        if ((static_cast<uint32_t>(it->flags) & static_cast<uint32_t>(HandleFlags::ProtectFromClose)) != 0)
          return false;

        if (--it->refcount == 0) {
          for (auto &stream : standard_streams) {
            if (stream.available && stream.value == h) stream.available = false;
          }
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
  using KObjectT::KObjectT;
  enum class State : uint8_t { Running,
                               Terminated };

  vmm::AddressSpace address_space{};
  KObjectPtr<KThreadObject> main_thread;
  KObjectPtr<KSubsystemObject> subsystem;
  int32_t exit_code{0};
  State state{State::Running};
  // Signalled (manual-reset) when the process exits.
  KDispatcherHeader exitHeader;
  // Loader completion, not process exit. Exit before completion signals failure.
  KObjectPtr<KEventObject> startupEvent;
  bool startupReady{false};

  ProcessHandleTable handles;
  DeviceContextTable dcTable;
  font::ResourceTable fontResources;

  bool AttachStandardStreams(const KObjectPtr<KObject> &sink) {
    if (!handles.AllocateStandardHandle(StandardStream::Stdin, sink, ProcessHandleTable::HandleAccess::Read)) return false;
    if (!handles.AllocateStandardHandle(StandardStream::Stdout, sink, ProcessHandleTable::HandleAccess::Write)) return false;
    return handles.AllocateStandardHandle(StandardStream::Stderr, sink, ProcessHandleTable::HandleAccess::Write);
  }

  bool InheritHandlesFrom(ProcessHandleTable &parentHandles) {
    for (const auto &entry : parentHandles.entries) {
      if ((static_cast<uint32_t>(entry.flags) & static_cast<uint32_t>(ProcessHandleTable::HandleFlags::Inheritable)) != 0) {
        const auto count = handles.entries.size();
        const auto inheritedHandle = handles.Allocate(entry.object, entry.access, entry.flags);
        if (handles.entries.size() != count + 1 || !handles.Lookup(inheritedHandle)) return false;
        for (uint32_t i = 0; i < 3; ++i) {
          const auto stream = static_cast<StandardStream>(i);
          KHandle parentStandardHandle{};
          if (parentHandles.GetStandardHandle(stream, parentStandardHandle) &&
              parentStandardHandle == entry.handle) {
            handles.SetStandardHandle(stream, inheritedHandle);
          }
        }
      }
    }
    return true;
  }
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
