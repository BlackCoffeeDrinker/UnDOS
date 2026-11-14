#ifndef UNDOS_WINDOWSX_H
#define UNDOS_WINDOWSX_H

/* windowsx.h message crackers and GDI selection macros, 32-bit (Win32)
   message packing. */

#include <windows.h>

#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#endif
#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif

#define GET_WM_ACTIVATE_STATE(wp, lp) LOWORD(wp)
#define GET_WM_ACTIVATE_FMINIMIZED(wp, lp) ((BOOL)HIWORD(wp))
#define GET_WM_ACTIVATE_HWND(wp, lp) ((HWND)(lp))

#define GET_WM_COMMAND_ID(wp, lp) LOWORD(wp)
#define GET_WM_COMMAND_HWND(wp, lp) ((HWND)(lp))
#define GET_WM_COMMAND_CMD(wp, lp) HIWORD(wp)
#define GET_WM_COMMAND_MPS(id, hwnd, cmd) \
  (WPARAM)MAKELONG(id, cmd), (LPARAM)(hwnd)

#define GET_WM_MENUSELECT_CMD(wp, lp) LOWORD(wp)
#define GET_WM_MENUSELECT_FLAGS(wp, lp) ((UINT)(int)(short)HIWORD(wp))
#define GET_WM_MENUSELECT_HMENU(wp, lp) ((HMENU)(lp))
#define GET_WM_MENUSELECT_MPS(cmd, f, hmenu) \
  (WPARAM)MAKELONG(cmd, f), (LPARAM)(hmenu)

#define GET_WM_MDIACTIVATE_FACTIVATE(hwnd, wp, lp) ((LPARAM)(hwnd) == (lp))
#define GET_WM_MDIACTIVATE_HWNDDEACT(wp, lp) ((HWND)(wp))
#define GET_WM_MDIACTIVATE_HWNDACTIVATE(wp, lp) ((HWND)(lp))
#define GET_WM_MDIACTIVATE_MPS(f, hwndD, hwndA) (WPARAM)(hwndA), 0

#define GET_WM_VKEYTOITEM_CODE(wp, lp) ((int)(short)LOWORD(wp))
#define GET_WM_VKEYTOITEM_ITEM(wp, lp) HIWORD(wp)
#define GET_WM_VKEYTOITEM_HWND(wp, lp) ((HWND)(lp))
#define GET_WM_CHARTOITEM_CHAR(wp, lp) ((TCHAR)LOWORD(wp))
#define GET_WM_CHARTOITEM_POS(wp, lp) HIWORD(wp)
#define GET_WM_CHARTOITEM_HWND(wp, lp) ((HWND)(lp))

#define GET_WM_VSCROLL_CODE(wp, lp) LOWORD(wp)
#define GET_WM_VSCROLL_POS(wp, lp) HIWORD(wp)
#define GET_WM_VSCROLL_HWND(wp, lp) ((HWND)(lp))
#define GET_WM_HSCROLL_CODE(wp, lp) LOWORD(wp)
#define GET_WM_HSCROLL_POS(wp, lp) HIWORD(wp)
#define GET_WM_HSCROLL_HWND(wp, lp) ((HWND)(lp))

#define GET_WM_CTLCOLOR_HDC(wp, lp, msg) ((HDC)(wp))
#define GET_WM_CTLCOLOR_HWND(wp, lp, msg) ((HWND)(lp))

#define GET_WM_PARENTNOTIFY_MSG(wp, lp) LOWORD(wp)
#define GET_WM_PARENTNOTIFY_ID(wp, lp) HIWORD(wp)
#define GET_WM_PARENTNOTIFY_HWNDCHILD(wp, lp) ((HWND)(lp))

#define SelectFont(hdc, hfont) ((HFONT)SelectObject((hdc), (HGDIOBJ)(hfont)))
#define SelectBrush(hdc, hbr) ((HBRUSH)SelectObject((hdc), (HGDIOBJ)(hbr)))
#define SelectPen(hdc, hpen) ((HPEN)SelectObject((hdc), (HGDIOBJ)(hpen)))
#define SelectBitmap(hdc, hbm) ((HBITMAP)SelectObject((hdc), (HGDIOBJ)(hbm)))
#define DeleteFont(hfont) DeleteObject((HGDIOBJ)(hfont))
#define DeleteBrush(hbr) DeleteObject((HGDIOBJ)(hbr))
#define DeletePen(hpen) DeleteObject((HGDIOBJ)(hpen))
#define DeleteBitmap(hbm) DeleteObject((HGDIOBJ)(hbm))
#define GetStockBrush(i) ((HBRUSH)GetStockObject(i))
#define GetStockPen(i) ((HPEN)GetStockObject(i))
#define GetStockFont(i) ((HFONT)GetStockObject(i))

#define GetWindowStyle(hwnd) ((DWORD)GetWindowLong(hwnd, GWL_STYLE))
#define GetWindowID(hwnd) GetDlgCtrlID(hwnd)

#define GlobalAllocPtr(flags, cb) GlobalLock(GlobalAlloc((flags), (cb)))
#define GlobalFreePtr(lp) GlobalFree(GlobalHandle(lp))
#define GlobalPtrHandle(lp) GlobalHandle(lp)

#endif
