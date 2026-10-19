#include "stdkrn.hpp"
#include "syscalls.hpp"
#include "vmm.hpp"
#include "process.hpp"

#include <kernel/kobject/KEventObject.hpp>
#include <kernel/process_startup.hpp>
#include <kernel/kobject/KIOObject.hpp>

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

namespace {
template<typename Arguments>
SyscallReturnCode create(Arguments &args, const kernel::ProcessStreams *streams) {
  args.handle = UINT32_MAX;
  const auto proc = current_process();
  if (!proc || args.flags != 0) return SyscallReturnCode_INVALID_ARGUMENT;
  kernel::process_startup::Arguments startup;
  if (!kernel::process_startup::parse(args.path.data(), args.path.size(),
                                    args.argument.data(), args.argument.size(), startup))
    return SyscallReturnCode_INVALID_ARGUMENT;

  // Reserve the handle before starting the loader: list allocation may fail
  // silently, and a successful launch must always return a real handle.
  const auto previous_count = proc->handles.entries.size();
  const kernel::KHandle h = proc->handles.Allocate(
      nullptr,
      kernel::ProcessHandleTable::HandleAccess::Read,
      kernel::ProcessHandleTable::HandleFlags::None);
  auto *entry = proc->handles.Lookup(h);
  if (!entry || entry->object || proc->handles.entries.size() != previous_count + 1)
    return SyscallReturnCode_OUT_OF_MEMORY;
  const auto child = streams
      ? KE_USER_CreateProcessWithStreams(kstd::string_view(args.path), kstd::string_view(args.argument), *streams)
      : KE_USER_CreateProcess(kstd::string_view(args.path), kstd::string_view(args.argument));
  if (!child) {
    proc->handles.Close(h);
    return SyscallReturnCode_OUT_OF_MEMORY;
  }
  // Wait for bounded loader work only, never constructors or application exit.
  // There is no safe remote-thread cancellation API: returning a timeout here
  // could leave a running, untracked child, so readiness/failure must complete.
  const auto status = KE_SCHED_WaitForObject(
      kernel::KObjectPtr<kernel::KObject>(child->startupEvent.get()), kernel::WAIT_INFINITE);
  if (status != kernel::WaitStatus::Signalled || !child->startupReady) {
    proc->handles.Close(h);
    return SyscallReturnCode_INVALID_ARGUMENT;
  }
  entry->object = kernel::KObjectPtr<kernel::KObject>(child.get());
  args.handle = static_cast<uint32_t>(h);
  return SyscallReturnCode_OKAY;
}
}

SyscallReturnCode handle_CreateProcess_V1(__KRN_PROC_CREATEPROCESS_V1 &&args) {
  return create(args, nullptr);
}

SyscallReturnCode handle_CreateProcess_V2(__KRN_PROC_CREATEPROCESS_V2 &&args) {
  args.handle = UINT32_MAX;
  const auto proc = current_process();
  if (!proc || args.flags != 0) return SyscallReturnCode_INVALID_ARGUMENT;
  kernel::ProcessStreams streams;
  const HANDLE values[] = {args.stdinHandle, args.stdoutHandle, args.stderrHandle};
  kernel::KObjectPtr<kernel::KObject> *objects[] = {&streams.input, &streams.output, &streams.error};
  for (unsigned i = 0; i < 3; ++i) {
    const auto handle = static_cast<kernel::KHandle>(values[i]);
    const auto entry = proc->handles.Lookup(handle);
    const auto access = i == 0 ? kernel::ProcessHandleTable::HandleAccess::Read
                               : kernel::ProcessHandleTable::HandleAccess::Write;
    if (!entry || !proc->handles.HasAccess(handle, access) ||
        !entry->object.As<kernel::KIOObject>()) return SyscallReturnCode_INVALID_ARGUMENT;
    *objects[i] = entry->object;
  }
  const auto input = streams.input.As<kernel::KIOObject>();
  if (input->ops == &kernel::io::TtySinkOps && input->vtty) {
    const auto reader = KE_IO_CreateTtySinkStream(input->vtty);
    if (!reader) return SyscallReturnCode_OUT_OF_MEMORY;
    reader->blockingRead = true;
    reader->ownsTtyEndpoint = false;
    streams.input = reader;
  }
  return create(args, &streams);
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
  if (event == proc->startupEvent) proc->startupReady = true;
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
  if (const auto proc = current_process(); proc && proc->startupEvent && !proc->startupReady)
    KE_EVENT_Signal(proc->startupEvent->header, true);
  kernel::process::terminate_current(static_cast<int32_t>(args.exitCode));
}
/** Allocates `size` bytes of pages with the given protection. */
SyscallReturnCode handle_AllocatePages_V1(__KRN_MEM_ALLOCATEPAGES_V1 &&args) {
  args.address = 0;
  const auto proc = current_process();
  // i386 cannot implement write-only or no-access present pages.
  const auto flags = static_cast<uint32_t>(args.flags);
  if (!proc || !args.size || args.size > UINT32_MAX - 4095 ||
      (flags & ~7u) || !(flags & 1u)) return SyscallReturnCode_INVALID_ARGUMENT;
  void *memory = kernel::vmm::allocate_memory(proc->address_space, args.size,
                                             static_cast<kernel::vmm::ProtectFlags>(flags));
  if (!memory) return SyscallReturnCode_OUT_OF_MEMORY;
  args.address = reinterpret_cast<uintptr_t>(memory);
  return SyscallReturnCode_OKAY;
}
SyscallReturnCode handle_MapFixedPages_V1(__KRN_MEM_MAPFIXEDPAGES_V1 &&args) {
  const auto thread = KE_SCHED_GetCurrentThread();
  const auto raw_flags = static_cast<uint32_t>(args.flags);
  // i386 cannot implement write-only or no-access present pages.
  if (!thread || !thread->owner_process || (raw_flags & ~7u) || !(raw_flags & 1u))
    return SyscallReturnCode_INVALID_ARGUMENT;
  const auto flags = static_cast<kernel::vmm::ProtectFlags>(args.flags);
  return kernel::vmm::map_fixed_memory(thread->owner_process->address_space, args.address, args.size, flags)
             ? SyscallReturnCode_OKAY : SyscallReturnCode_INVALID_ADDRESS;
}
/** Frees a previously allocated page range. */
SyscallReturnCode handle_FreePages_V1(__KRN_MEM_FREEPAGES_V1 &&args) {
  const auto proc = current_process();
  if (!proc || !args.size || args.size > UINT32_MAX - 4095 || (args.address & 4095))
    return SyscallReturnCode_INVALID_ARGUMENT;
  auto &as = proc->address_space;
  const auto address = kernel::VirtualAddress(args.address);
  const size_t size = (static_cast<size_t>(args.size) + 4095) & ~size_t(4095);
  const auto *vad = as.vads.find(address);
  // Whole eager anonymous regions only; never release aliases/backing objects.
  if (!vad || vad->start != address || vad->end.value - vad->start.value != size ||
      vad->backing || vad->lock_count) return SyscallReturnCode_INVALID_ADDRESS;
  for (size_t offset = 0; offset < size; offset += 4096) {
    if (!HAL_VMM_GetPhysicalAddress(address + offset)) return SyscallReturnCode_INVALID_ADDRESS;
  }
  for (size_t offset = 0; offset < size; offset += 4096) {
    const auto page = address + offset;
    const auto physical = HAL_VMM_GetPhysicalAddress(page);
    HAL_VMM_UnmapPage(page);
    HAL_SMP_ShootdownTlb(page);
    HAL_PMM_FreeFrames(physical, 1);
  }
  KE_VMM_FreeRegion(as, address.as_ptr());
  return SyscallReturnCode_OKAY;
}
/** Changes the protection of an existing page range (e.g. mark executable). */
SyscallReturnCode handle_Protect_V1(__KRN_MEM_PROTECT_V1 &&args) {
  (void) args;
  // No partial VAD splitting/protection updates in this milestone.
  return SyscallReturnCode_INVALID_ARGUMENT;
}
