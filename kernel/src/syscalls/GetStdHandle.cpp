#include "stdkrn.hpp"
#include "syscalls.hpp"

#include <kernel/kobject/KProcessObject.hpp>

kstd::error_code handle_GetStdHandle_V1(__KRN_IO_GETSTDHANDLE_V1 &&args) {
  args.handle = UINT32_MAX;

  const auto thread = KE_SCHED_GetCurrentThread();
  if (!thread) return kernel::KernelError::NoCurrentThread;
  if (!thread->owner_process) return kernel::KernelError::NoCurrentProcess;

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
      return kernel::KernelError::InvalidArgument;
  }

  kernel::KHandle handle{};
  if (const auto error = thread->owner_process->handles.GetStandardHandle(stream, handle);
      error) return error;

  args.handle = static_cast<uint32_t>(handle);
  return {};
}
