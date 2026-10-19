#include "stdkrn.hpp"
#include "syscalls.hpp"
#include <kernel/kobject/KIOObject.hpp>
#include <kernel/object_manager.hpp>
#include <kernel/tty/tty_device.hpp>

SyscallReturnCode handle_Open_V1(__KRN_IO_OPEN_V1 &&args) {
  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (currentThread == nullptr) return SyscallReturnCode_INVALID_ARGUMENT;
  const auto proc = currentThread->owner_process;
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;
  
  kernel::KIOObject::OpenMode mode = {};
  kernel::ProcessHandleTable::HandleAccess access = {};

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
    kstd::static_string<128> normalized;
    for (char c : pathView) {
      if (c == '\\') {
        if (normalized.empty() || normalized[normalized.size() - 1] != '/') {
          normalized.push_back('/');
        }
      } else {
        normalized.push_back(c);
      }
    }

    if (auto obj = KE_OB_LookupObject(normalized)) {
      if (auto devObj = obj.As<kernel::KDeviceObject>()) {
        if (devObj->device) {
          auto ioObj = KE_IO_CreateDeviceStream(devObj->device, mode, devObj->name);
          if (!ioObj) return SyscallReturnCode_OUT_OF_MEMORY;
          kernel::KHandle h = proc->handles.Allocate(
              ioObj,
              access,
              kernel::ProcessHandleTable::HandleFlags::None);
          args.handle = static_cast<uint32_t>(h);
          return SyscallReturnCode_OKAY;
        }
      }
      if (auto ioObj = KE_IO_FromObject(obj)) {
        kernel::KHandle h = proc->handles.Allocate(
            ioObj,
            access,
            kernel::ProcessHandleTable::HandleFlags::None);
        args.handle = static_cast<uint32_t>(h);
        return SyscallReturnCode_OKAY;
      }
    }
  }

  // Fallback for default console
  if (args.path == "\\Device\\Console" || args.path == "/Device/Console") {
    if (const auto defSink = KE_TTY_GetDefaultConsoleSink()) {
      auto ioObj = KE_IO_FromObject(defSink);
      if (!ioObj) return SyscallReturnCode_OUT_OF_MEMORY;
      kernel::KHandle h = proc->handles.Allocate(
          ioObj,
          access,
          kernel::ProcessHandleTable::HandleFlags::Inheritable);
      args.handle = static_cast<uint32_t>(h);
      return SyscallReturnCode_OKAY;
    }
  }

  // A VFS file ?
  if (const auto ioObj = KE_VFS_OpenFile(args.path, mode)) {
    kernel::KHandle h = proc->handles.Allocate(
        ioObj,
        access,
        kernel::ProcessHandleTable::HandleFlags::None);

    args.handle = static_cast<uint32_t>(h);
    return SyscallReturnCode_OKAY;
  }

  return SyscallReturnCode_INVALID_ARGUMENT;
}

SyscallReturnCode handle_Close_V1(__KRN_IO_CLOSE_V1 &&args) {
  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (currentThread == nullptr) return SyscallReturnCode_INVALID_ARGUMENT;
  const auto proc = currentThread->owner_process;
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;

  const auto h = static_cast<kernel::KHandle>(args.handle);
  if (proc->handles.Close(h)) {
    return SyscallReturnCode_OKAY;
  }

  return SyscallReturnCode_INVALID_ARGUMENT;
}

/** Reads data from an open handle into `data`. */
SyscallReturnCode handle_Read_V1(__KRN_IO_READ_V1 &&args) {
  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (currentThread == nullptr) return SyscallReturnCode_INVALID_ARGUMENT;
  const auto proc = currentThread->owner_process;
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;

  const auto h = static_cast<kernel::KHandle>(args.handle);
  const auto entry = proc->handles.Lookup(h);
  if (!entry || !proc->handles.HasAccess(h, kernel::ProcessHandleTable::HandleAccess::Read)) return SyscallReturnCode_INVALID_ARGUMENT;

  const auto ioObj = entry->object.As<kernel::KIOObject>();
  if (!ioObj) return SyscallReturnCode_INVALID_ARGUMENT;

  size_t transferred = 0;
  const auto status = ioObj->Read(args.data, transferred);
  if (status == kernel::IoStatus::InvalidSize) {
    return SyscallReturnCode_INVALID_SIZE;
  }
  if (status != kernel::IoStatus::Success) {
    return SyscallReturnCode_INVALID_ARGUMENT;
  }

  args.data_len = static_cast<decltype(args.data_len)>(transferred);
  return SyscallReturnCode_OKAY;
}

/** Writes data from `data` to an open handle. */
SyscallReturnCode handle_Write_V1(__KRN_IO_WRITE_V1 &&args) {
  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (currentThread == nullptr) return SyscallReturnCode_INVALID_ARGUMENT;
  const auto proc = currentThread->owner_process;
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;

  const auto h = static_cast<kernel::KHandle>(args.handle);
  const auto entry = proc->handles.Lookup(h);
  if (!entry || !proc->handles.HasAccess(h, kernel::ProcessHandleTable::HandleAccess::Write)) return SyscallReturnCode_INVALID_ARGUMENT;

  const auto ioObj = entry->object.As<kernel::KIOObject>();
  if (!ioObj) return SyscallReturnCode_INVALID_ARGUMENT;

  size_t transferred = 0;
  const auto status = ioObj->Write(kstd::span<const uint8_t>(args.data.get(), args.data.size()), transferred);
  if (status != kernel::IoStatus::Success) {
    return SyscallReturnCode_INVALID_ARGUMENT;
  }

  args.data_len = static_cast<decltype(args.data_len)>(transferred);
  return SyscallReturnCode_OKAY;
}

/** Versioned byte-counting write, preserving the V1 ABI. */
SyscallReturnCode handle_Write_V2(__KRN_IO_WRITE_V2 &&args) {
  args.transferred = 0;
  const auto thread = KE_SCHED_GetCurrentThread();
  if (!thread || !thread->owner_process) return SyscallReturnCode_INVALID_ARGUMENT;
  auto &handles = thread->owner_process->handles;
  const auto h = static_cast<kernel::KHandle>(args.handle);
  const auto entry = handles.Lookup(h);
  if (!entry || !handles.HasAccess(h, kernel::ProcessHandleTable::HandleAccess::Write))
    return SyscallReturnCode_INVALID_ARGUMENT;
  const auto object = entry->object.As<kernel::KIOObject>();
  if (!object) return SyscallReturnCode_INVALID_ARGUMENT;
  size_t transferred = 0;
  const auto status = object->Write({args.data.get(), args.data.size()}, transferred);
  if (status != kernel::IoStatus::Success || transferred > args.data.size())
    return SyscallReturnCode_INVALID_ARGUMENT;
  args.transferred = static_cast<uint32_t>(transferred);
  return SyscallReturnCode_OKAY;
}

/** Sends a control code to the device behind an open handle. */
SyscallReturnCode handle_Control_V1(__KRN_IO_CONTROL_V1 &&args) {
  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (currentThread == nullptr) return SyscallReturnCode_INVALID_ARGUMENT;
  const auto proc = currentThread->owner_process;
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;

  const auto h = static_cast<kernel::KHandle>(args.handle);
  const auto entry = proc->handles.Lookup(h);
  if (!entry) return SyscallReturnCode_INVALID_ARGUMENT;

  const auto ioObj = entry->object.As<kernel::KIOObject>();
  if (!ioObj) return SyscallReturnCode_INVALID_ARGUMENT;

  size_t transferred = 0;
  const auto status = ioObj->Control(
      args.code,
      kstd::span<const uint8_t>(args.input.get(), args.input.size()),
      kstd::span<uint8_t>(args.output.get(), args.output_len),
      transferred);
  if (status == kernel::IoStatus::Unsupported) return SyscallReturnCode_UNSUPPORTED;
  if (status == kernel::IoStatus::InvalidSize) return SyscallReturnCode_INVALID_SIZE;
  if (status != kernel::IoStatus::Success) return SyscallReturnCode_INVALID_ARGUMENT;

  args.output_len = static_cast<decltype(args.output_len)>(transferred);
  return SyscallReturnCode_OKAY;
}
