#ifndef UNDOS_WINDOWS_H
#define UNDOS_WINDOWS_H

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

typedef struct tagPOINT { LONG x, y; } POINT;
typedef struct tagRECT { LONG left, top, right, bottom; } RECT;
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
typedef struct tagWNDCLASS {
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
} WNDCLASS;

#define FALSE 0
#define TRUE 1
#define ERROR_SUCCESS 0
#define ERROR_INVALID_FUNCTION 1
#define ERROR_INVALID_HANDLE 6
#define ERROR_NOT_ENOUGH_MEMORY 8
#define ERROR_GEN_FAILURE 31
#define ERROR_INVALID_PARAMETER 87
#define ERROR_INSUFFICIENT_BUFFER 122
#define ERROR_CALL_NOT_IMPLEMENTED 120
#define ERROR_INVALID_WINDOW_HANDLE 1400
#define ERROR_CLASS_ALREADY_EXISTS 1410
#define ERROR_CLASS_DOES_NOT_EXIST 1411
#define ERROR_TIMEOUT 1460
#define ERROR_NOT_FOUND 1168
#define ERROR_RETRY 1237
#define ERROR_NO_UNICODE_TRANSLATION 1113
#define ERROR_NOACCESS 998

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
#define WM_FONTCHANGED 0x007D
#define WM_NCHITTEST 0x0084
#define WM_NCMOUSEMOVE 0x00A0
#define WM_NCLBUTTONDOWN 0x00A1
#define WM_NCLBUTTONUP 0x00A2
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define WM_COMMAND 0x0111
#define WM_SYSCOMMAND 0x0112
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_LBUTTONDBLCLK 0x0203
#define WM_RBUTTONDOWN 0x0204
#define WM_RBUTTONUP 0x0205
#define WM_NCRBUTTONDOWN 0x00A4
#define WM_NCRBUTTONUP 0x00A5
#define WM_NCLBUTTONDBLCLK 0x00A3
#define WM_NCRBUTTONDBLCLK 0x00A6
#define WM_CAPTURECHANGED 0x0215
#define WM_USER 0x0400

#define VK_TAB 0x09
#define VK_ESCAPE 0x1B
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define HTSYSMENU 3
#define SC_SIZE 0xF000u
#define SC_MOVE 0xF010u
#define SC_MINIMIZE 0xF020u
#define SC_MAXIMIZE 0xF030u
#define SC_RESTORE 0xF120u
#define SC_CLOSE 0xF060u

#define WS_OVERLAPPED 0x00000000u
#define WS_POPUP 0x80000000u
#define WS_CHILD 0x40000000u
#define WS_MINIMIZE 0x20000000u
#define WS_VISIBLE 0x10000000u
#define WS_DISABLED 0x08000000u
#define WS_CLIPSIBLINGS 0x04000000u
#define WS_CLIPCHILDREN 0x02000000u
#define WS_MAXIMIZE 0x01000000u
#define WS_CAPTION 0x00C00000u
#define WS_BORDER 0x00800000u
#define WS_SYSMENU 0x00080000u
#define WS_THICKFRAME 0x00040000u
#define WS_MINIMIZEBOX 0x00020000u
#define WS_MAXIMIZEBOX 0x00010000u
#define WS_OVERLAPPEDWINDOW 0x00CF0000u

#define CW_USEDEFAULT ((int)0x80000000u)
#define GWLP_WNDPROC (-4)
#define GWLP_USERDATA (-21)
#define GWL_STYLE (-16)
#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SW_SHOWMINIMIZED 2
#define SW_MAXIMIZE 3
#define SW_SHOWMAXIMIZED 3
#define SW_SHOW 5
#define SW_MINIMIZE 6
#define SW_RESTORE 9
#define PM_NOREMOVE 0x0000u
#define PM_REMOVE 0x0001u
#define TRANSPARENT 1
#define OPAQUE 2
#define CLR_INVALID ((COLORREF)0xFFFFFFFFu)
#define MF_STRING 0x0000u
#define MF_GRAYED 0x0001u
#define MF_DISABLED 0x0002u
#define MF_BITMAP 0x0004u
#define MF_CHECKED 0x0008u
#define MF_POPUP 0x0010u
#define MF_OWNERDRAW 0x0100u
#define MF_SEPARATOR 0x0800u

#define RGB(r, g, b) \
  ((COLORREF)(((BYTE)(r)) | ((WORD)((BYTE)(g)) << 8) | \
              (((DWORD)(BYTE)(b)) << 16)))
#define TEXT(value) value
#define _T(value) value

ATOM RegisterClass(const WNDCLASS *windowClass);
HWND CreateWindowEx(DWORD exStyle, LPCSTR className, LPCSTR windowName, DWORD style,
                    int x, int y, int width, int height, HWND parent, HMENU menu,
                    HINSTANCE instance, LPVOID parameter);
HWND CreateWindow(LPCSTR className, LPCSTR windowName, DWORD style,
                  int x, int y, int width, int height, HWND parent, HMENU menu,
                  HINSTANCE instance, LPVOID parameter);
BOOL DestroyWindow(HWND window);
BOOL ShowWindow(HWND window, int command);
BOOL MoveWindow(HWND window, int x, int y, int width, int height, BOOL repaint);
BOOL InvalidateRect(HWND window, const RECT *rect, BOOL erase);
BOOL GetClientRect(HWND window, RECT *rect);
BOOL GetWindowRect(HWND window, RECT *rect);
LONG_PTR GetWindowLongPtr(HWND window, int index);
LONG_PTR SetWindowLongPtr(HWND window, int index, LONG_PTR value);
LONG GetWindowLong(HWND window, int index);
LONG SetWindowLong(HWND window, int index, LONG value);
BOOL SetWindowText(HWND window, LPCSTR text);
HMENU CreateMenu(void);
HMENU CreatePopupMenu(void);
BOOL DestroyMenu(HMENU menu);
BOOL AppendMenu(HMENU menu, UINT flags, UINT_PTR item, LPCSTR text);
BOOL SetMenu(HWND window, HMENU menu);
HMENU GetMenu(HWND window);
int GetMessage(MSG *message, HWND window, UINT minimum, UINT maximum);
BOOL PeekMessage(MSG *message, HWND window, UINT minimum, UINT maximum, UINT remove);
BOOL PostMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
void PostQuitMessage(int exitCode);
LRESULT DispatchMessage(const MSG *message);
LRESULT DefWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
BOOL TranslateMessage(const MSG *message);
HDC BeginPaint(HWND window, PAINTSTRUCT *paint);
BOOL EndPaint(HWND window, const PAINTSTRUCT *paint);
COLORREF SetTextColor(HDC dc, COLORREF color);
COLORREF SetBkColor(HDC dc, COLORREF color);
int SetBkMode(HDC dc, int mode);
DWORD GetLastError(void);
void SetLastError(DWORD error);

#ifdef __cplusplus
}
#endif
#endif
