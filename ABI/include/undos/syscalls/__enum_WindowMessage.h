#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

enum WindowMessage {
    WindowMessage_Null = 0,
    WindowMessage_Create = 1,
    WindowMessage_Destroy = 2,
    WindowMessage_Move = 3,
    WindowMessage_Size = 5,
    WindowMessage_Activate = 6,
    WindowMessage_SetFocus = 7,
    WindowMessage_KillFocus = 8,
    WindowMessage_Paint = 15,
    WindowMessage_Close = 16,
    WindowMessage_Quit = 18,
    /**
     * Coalesced display change; wParam=bpp, lParam=height<<16 | width.
     * An inactive output reports zero dimensions and bpp. Query GetDisplayMode
     * for the current generation and full state.
     */
    WindowMessage_DisplayChange = 126,
    WindowMessage_NcHitTest = 132,
    WindowMessage_NcMouseMove = 160,
    /**
     * Single system-box click: wParam=SysMenu (3), lParam signed screen x/y.
     * Userland presents the system menu; system-box double-click sends Close.
     */
    WindowMessage_NcLButtonDown = 161,
    WindowMessage_NcLButtonUp = 162,
    WindowMessage_KeyDown = 256,
    WindowMessage_KeyUp = 257,
    WindowMessage_Char = 258,
    WindowMessage_MouseMove = 512,
    WindowMessage_LButtonDown = 513,
    WindowMessage_LButtonUp = 514,
    WindowMessage_LButtonDblClk = 515,
    WindowMessage_RButtonDown = 516,
    WindowMessage_RButtonUp = 517,
    WindowMessage_CaptureChanged = 533,
    WindowMessage_User = 1024,
};
