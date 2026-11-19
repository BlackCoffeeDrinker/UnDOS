#include "dispatch.hpp"

#include "sched_wait.hpp"
#include <Kernel.hpp>
#include <kernel/hal_interface.hpp>
#include <kernel/kobject/KDispatcherHeader.hpp>
#include <kernel/mutex.hpp>
#include <mutex.hpp>

namespace {

// Constant-initialised: registration must work even if the kernel image's
// constructor walk is missing (an unconstructed core rejects every IRQ).
constinit kernel::dispatch::DpcCore g_dpcCore;

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

    if (const auto waitStatus = kernel::sched::wait::block_current(g_recordAvailable)) {
      KE_Print("interrupt dispatch wait failed: {}", waitStatus.message());
      kernel::sched::wait::yield_current();
    }
  }
}

// Publish the queue slot first (done by the caller), then wake the worker.
// Sticky signal, usable from an interrupt gate: sched::wait::wake masks
// interrupts itself and is safe before init() created the thread.
inline void WakeWorker() noexcept {
  kernel::sched::wait::wake(g_recordAvailable);
}
}// namespace

namespace kernel::dispatch {
void init() noexcept {
  g_dpcCore.Reset();
  // Thread identity feeds the core's self-disconnect detection (a handler
  // calling KE_IO_DisconnectInterrupt from its own dispatch callback).
  g_dpcCore.SetThreadIdSource(
      []() noexcept -> void * { return sched::wait::current_thread(); });
  g_recordAvailable.manualReset = false;
  g_recordAvailable.signalled = false;
  // Spawn dispatch thread at Realtime priority, ahead of Normal threads
  if (const auto status = sched::wait::ready_worker(&InterruptDispatchThread)) {
    KE_Print("Failed to initialize dispatch: {}", status.message());
    HAL_PLATFORM_Panic("Failed to initialize dispatch", __FILE__, __LINE__);
  }
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
