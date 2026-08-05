#pragma once

// x86-only public HAL contract. Deliberately kept OUTSIDE of
// kernel/hal_interface.hpp: no other HAL port is required to implement this.
// Built on top of HAL_x86_ExecBiosInt (INT 13h extended disk services).

#include <kernel/__core.hpp>
#include <span.hpp>

/**
 * @ingroup BOOTDEV
 * @brief Method HAL_BOOTDEV_IsPresent
 *
 * Probes the first BIOS hard disk (DL=0x80) for INT 13h extensions via
 * AH=0x41 / BX=0x55AA. Returns true only when the call completes with CF
 * clear and BX flipped to 0xAA55. Implemented via HAL_x86_ExecBiosInt(0x13, ...).
 */
UNDOS_HAL_PUBLIC_V1API(bool, HAL_BOOTDEV_IsPresent);

/**
 * @ingroup BOOTDEV
 * @brief Method HAL_BOOTDEV_ReadSectors
 *
 * Reads `count` 512-byte sectors starting at absolute LBA `lba` from the
 * first BIOS hard disk (DL=0x80) into `buffer` using INT 13h extended read
 * (AH=0x42) with a Disk Address Packet. The DAP and transfer buffer live in
 * identity-mapped conventional memory; data is then copied up into `buffer`.
 * `buffer` must be large enough for `count * 512` bytes. Returns false on
 * BIOS error (CF set) or invalid arguments. Implemented via
 * HAL_x86_ExecBiosInt(0x13, ...).
 */
UNDOS_HAL_PUBLIC_V1API(bool, HAL_BOOTDEV_ReadSectors, uint32_t lba, uint32_t count, kstd::span<uint8_t> buffer);
