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
    WindowMessage_Paint = 15,
    WindowMessage_Close = 16,
    WindowMessage_Quit = 18,
    WindowMessage_NcHitTest = 132,
    WindowMessage_NcMouseMove = 160,
    WindowMessage_NcLButtonDown = 161,
    WindowMessage_NcLButtonUp = 162,
    WindowMessage_KeyDown = 256,
    WindowMessage_KeyUp = 257,
    WindowMessage_Char = 258,
    WindowMessage_MouseMove = 512,
    WindowMessage_LButtonDown = 513,
    WindowMessage_LButtonUp = 514,
    WindowMessage_RButtonDown = 516,
    WindowMessage_RButtonUp = 517,
    WindowMessage_User = 1024,
};
