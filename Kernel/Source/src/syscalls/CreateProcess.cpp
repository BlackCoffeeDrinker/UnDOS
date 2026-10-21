#include "stdkrn.hpp"
#include "syscalls.hpp"
#include "vmm.hpp"
#include "process.hpp"

#include <kernel/kobject/KEventObject.hpp>
#include <kernel/kobject/KIOObject.hpp>

namespace {
kernel::KProcessObject *current_process() {
  const auto thread = KE_SCHED_GetCurrentThread();
  return thread ? thread->owner_process : nullptr;
}

kstd::chrono::milliseconds to_timeout(uint32_t ms) {
  return ms == WaitTimeout_Infinite ? kernel::WAIT_INFINITE : kstd::chrono::milliseconds(ms);
}

kstd::error_code wait_handle(const kernel::KObjectPtr<kernel::KObject> &object, uint32_t timeoutMs) {
  return KE_SCHED_WaitForObject(object, to_timeout(timeoutMs));
}
} // namespace

namespace {
template<typename Arguments>
kstd::error_code create(Arguments &args, const kernel::ProcessStreams *streams) {
  args.handle = UINT32_MAX;
  const auto proc = current_process();
  if (!proc) return kernel::KernelError::NoCurrentProcess;
  if (args.flags != 0) return kernel::KernelError::InvalidArgument;
  // Reserve a handle before the loader can start a child.
  const auto reserved = proc->handles.Reserve(
      kernel::ProcessHandleTable::HandleAccess::Read,
      kernel::ProcessHandleTable::HandleFlags::None);
  if (!reserved) return reserved.error();
  const auto h = *reserved;
  auto *entry = proc->handles.Lookup(h);
  if (!entry || entry->object) {
    proc->handles.Close(h);
    return kernel::KernelError::InvalidState;
  }
  const auto created = streams
      ? KE_USER_CreateProcessWithStreams(kstd::string_view(args.path), kstd::string_view(args.argument), *streams)
      : KE_USER_CreateProcess(kstd::string_view(args.path), kstd::string_view(args.argument));
  if (!created) {
      proc->handles.Close(h);
    return created.error();
  }
  const auto &child = *created;
  // Wait for bounded loader work only, never constructors or application exit.
  // There is no safe remote-thread cancellation API: returning a timeout here
  // could leave a running, untracked child, so readiness/failure must complete.
  const auto status = KE_SCHED_WaitForObject(
      kernel::KObjectPtr<kernel::KObject>(child->startupEvent.get()), kernel::WAIT_INFINITE);
  if (status || !child->startupReady) {
    proc->handles.Close(h);
    return status ? status : kernel::KernelError::ProcessStartupFailed;
  }
  entry->object = kernel::KObjectPtr<kernel::KObject>(child.get());
  args.handle = static_cast<uint32_t>(h);
  return {};
}
}

kstd::error_code handle_CreateProcess_V1(__KRN_PROC_CREATEPROCESS_V1 &&args) {
  return create(args, nullptr);
}

kstd::error_code handle_CreateProcess_V2(__KRN_PROC_CREATEPROCESS_V2 &&args) {
  args.handle = UINT32_MAX;
  const auto proc = current_process();
  if (!proc) return kernel::KernelError::NoCurrentProcess;
  if (args.flags != 0) return kernel::KernelError::InvalidArgument;
  kernel::ProcessStreams streams;
  const HANDLE values[] = {args.stdinHandle, args.stdoutHandle, args.stderrHandle};
  kernel::KObjectPtr<kernel::KObject> *objects[] = {&streams.input, &streams.output, &streams.error};
  for (unsigned i = 0; i < 3; ++i) {
    const auto handle = static_cast<kernel::KHandle>(values[i]);
    const auto entry = proc->handles.Lookup(handle);
    const auto access = i == 0 ? kernel::ProcessHandleTable::HandleAccess::Read
                               : kernel::ProcessHandleTable::HandleAccess::Write;
    if (!entry) return kernel::KernelError::InvalidHandle;
    if (!proc->handles.HasAccess(handle, access)) return kernel::KernelError::AccessDenied;
    if (!entry->object.As<kernel::KIOObject>()) return kernel::KernelError::WrongObjectType;
    *objects[i] = entry->object;
  }
  const auto input = streams.input.As<kernel::KIOObject>();
  if (input->ops == &kernel::io::TtySinkOps && input->vtty) {
    const auto created = KE_IO_CreateTtySinkStream(input->vtty);
    if (!created) return created.error();
    const auto &reader = *created;
    reader->blockingRead = true;
    reader->ownsTtyEndpoint = false;
    streams.input = reader;
  }
  return create(args, &streams);
}

/** Blocks until a waitable handle (event or process) is signalled. */
kstd::error_code handle_WaitForObject_V1(__KRN_PROC_WAITFOROBJECT_V1 &&args) {
  const auto proc = current_process();
  if (!proc) return kernel::KernelError::NoCurrentProcess;
  const auto entry = proc->handles.Lookup(static_cast<kernel::KHandle>(args.handle));
  if (!entry) return kernel::KernelError::InvalidHandle;
  return wait_handle(entry->object, args.timeoutMs);
}

/** Blocks until a process exits and returns its exit code. */
kstd::error_code handle_WaitProcess_V1(__KRN_PROC_WAITPROCESS_V1 &&args) {
  const auto proc = current_process();
  if (!proc) return kernel::KernelError::NoCurrentProcess;
  const auto entry = proc->handles.Lookup(static_cast<kernel::KHandle>(args.handle));
  if (!entry) return kernel::KernelError::InvalidHandle;
  const auto child = entry->object.As<kernel::KProcessObject>();
  if (!child) return kernel::KernelError::WrongObjectType;

  const auto ret = wait_handle(entry->object, args.timeoutMs);
  if (!ret) {
    args.exitCode = static_cast<uint32_t>(child->exit_code);
  }
  return ret;
}

/** Creates an event object. */
kstd::error_code handle_CreateEvent_V1(__KRN_PROC_CREATEEVENT_V1 &&args) {
  const auto proc = current_process();
  if (!proc) return kernel::KernelError::NoCurrentProcess;
  const auto created = KE_EVENT_Create((args.flags & EventFlags_ManualReset) != 0,
                                     (args.flags & EventFlags_InitialState) != 0);
  if (!created) return created.error();
  const auto &event = *created;
  const auto allocated = proc->handles.Allocate(
      kernel::KObjectPtr<kernel::KObject>(event.get()),
      kernel::ProcessHandleTable::HandleAccess::Read | kernel::ProcessHandleTable::HandleAccess::Write,
      kernel::ProcessHandleTable::HandleFlags::None);
  if (!allocated) return allocated.error();
  args.handle = static_cast<uint32_t>(*allocated);
  return {};
}

/** Signals an event. */
kstd::error_code handle_SetEvent_V1(__KRN_PROC_SETEVENT_V1 &&args) {
  const auto proc = current_process();
  if (!proc) return kernel::KernelError::NoCurrentProcess;
  const auto entry = proc->handles.Lookup(static_cast<kernel::KHandle>(args.handle));
  if (!entry) return kernel::KernelError::InvalidHandle;
  const auto event = entry->object.As<kernel::KEventObject>();
  if (!event) return kernel::KernelError::WrongObjectType;
  if (event == proc->startupEvent) proc->startupReady = true;
  KE_EVENT_Signal(event->header, false);
  return {};
}
/** Retrieves information about a running or exited process. */
kstd::error_code handle_GetInfo_V1(__KRN_PROC_GETINFO_V1 &&args) {
  (void) args;
  return kernel::KernelError::NotImplemented;
}
/** Terminates the current process with the given return code. */
kstd::error_code handle_Exit_V1(__KRN_PROC_EXIT_V1 &&args) {
  if (const auto proc = current_process(); proc && proc->startupEvent && !proc->startupReady)
    KE_EVENT_Signal(proc->startupEvent->header, true);
  kernel::process::terminate_current(static_cast<int32_t>(args.exitCode));
}
/** Allocates `size` bytes of pages with the given protection. */
kstd::error_code handle_AllocatePages_V1(__KRN_MEM_ALLOCATEPAGES_V1 &&args) {
  args.address = 0;
  const auto proc = current_process();
  if (!proc) return kernel::KernelError::NoCurrentProcess;
  // i386 cannot implement write-only or no-access present pages.
  const auto flags = static_cast<uint32_t>(args.flags);
  if (!args.size || args.size > UINT32_MAX - 4095 ||
      (flags & ~7u) || !(flags & 1u)) return kernel::KernelError::InvalidArgument;
  const auto memory = kernel::vmm::allocate_memory(proc->address_space, args.size,
                                             static_cast<kernel::vmm::ProtectFlags>(flags));
  if (!memory) return memory.error();
  args.address = reinterpret_cast<uintptr_t>(*memory);
  return {};
}
kstd::error_code handle_MapFixedPages_V1(__KRN_MEM_MAPFIXEDPAGES_V1 &&args) {
  const auto thread = KE_SCHED_GetCurrentThread();
  if (!thread) return kernel::KernelError::NoCurrentThread;
  if (!thread->owner_process) return kernel::KernelError::NoCurrentProcess;
  const auto raw_flags = static_cast<uint32_t>(args.flags);
  // i386 cannot implement write-only or no-access present pages.
  if ((raw_flags & ~7u) || !(raw_flags & 1u))
    return kernel::KernelError::InvalidArgument;
  const auto flags = static_cast<kernel::vmm::ProtectFlags>(args.flags);
  return kernel::vmm::map_fixed_memory(thread->owner_process->address_space, args.address, args.size, flags);
}
/** Frees a previously allocated page range. */
kstd::error_code handle_FreePages_V1(__KRN_MEM_FREEPAGES_V1 &&args) {
  const auto proc = current_process();
  if (!proc) return kernel::KernelError::NoCurrentProcess;
  if (!args.size || args.size > UINT32_MAX - 4095 || (args.address & 4095))
    return kernel::KernelError::InvalidArgument;
  auto &as = proc->address_space;
  const auto address = kernel::VirtualAddress(args.address);
  const size_t size = (static_cast<size_t>(args.size) + 4095) & ~size_t(4095);
  const auto *vad = as.vads.find(address);
  // Whole eager anonymous regions only; never release aliases/backing objects.
  if (!vad || vad->start != address || vad->end.value - vad->start.value != size ||
      vad->backing || vad->lock_count) return kernel::KernelError::InvalidAddress;
  for (size_t offset = 0; offset < size; offset += 4096) {
    if (!HAL_VMM_GetPhysicalAddress(address + offset)) return kernel::KernelError::InvalidAddress;
  }
  for (size_t offset = 0; offset < size; offset += 4096) {
    const auto page = address + offset;
    const auto physical = HAL_VMM_GetPhysicalAddress(page);
    HAL_VMM_UnmapPage(page);
    HAL_SMP_ShootdownTlb(page);
    HAL_PMM_FreeFrames(physical, 1);
  }
  return KE_VMM_FreeRegion(as, address.as_ptr());
}
/** Changes the protection of an existing page range (e.g. mark executable). */
kstd::error_code handle_Protect_V1(__KRN_MEM_PROTECT_V1 &&args) {
  (void) args;
  // No partial VAD splitting/protection updates in this milestone.
  return kernel::KernelError::NotImplemented;
}
