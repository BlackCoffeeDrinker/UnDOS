#include "Scheduler.hpp"

#include <Kernel.hpp>

#include "../stdkrn.hpp"
#include "CpuContext.hpp"
#include "ReadyQueue.hpp"

// Page size published by the boot path (see kernel/src/main.cpp).
extern uint32_t g_page_size;

namespace kernel::sched {
namespace {

// Number of timer ticks a thread runs before it is rotated out (round-robin).
constexpr uint32_t DEFAULT_QUANTUM = 5;

void log_cleanup_failure(const char *operation, kstd::error_code status) noexcept {
  if (status) {
    KE_Print("{} cleanup failed: {}", operation, status.message());
  }
}

// Thread naming counter. Shared by every CPU, so bumped atomically.
uint32_t g_next_id = 0;

uint32_t next_thread_id() noexcept {
  return HAL_ATOMIC_FetchAdd32(
      VirtualAddress::from_ptr(&g_next_id), 1, PlatformMemoryOrder::Relaxed);
}

// Common entry point for every freshly created thread. The HAL frames a new
// stack so the first switch into it "returns" here. The owning CPU's `current`
// already points at this thread (set by schedule_next before the switch).
void thread_trampoline() {
  // We arrived here from a context switch performed with interrupts disabled;
  // a running thread must have them enabled.
  HAL_CPU_EnableInterrupts();

  const KObjectPtr<KThreadObject> self = cpu::current_context().current;
  if (self && self->entry) {
    self->entry(self->argument);
  }
  KE_SCHED_Terminate();
}

// The idle thread: run only when nothing else is ready; halt until the next
// interrupt, then offer the CPU back to the scheduler.
void idle_entry(void *) {
  for (;;) {
    HAL_CPU_Halt();
    KE_SCHED_Yield();
  }
}

// Create an idle thread pinned to `cpu_id`: lowest priority, so it is only
// picked when that CPU's queue holds nothing else.
kstd::expected<KObjectPtr<KThreadObject>, kstd::error_code> create_idle_thread(const uint32_t cpu_id) noexcept {
  auto idle = KE_SCHED_CreateThread(cfunc<void(void *)>(&idle_entry), nullptr, 0);
  if (!idle) {
    KE_Print("Failed to create thread idle: {}", idle.error().message());
    return kstd::unexpected(idle.error());
  }

  // KE_SCHED_CreateThread placed it via pick_target_cpu(); move it onto the
  // CPU it must idle for.
  for (size_t i = 0; i < cpu::contexts().size(); ++i) {
    auto &ctx = cpu::contexts()[i];
    const uintptr_t state = ctx.ready_lock.lock_irqsave();
    ctx.ready.remove(idle->get());
    ctx.ready_lock.unlock_irqrestore(state);
  }
  enqueue_on(cpu_id, idle->get());
  return *idle;
}

}// namespace

// Allocate a fresh, page-backed kernel stack. Returns the allocation base, or a
// null VirtualAddress on failure. Mirrors kernel::vmm::allocate_memory but
// keeps the pages kernel-only (no USER flag).
kstd::expected<VirtualAddress, kstd::error_code> alloc_kernel_stack(size_t size) noexcept {
  const size_t ps = g_page_size;
  if (size == 0 || ps == 0 || size > SIZE_MAX - (ps - 1)) {
    return kstd::unexpected(KernelError::InvalidSize);
  }
  const size_t pages = (size + ps - 1) / ps;
  const size_t bytes = pages * ps;

  // One dedicated BackingObject (+ VAD) per stack, registered under \Memory
  // so it is inspectable via the Object Manager.
  static uint32_t g_next_stack_id = 0;
  kstd::static_string<64> name;
  kstd::format(name, "KernelStack{}",
               HAL_ATOMIC_FetchAdd32(VirtualAddress::from_ptr(&g_next_stack_id), 1,
                                     PlatformMemoryOrder::Relaxed));

  const auto backing = kernel::CreateKObject<kernel::KMemoryBackingObject>(name);
  if (!backing) return kstd::unexpected(backing.error());
  backing.value()->kind = kernel::KMemoryBackingObject::Kind::Anonymous;
  backing.value()->size = bytes;
  if (const kstd::error_code insert_status = KE_OB_InsertObject(KE_OB_GetMemoryDirectory(), *backing)) {
    return kstd::unexpected(insert_status);
  }

  auto &kas = KE_VMM_GetKernelAddressSpace();
  auto vad = KE_VMM_MapBackingObject(kas, *backing, bytes, vmm::ProtectFlags::READ | vmm::ProtectFlags::WRITE);
  if (!vad) {
    log_cleanup_failure("kernel stack backing removal", KE_OB_RemoveObject(KE_OB_GetMemoryDirectory(), *backing));
    return kstd::unexpected(vad.error());
  }

  const PhysicalAddress phys = HAL_PMM_AllocateFrames(pages);
  if (!phys) {
    log_cleanup_failure("kernel stack region release", KE_VMM_FreeRegion(kas, (*vad)->start.as_ptr()));
    log_cleanup_failure("kernel stack backing removal", KE_OB_RemoveObject(KE_OB_GetMemoryDirectory(), *backing));
    return kstd::unexpected(KernelError::OutOfMemory);
  }

  const VirtualAddress base = (*vad)->start;
  for (size_t i = 0; i < pages; ++i) {
    if (!HAL_VMM_MapPage(base + i * ps, phys + i * ps, vmm::ProtectFlags::READ | vmm::ProtectFlags::WRITE)) {
      KE_Print("Kernel stack mapping failed at {x}: existing frame {x}, requested frame {x}",
               (base + i * ps).value, HAL_VMM_GetPhysicalAddress(base + i * ps).value, (phys + i * ps).value);
      for (size_t mapped = 0; mapped < i; ++mapped) {
        const auto page = base + mapped * ps;
        HAL_VMM_UnmapPage(page);
        HAL_SMP_ShootdownTlb(page);
      }
      HAL_PMM_FreeFrames(phys, pages);
      log_cleanup_failure("kernel stack region release", KE_VMM_FreeRegion(kas, base.as_ptr()));
      log_cleanup_failure("kernel stack backing removal", KE_OB_RemoveObject(KE_OB_GetMemoryDirectory(), *backing));
      return kstd::unexpected(KernelError::DeviceError);
    }
    HAL_SMP_ShootdownTlb(base + i * ps);
  }
  return base;
}

uint32_t pick_target_cpu() noexcept {
  uint32_t best = 0;
  size_t best_load = ~size_t(0);
  bool found = false;

  auto &all = cpu::contexts();
  for (size_t i = 0; i < all.size(); ++i) {
    auto &ctx = all[i];
    if (!ctx.online) continue;

    const uintptr_t state = ctx.ready_lock.lock_irqsave();
    const size_t load = ctx.ready.size();
    ctx.ready_lock.unlock_irqrestore(state);

    if (!found || load < best_load) {
      found = true;
      best_load = load;
      best = static_cast<uint32_t>(i);
    }
  }

  // Before init_bsp marks CPU 0 online, everything lands on CPU 0 anyway.
  return found ? best : 0;
}

void enqueue_on(const uint32_t cpu_id, KThreadObject *thread) noexcept {
  if (!thread) return;

  const uint32_t target = cpu_id < cpu::contexts().size() ? cpu_id : 0;
  auto &ctx = cpu::contexts()[target];

  const uintptr_t state = ctx.ready_lock.lock_irqsave();
  ctx.ready.enqueue(thread);
  ctx.ready_lock.unlock_irqrestore(state);

  // A CPU only looks at its own queue, so a remote CPU has to be told there
  // is new work waiting for it.
  if (static_cast<size_t>(target) != cpu::current_id()) {
    HAL_SMP_SendRescheduleIpi(target);
  }
}

// Core scheduling decision + context switch. MUST be called with interrupts
// disabled. Picks the highest-priority thread on THIS CPU's queue, re-queues
// the outgoing thread if it is still runnable, and swaps CPU context. When
// there is nothing else to run, the current thread simply keeps the CPU.
void schedule_next() noexcept {
  auto &ctx = cpu::current_context();
  KThreadObject *prev = ctx.current.get();

  // Pick and re-queue under the queue lock, but drop it before switching:
  // the outgoing thread resumes inside this function much later, and must
  // not be holding a lock while it is descheduled.
  const uintptr_t state = ctx.ready_lock.lock_irqsave();
  KThreadObject *next = ctx.ready.pop_next();
  if (next && prev && prev->state == KThreadObject::State::Running) {
    prev->state = KThreadObject::State::Ready;
    ctx.ready.enqueue(prev);
  }
  ctx.ready_lock.unlock_irqrestore(state);

  if (!next) {
    // Nothing else ready on this CPU: keep running the current thread.
    if (prev) prev->quantum_ticks = DEFAULT_QUANTUM;
    return;
  }

  next->state = KThreadObject::State::Running;
  next->quantum_ticks = DEFAULT_QUANTUM;

  // Publish the new current thread before switching, so the incoming context
  // (including a brand-new thread's trampoline) observes itself as current.
  ctx.current = KObjectPtr(next);

  // User threads carry their own translation root; switch address spaces first.
  if (next->translation_root != PhysicalAddress(0) &&
      HAL_VMM_GetCurrentTranslationRoot() != next->translation_root) {
    HAL_VMM_SwitchAddressSpace(next->translation_root);
  }

  // Keep the TSS esp0 pointed at the incoming thread's own kernel stack, so a
  // ring-3 -> ring-0 transition (syscall, IRQ) while it is running lands on
  // the right stack rather than whichever thread ran last. The outgoing
  // thread's live value is saved first: it may differ from its stack top
  // (e.g. the v86 monitor's private trap stack during a BIOS call), and
  // restoring a stale value on resume would let the next trap scribble
  // over arbitrary memory.
  if (prev && prev != next) {
    prev->kernel_stack_esp0 = HAL_CPU_GetKernelStack();
  }
  HAL_CPU_SetKernelStack(next->kernel_stack_esp0);

  if (prev == next) {
    return;
  }

  // Save prev's SP into its TCB and load next's saved SP. When prev is later
  // rescheduled, control resumes right here.
  HAL_CPU_SwitchContext(prev->kernel_stack_top, next->kernel_stack_top);
}

void init_bsp() noexcept {
  cpu::init_contexts();

  auto &ctx = cpu::current_context();
  ctx.online = true;

  // Adopt the currently-running execution stream as the bootstrap thread. Its
  // saved stack pointer is captured automatically by the first context switch,
  // so no stack needs to be framed here.
  if (const auto boot = CreateKObject<KThreadObject>("thread0")) {
    (*boot)->state = KThreadObject::State::Running;
    (*boot)->priority = 1;
    (*boot)->quantum_ticks = DEFAULT_QUANTUM;
    (*boot)->translation_root = PhysicalAddress(0);
    ctx.current = *boot;
    HAL_ATOMIC_Store32(VirtualAddress::from_ptr(&g_next_id), 1, PlatformMemoryOrder::Relaxed);

    // Register the bootstrap thread now that its directory exists.
    const auto status = KE_OB_InsertObject(KE_OB_GetThreadsDirectory(), KObjectPtr<KObject>(ctx.current.get()));
    if (status) HAL_PLATFORM_Panic("Failed to register bootstrap thread", __FILE__, __LINE__);
  } else {
    // Panic
    HAL_PLATFORM_Panic("Failed to create bootstrap thread", __FILE__, __LINE__);
  }

  auto idle = create_idle_thread(ctx.id);
  if (!idle) {
    KE_Print("Failed to create thread idle: {}", idle.error().message());
    HAL_PLATFORM_Panic("Failed to create BSP idle thread", __FILE__, __LINE__);
  }
  ctx.idle = *idle;
}

void init_ap(const uint32_t cpu_id) noexcept {
  auto &ctx = cpu::contexts()[cpu_id];
  ctx.online = true;

  // Same trick as the BSP: the stream this AP arrived on becomes its first
  // thread, so a context switch away from it has somewhere to save state.
  kstd::static_string<64> name;
  kstd::format(name, "cpu{}boot", cpu_id);
  if (const auto boot = CreateKObject<KThreadObject>(name)) {
    (*boot)->state = KThreadObject::State::Running;
    (*boot)->priority = 1;
    (*boot)->quantum_ticks = DEFAULT_QUANTUM;
    (*boot)->translation_root = PhysicalAddress(0);
    ctx.current = *boot;
    const auto status = KE_OB_InsertObject(KE_OB_GetThreadsDirectory(), KObjectPtr<KObject>(ctx.current.get()));
    if (status) HAL_PLATFORM_Panic("Failed to register AP bootstrap thread", __FILE__, __LINE__);
  } else {
    HAL_PLATFORM_Panic("Failed to create bootstrap thread for CPU", __FILE__, __LINE__);
  }

  auto idle = create_idle_thread(cpu_id);
  if (!idle) HAL_PLATFORM_Panic("Failed to create AP idle thread", __FILE__, __LINE__);
  ctx.idle = *idle;
}

void tick() noexcept {
  auto &ctx = cpu::current_context();
  ++ctx.ticks;

  KThreadObject *cur = ctx.current.get();
  if (!cur) return;

  if (cur->quantum_ticks > 0) {
    --cur->quantum_ticks;
  }
  if (cur->quantum_ticks == 0) {
    // Called from the timer ISR: interrupts are already disabled here.
    schedule_next();
  }
}

void block_current_locked() noexcept {
  if (const auto &ctx = cpu::current_context(); ctx.current) {
    ctx.current->state = KThreadObject::State::Blocked;
  }
  schedule_next();
}

void ready_locked(KThreadObject *thread) noexcept {
  if (thread && thread->state == KThreadObject::State::Blocked) {
    thread->state = KThreadObject::State::Ready;
    enqueue_on(pick_target_cpu(), thread);
  }
}
}// namespace kernel::sched

using namespace kernel;

UNDOS_KERNEL_API_DEF kstd::expected<KObjectPtr<KThreadObject>, kstd::error_code>
KE_SCHED_CreateThread(cfunc<void(void *)> entry, void *arg, uint8_t priority) noexcept {
  kstd::static_string<64> name;
  kstd::format(name, "thread{}", sched::next_thread_id());

  auto t = CreateKObject<KThreadObject>(name);
  if (!t) {
    KE_Print("Failed to create thread object: {}", t.error().message());
    return kstd::unexpected(t.error());
  }

  auto stack = sched::alloc_kernel_stack(sched::KERNEL_STACK_SIZE);
  if (!stack) {
    KE_Print("Failed to allocate kernel stack: {}", stack.error().message());
    return kstd::unexpected(stack.error());
  }
  const VirtualAddress base = *stack;

  const VirtualAddress top = base + sched::KERNEL_STACK_SIZE;

  t.value()->entry = entry;
  t.value()->argument = arg;
  t.value()->priority = priority;
  t.value()->state = KThreadObject::State::Ready;
  t.value()->translation_root = PhysicalAddress(0);
  t.value()->kernel_stack_base = base;
  t.value()->kernel_stack_esp0 = top;
  t.value()->kernel_stack_top = HAL_CPU_InitThreadContext(top, cfunc<void()>(&sched::thread_trampoline));

  if (const auto status = KE_OB_InsertObject(KE_OB_GetThreadsDirectory(), KObjectPtr<KObject>(t->get()))) {
    for (size_t i = 0; i < sched::KERNEL_STACK_SIZE / g_page_size; ++i) {
      const auto page = base + i * g_page_size;
      const auto phys = HAL_VMM_GetPhysicalAddress(page);
      HAL_VMM_UnmapPage(page);
      if (phys) HAL_PMM_FreeFrames(phys, 1);
    }
    sched::log_cleanup_failure("thread stack region release",
                               KE_VMM_FreeRegion(KE_VMM_GetKernelAddressSpace(), base.as_ptr()));

    KE_Print("Failed to insert thread object: {}", status.message());
    return kstd::unexpected(status);
  }

  sched::enqueue_on(sched::pick_target_cpu(), t->get());

  return *t;
}

UNDOS_KERNEL_API_DEF kstd::expected<KObjectPtr<KThreadObject>, kstd::error_code>
KE_SCHED_CreateUserThread(const vmm::AddressSpace &as, VirtualAddress user_entry, VirtualAddress user_stack_top) noexcept {
  kstd::static_string<64> name;
  kstd::format(name, "uthread{}", sched::next_thread_id());

  auto t = CreateKObject<KThreadObject>(name);
  if (!t) return kstd::unexpected(t.error());

  // The thread still needs its own kernel stack: ring-3 -> ring-0 transitions
  // (syscalls, IRQs, exceptions) land on it via the TSS's esp0.
  auto stack = sched::alloc_kernel_stack(sched::KERNEL_STACK_SIZE);
  if (!stack) return kstd::unexpected(stack.error());
  const VirtualAddress base = *stack;

  const VirtualAddress top = base + sched::KERNEL_STACK_SIZE;

  t.value()->priority = 1;
  t.value()->state = KThreadObject::State::Ready;
  t.value()->translation_root = as.translation_root;
  t.value()->kernel_stack_base = base;
  t.value()->kernel_stack_esp0 = top;
  t.value()->kernel_stack_top = HAL_CPU_InitUserThreadContext(top, user_entry, user_stack_top);

  const auto status = KE_OB_InsertObject(KE_OB_GetThreadsDirectory(), KObjectPtr<KObject>(t->get()));
  if (status) {
    for (size_t i = 0; i < sched::KERNEL_STACK_SIZE / g_page_size; ++i) {
      const auto page = base + i * g_page_size;
      const auto phys = HAL_VMM_GetPhysicalAddress(page);
      HAL_VMM_UnmapPage(page);
      if (phys) HAL_PMM_FreeFrames(phys, 1);
    }
    sched::log_cleanup_failure("user thread stack region release",
                               KE_VMM_FreeRegion(KE_VMM_GetKernelAddressSpace(), base.as_ptr()));
    return kstd::unexpected(status);
  }

  sched::enqueue_on(sched::pick_target_cpu(), t->get());

  return *t;
}

UNDOS_KERNEL_API_DEF void KE_SCHED_Yield() noexcept {
  const uintptr_t state = HAL_CPU_SaveAndDisableInterrupts();
  sched::schedule_next();
  HAL_CPU_RestoreInterrupts(state);
}

UNDOS_KERNEL_API_DEF void KE_SCHED_Block() noexcept {
  const uintptr_t state = HAL_CPU_SaveAndDisableInterrupts();
  sched::block_current_locked();
  HAL_CPU_RestoreInterrupts(state);
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_SCHED_Unblock(const KObjectPtr<KThreadObject> &thread) noexcept {
  if (!thread) return KernelError::InvalidArgument;
  const uintptr_t state = HAL_CPU_SaveAndDisableInterrupts();
  sched::ready_locked(thread.get());
  HAL_CPU_RestoreInterrupts(state);
  return KernelError::Success;
}

UNDOS_KERNEL_API_DEF [[noreturn]] void KE_SCHED_Terminate() noexcept {
  // Nothing to restore: this thread never runs again.
  (void) HAL_CPU_SaveAndDisableInterrupts();
  const auto &ctx = cpu::current_context();
  if (ctx.current) {
    ctx.current->state = KThreadObject::State::Terminated;
  }
  sched::schedule_next();

  // schedule_next never returns to a terminated thread; guard just in case.
  for (;;) {
    HAL_CPU_Halt();
  }
}

UNDOS_KERNEL_API_DEF KObjectPtr<KThreadObject> KE_SCHED_GetCurrentThread() noexcept {
  return cpu::current_context().current;
}
