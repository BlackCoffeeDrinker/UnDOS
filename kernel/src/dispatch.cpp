#include "dispatch.hpp"

#include "scheduler/Scheduler.hpp"
#include <Kernel.hpp>
#include <kernel/hal_interface.hpp>
#include <kernel/kobject/KDispatcherHeader.hpp>
#include <kernel/mutex.hpp>
#include <kernel/scheduler.hpp>
#include <mutex.hpp>

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

// Auto-reset dispatcher header the thread parks on. The signal is sticky
// (KDispatcherHeader::TryAcquire consumes it), so an enqueue that lands
// between the thread observing an empty ring and actually going to sleep
// cannot be lost -- which the old check-then-block pair could not guarantee
// against a producer on another CPU. Same pattern as pnp_manager.cpp.
kernel::KDispatcherHeader g_recordAvailable{};

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

    const auto waitStatus = KE_SCHED_WaitForHeader(g_recordAvailable, kernel::WAIT_INFINITE);
    if (waitStatus) {
      KE_Print("interrupt dispatch wait failed: {}", waitStatus.message());
      KE_SCHED_Yield();
    }
  }
}

}// namespace

namespace kernel::dispatch {

void init() noexcept {
  g_interruptRing.Reset();
  g_recordAvailable.manualReset = false;
  g_recordAvailable.signalled = false;
  // Spawn dispatch thread at elevated priority (10), running ahead of normal threads (priority 1)
  auto thread = KE_SCHED_CreateThread(cfunc<void(void *)>(&InterruptDispatchThread), nullptr, 10);
  if (!thread) {
    KE_Print("Failed to initialize dispatch: {}", thread.error().message());
    HAL_PLATFORM_Panic("Failed to initialize dispatch", __FILE__, __LINE__);
  }
  g_dispatchThread = *thread;
}

InterruptRecordRing &get_ring() noexcept {
  return g_interruptRing;
}

kstd::error_code enqueue(const HalInterruptRecord &record) noexcept {
  const kstd::error_code status = g_interruptRing.Push(record);
  if (!status) {
    // Sticky signal, usable from an interrupt gate: KE_EVENT_Signal masks
    // interrupts itself and is safe before init() created the thread.
    KE_EVENT_Signal(g_recordAvailable, false);
  }
  return status;
}

kstd::error_code register_handler(uint32_t irq, const KObjectPtr<KInterruptServiceRoutineObject> &isr) noexcept {
  if (irq >= 16 || !isr) return KernelError::InvalidArgument;
  kstd::unique_lock guard(g_handlersLock);
  for (size_t i = 0; i < g_irqHandlers[irq].count; ++i) {
    if (g_irqHandlers[irq].handlers[i] == isr) {
      return KernelError::Success;
    }
  }
  if (g_irqHandlers[irq].count >= kMaxHandlersPerIrq) {
    return KernelError::ResourceLimitExceeded;
  }
  g_irqHandlers[irq].handlers[g_irqHandlers[irq].count++] = isr;
  return KernelError::Success;
}

kstd::error_code unregister_handler(uint32_t irq) noexcept {
  if (irq >= 16) {
    return KernelError::InvalidArgument;
  }
  kstd::unique_lock guard(g_handlersLock);
  for (size_t i = 0; i < g_irqHandlers[irq].count; ++i) {
    g_irqHandlers[irq].handlers[i] = nullptr;
  }
  g_irqHandlers[irq].count = 0;
  return KernelError::Success;
}

}// namespace kernel::dispatch

UNDOS_KERNEL_API_DEF kstd::error_code KE_DISPATCH_EnqueueInterrupt(const kernel::HalInterruptRecord &record) noexcept {
  return kernel::dispatch::enqueue(record);
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_DISPATCH_RegisterHandler(
    uint32_t irq,
    const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &isr) noexcept {
  return kernel::dispatch::register_handler(irq, isr);
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_DISPATCH_UnregisterHandler(uint32_t irq) noexcept {
  return kernel::dispatch::unregister_handler(irq);
}
