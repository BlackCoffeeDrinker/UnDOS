#pragma once

// Generic Legacy ISA Reservations Enumerator: "the registry is the
// enumeration". Reads LocalMachine's "LegacyISAReservations" registry
// subtree - one child key per legacy device, each carrying HwID/
// DriverImage/IoPorts/Memory/Irq/Dma values (e.g. VGA/SB16/NE2000-style
// fixed-resource ISA hardware) - probes each one for presence, and reports a
// PDO (with a resource requirements list built straight from the registry
// values) to the PnP manager for every instance detected. Adding another
// device of the same type (e.g. a second NE2000 at a different I/O base) is
// just another registry entry: no code change is needed here.
namespace kernel::driver {
void EnumerateLegacyISADevices();
}// namespace kernel::driver
