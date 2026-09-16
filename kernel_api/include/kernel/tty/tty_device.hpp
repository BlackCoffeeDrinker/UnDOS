#pragma once

#include <kernel/__core.hpp>
#include <kernel/fwd/KDevicePtr.hpp>
#include <kernel/kobject/KVttyObject.hpp>
#include <kernel/tty/tty.hpp>

namespace kernel {
struct KDevice;
}

// Generic API linking Virtual TTYs to any I/O device (virtual or physical)
UNDOS_KERNEL_PUBLIC_V1API(bool, KE_TTY_CreatePair,
                          kernel::tty::VttyPtr &outVtty,
                          kernel::KObjectPtr<kernel::KVttySourceObject> &outSource,
                          kernel::KObjectPtr<kernel::KVttySinkObject> &outSink,
                          const kstd::string_view &namePrefix);

UNDOS_KERNEL_PUBLIC_V1API(bool, KE_TTY_AttachDevice, const kernel::tty::VttyPtr &vtty, const kernel::KDevicePtr<kernel::KDevice> &device);
UNDOS_KERNEL_PUBLIC_V1API(void, KE_TTY_DetachDevice, const kernel::tty::VttyPtr &vtty);
UNDOS_KERNEL_PUBLIC_V1API(kernel::KDevicePtr<kernel::KDevice>, KE_TTY_GetAttachedDevice, const kernel::tty::VttyPtr &vtty);
UNDOS_KERNEL_PUBLIC_V1API(kernel::tty::VttyPtr, KE_TTY_GetDeviceVtty, const kernel::KDevicePtr<kernel::KDevice> &device);

UNDOS_KERNEL_PUBLIC_V1API(size_t, KE_TTY_PumpDevice, const kernel::tty::VttyPtr &vtty);
UNDOS_KERNEL_PUBLIC_V1API(void, KE_TTY_PumpAllAttachedDevices);

// Default console sink endpoint (set by whatever driver or subsystem provides the system console)
UNDOS_KERNEL_PUBLIC_V1API(void, KE_TTY_SetDefaultConsole, const kernel::KObjectPtr<kernel::KVttySinkObject> &sink);
UNDOS_KERNEL_PUBLIC_V1API(kernel::KObjectPtr<kernel::KVttySinkObject>, KE_TTY_GetDefaultConsoleSink);
