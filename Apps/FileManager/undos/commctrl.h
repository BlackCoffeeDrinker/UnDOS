/* UnDOS: commctrl.h shim. Only the status bar is provided (as a stub in
   stubs.c that creates no window, so SendMessage to it fails harmlessly);
   the toolbar is removed from the UnDOS WINFILE (tbar.c). */
#pragma once
#include <windows.h>

#define SB_SETTEXT     (WM_USER + 1)
#define SB_GETTEXT     (WM_USER + 2)
#define SB_SETPARTS    (WM_USER + 4)
#define SB_SIMPLE      (WM_USER + 9)
#define SBT_NOBORDERS  0x0100
#define SBT_POPOUT     0x0200
#define CCS_ADJUSTABLE 0x0020
#define TTN_NEEDTEXT   (0U - 520U)

HWND CreateStatusWindow(LONG style, LPCSTR lpszText, HWND hwndParent, UINT wID);
