#pragma once

#include <list.hpp>

#include <kernel/__core.hpp>
#include <kernel/kerror.hpp>
#include <kernel/gfx/KDeviceContextTable.hpp>
#include <kernel/gfx/KFontResourceTable.hpp>
#include <kernel/kobject/KDispatcherHeader.hpp>
#include <kernel/kobject/KEventObject.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/kobject/KThreadObject.hpp>
#include <kernel/kobject/ObjectType.hpp>
#include <kernel/virtual_memory.hpp>
#include <expected.hpp>

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
    // Nonzero while the slot is an unbound reservation. Reserved entries are
    // invisible to Lookup, HasAccess, Close and inheritance.
    uint32_t reservation{0};
  };

  // Opaque proof of one Reserve() call: the slot number plus a per-table
  // identity, so a stale token cannot name a later entry that reuses the slot.
  class Reservation {
   public:
    Reservation() = default;

   private:
    friend struct ProcessHandleTable;
    Reservation(KHandle handle, uint32_t identity) : handle_(handle), identity_(identity) {}
    KHandle handle_{};
    uint32_t identity_{0};
  };

  kstd::list<Entry> entries;
  kstd::list<KHandle> free_list;
  uint32_t next_raw_handle{0};
  uint32_t next_reservation{0};
  struct StandardStreamHandle {
    KHandle value{};
    bool available{false};
  };
  StandardStreamHandle standard_streams[3]{};

  kstd::error_code GetStandardHandle(StandardStream stream, KHandle &handle) const {
    const auto index = static_cast<uint32_t>(stream);
    if (index >= 3) return KernelError::InvalidArgument;
    if (!standard_streams[index].available) return KernelError::StandardStreamUnavailable;
    for (const auto &entry : entries) {
      if (entry.handle == standard_streams[index].value && entry.object) {
        handle = entry.handle;
        return KernelError::Success;
      }
    }
    return KernelError::InvalidHandle;
  }

  kstd::error_code SetStandardHandle(StandardStream stream, KHandle handle) {
    const auto index = static_cast<uint32_t>(stream);
    if (index >= 3) return KernelError::InvalidArgument;
    const auto *entry = Lookup(handle);
    if (!entry || !entry->object) return KernelError::InvalidHandle;
    standard_streams[index] = {.value = handle, .available = true};
    return KernelError::Success;
  }

  kstd::error_code AllocateStandardHandle(StandardStream stream, const KObjectPtr<KObject> &object,
                              HandleAccess access) {
    if (!object) return KernelError::InvalidArgument;
    if (static_cast<uint32_t>(stream) >= 3) return KernelError::InvalidArgument;
    auto handle = Allocate(object, access, HandleFlags::Inheritable);
    if (!handle) return handle.error();
    const auto status = SetStandardHandle(stream, *handle);
    if (status) Close(*handle);
    return status;
  }

  kstd::expected<KHandle, kstd::error_code> Allocate(const KObjectPtr<KObject> &obj,
                   HandleAccess access,
                   HandleFlags flags) {
    if (!obj) return kstd::unexpected(KernelError::InvalidArgument);
    return AllocateEntry(obj, access, flags);
  }

  // Reserves a handle number with no object yet. It stays unusable by user
  // handle operations until Bind() publishes it or Cancel() releases it.
  // Validation relies on the same serialization as every other table
  // operation: syscalls enter through an interrupt gate on one CPU.
  kstd::expected<Reservation, kstd::error_code> Reserve(HandleAccess access, HandleFlags flags) {
    const auto handle = AllocateEntry(nullptr, access, flags);
    if (!handle) return kstd::unexpected(handle.error());
    if (++next_reservation == 0) next_reservation = 1;
    entries.back().reservation = next_reservation;
    return Reservation{*handle, next_reservation};
  }

  // Publishes an object in a reservation. Never allocates. Fails only for a
  // token that no longer names its own unbound reservation, or a null object.
  kstd::expected<KHandle, kstd::error_code> Bind(const Reservation &token,
                                                 const KObjectPtr<KObject> &obj) {
    if (!obj) return kstd::unexpected(KernelError::InvalidArgument);
    Entry *entry = FindReservation(token);
    if (!entry) return kstd::unexpected(KernelError::InvalidHandle);
    entry->object = obj;
    entry->reservation = 0;
    return entry->handle;
  }

  // Releases an unbound reservation; a stale token changes nothing.
  kstd::error_code Cancel(const Reservation &token) {
    for (auto it = entries.begin(); it != entries.end(); ++it) {
      if (token.identity_ && it->reservation == token.identity_ && it->handle == token.handle_) {
        free_list.push_back(it->handle);
        entries.erase(it);
        return KernelError::Success;
      }
    }
    return KernelError::InvalidHandle;
  }

  Entry *FindReservation(const Reservation &token) {
    if (!token.identity_) return nullptr;
    for (auto &e: entries)
      if (e.reservation == token.identity_ && e.handle == token.handle_) return &e;
    return nullptr;
  }

  kstd::expected<KHandle, kstd::error_code> AllocateEntry(const KObjectPtr<KObject> &obj,
                   HandleAccess access,
                   HandleFlags flags) {
    uint32_t raw;

    if (!free_list.empty()) {
      raw = static_cast<uint32_t>(free_list.front());
      free_list.pop_front();
    } else {
      if (next_raw_handle == UINT32_MAX) return kstd::unexpected(KernelError::ResourceLimitExceeded);
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
      if (e.handle == h && !e.reservation)
        return &e;
    return nullptr;
  }

  bool HasAccess(KHandle h, HandleAccess required) {
    const auto *entry = Lookup(h);
    return entry && (static_cast<uint32_t>(entry->access) & static_cast<uint32_t>(required)) == static_cast<uint32_t>(required);
  }

  kstd::error_code Close(KHandle h) {
    for (auto it = entries.begin(); it != entries.end(); ++it) {
      if (it->handle == h && !it->reservation) {
        if ((static_cast<uint32_t>(it->flags) & static_cast<uint32_t>(HandleFlags::ProtectFromClose)) != 0)
          return KernelError::AccessDenied;

        if (--it->refcount == 0) {
          for (auto &stream : standard_streams) {
            if (stream.available && stream.value == h) stream.available = false;
          }
          free_list.push_back(h);
          entries.erase(it);
        }
        return KernelError::Success;
      }
    }
    return KernelError::InvalidHandle;
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

  kstd::error_code AttachStandardStreams(const KObjectPtr<KObject> &sink) {
    if (!sink) return KernelError::InvalidArgument;
    const auto input = handles.AllocateStandardHandle(StandardStream::Stdin, sink, ProcessHandleTable::HandleAccess::Read);
    if (input) return input;
    const auto output = handles.AllocateStandardHandle(StandardStream::Stdout, sink, ProcessHandleTable::HandleAccess::Write);
    if (output) {
      KHandle handle{};
      if (!handles.GetStandardHandle(StandardStream::Stdin, handle)) handles.Close(handle);
      return output;
    }
    const auto error = handles.AllocateStandardHandle(StandardStream::Stderr, sink, ProcessHandleTable::HandleAccess::Write);
    if (error) {
      KHandle handle{};
      if (!handles.GetStandardHandle(StandardStream::Stdin, handle)) handles.Close(handle);
      if (!handles.GetStandardHandle(StandardStream::Stdout, handle)) handles.Close(handle);
    }
    return error;
  }

  kstd::error_code InheritHandlesFrom(ProcessHandleTable &parentHandles) {
    const auto rollback = [this]() {
      while (!handles.entries.empty()) {
        const KHandle handle = handles.entries.front().handle;
        // A new child owns no reservations; stop rather than spin if one appears.
        if (handles.Close(handle)) break;
      }
    };
    for (const auto &entry : parentHandles.entries) {
      if (entry.reservation) continue;
      if ((static_cast<uint32_t>(entry.flags) & static_cast<uint32_t>(ProcessHandleTable::HandleFlags::Inheritable)) != 0) {
        const auto count = handles.entries.size();
        auto inheritedHandle = handles.Allocate(entry.object, entry.access, entry.flags);
        if (!inheritedHandle) {
          rollback();
          return inheritedHandle.error();
        }
        if (handles.entries.size() != count + 1 || !handles.Lookup(*inheritedHandle)) {
          handles.Close(*inheritedHandle);
          rollback();
          return KernelError::OutOfMemory;
        }
        for (uint32_t i = 0; i < 3; ++i) {
          const auto stream = static_cast<StandardStream>(i);
          KHandle parentStandardHandle{};
          if (!parentHandles.GetStandardHandle(stream, parentStandardHandle) &&
              parentStandardHandle == entry.handle) {
            const auto status = handles.SetStandardHandle(stream, *inheritedHandle);
            if (status) {
              rollback();
              return status;
            }
          }
        }
      }
    }
    return KernelError::Success;
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
