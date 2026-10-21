#include <Kernel.hpp>

#include "__kernel_types.h"

// Exported to other modules (NIC drivers, protocol modules).
UNDOS_MODULE_EXPORT uint32_t NET_Core_GetVersion() { return 1; }

// Provider of the NET syscall group, bound by the kernel's generated slot table.
UNDOS_MODULE_EXPORT kstd::error_code handle_QueryNetworkVersion_V1(__KRN_NET_QUERYNETWORKVERSION_V1 &&args) {
  args.version = 1;
  return kernel::KernelError::Success;
}

extern "C" void SubsystemEntry(const kernel::KObjectPtr<kernel::KModuleObject> &) {
  KE_Print("network: subsystem initialized");
}
