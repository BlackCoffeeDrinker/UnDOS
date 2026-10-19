#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/**
 * Hide preserves state, Show preserves state, Restore restores saved geometry.
 * Minimize creates an icon plus label for children in the parent client
 * (roots retain a desktop caption). Click selects a child icon, double-click
 * or Enter restores it; Ctrl+Tab/F6 cycles including minimized child icons.
 * Maximize fills the parent client or desktop. Parent hide/minimize suppresses
 * descendant visibility.
 */
struct __SYS_USER_SHOWWINDOW_V1 {
    uint32_t hwnd;
    uint32_t command;
};

constexpr size_t   __SYS_USER_SHOWWINDOW_V1_SIZE = sizeof(struct __SYS_USER_SHOWWINDOW_V1);
constexpr uint32_t __SYS_USER_SHOWWINDOW_V1_CALLNUMBER = 403;

