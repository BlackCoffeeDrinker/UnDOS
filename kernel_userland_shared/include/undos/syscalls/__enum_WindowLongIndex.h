#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

enum WindowLongIndex {
    WindowLongIndex_WndProc = 0,
    WindowLongIndex_UserData = 1,
    WindowLongIndex_Style = 2,
    /** Parent HWND, or zero for a root. Read-only. */
    WindowLongIndex_Parent = 3,
};
