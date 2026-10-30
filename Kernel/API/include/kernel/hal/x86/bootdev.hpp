#pragma once

// x86-only public HAL contract. Deliberately kept OUTSIDE of
// kernel/hal_interface.hpp: no other HAL port is required to implement this.
// Built on top of HAL_x86_ExecBiosInt (INT 13h disk services).

#include <kernel/__core.hpp>
#include <span.hpp>

/**
 * @ingroup BOOTDEV
 * @brief Method HAL_BOOTDEV_IsPresent
 *
 * Probes the first BIOS hard disk (DL=0x80). Returns true when EDD is
 * advertised by AH=0x41 (CF clear, BX=0xAA55, CX bit 0 set), or when AH=0x08
 * returns usable CHS geometry. Implemented via
 * HAL_x86_ExecBiosInt(0x13, ...).
 */
UNDOS_HAL_PUBLIC_V1API(bool, HAL_BOOTDEV_IsPresent);

/**
 * @ingroup BOOTDEV
 * @brief Method HAL_BOOTDEV_ReadSectors
 *
 * Reads `count` 512-byte sectors starting at absolute LBA `lba` from the
 * first BIOS hard disk (DL=0x80) into `buffer`. Uses INT 13h extended read
 * (AH=0x42) when EDD is available; otherwise it converts LBA to BIOS CHS
 * geometry and uses track-limited AH=0x02 reads. Transfers use the existing
 * identity-mapped conventional-memory scratch buffer and are copied into
 * `buffer` only after a successful BIOS call. `buffer` must be large enough
 * for `count * 512` bytes, and the final LBA must not overflow. A zero count
 * succeeds without a BIOS call. Returns false on BIOS error (CF set), invalid
 * arguments, or unsupported geometry. A failure after successful chunks can
 * leave that completed prefix in `buffer`. An EDD read error is returned
 * directly and does not trigger a CHS retry. Implemented via
 * HAL_x86_ExecBiosInt(0x13, ...).
 */
UNDOS_HAL_PUBLIC_V1API(bool, HAL_BOOTDEV_ReadSectors, uint32_t lba, uint32_t count, kstd::span<uint8_t> buffer);
