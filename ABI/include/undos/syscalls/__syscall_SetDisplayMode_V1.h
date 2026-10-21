#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/**
 * Activates or changes the primary display. Any process may call this.
 * modeId comes from EnumDisplayModes, not its enumeration ordinal.
 * Existing screen DC handles remain valid; pixels are lost and all windows
 * repaint. DisplayChange is coalesced per window and survives a full queue.
 * Invalid/monitor-rejected IDs fail without changing the display. Allocation
 * failures preserve the previous mode. Hardware enable failure reports UNKNOWN;
 * GetDisplayMode then reports the programmed mode as inactive.
 * This call does not persist a preference.
 */
struct __SYS_GFX_SETDISPLAYMODE_V1 {
    uint32_t modeId;
};

constexpr size_t   __SYS_GFX_SETDISPLAYMODE_V1_SIZE = sizeof(struct __SYS_GFX_SETDISPLAYMODE_V1);
constexpr uint32_t __SYS_GFX_SETDISPLAYMODE_V1_CALLNUMBER = 352;

