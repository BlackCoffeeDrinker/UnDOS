
#include "elf_loader.hpp"
#include "stdkrn.hpp"
#include "vmm.hpp"
#include <kernel/kobject/KIOObject.hpp>
#include <kernel/process_startup.hpp>
#include <kernel/tty/tty_device.hpp>

#include <Kernel.hpp>


namespace {
// User stack size for the process's single (main) thread.
constexpr size_t USER_STACK_SIZE = 0x4000;// 16 KB
constexpr size_t PROCESS_NAME_TAIL = 48;

// \Processes names are "<basename>#<serial>" so repeated launches of one
// executable get distinct directory entries; argv[0] keeps the full path.
uint32_t g_next_process_serial = 1;

kstd::static_string<64> make_process_name(const kstd::string_view &exec_path) noexcept {
  size_t start = 0;
  for (size_t i = 0; i < exec_path.size(); ++i) {
    if (exec_path[i] == '/') start = i + 1;
  }
  auto base = exec_path.substr(start);
  if (base.empty()) base = exec_path;
  if (base.size() > PROCESS_NAME_TAIL) base = base.substr(base.size() - PROCESS_NAME_TAIL);
  kstd::static_string<64> name;
  kstd::format(name, "{}#{}", base, __atomic_fetch_add(&g_next_process_serial, 1u, __ATOMIC_RELAXED));
  return name;
}
}// namespace

kstd::expected<kernel::KObjectPtr<kernel::KProcessObject>, kstd::error_code> create_process(
    const kstd::string_view &exec_path, const kstd::string_view &arguments,
    const kernel::ProcessStreams *streams) noexcept {
  kernel::process_startup::Arguments startup;
  const auto parse_status = kernel::process_startup::parse(exec_path.data(), exec_path.size(),
                                                           arguments.data(), arguments.size(), startup);
  if (parse_status) return kstd::unexpected(parse_status);
  const auto startup_size = kernel::process_startup::stack_size(startup);
  if (startup_size > USER_STACK_SIZE / 2) return kstd::unexpected(kernel::KernelError::BufferTooSmall);
  // Step 1 — Create an isolated address space for the process.
  const auto process_name = make_process_name(exec_path);
  const auto process = kernel::CreateKObject<kernel::KProcessObject>(kstd::string_view(process_name));
  if (!process) {
    KE_Print("KE_USER_CreateProcess: failed to allocate KProcessObject");
    return kstd::unexpected(process.error());
  }

  // Inherit inheritable handles (e.g. standard streams) from parent process if present
  if (streams) {
    auto status = process.value()->handles.AllocateStandardHandle(kernel::StandardStream::Stdin, streams->input,
                                                                  kernel::ProcessHandleTable::HandleAccess::Read);
    if (status) return kstd::unexpected(status);
    status = process.value()->handles.AllocateStandardHandle(kernel::StandardStream::Stdout, streams->output,
                                                             kernel::ProcessHandleTable::HandleAccess::Write);
    if (status) return kstd::unexpected(status);
    status = process.value()->handles.AllocateStandardHandle(kernel::StandardStream::Stderr, streams->error,
                                                             kernel::ProcessHandleTable::HandleAccess::Write);
    if (status) return kstd::unexpected(status);
  } else if (const auto currentThread = KE_SCHED_GetCurrentThread()) {
    if (currentThread->owner_process) {
      if (const auto status = process.value()->InheritHandlesFrom(currentThread->owner_process->handles)) {
        KE_Print("KE_USER_CreateProcess: failed to inherit handles");
        return kstd::unexpected(status);
      }
    }
  }

  // If no handles were inherited, attach default console streams if available
  if (!streams && process.value()->handles.entries.empty()) {
    if (const auto defaultSink = KE_TTY_GetDefaultConsoleSink()) {
      const auto ioObj = KE_IO_FromObject(defaultSink);
      if (!ioObj) return kstd::unexpected(ioObj.error());
      const auto status = process.value()->AttachStandardStreams(*ioObj);
      if (status) {
        KE_Print("KE_USER_CreateProcess: failed to attach standard streams");
        return kstd::unexpected(status);
      }
    }
  }

  auto startup_event = KE_EVENT_Create(true, false);
  if (!startup_event) return kstd::unexpected(startup_event.error());
  process.value()->startupEvent = *startup_event;
  auto startup_handle = process.value()->handles.Allocate(
      kernel::KObjectPtr<kernel::KObject>(process.value()->startupEvent.get()),
      kernel::ProcessHandleTable::HandleAccess::Write,
      kernel::ProcessHandleTable::HandleFlags::None);
  if (!startup_handle) return kstd::unexpected(startup_handle.error());
  if (!process.value()->handles.Lookup(*startup_handle)) return kstd::unexpected(kernel::KernelError::OutOfMemory);

  if (const auto as_status = kernel::vmm::create_user_address_space(process.value()->address_space)) {
    KE_Print("KE_USER_CreateProcess: failed to create address space for {}", exec_path);
    return kstd::unexpected(as_status);
  }

  // Look up the long-lived subsystem that owns this loader, so the process
  // can later be queried for which subsystem bootstrapped it.
  const auto subsystem = KE_OB_LookupObjectOfTypeWithRoot<kernel::KSubsystemObject>(
      KE_OB_GetSubsystemDirectory(), "NativeElfLoader");

  if (!subsystem) {
    KE_Print("KE_USER_CreateProcess: failed to find NativeElfLoader subsystem for {}", exec_path);
    kernel::vmm::destroy_user_address_space(process.value()->address_space);
    return kstd::unexpected(kernel::KernelError::NotFound);
  }
  process.value()->subsystem = subsystem;

  // Step 2 — Load the Loader into the process's address space via the VFS.
  kernel::elf::LoadPolicy policy;
  policy.user_mode = true;
  policy.target_as = &process.value()->address_space;

  const auto result = kernel::elf::LoadElfFromVfs(process.value()->subsystem->loader_path, policy);
  if (!result) {
    KE_Print("KE_USER_CreateProcess: failed to load Loader for: {}", exec_path);
    kernel::vmm::destroy_user_address_space(process.value()->address_space);
    return kstd::unexpected(result.error());
  }

  const auto entry = result->entry_point;

  // A dedicated user-mode stack region, mapped USER|READ|WRITE.
  auto user_stack = kernel::vmm::allocate_memory(
      process.value()->address_space, USER_STACK_SIZE,
      kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE);
  if (!user_stack) {
    KE_Print("KE_USER_CreateProcess: failed to allocate user stack for {}", exec_path);
    kernel::vmm::destroy_user_address_space(process.value()->address_space);
    return kstd::unexpected(user_stack.error());
  }

  auto user_stack_top = kernel::VirtualAddress::from_ptr(*user_stack) + USER_STACK_SIZE;

  // The immutable startup block is above the loader's downward-growing stack.
  user_stack_top = (user_stack_top - startup_size).align_down(16);
  {
    const kernel::PhysicalAddress caller_root = HAL_VMM_GetCurrentTranslationRoot();
    const uintptr_t irq_state = HAL_CPU_SaveAndDisableInterrupts();
    HAL_VMM_SwitchAddressSpace(process.value()->address_space.translation_root);

    const auto written = kernel::process_startup::write_stack(
        user_stack_top.as_ptr(), startup_size, user_stack_top.value, startup,
        static_cast<uint32_t>(*startup_handle));

    HAL_VMM_SwitchAddressSpace(caller_root);
    HAL_CPU_RestoreInterrupts(irq_state);
    if (written) {
      kernel::vmm::destroy_user_address_space(process.value()->address_space);
      return kstd::unexpected(written);
    }
  }

  // Step 4 — Register the process under \Processes before the thread can
  // run. This entry owns the process once the creator closes its handle.
  if (const auto insert_status = KE_OB_InsertObject(KE_OB_GetProcessDirectory(), kernel::KObjectPtr<kernel::KObject>(process.value().get()))) {
    kernel::vmm::destroy_user_address_space(process.value()->address_space);
    return kstd::unexpected(insert_status);
  }

  // Step 5 — Create the process's ring-3 main thread (registered under
  // \Threads by KE_SCHED_CreateUserThread).
  const auto thread = KE_SCHED_CreateUserThread(process.value()->address_space, entry, user_stack_top);
  if (!thread) {
    const auto rollback_status = KE_OB_RemoveObject(
        KE_OB_GetProcessDirectory(), kernel::KObjectPtr<kernel::KObject>(process.value().get()));
    if (rollback_status) {
      KE_Print("KE_USER_CreateProcess: failed to roll back process registration: {}",
               rollback_status.message());
    }
    kernel::vmm::destroy_user_address_space(process.value()->address_space);
    return kstd::unexpected(thread.error());
  }

  thread.value()->owner_process = process.value().get();
  process.value()->main_thread = *thread;

  // Step 6 — Ring-3 code can now reach the kernel via int 0x80 (see
  // kernel_hal/x86_old/intr.cpp's syscall_handler / kernel/src/syscall.cpp's
  // dispatcher for SYS_EXIT/SYS_WRITE).
  return *process;
}

extern "C" UNDOS_KERNEL_API_DEF kstd::expected<kernel::KObjectPtr<kernel::KProcessObject>, kstd::error_code> KE_USER_CreateProcess(
    const kstd::string_view &exec_path, const kstd::string_view &arguments) noexcept {
  return create_process(exec_path, arguments, nullptr);
}

extern "C" UNDOS_KERNEL_API_DEF kstd::expected<kernel::KObjectPtr<kernel::KProcessObject>, kstd::error_code> KE_USER_CreateProcessWithStreams(
    const kstd::string_view &exec_path, const kstd::string_view &arguments,
    const kernel::ProcessStreams &streams) noexcept {
  return create_process(exec_path, arguments, &streams);
}
