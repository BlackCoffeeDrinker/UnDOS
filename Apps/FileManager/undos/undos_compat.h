/* UnDOS compatibility shim for the WINFILE port.

   Holds the Win32 names WINFILE uses that the UnDOS SDK does not provide.
   Each one is either a real implementation or a stub (in stubs.c) that
   fails with ERROR_CALL_NOT_IMPLEMENTED. Anything that belongs in the SDK
   goes there instead; this header only covers what is out of scope for
   UnDOS (profile files, printing, change notification, WNet, fmifs,
   extensions) and the single-byte TCHAR model. */
#ifndef UNDOS_COMPAT_H
#define UNDOS_COMPAT_H

#include <windows.h>
#include "win32_extra.h"
#include "mounts.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Character model: TCHAR is char and the W names map to A. ----------
   The SDK's WCHAR is the 16-bit unit of resource templates; WINFILE's
   WCHAR text becomes char from here on, so these are macros, not typedefs.
   The generated resources.c does not use them. */
#define WCHAR char
/* The pointer types must be typedefs: as macros, "LPWSTR p, pT;" made pT a
   plain char, truncating pointers (the DirGetSelection crash). */
typedef char *PWCHAR;
typedef char *LPWSTR;
typedef char *PWSTR;
typedef const char *LPCWSTR;
typedef const char *PCWSTR;

/* --- 16-bit pointer qualifiers and NT names WINFILE's headers use. ----- */
#ifndef FAR
#define FAR
#endif
#ifndef NEAR
#define NEAR
#endif
#ifndef cdecl
#define cdecl
#endif
/* Class background for a system colour. Win32 uses (HBRUSH)(COLOR_x + 1),
   but the UnDOS SDK reads small brush values as palette indexes, so map the
   system colour to its nearest palette brush instead (stubs.c). */
HBRUSH UndosSysColorBrush(int index);
#ifndef CDECL
#define CDECL
#endif
#ifndef UNLEN
#define UNLEN 256
#endif
typedef struct _LUID {
  DWORD LowPart;
  LONG HighPart;
} LUID, *PLUID;

/* --- Dynamic libraries: UnDOS apps are static, so nothing loads. -------
   WINFILE loads fmifs.dll and mpr.dll (WNet) this way and greys out the
   matching commands when loading fails. */
typedef void *HMODULE;
typedef INT_PTR (WINAPI *FARPROC)(void);
#define ERROR_MOD_NOT_FOUND 126
#define ERROR_PROC_NOT_FOUND 127
HMODULE LoadLibrary(LPCSTR name);
FARPROC GetProcAddress(HMODULE module, LPCSTR name);
BOOL FreeLibrary(HMODULE module);

/* --- Profile API (winfile.ini): stubbed to return the defaults. -------- */
DWORD GetPrivateProfileString(LPCSTR section, LPCSTR key, LPCSTR fallback,
                              LPSTR buffer, DWORD size, LPCSTR file);
UINT GetPrivateProfileInt(LPCSTR section, LPCSTR key, INT fallback,
                          LPCSTR file);
BOOL WritePrivateProfileString(LPCSTR section, LPCSTR key, LPCSTR value,
                               LPCSTR file);
DWORD GetProfileString(LPCSTR section, LPCSTR key, LPCSTR fallback,
                       LPSTR buffer, DWORD size);

/* --- Windows 3.x internal drag protocol (WM_DRAGLOOP, WM_DROPOBJECT, ...).
   WINFILE drags between its own windows with these; UnDOS USER does not
   send them, so dragging out of a window does nothing yet. */
typedef struct tagDROPSTRUCT {
  HWND hwndSource;
  HWND hwndSink;
  DWORD wFmt;
  ULONG_PTR dwData;
  POINT ptDrop;
  DWORD dwControlData;
} DROPSTRUCT, *PDROPSTRUCT, *LPDROPSTRUCT;
#define DOF_EXECUTABLE 0x8001
#define DOF_DOCUMENT 0x8002
#define DOF_DIRECTORY 0x8003
#define DOF_MULTIPLE 0x8004
#define DOF_PROGMAN 0x0001
#define DOF_SHELLDATA 0x0002
#define DO_DROPFILE 0x454C4946L
#define DO_PRINTFILE 0x544E5250L
#define WM_DROPOBJECT 0x022A
#define WM_QUERYDROPOBJECT 0x022B
#define WM_BEGINDRAG 0x022C
#define WM_DRAGLOOP 0x022D
#define WM_DRAGSELECT 0x022E
#define WM_DRAGMOVE 0x022F
DWORD DragObject(HWND parent, HWND from, UINT fmt, ULONG_PTR data, HCURSOR cursor);

/* --- WNet (mpr.dll) types: networking is out of scope; the calls fail. */
typedef struct _NETRESOURCE {
  DWORD dwScope, dwType, dwDisplayType, dwUsage;
  LPSTR lpLocalName, lpRemoteName, lpComment, lpProvider;
} NETRESOURCE, *LPNETRESOURCE;

#include "win32_decls.h"

#ifdef __cplusplus
}
#endif

#endif
