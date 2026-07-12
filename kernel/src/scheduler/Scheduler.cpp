#include "Scheduler.hpp"

#include <Kernel.hpp>

#include "../stdkrn.hpp"
#include "ReadyQueue.hpp"

// Page size published by the boot path (see kernel/src/main.cpp).
extern uint32_t g_page_size;

namespace kernel::sched {
namespace {

// Number of timer ticks a thread runs before it is rotated out (round-robin).
constexpr uint32_t DEFAULT_QUANTUM = 5;

// Per-thread kernel stack size. Larger than a single SLAB slot, so stacks are
// backed by real frames mapped into the kernel address space rather than
// KE_Malloc'd.
constexpr size_t KERNEL_STACK_SIZE = 16 * 1024;

ReadyQueue g_ready;
KObjectPtr<KThreadObject> g_current;
KObjectPtr<KThreadObject> g_idle;
uint32_t g_next_id = 0;

// Allocate a fresh, page-backed kernel stack. Returns the allocation base, or a
// null VirtualAddress on failure. Mirrors kernel::vmm::allocate_user_memory but
// keeps the pages kernel-only (no USER flag).
VirtualAddress alloc_kernel_stack(size_t size) noexcept {
  const size_t ps = g_page_size;
  const size_t pages = (size + ps - 1) / ps;
  const size_t bytes = pages * ps;

  auto &kas = KE_VMM_GetKernelAddressSpace();
  const void *region = KE_VMM_AllocateRegion(kas, bytes, vmm::ProtectFlags::READ | vmm::ProtectFlags::WRITE);
  if (!region) return {0};

  const PhysicalAddress phys = HAL_PMM_AllocateFrames(pages);
  if (!phys) return {0};

  const VirtualAddress base = VirtualAddress::from_ptr(region);
  for (size_t i = 0; i < pages; ++i) {
    if (!HAL_VMM_MapPage(base + i * ps, phys + i * ps, vmm::ProtectFlags::READ | vmm::ProtectFlags::WRITE)) {
      return {0};
    }
    HAL_VMM_Flush(base + i * ps);
  }
  return base;
}

// Common entry point for every freshly created thread. The HAL frames a new
// stack so the first switch into it "returns" here. g_current already points at
// this thread (set by schedule_next before the switch).
void thread_trampoline() {
  // We arrived here from a context switch performed with interrupts disabled;
  // a running thread must have them enabled.
  HAL_CPU_EnableInterrupts();

  const KObjectPtr<KThreadObject> self = g_current;
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

// Core scheduling decision + context switch. MUST be called with interrupts
// disabled. Picks the highest-priority ready thread, re-queues the outgoing
// thread if it is still runnable, and swaps CPU context. When there is nothing
// else to run, the current thread simply keeps the CPU.
void schedule_next() noexcept {
  KThreadObject *prev = g_current.get();

  KThreadObject *next = g_ready.pop_next();
  if (!next) {
    // Nothing else ready: keep running the current thread.
    if (prev) prev->quantum_ticks = DEFAULT_QUANTUM;
    return;
  }

  if (prev && prev->state == KThreadObject::State::Running) {
    prev->state = KThreadObject::State::Ready;
    g_ready.enqueue(prev);
  }

  next->state = KThreadObject::State::Running;
  next->quantum_ticks = DEFAULT_QUANTUM;

  // Publish the new current thread before switching, so the incoming context
  // (including a brand-new thread's trampoline) observes itself as current.
  g_current = KObjectPtr(next);

  // User threads carry their own translation root; switch address spaces first.
  if (next->translation_root != PhysicalAddress(0) &&
      HAL_VMM_GetCurrentTranslationRoot() != next->translation_root) {
    HAL_VMM_SwitchAddressSpace(next->translation_root);
  }

  // Keep the TSS esp0 pointed at the incoming thread's own kernel stack, so a
  // ring-3 -> ring-0 transition (syscall, IRQ) while it is running lands on
  // the right stack rather than whichever thread ran last.
  HAL_CPU_SetKernelStack(next->kernel_stack_esp0);

  if (prev == next) {
    return;
  }

  // Save prev's SP into its TCB and load next's saved SP. When prev is later
  // rescheduled, control resumes right here.
  HAL_CPU_SwitchContext(prev->kernel_stack_top, next->kernel_stack_top);
}

}// namespace

void init() noexcept {
  // Adopt the currently-running execution stream as the bootstrap thread. Its
  // saved stack pointer is captured automatically by the first context switch,
  // so no stack needs to be framed here.
  if (const auto boot = CreateKObject<KThreadObject>("thread0")) {
    boot->state = KThreadObject::State::Running;
    boot->priority = 1;
    boot->quantum_ticks = DEFAULT_QUANTUM;
    boot->translation_root = PhysicalAddress(0);
    g_current = boot;
    g_next_id = 1;

    // Register the bootstrap thread now that its directory exists.
    KE_OB_InsertObject(KE_OB_GetThreadsDirectory(), KObjectPtr<KObject>(g_current.get()));
  }

  // Idle thread runs at the lowest priority; it is always ready so the ready
  // queue is never empty once the scheduler is live.
  g_idle = KE_SCHED_CreateThread(cfunc<void(void *)>(&idle_entry), nullptr, 0);
  
  early_print_fmt("Scheduler: Ready\r\n");
}

void tick() noexcept {
  KThreadObject *cur = sched::g_current.get();
  if (!cur) return;

  if (cur->quantum_ticks > 0) {
    --cur->quantum_ticks;
  }
  if (cur->quantum_ticks == 0) {
    // Called from the timer ISR: interrupts are already disabled here.
    sched::schedule_next();
  }
}
}// namespace kernel::sched

using namespace kernel;

UNDOS_KERNEL_API_DEF KObjectPtr<KThreadObject>
KE_SCHED_CreateThread(cfunc<void(void *)> entry, void *arg, uint8_t priority) noexcept {
  kstd::static_string<64> name;
  kstd::format(name, "thread{}", sched::g_next_id++);

  auto t = CreateKObject<KThreadObject>(name);
  if (!t) return nullptr;

  const VirtualAddress base = sched::alloc_kernel_stack(sched::KERNEL_STACK_SIZE);
  if (!base) return nullptr;

  const VirtualAddress top = base + sched::KERNEL_STACK_SIZE;

  t->entry = entry;
  t->argument = arg;
  t->priority = priority;
  t->state = KThreadObject::State::Ready;
  t->translation_root = PhysicalAddress(0);
  t->kernel_stack_base = base;
  t->kernel_stack_esp0 = top;
  t->kernel_stack_top = HAL_CPU_InitThreadContext(top, cfunc<void()>(&sched::thread_trampoline));

  KE_OB_InsertObject(KE_OB_GetThreadsDirectory(), KObjectPtr<KObject>(t.get()));

  // Before the scheduler is armed (during boot) interrupts are still globally
  // masked; don't re-enable them early. Once live, guard the shared queue.
  sched::g_ready.enqueue(t.get());

  return t;
}

UNDOS_KERNEL_API_DEF KObjectPtr<KThreadObject>
KE_SCHED_CreateUserThread(const vmm::AddressSpace &as, VirtualAddress user_entry, VirtualAddress user_stack_top) noexcept {
  kstd::static_string<64> name;
  kstd::format(name, "uthread{}", sched::g_next_id++);

  auto t = CreateKObject<KThreadObject>(name);
  if (!t) return nullptr;

  // The thread still needs its own kernel stack: ring-3 -> ring-0 transitions
  // (syscalls, IRQs, exceptions) land on it via the TSS's esp0.
  const VirtualAddress base = sched::alloc_kernel_stack(sched::KERNEL_STACK_SIZE);
  if (!base) return nullptr;

  const VirtualAddress top = base + sched::KERNEL_STACK_SIZE;

  t->priority = 1;
  t->state = KThreadObject::State::Ready;
  t->translation_root = as.translation_root;
  t->kernel_stack_base = base;
  t->kernel_stack_esp0 = top;
  t->kernel_stack_top = HAL_CPU_InitUserThreadContext(top, user_entry, user_stack_top);

  KE_OB_InsertObject(KE_OB_GetThreadsDirectory(), KObjectPtr<KObject>(t.get()));

  sched::g_ready.enqueue(t.get());

  return t;
}

UNDOS_KERNEL_API_DEF void KE_SCHED_Yield() noexcept {
  HAL_CPU_DisableInterrupts();
  sched::schedule_next();
  HAL_CPU_EnableInterrupts();
}

UNDOS_KERNEL_API_DEF void KE_SCHED_Block() noexcept {
  HAL_CPU_DisableInterrupts();
  if (sched::g_current) {
    sched::g_current->state = KThreadObject::State::Blocked;
  }
  sched::schedule_next();
  HAL_CPU_EnableInterrupts();
}

UNDOS_KERNEL_API_DEF void KE_SCHED_Unblock(const KObjectPtr<KThreadObject> &thread) noexcept {
  if (!thread) return;
  HAL_CPU_DisableInterrupts();
  if (thread->state == KThreadObject::State::Blocked) {
    thread->state = KThreadObject::State::Ready;
    sched::g_ready.enqueue(thread.get());
  }
  HAL_CPU_EnableInterrupts();
}

UNDOS_KERNEL_API_DEF [[noreturn]] void KE_SCHED_Terminate() noexcept {
  HAL_CPU_DisableInterrupts();
  if (sched::g_current) {
    sched::g_current->state = KThreadObject::State::Terminated;
  }
  sched::schedule_next();

  // schedule_next never returns to a terminated thread; guard just in case.
  for (;;) {
    HAL_CPU_Halt();
  }
}

UNDOS_KERNEL_API_DEF KObjectPtr<KThreadObject> KE_SCHED_GetCurrentThread() noexcept {
  return sched::g_current;
}
