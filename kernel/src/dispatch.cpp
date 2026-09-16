#include "dispatch.hpp"

#include <Kernel.hpp>
#include <kernel/hal_interface.hpp>
#include <kernel/mutex.hpp>
#include <kernel/scheduler.hpp>
#include <mutex.hpp>
#include "scheduler/Scheduler.hpp"

namespace {

kernel::InterruptRecordRing g_interruptRing;
kernel::KObjectPtr<kernel::KThreadObject> g_dispatchThread;

constexpr size_t kMaxHandlersPerIrq = 4;
struct IrqSlot {
  kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> handlers[kMaxHandlersPerIrq];
  size_t count{0};
};

IrqSlot g_irqHandlers[16]{};
kernel::Mutex g_handlersLock;

void InterruptDispatchThread(void *) {
  for (;;) {
    kernel::HalInterruptRecord record{};

    while (g_interruptRing.Pop(record)) {
      if (record.irq < 16) {
        kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> handlers[kMaxHandlersPerIrq];
        size_t count = 0;
        {
          kstd::unique_lock guard(g_handlersLock);
          count = g_irqHandlers[record.irq].count;
          for (size_t i = 0; i < count; ++i) {
            handlers[i] = g_irqHandlers[record.irq].handlers[i];
          }
        }
        for (size_t i = 0; i < count; ++i) {
          if (handlers[i] && handlers[i]->service_routine) {
            handlers[i]->service_routine(handlers[i], handlers[i]->service_context);
          }
        }
      }
    }

    HAL_CPU_DisableInterrupts();
    if (g_interruptRing.Empty()) {
      auto current = KE_SCHED_GetCurrentThread();
      if (current) {
        current->state = kernel::KThreadObject::State::Blocked;
      }
      kernel::sched::schedule_next();
    }
    HAL_CPU_EnableInterrupts();
  }
}

} // namespace

namespace kernel::dispatch {

void init() noexcept {
  g_interruptRing.Reset();
  // Spawn dispatch thread at elevated priority (10), running ahead of normal threads (priority 1)
  g_dispatchThread = KE_SCHED_CreateThread(cfunc<void(void *)>(&InterruptDispatchThread), nullptr, 10);
  early_print_fmt("Interrupt dispatch service initialized.\r\n");
}

InterruptRecordRing &get_ring() noexcept {
  return g_interruptRing;
}

bool enqueue(const HalInterruptRecord &record) noexcept {
  const bool pushed = g_interruptRing.Push(record);
  if (pushed && g_dispatchThread) {
    KE_SCHED_UnblockFromIsr(g_dispatchThread);
  }
  return pushed;
}

bool register_handler(uint32_t irq, const KObjectPtr<KInterruptServiceRoutineObject> &isr) noexcept {
  if (irq >= 16 || !isr) {
    return false;
  }
  kstd::unique_lock guard(g_handlersLock);
  for (size_t i = 0; i < g_irqHandlers[irq].count; ++i) {
    if (g_irqHandlers[irq].handlers[i] == isr) {
      return true;
    }
  }
  if (g_irqHandlers[irq].count >= kMaxHandlersPerIrq) {
    return false;
  }
  g_irqHandlers[irq].handlers[g_irqHandlers[irq].count++] = isr;
  return true;
}

void unregister_handler(uint32_t irq) noexcept {
  if (irq >= 16) {
    return;
  }
  kstd::unique_lock guard(g_handlersLock);
  for (size_t i = 0; i < g_irqHandlers[irq].count; ++i) {
    g_irqHandlers[irq].handlers[i] = nullptr;
  }
  g_irqHandlers[irq].count = 0;
}

} // namespace kernel::dispatch

UNDOS_KERNEL_API_DEF bool KE_DISPATCH_EnqueueInterrupt(const kernel::HalInterruptRecord &record) noexcept {
  return kernel::dispatch::enqueue(record);
}

UNDOS_KERNEL_API_DEF bool KE_DISPATCH_RegisterHandler(
    uint32_t irq,
    const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &isr) noexcept {
  return kernel::dispatch::register_handler(irq, isr);
}

UNDOS_KERNEL_API_DEF void KE_DISPATCH_UnregisterHandler(uint32_t irq) noexcept {
  kernel::dispatch::unregister_handler(irq);
}
