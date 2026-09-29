#include "stdkrn.hpp"
#include "syscalls.hpp"
#include <kernel/kobject/KIOObject.hpp>
#include <kernel/kobject/KVttyObject.hpp>
#include <kernel/object_manager.hpp>

SyscallReturnCode handle_Create_V1(__KRN_TTY_CREATE_V1 &&args) {
  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (currentThread == nullptr) return SyscallReturnCode_INVALID_ARGUMENT;
  const auto proc = currentThread->owner_process;
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;

  kernel::tty::VttyPtr vtty(new kernel::tty::KVtty());
  if (!vtty) return SyscallReturnCode_OUT_OF_MEMORY;

  auto ioSource = KE_IO_CreateTtySourceStream(vtty, "TtySource");
  auto ioSink = KE_IO_CreateTtySinkStream(vtty, "TtySink");
  if (!ioSource || !ioSink) return SyscallReturnCode_OUT_OF_MEMORY;

  kernel::KHandle sourceHandle = proc->handles.Allocate(
      ioSource,
      kernel::ProcessHandleTable::HandleAccess::Read | kernel::ProcessHandleTable::HandleAccess::Write,
      kernel::ProcessHandleTable::HandleFlags::Inheritable);
  kernel::KHandle sinkHandle = proc->handles.Allocate(
      ioSink,
      kernel::ProcessHandleTable::HandleAccess::Read | kernel::ProcessHandleTable::HandleAccess::Write,
      kernel::ProcessHandleTable::HandleFlags::Inheritable);

  args.sourceHandle = static_cast<uint32_t>(sourceHandle);
  args.sinkHandle = static_cast<uint32_t>(sinkHandle);
  return SyscallReturnCode_OKAY;
}

SyscallReturnCode handle_SetConfig_V1(__KRN_TTY_SETCONFIG_V1 &&args) {
  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (currentThread == nullptr) return SyscallReturnCode_INVALID_ARGUMENT;
  const auto proc = currentThread->owner_process;
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;

  const auto h = static_cast<kernel::KHandle>(args.handle);
  const auto entry = proc->handles.Lookup(h);
  if (!entry) return SyscallReturnCode_INVALID_ARGUMENT;

  kernel::tty::VttyPtr vtty;
  if (auto io = entry->object.As<kernel::KIOObject>()) {
    vtty = io->vtty;
  } else if (auto source = entry->object.As<kernel::KVttySourceObject>()) {
    vtty = source->vtty;
  } else if (auto sink = entry->object.As<kernel::KVttySinkObject>()) {
    vtty = sink->vtty;
  } else {
    return SyscallReturnCode_INVALID_ARGUMENT;
  }

  if (!vtty) return SyscallReturnCode_INVALID_ARGUMENT;

  kernel::tty::TtyConfig cfg = vtty->get_config();
  cfg.flags = static_cast<uint32_t>(args.flags);
  cfg.size.ws_row = args.rows;
  cfg.size.ws_col = args.cols;
  vtty->set_config(cfg);
  return SyscallReturnCode_OKAY;
}

SyscallReturnCode handle_GetConfig_V1(__KRN_TTY_GETCONFIG_V1 &&args) {
  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (currentThread == nullptr) return SyscallReturnCode_INVALID_ARGUMENT;
  const auto proc = currentThread->owner_process;
  if (!proc) return SyscallReturnCode_INVALID_ARGUMENT;

  const auto h = static_cast<kernel::KHandle>(args.handle);
  const auto entry = proc->handles.Lookup(h);
  if (!entry) return SyscallReturnCode_INVALID_ARGUMENT;

  kernel::tty::VttyPtr vtty;
  if (auto io = entry->object.As<kernel::KIOObject>()) {
    vtty = io->vtty;
  } else if (auto source = entry->object.As<kernel::KVttySourceObject>()) {
    vtty = source->vtty;
  } else if (auto sink = entry->object.As<kernel::KVttySinkObject>()) {
    vtty = sink->vtty;
  } else {
    return SyscallReturnCode_INVALID_ARGUMENT;
  }

  if (!vtty) return SyscallReturnCode_INVALID_ARGUMENT;

  kernel::tty::TtyConfig cfg = vtty->get_config();
  args.flags = static_cast<enum TtyModeFlags>(cfg.flags);
  args.rows = cfg.size.ws_row;
  args.cols = cfg.size.ws_col;
  return SyscallReturnCode_OKAY;
}
