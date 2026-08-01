#include "stdkrn.hpp"
#include "syscalls.hpp"

SyscallReturnCode handle_Open_V1(__KRN_FS_OPEN_V1 &&args) {
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

  if (const auto file = KE_VFS_OpenFile(args.path, mode)) {
    // 2. Insert into process handle table
    kernel::KHandle h = proc->handles.Allocate(
        file,
        kernel::ProcessHandleTable::HandleAccess::Read |
            kernel::ProcessHandleTable::HandleAccess::Write,
        kernel::ProcessHandleTable::HandleFlags::None);

    args.fileDescriptor = static_cast<uint32_t>(h);
    return SyscallReturnCode_OKAY;
  }

  return SyscallReturnCode_INVALID_ARGUMENT;
}

SyscallReturnCode handle_Close_V1(__KRN_FS_CLOSE_V1 &&args) {
  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (currentThread == nullptr) return SyscallReturnCode_INVALID_ARGUMENT;
  const auto proc = currentThread->owner_process;
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;

  const auto h = static_cast<kernel::KHandle>(args.fileDescriptor);
  const auto entry = proc->handles.Lookup(h);
  if (!entry) return SyscallReturnCode_INVALID_ARGUMENT;

  if (auto file = entry->object.As<kernel::KFileObject>();
      file && !file->isClosed) {
    file->isClosed = true;
    KE_VFS_CloseFile(kstd::move(file));
  }

  if (proc->handles.Close(h)) {
    return SyscallReturnCode_OKAY;
  }

  return SyscallReturnCode_INVALID_ARGUMENT;
}

/** Reads data from an open file descriptor into `data`. */
SyscallReturnCode handle_Read_V1(__KRN_FS_READ_V1 &&args) {
  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (currentThread == nullptr) return SyscallReturnCode_INVALID_ARGUMENT;
  const auto proc = currentThread->owner_process;
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;

  const auto h = static_cast<kernel::KHandle>(args.fileDescriptor);
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

  return SyscallReturnCode_INVALID_ARGUMENT;
}

/** Writes data from `data` to an open file descriptor. */
SyscallReturnCode handle_Write_V1(__KRN_FS_WRITE_V1 &&args) {
  (void) args;
  return SyscallReturnCode_INVALID_ARGUMENT;
}
