
#pragma once

#include <Kernel.hpp>
#include <kernel/adt/avl_tree.hpp>
#include <static_string.hpp>
#include <string_view.hpp>

namespace kernel::driver {
void init(const kernel::BootInfoT &boot_info);
void LoadBootDrivers();

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
}// namespace kernel::driver
