
#include <Kernel.hpp>

#include "pnp_test_hooks.hpp"

#include <cstdio>
#include <kernel/input.hpp>

UNDOS_KERNEL_API_DEF kernel::input::InputEventResult KE_INPUT_WaitEvent(kstd::chrono::milliseconds) noexcept {
  return kstd::unexpected(kernel::KernelError::Timeout);
}

UNDOS_KERNEL_API_DEF bool KE_INPUT_DequeueEvent(kernel::input::AnyInputEvent &) noexcept {
  return false;
}

kernel::KDevicePtr<kernel::KDevice> g_lastReportedPdo;

UNDOS_KERNEL_API_DEF void KE_PrintBuffer(const char *str, size_t n) noexcept {
  fwrite(str, 1, n, stdout);
  fputc('\n', stdout);
}

// The PnP manager (pnp_manager.cpp) is out of scope for hosted tests; tests
// call driver AddDevice/StartDevice entry points directly instead of going
// through the full PnP device-interest dispatch. These no-op stubs only
// exist so drivers that call them (e.g. volmgr.cpp) can still link.
UNDOS_KERNEL_API_DEF kstd::error_code KE_PNP_ReportNewDevice(const kernel::KDevicePtr<kernel::KDeviceBus> &parent, const kernel::KDevicePtr<kernel::KDevice> &pdo) noexcept {
  (void) parent;
  g_lastReportedPdo = pdo;
  return kernel::KernelError::Success;
}

// The background PnP work queue/worker thread live in pnp_manager.cpp, which
// is out of scope for hosted tests (see comment above). Since
// KE_PNP_ReportNewDevice above is already a synchronous no-op stub here,
// there is never any queued work to wait for.
UNDOS_KERNEL_API_DEF void KE_PNP_WaitForQuiescence() noexcept {
}

UNDOS_KERNEL_API_DEF const kernel::event::Category &KE_PNP_EventCategory() noexcept {
  struct PnpEventCategory final : kernel::event::Category {
    [[nodiscard]] const char *name() const noexcept override { return "PnP"; }
    [[nodiscard]] kstd::string_view message(int) const noexcept override { return ""; }
    [[nodiscard]] bool equivalent(const kernel::event::Code &code) const noexcept override { return &code.category() == this; }
  };
  static PnpEventCategory category;
  return category;
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_PNP_RegisterDeviceInterest(kernel::DeviceType type, const kernel::KObjectPtr<kernel::KDriverObject> &driver) noexcept {
  (void) type;
  (void) driver;
  return kernel::KernelError::Success;
}

// The scheduler (scheduler.cpp) is out of scope for hosted tests; there is no
// running thread outside of the test harness itself. Page-fault handling
// (vmm.cpp) only consults this to resolve a user-mode fault's owning
// address space, so returning a null thread here just means such tests
// exercise the kernel-address-space fallback path.
UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KThreadObject> KE_SCHED_GetCurrentThread() noexcept {
  return nullptr;
}

// Hosted tests run single-threaded (there is no real scheduler/HAL backing
// kernel::Mutex), so KE_SCHED_Yield is a no-op and the atomics below are
// implemented with plain (non-atomic) loads/stores/compare-exchange. This is
// sufficient for the single-threaded test harness while still satisfying the
// symbols kernel::Mutex (mutex.hpp) needs to link.
UNDOS_KERNEL_API_DEF void KE_SCHED_Yield() noexcept {
}

// event_broadcaster.cpp / pnp_manager.cpp create their pump threads in init(),
// which hosted tests never call: they drive dispatch synchronously instead
// (kernel::event::detail::pump_once()). This stub only needs to satisfy the
// linker.
UNDOS_KERNEL_API_DEF kstd::expected<kernel::KObjectPtr<kernel::KThreadObject>, kstd::error_code> KE_SCHED_CreateThread(
    kernel::cfunc<void(void *)> entry, void *arg, uint8_t priority) noexcept {
  (void) entry;
  (void) arg;
  (void) priority;
  return kstd::unexpected(kernel::KernelError::NotImplemented);
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_SCHED_WaitForHeader(kernel::KDispatcherHeader &header, kstd::chrono::milliseconds) noexcept {
  if (header.TryAcquire()) return kernel::KernelError::Success;
  return kernel::KernelError::Timeout;
}

UNDOS_KERNEL_API_DEF void KE_EVENT_Signal(kernel::KDispatcherHeader &header, bool wakeAll) noexcept {
  auto wake = [](kernel::KThreadObject *) {};
  header.Signal(wakeAll, wake);
}

// Settable rather than fixed at zero so dpc_test.cpp can walk simulated time
// past a timer DPC's deadline without a real clock interrupt.
namespace {
kstd::chrono::nanoseconds g_hostSystemTime{0};
}// namespace

namespace test_time {
void SetSystemTime(kstd::chrono::nanoseconds now) noexcept { g_hostSystemTime = now; }
}// namespace test_time

UNDOS_KERNEL_API_DEF kstd::chrono::nanoseconds KE_TIME_GetSystemTime() noexcept {
  return g_hostSystemTime;
}

UNDOS_HAL_API_DEF void HAL_ATOMIC_Store32(kernel::VirtualAddress addr, uint32_t value, PlatformMemoryOrder order) noexcept {
  (void) order;
  *addr.as_ptr<uint32_t>() = value;
}

UNDOS_HAL_API_DEF bool HAL_ATOMIC_CompareExchange32(kernel::VirtualAddress addr, uint32_t &expected_out, uint32_t desired, PlatformMemoryOrder order) noexcept {
  (void) order;
  auto *ptr = addr.as_ptr<uint32_t>();
  if (*ptr == expected_out) {
    *ptr = desired;
    return true;
  }
  expected_out = *ptr;
  return false;
}

// Real hardware IRQ lines/PIC (intr.cpp) are out of scope for hosted tests;
// KE_IO_ConnectInterrupt/KE_IO_DisconnectInterrupt (io.cpp) just need
// something to link against here. These no-op stubs mirror the "no locking,
// no real hardware" spirit of the other HAL stubs above.
UNDOS_HAL_API_DEF bool HAL_INTR_ConnectInterrupt(
    uint32_t irq,
    const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &isr) noexcept {
  (void) irq;
  (void) isr;
  return true;
}

UNDOS_HAL_API_DEF void HAL_INTR_DisconnectInterrupt(uint32_t irq) noexcept {
  (void) irq;
}

#include "dispatch_test_hooks.hpp"

#include "dispatch.hpp"

namespace {
kernel::InterruptRecordRing g_testRing;
kernel::dispatch::InterruptHandlerRegistry g_testHandlers;
}// namespace

namespace test_dispatch {

kernel::InterruptRecordRing &GetRing() noexcept {
  return g_testRing;
}

kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> GetHandler(uint32_t irq) noexcept {
  kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject>
      handlers[kernel::dispatch::InterruptHandlerRegistry::kMaxHandlersPerInterrupt]{};
  size_t count = 0;
  g_testHandlers.CopyHandlers(irq, handlers, count);
  return count > 0 ? handlers[0] : nullptr;
}

void Reset() noexcept {
  g_testRing.Reset();
  g_testHandlers.Reset();
}

void Pump() noexcept {
  kernel::HalInterruptRecord record{};
  while (g_testRing.Pop(record)) {
    kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject>
        handlers[kernel::dispatch::InterruptHandlerRegistry::kMaxHandlersPerInterrupt]{};
    size_t count = 0;
    g_testHandlers.CopyHandlers(record.irq, handlers, count);
    for (size_t i = 0; i < count; ++i) {
      if (handlers[i] && handlers[i]->service_routine) {
        handlers[i]->service_routine(handlers[i], handlers[i]->service_context);
      }
    }
  }
}

}// namespace test_dispatch

UNDOS_KERNEL_API_DEF kstd::error_code KE_DISPATCH_EnqueueInterrupt(const kernel::HalInterruptRecord &record) noexcept {
  return g_testRing.Push(record);
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_DISPATCH_RegisterHandler(
    uint32_t irq,
    const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &isr) noexcept {
  return g_testHandlers.Register(irq, isr);
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_DISPATCH_UnregisterHandler(uint32_t irq) noexcept {
  return g_testHandlers.Unregister(irq);
}
#include <stdexcept>
void HAL_PLATFORM_Panic(const kstd::string_view &message, const char *file, int line) noexcept {
  (void) file;
  (void) line;
  printf("PANIC: %s\n", message.data());
  abort();
}

#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

namespace kernel::vmm {
void init() noexcept;
}
struct VmmInitListener : Catch::EventListenerBase {
  using EventListenerBase::EventListenerBase;
  void testRunStarting(Catch::TestRunInfo const &) override {
    kernel::vmm::init();
  }
};
CATCH_REGISTER_LISTENER(VmmInitListener)
