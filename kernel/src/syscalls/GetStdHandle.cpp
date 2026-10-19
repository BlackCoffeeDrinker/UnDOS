#include "stdkrn.hpp"
#include "syscalls.hpp"

#include <kernel/kobject/KProcessObject.hpp>

SyscallReturnCode handle_GetStdHandle_V1(__KRN_IO_GETSTDHANDLE_V1 &&args) {
  args.handle = UINT32_MAX;

  const auto thread = KE_SCHED_GetCurrentThread();
  if (!thread || !thread->owner_process) return SyscallReturnCode_INVALID_ARGUMENT;

  kernel::StandardStream stream;
  switch (args.selector) {
    case StdHandleSelector_Stdin:
      stream = kernel::StandardStream::Stdin;
      break;
    case StdHandleSelector_Stdout:
      stream = kernel::StandardStream::Stdout;
      break;
    case StdHandleSelector_Stderr:
      stream = kernel::StandardStream::Stderr;
      break;
    default:
      return SyscallReturnCode_INVALID_ARGUMENT;
  }

  kernel::KHandle handle{};
  if (!thread->owner_process->handles.GetStandardHandle(stream, handle))
    return SyscallReturnCode_INVALID_ARGUMENT;

  args.handle = static_cast<uint32_t>(handle);
  return SyscallReturnCode_OKAY;
}
