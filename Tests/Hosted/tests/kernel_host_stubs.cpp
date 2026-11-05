
#include <atomic>

#include <Kernel.hpp>

#include "pnp_test_hooks.hpp"
#include "sched_test_hooks.hpp"
#include <kernel/kobject/KThreadObject.hpp>

#include <cstdio>
#include <kernel/input.hpp>

UNDOS_KERNEL_API_DEF kernel::input::InputEventResult KE_INPUT_WaitEvent(kstd::chrono::milliseconds) noexcept {
  return kstd::unexpected(kernel::KernelError::Timeout);
}

UNDOS_KERNEL_API_DEF bool KE_INPUT_DequeueEvent(kernel::input::AnyInputEvent &) noexcept {
  return false;
}

kernel::KDevicePtr<kernel::KDevice> g_lastReportedPdo;

void KE_ConsoleBeginPanic() noexcept {}

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
// Tests that call syscall handlers can install a current thread through
// sched_test_hooks.hpp; it is null otherwise.
namespace {
kernel::KObjectPtr<kernel::KThreadObject> g_hostCurrentThread;
void (*g_hostYieldHook)(){};
}

void test_sched::SetCurrentThread(kernel::KObjectPtr<kernel::KThreadObject> thread) noexcept {
  g_hostCurrentThread = thread;
}

void test_sched::SetYieldHook(void (*hook)()) noexcept {
  g_hostYieldHook = hook;
}

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KThreadObject> KE_SCHED_GetCurrentThread() noexcept {
  return g_hostCurrentThread;
}

// Hosted tests run single-threaded (there is no real scheduler/HAL backing
// kernel::Mutex), so KE_SCHED_Yield only runs an optional cooperative hook and the atomics below are
// implemented with plain (non-atomic) loads/stores/compare-exchange. This is
// sufficient for the single-threaded test harness while still satisfying the
// symbols kernel::Mutex (mutex.hpp) needs to link.
UNDOS_KERNEL_API_DEF void KE_SCHED_Yield() noexcept {
  if (const auto hook = g_hostYieldHook) {
    g_hostYieldHook = nullptr;
    hook();
  }
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

namespace test_sched {
void (*blockHook)() = nullptr;
void (*beforeWaitHook)(kernel::KDispatcherHeader &) = nullptr;
}
UNDOS_KERNEL_PUBLIC_V1API(kstd::error_code, HostedProductionWaitForHeader,
    kernel::KDispatcherHeader &, kstd::chrono::milliseconds);
UNDOS_KERNEL_API_DEF kstd::error_code KE_SCHED_WaitForHeader(
    kernel::KDispatcherHeader &header, kstd::chrono::milliseconds timeout) noexcept {
  if (KE_SCHED_GetCurrentThread()) {
    if (test_sched::beforeWaitHook) test_sched::beforeWaitHook(header);
    return HostedProductionWaitForHeader(header, timeout);
  }
  // Legacy driver/scene fixtures have no scheduler thread and only perform
  // uncontended header acquisition. They do not certify blocking behavior.
  return header.TryAcquire() ? kernel::KernelError::Success : kernel::KernelError::Timeout;
}
namespace kernel::sched {
void block_current_locked() noexcept {
  const auto current = KE_SCHED_GetCurrentThread();
  current->state = kernel::KThreadObject::State::Blocked;
  if (test_sched::blockHook) test_sched::blockHook();
  // Host continuation resumes here after the production wait/signal logic
  // has made this thread Ready; no machine stack switch is emulated.
  current->state = kernel::KThreadObject::State::Running;
}
void ready_locked(kernel::KThreadObject *thread) noexcept {
  if (thread && thread->state == kernel::KThreadObject::State::Blocked)
    thread->state = kernel::KThreadObject::State::Ready;
}
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

namespace test_sync {
namespace {
std::atomic_size_t spinlocksHeld{};
}
size_t held_spinlocks() noexcept { return spinlocksHeld.load(std::memory_order_relaxed); }
}

namespace {
int to_builtin_memory_order(PlatformMemoryOrder order) noexcept {
  switch (order) {
    case PlatformMemoryOrder::Relaxed: return __ATOMIC_RELAXED;
    case PlatformMemoryOrder::Consume: return __ATOMIC_CONSUME;
    case PlatformMemoryOrder::Acquire: return __ATOMIC_ACQUIRE;
    case PlatformMemoryOrder::Release: return __ATOMIC_RELEASE;
    case PlatformMemoryOrder::AcquireRelease: return __ATOMIC_ACQ_REL;
    case PlatformMemoryOrder::SequentiallyConsistent: return __ATOMIC_SEQ_CST;
  }
  return __ATOMIC_SEQ_CST;
}
}

UNDOS_HAL_API_DEF uint32_t HAL_ATOMIC_Load32(kernel::VirtualAddress addr, PlatformMemoryOrder order) noexcept {
  return __atomic_load_n(addr.as_ptr<uint32_t>(), to_builtin_memory_order(order));
}

UNDOS_HAL_API_DEF uint32_t HAL_ATOMIC_FetchAdd32(kernel::VirtualAddress addr, uint32_t delta, PlatformMemoryOrder order) noexcept {
  return __atomic_fetch_add(addr.as_ptr<uint32_t>(), delta, to_builtin_memory_order(order));
}

UNDOS_HAL_API_DEF void HAL_ATOMIC_Store32(kernel::VirtualAddress addr, uint32_t value, PlatformMemoryOrder order) noexcept {
  auto *ptr = addr.as_ptr<uint32_t>();
  const uint32_t previous = __atomic_load_n(ptr, __ATOMIC_RELAXED);
  __atomic_store_n(ptr, value, to_builtin_memory_order(order));
  if (previous == 1 && value == 0) --test_sync::spinlocksHeld;
}

UNDOS_HAL_API_DEF bool HAL_ATOMIC_CompareExchange32(kernel::VirtualAddress addr, uint32_t &expected_out, uint32_t desired, PlatformMemoryOrder order) noexcept {
  auto *ptr = addr.as_ptr<uint32_t>();
  const int successOrder = to_builtin_memory_order(order);
  const int failureOrder = order == PlatformMemoryOrder::Release || order == PlatformMemoryOrder::AcquireRelease
      ? __ATOMIC_ACQUIRE : successOrder;
  const uint32_t previous = expected_out;
  const bool exchanged = __atomic_compare_exchange_n(ptr, &expected_out, desired, false, successOrder, failureOrder);
  if (exchanged && previous == 0 && desired == 1) ++test_sync::spinlocksHeld;
  return exchanged;
}

#include "dispatch_test_hooks.hpp"

#include "dispatch.hpp"

namespace {
kernel::dispatch::DpcCore g_testCore;

// Hosted tests drive the core from real std::threads, so thread identity for
// the self-disconnect detection comes from a thread_local's address instead
// of the (global, host-faked) KE_SCHED_GetCurrentThread.
void *HostThreadId() noexcept {
  static thread_local int marker;
  return &marker;
}
}// namespace

// Real hardware IRQ lines/PIC (intr.cpp) are out of scope for hosted tests;
// KE_IO_ConnectInterrupt/KE_IO_DisconnectInterrupt (io.cpp) just need
// something to link against here. These recording fakes mirror the "no
// locking, no real hardware" spirit of the other HAL stubs above while
// letting tests observe connect/disconnect/mask/complete ordering.
UNDOS_HAL_API_DEF bool HAL_INTR_ConnectInterrupt(
    uint32_t irq,
    const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &isr) noexcept {
  (void) isr;
  auto &hal = test_dispatch::FakeHal();
  if (hal.failNextConnect) {
    hal.failNextConnect = false;
    return false;
  }
  ++hal.connectCount;
  hal.lastConnectedIrq = irq;
  return true;
}

UNDOS_HAL_API_DEF void HAL_INTR_DisconnectInterrupt(uint32_t irq) noexcept {
  auto &hal = test_dispatch::FakeHal();
  ++hal.disconnectCount;
  hal.lastDisconnectedIrq = irq;
}

UNDOS_HAL_API_DEF void HAL_INTR_CompleteInterrupt(uint32_t irq) noexcept {
  auto &hal = test_dispatch::FakeHal();
  ++hal.completeCount;
  hal.lastCompletedIrq = irq;
  hal.events.push_back({test_dispatch::FakeHalEvent::Complete, irq});
}

namespace test_dispatch {

FakeHalState &FakeHal() noexcept {
  static FakeHalState state;
  return state;
}

kernel::dispatch::DpcCore &Core() noexcept {
  return g_testCore;
}

void RecordCallbackEvent(uint32_t irq) noexcept {
  FakeHal().events.push_back({FakeHalEvent::Callback, irq});
}

void Reset() noexcept {
  g_testCore.Reset();
  g_testCore.SetThreadIdSource(&HostThreadId);
  FakeHal() = FakeHalState{};
}

void Pump() noexcept {
  while (g_testCore.DrainOne()) {
  }
}

}// namespace test_dispatch

UNDOS_KERNEL_API_DEF kstd::error_code KE_DISPATCH_EnqueueInterrupt(const kernel::HalInterruptRecord &record) noexcept {
  return g_testCore.EnqueueInterrupt(record);
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_DISPATCH_RegisterHandler(
    uint32_t irq,
    const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &isr) noexcept {
  return g_testCore.RegisterHandler(irq, isr);
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_DISPATCH_UnregisterHandler(
    uint32_t irq,
    const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &isr) noexcept {
  return g_testCore.UnregisterHandler(irq, isr);
}

UNDOS_KERNEL_API_DEF size_t KE_DISPATCH_HandlerCount(uint32_t irq) noexcept {
  return g_testCore.HandlerCount(irq);
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_DPC_Register(kernel::KDpc &dpc) noexcept {
  return g_testCore.RegisterDpc(dpc);
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_DPC_Unregister(kernel::KDpc &dpc) noexcept {
  return g_testCore.UnregisterDpc(dpc);
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_DPC_Queue(
    kernel::KDpc &dpc,
    const kernel::HalInterruptRecord &args) noexcept {
  return g_testCore.QueueDpc(dpc, args);
}

UNDOS_KERNEL_API_DEF bool KE_DPC_Cancel(kernel::KDpc &dpc) noexcept {
  return g_testCore.CancelDpc(dpc);
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
