
#pragma once

#include <Kernel.hpp>
#include <kernel/kerror.hpp>
#include <kernel/adt/avl_tree.hpp>
#include <kernel/memory/address.hpp>
#include <expected.hpp>
#include <static_string.hpp>
#include <string_view.hpp>

namespace kernel::vmm {
struct AddressSpace;
}// namespace kernel::vmm

namespace kernel::driver {
// Maps the user-mode ELF image at `path` into `target_as`; returns its entry
// point. Core registers it with ps::SetImageLoader before init is created.
kstd::expected<VirtualAddress, kstd::error_code> LoadUserImage(
    const kstd::string_view &path, vmm::AddressSpace &target_as) noexcept;

void LoadBootDrivers();

// Resolves a symbol against the kernel/HAL boot symbol table only.

// Loads every driver declared under LocalMachine's "/Drivers" key whose
// "Start" value is "Automatic": drivers that aren't needed to mount the root
// filesystem (unlike "Boot" drivers, loaded earlier by LoadBootDrivers()) and must wait
// until the real root fs is mounted and writable. Call this once, right
// after the root mount + LocalMachine hive rebind in kernel_core_main.
void LoadAutomaticStartDrivers();

// Loads every driver declared with Start == "LastResort" and enumerates the
// matching LegacyISAReservations entries. These stand in for a job nothing
// else took (today: the VGA display driver), so the caller decides whether
// the pass is needed at all, once the normal passes have settled.
void LoadLastResortDrivers();

// Loads the Start == "Demand" driver `service` (its /Drivers/<service>/
// ImagePath) because PnP found a device mapped to it. PnP does not call this:
// it raises DeviceEvent::DriverRequested and the loader drains
// kernel::pnp::TakeDemandRequest(). Requests made before
// the root filesystem is mounted are queued and run by
// LoadAutomaticStartDrivers(). Each service is attempted at most once; a
// failure is logged and never retried. Must not be called with PnP's bind
// lock held.
void RequestDemandLoad(const kstd::string_view &service);
}// namespace kernel::driver
