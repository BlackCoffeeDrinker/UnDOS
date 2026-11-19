
#pragma once
#include <kernel/kerror.hpp>
#include <kernel/kobject/KDriverObject.hpp>
#include <kernel/event_broadcast.hpp>
#include <string.hpp>

namespace kernel::pnp {
void init();
// Reports one ROOT child per LocalMachine/Enum/Root subkey (HardwareId value).
void EnumerateRootFromHive();

// Event source for DeviceEvent::DriverRequested (the ROOT device). Valid
// after init().
kernel::event::Source DemandLoadEventSource();

// Pops the next driver service name PnP wants loaded. Returns false when
// none is pending. Names stay queued until taken, so a dropped event loses
// nothing: the next drain picks them up.
bool TakeDemandRequest(kstd::string &service);
}

kstd::error_code KE_PNP_RegisterDriver(const kernel::KObjectPtr<kernel::KDriverObject> &driver) noexcept;
