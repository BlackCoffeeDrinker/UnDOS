#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/**
 * Focus a visible same-process window and activate its root. A minimized
 * child icon may receive focus for Enter/Alt+Space; minimized roots cannot.
 * hwnd == 0 clears this process's focus without changing activation.
 * Key lParam: bits 0..7 scan code, 8..10 key_flags, 16..24 modifier_flags.
 * Char lParam carries the same metadata. Mouse wParam low bits: left=1,right=2;
 * modifier_flags occupy bits 16..24. Mouse lParam packs signed client x/y.
 */
struct __SYS_USER_SETFOCUS_V1 {
    uint32_t hwnd;
};

constexpr size_t   __SYS_USER_SETFOCUS_V1_SIZE = sizeof(struct __SYS_USER_SETFOCUS_V1);
constexpr uint32_t __SYS_USER_SETFOCUS_V1_CALLNUMBER = 417;

