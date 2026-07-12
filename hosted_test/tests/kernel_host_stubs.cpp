
#include <kernel/hal_interface.hpp>
#include <kernel/pnp.hpp>
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
