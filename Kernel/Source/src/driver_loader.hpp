
#pragma once

#include <Kernel.hpp>
#include <kernel/kerror.hpp>
#include <kernel/adt/avl_tree.hpp>
#include <expected.hpp>
#include <static_string.hpp>
#include <string_view.hpp>

namespace kernel::driver {
void init();

void LoadBootDrivers();

// Resolves a symbol against the kernel/HAL boot symbol table only.

// Loads every driver declared under LocalMachine's "/Drivers" key whose
// "Start" value is "Automatic": drivers that aren't needed to mount the root
// filesystem (unlike "Boot" drivers, loaded earlier by init()) and must wait
// until the real root fs is mounted and writable. Call this once, right
// after the root mount + LocalMachine hive rebind in kernel_core_main.
void LoadAutomaticStartDrivers();

// Loads every driver declared with Start == "LastResort" and enumerates the
// matching LegacyISAReservations entries. These stand in for a job nothing
// else took (today: the VGA display driver), so the caller decides whether
// the pass is needed at all, once the normal passes have settled.
void LoadLastResortDrivers();

// Loads the Start == "Demand" driver `service` (its /Drivers/<service>/
// ImagePath) because PnP found a device mapped to it. Requests made before
// the root filesystem is mounted are queued and run by
// LoadAutomaticStartDrivers(). Each service is attempted at most once; a
// failure is logged and never retried. Must not be called with PnP's bind
// lock held.
void RequestDemandLoad(const kstd::string_view &service);
}// namespace kernel::driver
