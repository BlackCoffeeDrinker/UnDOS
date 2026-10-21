#pragma once

// Tests bypass the real PnP manager (pnp_manager.cpp is out of scope for
// hosted tests): drivers are driven directly through their AddDevice/
// StartDevice entry points. However, some drivers (e.g. volmgr.cpp) still
// call KE_PNP_ReportNewDevice internally to hand off newly enumerated child
// PDOs (like Volume devices) to the PnP tree. The no-op host stub for that
// API (in kernel_host_stubs.cpp) records the most recently reported device
// here so tests can retrieve it without needing access to a driver's private
// device-extension state.

#include <kernel/device.hpp>

extern kernel::KDevicePtr<kernel::KDevice> g_lastReportedPdo;
