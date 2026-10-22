#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WINAPI
#define CALLBACK

typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint16_t ATOM;
typedef uint32_t DWORD;
typedef uint32_t UINT;
typedef uint32_t COLORREF;
typedef int32_t LONG;
typedef int32_t BOOL;
typedef intptr_t LONG_PTR;
typedef uintptr_t ULONG_PTR;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;
typedef intptr_t LRESULT;
typedef uintptr_t UINT_PTR;
typedef char TCHAR;
typedef const char *LPCSTR;
typedef const char *LPCTSTR;
typedef char *LPSTR;
typedef char *LPTSTR;
typedef void *LPVOID;

typedef struct HWND__ *HWND;
typedef struct HDC__ *HDC;
typedef struct HBRUSH__ *HBRUSH;
typedef struct HMENU__ *HMENU;
typedef struct HINSTANCE__ *HINSTANCE;
typedef struct HICON__ *HICON;
typedef struct HCURSOR__ *HCURSOR;

typedef struct tagPOINT {
  LONG x;
  LONG y;
} POINT;

typedef struct tagRECT {
  LONG left;
  LONG top;
  LONG right;
  LONG bottom;
} RECT;

typedef struct tagMSG {
  HWND hwnd;
  UINT message;
  WPARAM wParam;
  LPARAM lParam;
  DWORD time;
  POINT pt;
} MSG;

typedef struct tagPAINTSTRUCT {
  HDC hdc;
  BOOL fErase;
  RECT rcPaint;
  BOOL fRestore;
  BOOL fIncUpdate;
  BYTE rgbReserved[32];
} PAINTSTRUCT;

typedef LRESULT (CALLBACK *WNDPROC)(HWND, UINT, WPARAM, LPARAM);

typedef struct tagWNDCLASSA {
  UINT style;
  WNDPROC lpfnWndProc;
  int cbClsExtra;
  int cbWndExtra;
  HINSTANCE hInstance;
  HICON hIcon;
  HCURSOR hCursor;
  HBRUSH hbrBackground;
  LPCSTR lpszMenuName;
  LPCSTR lpszClassName;
} WNDCLASSA;

typedef WNDCLASSA WNDCLASS;

#define FALSE 0
#define TRUE 1

#define WM_NULL 0x0000
#define WM_CREATE 0x0001
#define WM_DESTROY 0x0002
#define WM_MOVE 0x0003
#define WM_SIZE 0x0005
#define WM_ACTIVATE 0x0006
#define WM_SETFOCUS 0x0007
#define WM_KILLFOCUS 0x0008
#define WM_PAINT 0x000F
#define WM_CLOSE 0x0010
#define WM_QUIT 0x0012
#define WM_DISPLAYCHANGE 0x007E
#define WM_NCHITTEST 0x0084
#define WM_NCMOUSEMOVE 0x00A0
#define WM_NCLBUTTONDOWN 0x00A1
#define WM_NCLBUTTONUP 0x00A2
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define WM_COMMAND 0x0111
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_LBUTTONDBLCLK 0x0203
#define WM_RBUTTONDOWN 0x0204
#define WM_RBUTTONUP 0x0205
#define WM_CAPTURECHANGED 0x0215
#define WM_USER 0x0400

#define WS_CHILD 0x40000000u
#define WS_VISIBLE 0x10000000u
#define WS_BORDER 0x00800000u
#define WS_CAPTION 0x00C00000u
#define WS_OVERLAPPED 0x00000000u
#define WS_OVERLAPPEDWINDOW 0x00CF0000u
#define WS_POPUP 0x80000000u
#define WS_SYSMENU 0x00080000u
#define WS_THICKFRAME 0x00040000u
#define WS_MINIMIZEBOX 0x00020000u
#define WS_MAXIMIZEBOX 0x00010000u

#define CW_USEDEFAULT ((int)0x80000000u)
#define GWLP_WNDPROC 0
#define GWLP_USERDATA 1
#define GWLP_STYLE 2

#define SW_HIDE 0
#define SW_SHOW 1
#define SW_MINIMIZE 2
#define SW_MAXIMIZE 3
#define SW_RESTORE 4

#define TRANSPARENT 1
#define OPAQUE 2
#define CLR_INVALID ((COLORREF)0xFFFFFFFFu)
#define UNDOS_PALETTE_BRUSH(index) ((HBRUSH)(uintptr_t)((index) + 1u))
#define MF_STRING 0x0000u
#define MF_GRAYED 0x0001u
#define MF_DISABLED 0x0002u
#define MF_POPUP 0x0010u
#define MF_SEPARATOR 0x0800u

#define RGB(r, g, b) \
  ((COLORREF)(((BYTE)(r)) | ((WORD)((BYTE)(g)) << 8) | (((DWORD)(BYTE)(b)) << 16)))
#define TEXT(value) value
#define _T(value) value

ATOM RegisterClassA(const WNDCLASSA *windowClass);
HWND CreateWindowExA(DWORD exStyle, LPCSTR className, LPCSTR windowName, DWORD style,
                     int x, int y, int width, int height, HWND parent, HMENU menu,
                     HINSTANCE instance, LPVOID parameter);
HWND CreateWindowA(LPCSTR className, LPCSTR windowName, DWORD style, int x, int y,
                   int width, int height, HWND parent, HMENU menu, HINSTANCE instance,
                   LPVOID parameter);
BOOL DestroyWindow(HWND window);
BOOL ShowWindow(HWND window, int command);
BOOL MoveWindow(HWND window, int x, int y, int width, int height, BOOL repaint);
BOOL InvalidateRect(HWND window, const RECT *rect, BOOL erase);
BOOL GetClientRect(HWND window, RECT *rect);
BOOL GetWindowRect(HWND window, RECT *rect);
LONG_PTR GetWindowLongPtrA(HWND window, int index);
LONG_PTR SetWindowLongPtrA(HWND window, int index, LONG_PTR value);
BOOL SetWindowTextA(HWND window, LPCSTR text);
HMENU CreateMenu(void);
HMENU CreatePopupMenu(void);
BOOL DestroyMenu(HMENU menu);
BOOL AppendMenuA(HMENU menu, UINT flags, UINT_PTR item, LPCSTR text);
BOOL SetMenu(HWND window, HMENU menu);
HMENU GetMenu(HWND window);
int GetMessageA(MSG *message, HWND window, UINT minimum, UINT maximum);
BOOL PeekMessageA(MSG *message, HWND window, UINT minimum, UINT maximum, UINT remove);
BOOL PostMessageA(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
void PostQuitMessage(int exitCode);
LRESULT DispatchMessageA(const MSG *message);
LRESULT DefWindowProcA(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
BOOL TranslateMessage(const MSG *message);

HDC BeginPaint(HWND window, PAINTSTRUCT *paint);
BOOL EndPaint(HWND window, const PAINTSTRUCT *paint);
BOOL Rectangle(HDC dc, int left, int top, int right, int bottom);
BOOL Ellipse(HDC dc, int left, int top, int right, int bottom);
BOOL MoveToEx(HDC dc, int x, int y, POINT *previous);
BOOL LineTo(HDC dc, int x, int y);
BOOL TextOutA(HDC dc, int x, int y, LPCSTR text, int length);
COLORREF SetTextColor(HDC dc, COLORREF color);
COLORREF SetBkColor(HDC dc, COLORREF color);
int SetBkMode(HDC dc, int mode);

DWORD GetLastError(void);
void SetLastError(DWORD error);

#define RegisterClass RegisterClassA
#define CreateWindowEx CreateWindowExA
#define CreateWindow CreateWindowA
#define GetWindowLongPtr GetWindowLongPtrA
#define SetWindowLongPtr SetWindowLongPtrA
#define GetWindowLong GetWindowLongPtrA
#define SetWindowLong SetWindowLongPtrA
#define SetWindowText SetWindowTextA
#define AppendMenu AppendMenuA
#define GetMessage GetMessageA
#define PeekMessage PeekMessageA
#define DispatchMessage DispatchMessageA
#define DefWindowProc DefWindowProcA
#define TextOut TextOutA

#ifdef __cplusplus
}
#endif
