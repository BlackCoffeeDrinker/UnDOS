#include "dispatch.hpp"

#include "scheduler/Scheduler.hpp"
#include <Kernel.hpp>
#include <kernel/hal_interface.hpp>
#include <kernel/kobject/KDispatcherHeader.hpp>
#include <kernel/mutex.hpp>
#include <kernel/scheduler.hpp>
#include <mutex.hpp>

namespace {

// Constant-initialised: registration must work even if the kernel image's
// constructor walk is missing (an unconstructed core rejects every IRQ).
constinit kernel::dispatch::DpcCore g_dpcCore;
kernel::KObjectPtr<kernel::KThreadObject> g_dispatchThread;

// Auto-reset dispatcher header the thread parks on. The signal is sticky
// (KDispatcherHeader::TryAcquire consumes it), so an enqueue that lands
// between the thread observing an empty ring and actually going to sleep
// cannot be lost -- which the old check-then-block pair could not guarantee
// against a producer on another CPU. Same pattern as pnp_manager.cpp.
kernel::KDispatcherHeader g_recordAvailable{};

void InterruptDispatchThread(void *) {
  for (;;) {
    while (g_dpcCore.DrainOne()) {
    }

    if (const auto waitStatus = KE_SCHED_WaitForHeader(g_recordAvailable, kernel::WAIT_INFINITE)) {
      KE_Print("interrupt dispatch wait failed: {}", waitStatus.message());
      KE_SCHED_Yield();
    }
  }
}

// Publish the queue slot first (done by the caller), then wake the worker.
// Sticky signal, usable from an interrupt gate: KE_EVENT_Signal masks
// interrupts itself and is safe before init() created the thread.
inline void WakeWorker() noexcept {
  KE_EVENT_Signal(g_recordAvailable, false);
}
}// namespace

namespace kernel::dispatch {
void init() noexcept {
  g_dpcCore.Reset();
  // Thread identity feeds the core's self-disconnect detection (a handler
  // calling KE_IO_DisconnectInterrupt from its own dispatch callback).
  g_dpcCore.SetThreadIdSource(
      []() noexcept -> void * { return KE_SCHED_GetCurrentThread().get(); });
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

DpcCore &core() noexcept {
  return g_dpcCore;
}
}// namespace kernel::dispatch

UNDOS_KERNEL_API_DEF kstd::error_code KE_DISPATCH_EnqueueInterrupt(const kernel::HalInterruptRecord &record) noexcept {
  const auto status = g_dpcCore.EnqueueInterrupt(record);
  if (!status) {
    WakeWorker();
  }
  return status;
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_DISPATCH_RegisterHandler(
    uint32_t interruptId,
    const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &isr) noexcept {
  return g_dpcCore.RegisterHandler(interruptId, isr);
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_DISPATCH_UnregisterHandler(
    uint32_t interruptId,
    const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &isr) noexcept {
  return g_dpcCore.UnregisterHandler(interruptId, isr);
}

UNDOS_KERNEL_API_DEF size_t KE_DISPATCH_HandlerCount(uint32_t interruptId) noexcept {
  return g_dpcCore.HandlerCount(interruptId);
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_DPC_Register(kernel::KDpc &dpc) noexcept {
  return g_dpcCore.RegisterDpc(dpc);
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_DPC_Unregister(kernel::KDpc &dpc) noexcept {
  return g_dpcCore.UnregisterDpc(dpc);
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_DPC_Queue(
    kernel::KDpc &dpc,
    const kernel::HalInterruptRecord &args) noexcept {
  const auto status = g_dpcCore.QueueDpc(dpc, args);
  if (!status) {
    WakeWorker();
  }
  return status;
}

UNDOS_KERNEL_API_DEF bool KE_DPC_Cancel(kernel::KDpc &dpc) noexcept {
  return g_dpcCore.CancelDpc(dpc);
}
