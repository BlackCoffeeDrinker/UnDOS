#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

enum HitTest {
    HitTest_Transparent = 1,
    HitTest_Nowhere = 0,
    HitTest_Client = 1,
    HitTest_Caption = 2,
    HitTest_SysMenu = 3,
    HitTest_MinButton = 8,
    HitTest_MaxButton = 9,
    HitTest_Left = 10,
    HitTest_Right = 11,
    HitTest_Top = 12,
    HitTest_TopLeft = 13,
    HitTest_TopRight = 14,
    HitTest_Bottom = 15,
    HitTest_BottomLeft = 16,
    HitTest_BottomRight = 17,
    HitTest_Border = 18,
    HitTest_Close = 20,
};
