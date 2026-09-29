#include "stdkrn.hpp"
#include "syscalls.hpp"
#include "vmm.hpp"
#include "process.hpp"

#include <kernel/kobject/KEventObject.hpp>

namespace {
kernel::KProcessObject *current_process() {
  const auto thread = KE_SCHED_GetCurrentThread();
  return thread ? thread->owner_process : nullptr;
}

kstd::chrono::milliseconds to_timeout(uint32_t ms) {
  return ms == WaitTimeout_Infinite ? kernel::WAIT_INFINITE : kstd::chrono::milliseconds(ms);
}

SyscallReturnCode wait_handle(const kernel::KObjectPtr<kernel::KObject> &object, uint32_t timeoutMs) {
  switch (KE_SCHED_WaitForObject(object, to_timeout(timeoutMs))) {
    case kernel::WaitStatus::Signalled: return SyscallReturnCode_OKAY;
    case kernel::WaitStatus::Timeout: return SyscallReturnCode_TIMEOUT;
    default: return SyscallReturnCode_INVALID_ARGUMENT;
  }
}
} // namespace

SyscallReturnCode handle_CreateProcess_V1(__KRN_PROC_CREATEPROCESS_V1 &&args) {
  const auto proc = current_process();
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;

  const auto child = KE_USER_CreateProcess(kstd::string_view(args.path));
  if (!child) return SyscallReturnCode_INVALID_ARGUMENT;

  const kernel::KHandle h = proc->handles.Allocate(
      kernel::KObjectPtr<kernel::KObject>(child.get()),
      kernel::ProcessHandleTable::HandleAccess::Read,
      kernel::ProcessHandleTable::HandleFlags::None);
  args.handle = static_cast<uint32_t>(h);
  return SyscallReturnCode_OKAY;
}

/** Blocks until a waitable handle (event or process) is signalled. */
SyscallReturnCode handle_WaitForObject_V1(__KRN_PROC_WAITFOROBJECT_V1 &&args) {
  const auto proc = current_process();
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;
  const auto entry = proc->handles.Lookup(static_cast<kernel::KHandle>(args.handle));
  if (!entry) return SyscallReturnCode_INVALID_ARGUMENT;
  return wait_handle(entry->object, args.timeoutMs);
}

/** Blocks until a process exits and returns its exit code. */
SyscallReturnCode handle_WaitProcess_V1(__KRN_PROC_WAITPROCESS_V1 &&args) {
  const auto proc = current_process();
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;
  const auto entry = proc->handles.Lookup(static_cast<kernel::KHandle>(args.handle));
  if (!entry) return SyscallReturnCode_INVALID_ARGUMENT;
  const auto child = entry->object.As<kernel::KProcessObject>();
  if (!child) return SyscallReturnCode_INVALID_ARGUMENT;

  const auto ret = wait_handle(entry->object, args.timeoutMs);
  if (ret == SyscallReturnCode_OKAY) {
    args.exitCode = static_cast<uint32_t>(child->exit_code);
  }
  return ret;
}

/** Creates an event object. */
SyscallReturnCode handle_CreateEvent_V1(__KRN_PROC_CREATEEVENT_V1 &&args) {
  const auto proc = current_process();
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;
  const auto event = KE_EVENT_Create((args.flags & EventFlags_ManualReset) != 0,
                                     (args.flags & EventFlags_InitialState) != 0);
  if (!event) return SyscallReturnCode_OUT_OF_MEMORY;
  const kernel::KHandle h = proc->handles.Allocate(
      kernel::KObjectPtr<kernel::KObject>(event.get()),
      kernel::ProcessHandleTable::HandleAccess::Read | kernel::ProcessHandleTable::HandleAccess::Write,
      kernel::ProcessHandleTable::HandleFlags::None);
  args.handle = static_cast<uint32_t>(h);
  return SyscallReturnCode_OKAY;
}

/** Signals an event. */
SyscallReturnCode handle_SetEvent_V1(__KRN_PROC_SETEVENT_V1 &&args) {
  const auto proc = current_process();
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;
  const auto entry = proc->handles.Lookup(static_cast<kernel::KHandle>(args.handle));
  if (!entry) return SyscallReturnCode_INVALID_ARGUMENT;
  const auto event = entry->object.As<kernel::KEventObject>();
  if (!event) return SyscallReturnCode_INVALID_ARGUMENT;
  KE_EVENT_Signal(event->header, false);
  return SyscallReturnCode_OKAY;
}
/** Retrieves information about a running or exited process. */
SyscallReturnCode handle_GetInfo_V1(__KRN_PROC_GETINFO_V1 &&args) {
  (void) args;
  return SyscallReturnCode_UNKNOWN;
}
/** Terminates the current process with the given return code. */
SyscallReturnCode handle_Exit_V1(__KRN_PROC_EXIT_V1 &&args) {
  kernel::process::terminate_current(static_cast<int32_t>(args.exitCode));
}
/** Allocates `size` bytes of pages with the given protection. */
SyscallReturnCode handle_AllocatePages_V1(__KRN_MEM_ALLOCATEPAGES_V1 &&args) {
  (void) args;
  return SyscallReturnCode_UNKNOWN;
}
SyscallReturnCode handle_MapFixedPages_V1(__KRN_MEM_MAPFIXEDPAGES_V1 &&args) {
  const auto thread = KE_SCHED_GetCurrentThread();
  if (!thread || !thread->owner_process ||
      (static_cast<uint32_t>(args.flags) & ~7u)) return SyscallReturnCode_INVALID_ARGUMENT;
  const auto flags = static_cast<kernel::vmm::ProtectFlags>(args.flags);
  return kernel::vmm::map_fixed_memory(thread->owner_process->address_space, args.address, args.size, flags)
             ? SyscallReturnCode_OKAY : SyscallReturnCode_INVALID_ADDRESS;
}
/** Frees a previously allocated page range. */
SyscallReturnCode handle_FreePages_V1(__KRN_MEM_FREEPAGES_V1 &&args) {
  (void) args;
  return SyscallReturnCode_UNKNOWN;
}
/** Changes the protection of an existing page range (e.g. mark executable). */
SyscallReturnCode handle_Protect_V1(__KRN_MEM_PROTECT_V1 &&args) {
  (void) args;
  return SyscallReturnCode_UNKNOWN;
}
