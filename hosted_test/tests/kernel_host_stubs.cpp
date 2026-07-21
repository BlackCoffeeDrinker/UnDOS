
#include <kernel/hal_interface.hpp>
#include <kernel/pnp.hpp>
#include <kernel/scheduler.hpp>
#include <kernel/virtual_memory.hpp>

#include "pnp_test_hooks.hpp"

#include <cstdio>

kernel::KDevicePtr<kernel::KDevice> g_lastReportedPdo;

// KE_Malloc/KE_Free are now the real kernel/src/vmm.cpp implementation,
// backed by fake RAM provided by kernel_host_vmm_stubs.cpp (see vmm_test.cpp).

UNDOS_HAL_API_DEF void early_print(const char *str) noexcept {
  fputs(str, stdout);
}

UNDOS_HAL_API_DEF void early_print_char(char c) noexcept {
  fputc(c, stdout);
}

// The PnP manager (pnp_manager.cpp) is out of scope for hosted tests; tests
// call driver AddDevice/StartDevice entry points directly instead of going
// through the full PnP device-interest dispatch. These no-op stubs only
// exist so drivers that call them (e.g. volmgr.cpp) can still link.
UNDOS_KERNEL_API_DEF void KE_PNP_ReportNewDevice(const kernel::KDevicePtr<kernel::KDeviceBus> &parent, const kernel::KDevicePtr<kernel::KDevice> &pdo) noexcept {
  (void) parent;
  g_lastReportedPdo = pdo;
}

UNDOS_KERNEL_API_DEF void KE_PNP_RegisterDeviceInterest(kernel::DeviceType type, const kernel::KObjectPtr<kernel::KDriverObject> &driver) noexcept {
  (void) type;
  (void) driver;
}

// The scheduler (scheduler.cpp) is out of scope for hosted tests; there is no
// running thread outside of the test harness itself. Page-fault handling
// (vmm.cpp) only consults this to resolve a user-mode fault's owning
// address space, so returning a null thread here just means such tests
// exercise the kernel-address-space fallback path.
UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KThreadObject> KE_SCHED_GetCurrentThread() noexcept {
  return nullptr;
}
#include <stdexcept>
void HAL_PLATFORM_Panic(const char *message, const char *file, int line) noexcept { 
  (void)file; (void)line;
  printf("PANIC: %s\n", message);
  abort();
}

#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

namespace kernel::vmm { void init() noexcept; }
struct VmmInitListener : Catch::EventListenerBase {
    using EventListenerBase::EventListenerBase;
    void testRunStarting(Catch::TestRunInfo const&) override {
        kernel::vmm::init();
    }
};
CATCH_REGISTER_LISTENER(VmmInitListener)
