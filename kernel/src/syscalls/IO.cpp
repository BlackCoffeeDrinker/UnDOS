#include "stdkrn.hpp"
#include "syscalls.hpp"
#include <kernel/kobject/KIOObject.hpp>
#include <kernel/object_manager.hpp>
#include <kernel/tty/tty_device.hpp>
#include <expected.hpp>

namespace {
kstd::expected<kernel::KObjectPtr<kernel::KIOObject>, kstd::error_code>
resolve_io(uint32_t value, kernel::ProcessHandleTable::HandleAccess access) {
  const auto thread = KE_SCHED_GetCurrentThread();
  if (!thread) return kstd::unexpected(kernel::KernelError::NoCurrentThread);
  if (!thread->owner_process) return kstd::unexpected(kernel::KernelError::NoCurrentProcess);
  auto &handles = thread->owner_process->handles;
  const auto handle = static_cast<kernel::KHandle>(value);
  const auto entry = handles.Lookup(handle);
  if (!entry) return kstd::unexpected(kernel::KernelError::InvalidHandle);
  if (!handles.HasAccess(handle, access)) return kstd::unexpected(kernel::KernelError::AccessDenied);
  
  auto object = entry->object.As<kernel::KIOObject>();
  if (!object) return kstd::unexpected(kernel::KernelError::WrongObjectType);
  return object;
}
}// namespace

kstd::error_code handle_Open_V1(__KRN_IO_OPEN_V1 &&args) {
  args.handle = UINT32_MAX;
  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (currentThread == nullptr) return kernel::KernelError::NoCurrentThread;
  const auto proc = currentThread->owner_process;
  if (!proc) return kernel::KernelError::NoCurrentProcess;
  
  kernel::KIOObject::OpenMode mode = {};
  kernel::ProcessHandleTable::HandleAccess access = {};
  constexpr uint32_t supported = FileOpenMode_Read | FileOpenMode_Write | FileOpenMode_Append |
                                 FileOpenMode_Create | FileOpenMode_Truncate | FileOpenMode_Exclusive;
  if ((static_cast<uint32_t>(args.mode) & ~supported) != 0) return kernel::KernelError::InvalidArgument;
  const auto allocate = [&](const kernel::KObjectPtr<kernel::KIOObject> &object,
                            kernel::ProcessHandleTable::HandleFlags flags) -> kstd::error_code {
    const auto result = proc->handles.Allocate(object, access, flags);
    if (!result) return result.error();
    args.handle = static_cast<uint32_t>(*result);
    return kernel::KernelError::Success;
  };

  if ((args.mode & FileOpenMode_Read) != 0) {
    mode = mode | kernel::KIOObject::OpenMode::Read;
    access = access | kernel::ProcessHandleTable::HandleAccess::Read;
  }
  if ((args.mode & FileOpenMode_Write) != 0) {
    mode = mode | kernel::KIOObject::OpenMode::Write;
    access = access | kernel::ProcessHandleTable::HandleAccess::Write;
  }
  if ((args.mode & FileOpenMode_Append) != 0) {
    mode = mode | kernel::KIOObject::OpenMode::Append;
  }
  if ((args.mode & FileOpenMode_Create) != 0) {
    mode = mode | kernel::KIOObject::OpenMode::Create;
  }
  if ((args.mode & FileOpenMode_Truncate) != 0) {
    mode = mode | kernel::KIOObject::OpenMode::Truncate;
  }
  if ((args.mode & FileOpenMode_Exclusive) != 0) {
    mode = mode | kernel::KIOObject::OpenMode::Exclusive;
  }

  // Object Manager device/stream lookup
  kstd::string_view pathView = args.path;
  if (pathView.starts_with("\\") || pathView.starts_with("/")) {
    kstd::string normalized;
    normalized.reserve(pathView.size());
    if (normalized.capacity() < pathView.size()) return kernel::KernelError::OutOfMemory;
    for (const char c : pathView) {
      if (c == '\\') {
        if (normalized.empty() || normalized[normalized.size() - 1] != '/') {
          normalized.push_back('/');
        }
      } else {
        normalized.push_back(c);
      }
    }

    if (const auto obj = KE_OB_LookupObject(normalized)) {
      if (const auto devObj = obj.As<kernel::KDeviceObject>()) {
        if (devObj->device) {
          const auto ioObj = KE_IO_CreateDeviceStream(devObj->device, mode, devObj->name);
          if (!ioObj) return ioObj.error();
          return allocate(*ioObj, kernel::ProcessHandleTable::HandleFlags::None);
        }
      }
      if (const auto ioObj = KE_IO_FromObject(obj)) {
        return allocate(*ioObj, kernel::ProcessHandleTable::HandleFlags::None);
      } else if (ioObj.error() != kernel::KernelError::WrongObjectType) {
        return ioObj.error();
      }
    }
  }

  // Fallback for default console
  if (args.path == "\\Device\\Console" || args.path == "/Device/Console") {
    if (const auto defSink = KE_TTY_GetDefaultConsoleSink()) {
      const auto ioObj = KE_IO_FromObject(defSink);
      if (!ioObj) return ioObj.error();
      return allocate(*ioObj, kernel::ProcessHandleTable::HandleFlags::Inheritable);
    }
  }

  // A VFS file ?
  const auto ioObj = KE_VFS_OpenFile(args.path, mode);
  if (!ioObj) return ioObj.error();
  return allocate(*ioObj, kernel::ProcessHandleTable::HandleFlags::None);
}

kstd::error_code handle_Close_V1(__KRN_IO_CLOSE_V1 &&args) {
  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (currentThread == nullptr) return kernel::KernelError::NoCurrentThread;
  const auto proc = currentThread->owner_process;
  if (!proc) return kernel::KernelError::NoCurrentProcess;

  const auto h = static_cast<kernel::KHandle>(args.handle);
  return proc->handles.Close(h);
}

/** Reads data from an open handle into `data`. */
kstd::error_code handle_Read_V1(__KRN_IO_READ_V1 &&args) {
  const auto resolved = resolve_io(args.handle, kernel::ProcessHandleTable::HandleAccess::Read);
  if (!resolved) return resolved.error();

  size_t transferred = 0;
  const auto status = (*resolved)->Read(args.data, transferred);
  if (status) return status;
  if (transferred > args.data.size()) return kernel::KernelError::IoError;

  args.data_len = static_cast<decltype(args.data_len)>(transferred);
  return kernel::KernelError::Success;
}

/** Writes data from `data` to an open handle. */
kstd::error_code handle_Write_V1(__KRN_IO_WRITE_V1 &&args) {
  const auto resolved = resolve_io(args.handle, kernel::ProcessHandleTable::HandleAccess::Write);
  if (!resolved) return resolved.error();

  size_t transferred = 0;
  const auto status = (*resolved)->Write(kstd::span<const uint8_t>(args.data.get(), args.data.size()), transferred);
  if (status) return status;
  if (transferred > args.data.size()) return kernel::KernelError::IoError;

  args.data_len = static_cast<decltype(args.data_len)>(transferred);
  return kernel::KernelError::Success;
}

/** Versioned byte-counting write, preserving the V1 ABI. */
kstd::error_code handle_Write_V2(__KRN_IO_WRITE_V2 &&args) {
  args.transferred = 0;
  const auto resolved = resolve_io(args.handle, kernel::ProcessHandleTable::HandleAccess::Write);
  if (!resolved) return resolved.error();
  size_t transferred = 0;
  const auto status = (*resolved)->Write({args.data.get(), args.data.size()}, transferred);
  if (status) return status;
  if (transferred > args.data.size()) return kernel::KernelError::IoError;
  args.transferred = static_cast<uint32_t>(transferred);
  return kernel::KernelError::Success;
}

/** Sends a control code to the device behind an open handle. */
kstd::error_code handle_Control_V1(__KRN_IO_CONTROL_V1 &&args) {
  const auto resolved = resolve_io(args.handle, kernel::ProcessHandleTable::HandleAccess::None);
  if (!resolved) return resolved.error();

  size_t transferred = 0;
  const auto status = (*resolved)->Control(
      args.code,
      kstd::span<const uint8_t>(args.input.get(), args.input.size()),
      kstd::span<uint8_t>(args.output.get(), args.output_len),
      transferred);
  if (status) return status;
  if (transferred > args.output.size()) return kernel::KernelError::IoError;

  args.output_len = static_cast<decltype(args.output_len)>(transferred);
  return kernel::KernelError::Success;
}
