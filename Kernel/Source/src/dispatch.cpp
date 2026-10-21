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
kernel::dispatch::InterruptHandlerRegistry g_interruptHandlers;
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
      kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject>
          handlers[kernel::dispatch::InterruptHandlerRegistry::kMaxHandlersPerInterrupt]{};
      size_t count = 0;
      {
        kstd::unique_lock guard(g_handlersLock);
        g_interruptHandlers.CopyHandlers(record.irq, handlers, count);
      }
      for (size_t i = 0; i < count; ++i) {
        if (handlers[i] && handlers[i]->service_routine) {
          handlers[i]->service_routine(handlers[i], handlers[i]->service_context);
        }
      }
    }

    if (const auto waitStatus = KE_SCHED_WaitForHeader(g_recordAvailable, kernel::WAIT_INFINITE)) {
      KE_Print("interrupt dispatch wait failed: {}", waitStatus.message());
      KE_SCHED_Yield();
    }
  }
}
}// namespace

namespace kernel::dispatch {
void init() noexcept {
  g_interruptRing.Reset();
  g_interruptHandlers.Reset();
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
}// namespace kernel::dispatch

UNDOS_KERNEL_API_DEF kstd::error_code KE_DISPATCH_EnqueueInterrupt(const kernel::HalInterruptRecord &record) noexcept {
  const auto status = g_interruptRing.Push(record);
  if (!status) {
    // Sticky signal, usable from an interrupt gate: KE_EVENT_Signal masks
    // interrupts itself and is safe before init() created the thread.
    KE_EVENT_Signal(g_recordAvailable, false);
  }
  return status;
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_DISPATCH_RegisterHandler(
    uint32_t interruptId,
    const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &isr) noexcept {
  kstd::unique_lock guard(g_handlersLock);
  return g_interruptHandlers.Register(interruptId, isr);
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_DISPATCH_UnregisterHandler(uint32_t interruptId) noexcept {
  kstd::unique_lock guard(g_handlersLock);
  return g_interruptHandlers.Unregister(interruptId);
}
