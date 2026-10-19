#include "stdkrn.hpp"
#include "syscalls.hpp"
#include <kernel/kobject/KIOObject.hpp>
#include <kernel/kobject/KVttyObject.hpp>
#include <kernel/object_manager.hpp>

kernel::KernelError handle_Create_V1(__KRN_TTY_CREATE_V1 &&args) {
  args.sourceHandle = args.sinkHandle = UINT32_MAX;
  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (currentThread == nullptr) return kernel::KernelError::NoCurrentThread;
  const auto proc = currentThread->owner_process;
  if (!proc) return kernel::KernelError::NoCurrentProcess;

  const kernel::tty::VttyPtr vtty(new kernel::tty::KVtty());
  if (!vtty) return kernel::KernelError::OutOfMemory;

  const auto ioSource = KE_IO_CreateTtySourceStream(vtty, "TtySource");
  if (!ioSource) return ioSource.error();
  const auto ioSink = KE_IO_CreateTtySinkStream(vtty, "TtySink");
  if (!ioSink) return ioSink.error();

  const auto sourceHandle = proc->handles.Allocate(
      *ioSource,
      kernel::ProcessHandleTable::HandleAccess::Read | kernel::ProcessHandleTable::HandleAccess::Write,
      kernel::ProcessHandleTable::HandleFlags::Inheritable);
  if (!sourceHandle) return sourceHandle.error();
  const auto sinkHandle = proc->handles.Allocate(
      *ioSink,
      kernel::ProcessHandleTable::HandleAccess::Read | kernel::ProcessHandleTable::HandleAccess::Write,
      kernel::ProcessHandleTable::HandleFlags::Inheritable);

  if (!sinkHandle) {
    proc->handles.Close(*sourceHandle);
    return sinkHandle.error();
  }
  args.sourceHandle = static_cast<uint32_t>(*sourceHandle);
  args.sinkHandle = static_cast<uint32_t>(*sinkHandle);
  return kernel::KernelError::Success;
}

kernel::KernelError handle_SetConfig_V1(__KRN_TTY_SETCONFIG_V1 &&args) {
  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (currentThread == nullptr) return kernel::KernelError::NoCurrentThread;
  const auto proc = currentThread->owner_process;
  if (!proc) return kernel::KernelError::NoCurrentProcess;

  const auto h = static_cast<kernel::KHandle>(args.handle);
  const auto entry = proc->handles.Lookup(h);
  if (!entry) return kernel::KernelError::InvalidHandle;

  kernel::tty::VttyPtr vtty;
  if (const auto io = entry->object.As<kernel::KIOObject>()) {
    vtty = io->vtty;
  } else if (const auto source = entry->object.As<kernel::KVttySourceObject>()) {
    vtty = source->vtty;
  } else if (const auto sink = entry->object.As<kernel::KVttySinkObject>()) {
    vtty = sink->vtty;
  } else {
    return kernel::KernelError::WrongObjectType;
  }

  if (!vtty) return kernel::KernelError::WrongObjectType;

  kernel::tty::TtyConfig cfg = vtty->get_config();
  cfg.flags = static_cast<uint32_t>(args.flags);
  cfg.size.ws_row = args.rows;
  cfg.size.ws_col = args.cols;
  vtty->set_config(cfg);
  return kernel::KernelError::Success;
}

kernel::KernelError handle_GetConfig_V1(__KRN_TTY_GETCONFIG_V1 &&args) {
  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (currentThread == nullptr) return kernel::KernelError::NoCurrentThread;
  const auto proc = currentThread->owner_process;
  if (!proc) return kernel::KernelError::NoCurrentProcess;

  const auto h = static_cast<kernel::KHandle>(args.handle);
  const auto entry = proc->handles.Lookup(h);
  if (!entry) return kernel::KernelError::InvalidHandle;

  kernel::tty::VttyPtr vtty;
  if (const auto io = entry->object.As<kernel::KIOObject>()) {
    vtty = io->vtty;
  } else if (const auto source = entry->object.As<kernel::KVttySourceObject>()) {
    vtty = source->vtty;
  } else if (const auto sink = entry->object.As<kernel::KVttySinkObject>()) {
    vtty = sink->vtty;
  } else {
    return kernel::KernelError::WrongObjectType;
  }

  if (!vtty) return kernel::KernelError::WrongObjectType;

  kernel::tty::TtyConfig cfg = vtty->get_config();
  args.flags = static_cast<enum TtyModeFlags>(cfg.flags);
  args.rows = cfg.size.ws_row;
  args.cols = cfg.size.ws_col;
  return kernel::KernelError::Success;
}
