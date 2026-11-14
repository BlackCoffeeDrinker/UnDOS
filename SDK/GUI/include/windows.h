#ifndef UNDOS_WINDOWS_H
#define UNDOS_WINDOWS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WINAPI
#define APIENTRY WINAPI
#define CALLBACK

typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef int16_t SHORT;
typedef uint16_t ATOM;
typedef uint32_t DWORD;
typedef uint32_t UINT;
typedef uint32_t COLORREF;
typedef int32_t LONG;
typedef int INT;
#define VOID void
typedef int32_t BOOL;
typedef intptr_t LONG_PTR;
typedef uintptr_t ULONG_PTR;
typedef uintptr_t DWORD_PTR;
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
typedef const void *LPCVOID;
typedef void *PVOID;
typedef uint16_t USHORT;
typedef uint32_t ULONG;
typedef uint8_t UCHAR;
typedef BYTE *PBYTE;
typedef BYTE *LPBYTE;
typedef WORD *LPWORD;
typedef WORD *PWORD;
typedef DWORD *LPDWORD;
typedef DWORD *PDWORD;
typedef INT *LPINT;
typedef INT *PINT;
typedef UINT *PUINT;
typedef LONG *LPLONG;
typedef LONG *PLONG;
typedef ULONG *PULONG;
typedef BOOL *LPBOOL;
typedef BOOL *PBOOL;
typedef char CHAR;
typedef CHAR *PSTR;
typedef const CHAR *PCSTR;
typedef TCHAR *PTSTR;
typedef const TCHAR *PCTSTR;
typedef size_t SIZE_T;
typedef int64_t LONGLONG;
typedef uint64_t ULONGLONG;
typedef union _LARGE_INTEGER {
  struct { DWORD LowPart; LONG HighPart; };
  struct { DWORD LowPart; LONG HighPart; } u;
  LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;
typedef union _ULARGE_INTEGER {
  struct { DWORD LowPart; DWORD HighPart; };
  struct { DWORD LowPart; DWORD HighPart; } u;
  ULONGLONG QuadPart;
} ULARGE_INTEGER, *PULARGE_INTEGER;

/* SAL-style annotations and further basic Win32 types. */
#ifndef IN
#define IN
#endif
#ifndef OUT
#define OUT
#endif
#ifndef OPTIONAL
#define OPTIONAL
#endif
#define UNREFERENCED_PARAMETER(p) ((void)(p))
#define FIELD_OFFSET(type, field) ((LONG)__builtin_offsetof(type, field))
typedef unsigned char BOOLEAN, *PBOOLEAN;
typedef DWORD LCID, LCTYPE;
typedef USHORT LANGID;
typedef LONG HRESULT;
typedef void *HKEY;
typedef void *HHOOK;
typedef double DATE;
#define SUCCEEDED(hr) ((HRESULT)(hr) >= 0)
#define FAILED(hr) ((HRESULT)(hr) < 0)
#define S_OK ((HRESULT)0)
#define S_FALSE ((HRESULT)1)
#define E_FAIL ((HRESULT)0x80004005L)
#define E_NOTIMPL ((HRESULT)0x80004001L)
#define MAKEWORD(low, high) ((WORD)(((BYTE)(low)) | ((WORD)((BYTE)(high))) << 8))
#define MAKELANGID(primary, sub) ((((WORD)(sub)) << 10) | (WORD)(primary))
#define PRIMARYLANGID(lang) ((WORD)(lang) & 0x3ff)
#define SUBLANGID(lang) ((WORD)(lang) >> 10)
#define MAKELCID(lang, sort) ((DWORD)((((DWORD)((WORD)(sort))) << 16) | ((DWORD)((WORD)(lang)))))
#define LANGIDFROMLCID(lcid) ((WORD)(lcid))
#define LANG_NEUTRAL 0x00
#define LANG_ENGLISH 0x09
#define SUBLANG_DEFAULT 0x01
#define SUBLANG_NEUTRAL 0x00

typedef struct HWND__ *HWND;
typedef struct HDC__ *HDC;
typedef struct HBRUSH__ *HBRUSH;
typedef struct HMENU__ *HMENU;
typedef struct HINSTANCE__ *HINSTANCE;
typedef struct HICON__ *HICON;
typedef struct HCURSOR__ *HCURSOR;
typedef struct HACCEL__ *HACCEL;
typedef void *HGDIOBJ;
typedef struct HPEN__ *HPEN;
typedef struct HFONT__ *HFONT;
typedef struct HBITMAP__ *HBITMAP;
typedef struct HRSRC__ *HRSRC;
typedef void *HGLOBAL;
typedef void *HLOCAL;

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

typedef struct tagSIZE { LONG cx, cy; } SIZE, *LPSIZE;
typedef RECT *PRECT, *LPRECT, *NPRECT;
typedef const RECT *LPCRECT;
typedef POINT *PPOINT, *LPPOINT;
typedef SIZE *PSIZE;
typedef MSG *PMSG, *LPMSG;
typedef PAINTSTRUCT *LPPAINTSTRUCT;
typedef WNDCLASS WNDCLASSA, *LPWNDCLASS;
typedef struct tagPOINTS { SHORT x, y; } POINTS, *PPOINTS;
typedef struct _POINTL { LONG x, y; } POINTL, *PPOINTL;
/* As in Win32: `pts` is a packed LONG/LPARAM (x low word, y high word) and
   POINTTOPOINTS yields one. */
#define POINTSTOPOINT(pt, pts) \
  ((pt).x = (LONG)(SHORT)((DWORD)(pts) & 0xFFFF), (pt).y = (LONG)(SHORT)(((DWORD)(pts) >> 16) & 0xFFFF))
#define POINTTOPOINTS(pt) ((LONG)((((DWORD)(WORD)(SHORT)(pt).y) << 16) | (DWORD)(WORD)(SHORT)(pt).x))
#define MAKEPOINTS(l) (*(POINTS *)&(l))
typedef struct tagCREATESTRUCT {
  LPVOID lpCreateParams;
  HINSTANCE hInstance;
  HMENU hMenu;
  HWND hwndParent;
  int cy, cx, y, x;
  LONG style;
  LPCSTR lpszName;
  LPCSTR lpszClass;
  DWORD dwExStyle;
} CREATESTRUCT, *LPCREATESTRUCT;

typedef struct tagBITMAP {
  LONG bmType;
  LONG bmWidth;
  LONG bmHeight;
  LONG bmWidthBytes;
  WORD bmPlanes;
  WORD bmBitsPixel;
  LPVOID bmBits;
} BITMAP;

typedef struct tagRGBQUAD {
  BYTE rgbBlue;
  BYTE rgbGreen;
  BYTE rgbRed;
  BYTE rgbReserved;
} RGBQUAD;

typedef struct tagBITMAPINFOHEADER {
  DWORD biSize;
  LONG biWidth;
  LONG biHeight;
  WORD biPlanes;
  WORD biBitCount;
  DWORD biCompression;
  DWORD biSizeImage;
  LONG biXPelsPerMeter;
  LONG biYPelsPerMeter;
  DWORD biClrUsed;
  DWORD biClrImportant;
} BITMAPINFOHEADER, *PBITMAPINFOHEADER, *LPBITMAPINFOHEADER;

typedef struct tagBITMAPINFO {
  BITMAPINFOHEADER bmiHeader;
  RGBQUAD bmiColors[1];
} BITMAPINFO;

typedef struct tagTEXTMETRICA {
  LONG tmHeight;
  LONG tmAscent;
  LONG tmDescent;
  LONG tmInternalLeading;
  LONG tmExternalLeading;
  LONG tmAveCharWidth;
  LONG tmMaxCharWidth;
  LONG tmWeight;
  LONG tmOverhang;
  LONG tmDigitizedAspectX;
  LONG tmDigitizedAspectY;
  BYTE tmFirstChar;
  BYTE tmLastChar;
  BYTE tmDefaultChar;
  BYTE tmBreakChar;
  BYTE tmItalic;
  BYTE tmUnderlined;
  BYTE tmStruckOut;
  BYTE tmPitchAndFamily;
  BYTE tmCharSet;
} TEXTMETRIC, *LPTEXTMETRIC;

typedef struct tagACCEL {
  BYTE fVirt;
  WORD key;
  WORD cmd;
} ACCEL, *LPACCEL;

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
#define ERROR_INVALID_INDEX 1413
#define CS_VREDRAW 0x0001
#define CS_HREDRAW 0x0002
#define CS_DBLCLKS 0x0008
#define ERROR_CLASS_ALREADY_EXISTS 1410
#define ERROR_CLASS_DOES_NOT_EXIST 1411
#define ERROR_TIMEOUT 1460
#define ERROR_NOT_FOUND 1168
#define ERROR_RETRY 1237
#define ERROR_NO_UNICODE_TRANSLATION 1113
#define ERROR_NOACCESS 998
#define ERROR_INVALID_MENU_HANDLE 1401
#define ERROR_INVALID_ACCEL_HANDLE 1403
#define ERROR_MENU_ITEM_NOT_FOUND 1456
#define ERROR_RESOURCE_NAME_NOT_FOUND 1814

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
#define WM_SYSKEYDOWN 0x0104
#define WM_SYSKEYUP 0x0105
#define WM_SYSCHAR 0x0106
#define WM_INITMENU 0x0116
#define WM_INITMENUPOPUP 0x0117
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
#define VK_BACK 0x08
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_CAPITAL 0x14
#define VK_NUMLOCK 0x90
#define VK_SCROLL 0x91
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_MULTIPLY 0x6A
#define VK_ADD 0x6B
#define VK_SUBTRACT 0x6D
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_F10 0x79
#define VK_F11 0x7A
#define VK_F12 0x7B
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_LMENU 0xA4
#define VK_RMENU 0xA5
#define VK_BROWSER_BACK 0xA6
#define VK_BROWSER_FORWARD 0xA7
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
#define HGDI_ERROR ((HGDIOBJ)(intptr_t)-1)
#define ERROR_INVALID_FLAGS 1004
#define WHITE_BRUSH 0
#define LTGRAY_BRUSH 1
#define GRAY_BRUSH 2
#define DKGRAY_BRUSH 3
#define BLACK_BRUSH 4
#define NULL_BRUSH 5
#define HOLLOW_BRUSH NULL_BRUSH
#define WHITE_PEN 6
#define BLACK_PEN 7
#define NULL_PEN 8
#define OEM_FIXED_FONT 10
#define ANSI_FIXED_FONT 11
#define ANSI_VAR_FONT 12
#define SYSTEM_FONT 13
#define DEVICE_DEFAULT_FONT 14
#define SYSTEM_FIXED_FONT 16
#define DEFAULT_GUI_FONT 17
#define PS_SOLID 0
#define PS_DASH 1
#define PS_DOT 2
#define PS_DASHDOT 3
#define PS_DASHDOTDOT 4
#define PS_NULL 5
#define PS_INSIDEFRAME 6
#define ETO_OPAQUE 0x0002u
#define ETO_CLIPPED 0x0004u
#define DT_TOP 0x0000u
#define DT_LEFT 0x0000u
#define DT_CENTER 0x0001u
#define DT_RIGHT 0x0002u
#define DT_VCENTER 0x0004u
#define DT_BOTTOM 0x0008u
#define DT_WORDBREAK 0x0010u
#define DT_SINGLELINE 0x0020u
#define DT_EXPANDTABS 0x0040u
#define DT_NOCLIP 0x0100u
#define DT_CALCRECT 0x0400u
#define DT_NOPREFIX 0x0800u
#define PATCOPY 0x00F00021u
#define PATINVERT 0x005A0049u
#define DSTINVERT 0x00550009u
#define BLACKNESS 0x00000042u
#define WHITENESS 0x00FF0062u
#define FW_NORMAL 400
#define TMPF_FIXED_PITCH 0x01u
#define SRCCOPY 0x00CC0020u
#define BI_RGB 0u
#define DIB_RGB_COLORS 0u
#define MAKEINTRESOURCE(id) ((LPSTR)(uintptr_t)(WORD)(id))
#define IS_INTRESOURCE(p) (((uintptr_t)(p) >> 16) == 0)
#define RT_BITMAP MAKEINTRESOURCE(2)
#define RT_ICON MAKEINTRESOURCE(3)
#define RT_MENU MAKEINTRESOURCE(4)
#define RT_STRING MAKEINTRESOURCE(6)
#define RT_ACCELERATOR MAKEINTRESOURCE(9)
#define ERROR_RESOURCE_TYPE_NOT_FOUND 1813
#define MF_STRING 0x0000u
#define MF_GRAYED 0x0001u
#define MF_DISABLED 0x0002u
#define MF_BITMAP 0x0004u
#define MF_CHECKED 0x0008u
#define MF_POPUP 0x0010u
#define MF_OWNERDRAW 0x0100u
#define MF_SEPARATOR 0x0800u
/* Menu-template flag: last item of a popup (resource data only). */
#define MF_END 0x0080u
#define MF_MENUBARBREAK 0x0020u
#define MF_MENUBREAK 0x0040u
#define MF_HELP 0x4000u
#define MF_ENABLED 0x0000u
#define MF_UNCHECKED 0x0000u
#define MF_BYCOMMAND 0x0000u
#define MF_BYPOSITION 0x0400u
#define FVIRTKEY 0x01u
#define FNOINVERT 0x02u
#define FSHIFT 0x04u
#define FCONTROL 0x08u
#define FALT 0x10u

#define RGB(r, g, b) \
  ((COLORREF)(((BYTE)(r)) | ((WORD)((BYTE)(g)) << 8) | \
              (((DWORD)(BYTE)(b)) << 16)))
#define LOWORD(value) ((WORD)(((DWORD_PTR)(value)) & 0xFFFFu))
#define HIWORD(value) ((WORD)((((DWORD_PTR)(value)) >> 16) & 0xFFFFu))
#define LOBYTE(value) ((BYTE)(((DWORD_PTR)(value)) & 0xFFu))
#define HIBYTE(value) ((BYTE)((((DWORD_PTR)(value)) >> 8) & 0xFFu))
#define MAKELONG(low, high) \
  ((LONG)(((WORD)((DWORD_PTR)(low) & 0xFFFFu)) | \
          ((DWORD)((WORD)((DWORD_PTR)(high) & 0xFFFFu))) << 16))
#define MAKEWPARAM(low, high) ((WPARAM)(DWORD)MAKELONG(low, high))
#define MAKELPARAM(low, high) ((LPARAM)(DWORD)MAKELONG(low, high))
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
DWORD CheckMenuItem(HMENU menu, UINT item, UINT flags);
BOOL EnableMenuItem(HMENU menu, UINT item, UINT flags);
UINT GetMenuState(HMENU menu, UINT item, UINT flags);
HMENU GetSubMenu(HMENU menu, int position);
UINT GetMenuItemID(HMENU menu, int position);
int GetMenuItemCount(HMENU menu);
HMENU LoadMenuIndirect(const void *menuTemplate);
HMENU LoadMenu(HINSTANCE instance, LPCSTR name);
BOOL InsertMenu(HMENU menu, UINT position, UINT flags, UINT_PTR item, LPCSTR text);
HACCEL CreateAcceleratorTable(LPACCEL entries, int count);
BOOL DestroyAcceleratorTable(HACCEL table);
HACCEL LoadAccelerators(HINSTANCE instance, LPCSTR name);
int TranslateAccelerator(HWND window, HACCEL table, MSG *message);
SHORT GetKeyState(int virtualKey);
BOOL TextOut(HDC dc, int x, int y, LPCSTR text, int count);
BOOL ExtTextOut(HDC dc, int x, int y, UINT options, const RECT *rect,
                LPCSTR text, UINT count, const int *dx);
int DrawText(HDC dc, LPCSTR text, int count, RECT *rect, UINT format);
BOOL GetTextExtentPoint32(HDC dc, LPCSTR text, int count, SIZE *size);
BOOL GetTextExtentPoint(HDC dc, LPCSTR text, int count, SIZE *size);
BOOL GetTextMetrics(HDC dc, TEXTMETRIC *metrics);
HPEN CreatePen(int style, int width, COLORREF color);
HBRUSH CreateSolidBrush(COLORREF color);
BOOL MoveToEx(HDC dc, int x, int y, POINT *previous);
BOOL LineTo(HDC dc, int x, int y);
int FillRect(HDC dc, const RECT *rect, HBRUSH brush);
BOOL PatBlt(HDC dc, int x, int y, int width, int height, DWORD rop);
HGDIOBJ SelectObject(HDC dc, HGDIOBJ object);
BOOL DeleteObject(HGDIOBJ object);
HGDIOBJ GetStockObject(int object);
BOOL SetRect(RECT *rect, int left, int top, int right, int bottom);
HDC CreateCompatibleDC(HDC dc);
BOOL DeleteDC(HDC dc);
HBITMAP CreateCompatibleBitmap(HDC dc, int width, int height);
HBITMAP CreateBitmap(int width, int height, UINT planes, UINT bitsPerPixel,
                     const void *bits);
int SetDIBits(HDC dc, HBITMAP bitmap, UINT start, UINT lines,
              const void *bits, const BITMAPINFO *info, UINT usage);
BOOL BitBlt(HDC dst, int x, int y, int width, int height, HDC src, int srcX,
            int srcY, DWORD rop);
int GetObject(HGDIOBJ object, int size, LPVOID buffer);
HRSRC FindResource(HINSTANCE instance, LPCSTR name, LPCSTR type);
HGLOBAL LoadResource(HINSTANCE instance, HRSRC resource);
LPVOID LockResource(HGLOBAL data);
DWORD SizeofResource(HINSTANCE instance, HRSRC resource);
HBITMAP LoadBitmap(HINSTANCE instance, LPCSTR name);
int LoadString(HINSTANCE instance, UINT id, LPSTR buffer, int size);
BOOL InflateRect(RECT *rect, int dx, int dy);
BOOL OffsetRect(RECT *rect, int dx, int dy);
SHORT GetAsyncKeyState(int virtualKey);
LRESULT SendMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
int GetMessage(MSG *message, HWND window, UINT minimum, UINT maximum);
BOOL PeekMessage(MSG *message, HWND window, UINT minimum, UINT maximum, UINT remove);
/* Blocks until a message is queued, a timer is due, or a quit is pending.
   The message stays queued. */
BOOL WaitMessage(void);
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

#include <winuser.h>
#include <winbase.h>

/* windef.h's min/max; C++ code keeps std::min/std::max. */
#if !defined(NOMINMAX) && !defined(__cplusplus)
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif
#endif

#endif
