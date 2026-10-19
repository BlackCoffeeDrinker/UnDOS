
#include "elf_loader.hpp"
#include "stdkrn.hpp"
#include "vmm.hpp"
#include <kernel/kobject/KIOObject.hpp>
#include <kernel/tty/tty_device.hpp>
#include <kernel/process_startup.hpp>

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

static kernel::KObjectPtr<kernel::KProcessObject> create_process(
    const kstd::string_view &exec_path, const kstd::string_view &arguments,
    const kernel::ProcessStreams *streams) noexcept {
  kernel::process_startup::Arguments startup;
  if (!kernel::process_startup::parse(exec_path.data(), exec_path.size(),
                                    arguments.data(), arguments.size(), startup)) return nullptr;
  const auto startup_size = kernel::process_startup::stack_size(startup);
  if (startup_size > USER_STACK_SIZE / 2) return nullptr;
  // Step 1 — Create an isolated address space for the process.
  const auto process_name = make_process_name(exec_path);
  const auto process = kernel::CreateKObject<kernel::KProcessObject>(kstd::string_view(process_name));
  if (!process) {
    KE_Print("KE_USER_CreateProcess: failed to allocate KProcessObject");
    return nullptr;
  }

  // Inherit inheritable handles (e.g. standard streams) from parent process if present
  if (streams) {
    if (!process->handles.AllocateStandardHandle(kernel::StandardStream::Stdin, streams->input,
            kernel::ProcessHandleTable::HandleAccess::Read) ||
        !process->handles.AllocateStandardHandle(kernel::StandardStream::Stdout, streams->output,
            kernel::ProcessHandleTable::HandleAccess::Write) ||
        !process->handles.AllocateStandardHandle(kernel::StandardStream::Stderr, streams->error,
            kernel::ProcessHandleTable::HandleAccess::Write)) return nullptr;
  } else if (const auto currentThread = KE_SCHED_GetCurrentThread()) {
    if (currentThread->owner_process) {
      if (!process->InheritHandlesFrom(currentThread->owner_process->handles)) {
        KE_Print("KE_USER_CreateProcess: failed to inherit handles");
        return nullptr;
      }
    }
  }

  // If no handles were inherited, attach default console streams if available
  if (!streams && process->handles.entries.empty()) {
    if (auto defaultSink = KE_TTY_GetDefaultConsoleSink()) {
      if (auto ioObj = KE_IO_FromObject(defaultSink)) {
        if (!process->AttachStandardStreams(ioObj)) {
          KE_Print("KE_USER_CreateProcess: failed to attach standard streams");
          return nullptr;
        }
      } else {
        if (!process->AttachStandardStreams(defaultSink)) {
          KE_Print("KE_USER_CreateProcess: failed to attach standard streams");
          return nullptr;
        }
      }
    }
  }

  process->startupEvent = KE_EVENT_Create(true, false);
  if (!process->startupEvent) return nullptr;
  const auto handle_count = process->handles.entries.size();
  const auto startup_handle = process->handles.Allocate(
      kernel::KObjectPtr<kernel::KObject>(process->startupEvent.get()),
      kernel::ProcessHandleTable::HandleAccess::Write,
      kernel::ProcessHandleTable::HandleFlags::None);
  if (process->handles.entries.size() != handle_count + 1 ||
      !process->handles.Lookup(startup_handle)) return nullptr;

  if (!kernel::vmm::create_user_address_space(process->address_space)) {
    KE_Print("KE_USER_CreateProcess: failed to create address space for {}", exec_path);
    return nullptr;
  }

  // Look up the long-lived subsystem that owns this loader, so the process
  // can later be queried for which subsystem bootstrapped it.
  const auto subsystem = KE_OB_LookupObjectOfTypeWithRoot<kernel::KSubsystemObject>(
      KE_OB_GetSubsystemDirectory(), "NativeElfLoader");

  if (!subsystem) {
    KE_Print("KE_USER_CreateProcess: failed to find NativeElfLoader subsystem for {}", exec_path);
    kernel::vmm::destroy_user_address_space(process->address_space);
    return nullptr;
  }
  process->subsystem = subsystem;

  // Step 2 — Load the Loader into the process's address space via the VFS.
  kernel::elf::LoadPolicy policy;
  policy.user_mode = true;
  policy.target_as = &process->address_space;

  const auto result = kernel::elf::LoadElfFromVfs(process->subsystem->loader_path, policy);
  if (!result.ok) {
    KE_Print("KE_USER_CreateProcess: failed to load Loader for: {}", exec_path);
    kernel::vmm::destroy_user_address_space(process->address_space);
    return nullptr;
  }

  const auto entry = result.entry_point;

  // A dedicated user-mode stack region, mapped USER|READ|WRITE.
  const void *user_stack = kernel::vmm::allocate_memory(
      process->address_space, USER_STACK_SIZE,
      kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE);
  if (!user_stack) {
    KE_Print("KE_USER_CreateProcess: failed to allocate user stack for {}", exec_path);
    kernel::vmm::destroy_user_address_space(process->address_space);
    return nullptr;
  }

  auto user_stack_top = kernel::VirtualAddress::from_ptr(user_stack) + USER_STACK_SIZE;

  // The immutable startup block is above the loader's downward-growing stack.
  user_stack_top = (user_stack_top - startup_size).align_down(16);
  {
    const kernel::PhysicalAddress caller_root = HAL_VMM_GetCurrentTranslationRoot();
    const uintptr_t irq_state = HAL_CPU_SaveAndDisableInterrupts();
    HAL_VMM_SwitchAddressSpace(process->address_space.translation_root);

    const bool written = kernel::process_startup::write_stack(
        user_stack_top.as_ptr(), startup_size, user_stack_top.value, startup,
        static_cast<uint32_t>(startup_handle));

    HAL_VMM_SwitchAddressSpace(caller_root);
    HAL_CPU_RestoreInterrupts(irq_state);
    if (!written) {
      kernel::vmm::destroy_user_address_space(process->address_space);
      return nullptr;
    }
  }

  // Step 4 — Register the process under \Processes before the thread can
  // run. This entry owns the process once the creator closes its handle.
  if (!KE_OB_InsertObject(KE_OB_GetProcessDirectory(), kernel::KObjectPtr<kernel::KObject>(process.get()))) {
    kernel::vmm::destroy_user_address_space(process->address_space);
    return nullptr;
  }

  // Step 5 — Create the process's ring-3 main thread (registered under
  // \Threads by KE_SCHED_CreateUserThread).
  const auto thread = KE_SCHED_CreateUserThread(process->address_space, entry, user_stack_top);
  if (!thread) {
    KE_OB_RemoveObject(KE_OB_GetProcessDirectory(), kernel::KObjectPtr<kernel::KObject>(process.get()));
    kernel::vmm::destroy_user_address_space(process->address_space);
    return nullptr;
  }

  thread->owner_process = process.get();
  process->main_thread = thread;

  // Step 6 — Ring-3 code can now reach the kernel via int 0x80 (see
  // kernel_hal/x86_old/intr.cpp's syscall_handler / kernel/src/syscall.cpp's
  // dispatcher for SYS_EXIT/SYS_WRITE).
  return process;
}

extern "C" UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KProcessObject> KE_USER_CreateProcess(
    const kstd::string_view &exec_path, const kstd::string_view &arguments) noexcept {
  return create_process(exec_path, arguments, nullptr);
}

extern "C" UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KProcessObject> KE_USER_CreateProcessWithStreams(
    const kstd::string_view &exec_path, const kstd::string_view &arguments,
    const kernel::ProcessStreams &streams) noexcept {
  return create_process(exec_path, arguments, &streams);
}
