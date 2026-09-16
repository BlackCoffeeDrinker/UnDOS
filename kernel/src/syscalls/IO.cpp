#include "stdkrn.hpp"
#include "syscalls.hpp"
#include <kernel/kobject/KVttyObject.hpp>
#include <kernel/tty/tty_device.hpp>

SyscallReturnCode handle_Open_V1(__KRN_IO_OPEN_V1 &&args) {
  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (currentThread == nullptr) return SyscallReturnCode_INVALID_ARGUMENT;
  const auto proc = currentThread->owner_process;
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;
  
  kernel::KFileObject::OpenMode mode = {};
  kernel::ProcessHandleTable::HandleAccess access = {};

  if ((args.mode & FileOpenMode_Read) != 0) {
    mode = mode | kernel::KFileObject::OpenMode::Read;
    access = access | kernel::ProcessHandleTable::HandleAccess::Read;
  }
  if ((args.mode & FileOpenMode_Write) != 0) {
    mode = mode | kernel::KFileObject::OpenMode::Write;
    access = access | kernel::ProcessHandleTable::HandleAccess::Write;
  }
  if ((args.mode & FileOpenMode_Append) != 0) {
    mode = mode | kernel::KFileObject::OpenMode::Append;
  }
  if ((args.mode & FileOpenMode_Create) != 0) {
    mode = mode | kernel::KFileObject::OpenMode::Create;
  }
  if ((args.mode & FileOpenMode_Truncate) != 0) {
    mode = mode | kernel::KFileObject::OpenMode::Truncate;
  }
  if ((args.mode & FileOpenMode_Exclusive) != 0) {
    mode = mode | kernel::KFileObject::OpenMode::Exclusive;
  }

  // A Driver/Object ?
  // TODO: This should lookup the OBJECT tree... not hardcoding paths
  if (args.path == "\\Device\\Console") {
    if (const auto defSink = KE_TTY_GetDefaultConsoleSink()) {
      kernel::KHandle h = proc->handles.Allocate(
          defSink,
          kernel::ProcessHandleTable::HandleAccess::Read | kernel::ProcessHandleTable::HandleAccess::Write,
          kernel::ProcessHandleTable::HandleFlags::Inheritable);
      args.handle = static_cast<uint32_t>(h);
      return SyscallReturnCode_OKAY;
    }
  }
  

  // A VFS file ?
  if (const auto file = KE_VFS_OpenFile(args.path, mode)) {
    kernel::KHandle h = proc->handles.Allocate(
        file,
        kernel::ProcessHandleTable::HandleAccess::Read |
            kernel::ProcessHandleTable::HandleAccess::Write,
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
  const auto entry = proc->handles.Lookup(h);
  if (!entry) return SyscallReturnCode_INVALID_ARGUMENT;

  if (auto file = entry->object.As<kernel::KFileObject>();
      file && !file->isClosed) {
    file->isClosed = true;
    KE_VFS_CloseFile(kstd::move(file));
  } else if (const auto source = entry->object.As<kernel::KVttySourceObject>()) {
    if (source->vtty) source->vtty->close_source();
  } else if (const auto sink = entry->object.As<kernel::KVttySinkObject>()) {
    if (sink->vtty) sink->vtty->close_sink();
  }

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
  if (!entry) return SyscallReturnCode_INVALID_ARGUMENT;

  if (const auto file = entry->object.As<kernel::KFileObject>()) {
    const auto originalOffset = KE_VFS_TellFile(file);
    const auto size_read = KE_VFS_ReadFile(file, args.data);
    if (size_read > kstd::numeric_limits<decltype(args.data.size())>::max()) {
      // This should never happen (args.data.size() is 32bit)
      KE_VFS_SeekFile(file, originalOffset);
      return SyscallReturnCode_INVALID_SIZE;
    }

    args.data_len = static_cast<decltype(args.data_len)>(size_read);
    return SyscallReturnCode_OKAY;
  }

  if (const auto sink = entry->object.As<kernel::KVttySinkObject>()) {
    if (!sink->vtty) return SyscallReturnCode_INVALID_ARGUMENT;
    KE_TTY_PumpDevice(sink->vtty);
    const auto n = sink->vtty->sink_read(args.data.get(), args.data.size());
    args.data_len = static_cast<decltype(args.data_len)>(n);
    return SyscallReturnCode_OKAY;
  }

  if (const auto source = entry->object.As<kernel::KVttySourceObject>()) {
    if (!source->vtty) return SyscallReturnCode_INVALID_ARGUMENT;
    const auto n = source->vtty->source_read(args.data.get(), args.data.size());
    args.data_len = static_cast<decltype(args.data_len)>(n);
    return SyscallReturnCode_OKAY;
  }

  return SyscallReturnCode_INVALID_ARGUMENT;
}

/** Writes data from `data` to an open handle. */
SyscallReturnCode handle_Write_V1(__KRN_IO_WRITE_V1 &&args) {
  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (currentThread == nullptr) return SyscallReturnCode_INVALID_ARGUMENT;
  const auto proc = currentThread->owner_process;
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;

  const auto h = static_cast<kernel::KHandle>(args.handle);
  const auto entry = proc->handles.Lookup(h);
  if (!entry) return SyscallReturnCode_INVALID_ARGUMENT;

  if (const auto file = entry->object.As<kernel::KFileObject>()) {
    const auto size_written = KE_VFS_WriteFile(file, kstd::span<const uint8_t>(args.data.get(), args.data.size()));
    args.data_len = static_cast<decltype(args.data_len)>(size_written);
    return SyscallReturnCode_OKAY;
  }

  if (const auto sink = entry->object.As<kernel::KVttySinkObject>()) {
    if (!sink->vtty) return SyscallReturnCode_INVALID_ARGUMENT;
    const auto n = sink->vtty->sink_write(args.data.get(), args.data.size());
    KE_TTY_PumpDevice(sink->vtty);
    args.data_len = static_cast<decltype(args.data_len)>(n);
    return SyscallReturnCode_OKAY;
  }

  if (const auto source = entry->object.As<kernel::KVttySourceObject>()) {
    if (!source->vtty) return SyscallReturnCode_INVALID_ARGUMENT;
    const auto n = source->vtty->source_write(args.data.get(), args.data.size());
    args.data_len = static_cast<decltype(args.data_len)>(n);
    return SyscallReturnCode_OKAY;
  }

  return SyscallReturnCode_INVALID_ARGUMENT;
}
