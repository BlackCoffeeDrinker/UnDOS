/* Stubs for the WINFILE parts that are out of scope on UnDOS.

   Replaces wfprint.c, wfchgnot.c, wfext.c and wnetcaps.c, and provides the
   profile and dynamic-library calls WINFILE expects from Win32. Calls that
   could succeed in Win32 fail with ERROR_CALL_NOT_IMPLEMENTED (or the
   matching "not found" error), so WINFILE takes its own fallback paths and
   greys out the commands. The prototypes match winfile.h, wfexti.h and
   wnetcaps.h; DRIVE is WINFILE's INT drive index. */
#include "undos_compat.h"

#include <undoscrt/string.h>
#include <undosgui/extensions.h>

typedef INT DRIVE;

/* --- Dynamic libraries ------------------------------------------------ */
HMODULE LoadLibrary(LPCSTR name) {
  (void)name;
  SetLastError(ERROR_MOD_NOT_FOUND);
  return NULL;
}

FARPROC GetProcAddress(HMODULE module, LPCSTR name) {
  (void)module;
  (void)name;
  SetLastError(ERROR_PROC_NOT_FOUND);
  return NULL;
}

BOOL FreeLibrary(HMODULE module) {
  (void)module;
  SetLastError(ERROR_INVALID_HANDLE);
  return FALSE;
}

/* --- Profile API: every read returns the caller's default. ------------ */
DWORD GetPrivateProfileString(LPCSTR section, LPCSTR key, LPCSTR fallback,
                              LPSTR buffer, DWORD size, LPCSTR file) {
  (void)section;
  (void)key;
  (void)file;
  if (!buffer || size == 0) return 0;
  DWORD length = fallback ? (DWORD)strlen(fallback) : 0;
  if (length > size - 1) length = size - 1;
  if (length) memcpy(buffer, fallback, length);
  buffer[length] = '\0';
  return length;
}

UINT GetPrivateProfileInt(LPCSTR section, LPCSTR key, INT fallback,
                          LPCSTR file) {
  (void)section;
  (void)key;
  (void)file;
  return (UINT)fallback;
}

BOOL WritePrivateProfileString(LPCSTR section, LPCSTR key, LPCSTR value,
                               LPCSTR file) {
  (void)section;
  (void)key;
  (void)value;
  (void)file;
  SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
  return FALSE;
}

DWORD GetProfileString(LPCSTR section, LPCSTR key, LPCSTR fallback,
                       LPSTR buffer, DWORD size) {
  return GetPrivateProfileString(section, key, fallback, buffer, size, NULL);
}

/* --- wfprint.c -------------------------------------------------------- */
DWORD WFPrint(LPSTR selection) {
  (void)selection;
  return ERROR_CALL_NOT_IMPLEMENTED;
}

/* --- wfchgnot.c: no change notification; WINFILE refreshes on F5. ------ */
// WINFILE waits here for input; change notifications never arrive on UnDOS.
VOID vWaitMessage(void) { WaitMessage(); }
VOID InitializeWatchList(VOID) {}
VOID DestroyWatchList(VOID) {}
VOID NotifyReset(void) {}
VOID NotifyPause(DRIVE drive, UINT type) {
  (void)drive;
  (void)type;
}
VOID NotifyResume(DRIVE drive, UINT type) {
  (void)drive;
  (void)type;
}
VOID ModifyWatchList(HWND watch, LPSTR path, DWORD filter) {
  (void)watch;
  (void)path;
  (void)filter;
}

/* --- wfext.c: no extension DLLs can load. ------------------------------ */
VOID ExtSelItemsInvalidate(void) {}
VOID FreeExtensions(VOID) {}
LRESULT ExtensionMsgProc(UINT message, WPARAM wParam, LPARAM lParam) {
  (void)message;
  (void)wParam;
  (void)lParam;
  return 0;
}

/* --- wnetcaps.c: no network provider. ---------------------------------- */
BOOL WNetStat(INT index) {
  (void)index;
  return FALSE;
}

/* Windows 3.x DragObject: USER has no internal drag loop, so the drag ends
   at once with nothing dropped. */
DWORD DragObject(HWND parent, HWND from, UINT fmt, ULONG_PTR data, HCURSOR cursor) {
  (void)parent;
  (void)from;
  (void)fmt;
  (void)data;
  (void)cursor;
  return 0;
}

/* --- Status bar: no common controls on UnDOS yet, so no status window is
   created; WINFILE's SendMessage calls to a NULL hwndStatus fail quietly. */
#include "commctrl.h"
HWND CreateStatusWindow(LONG style, LPCSTR lpszText, HWND hwndParent, UINT wID)
{
  (void)style; (void)lpszText; (void)hwndParent; (void)wID;
  SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
  return NULL;
}

/* --- Class background brushes: nearest 16-colour palette entry. */
HBRUSH UndosSysColorBrush(int index)
{
  static const DWORD rgb[16] = {
      0x000000, 0x0000aa, 0x00aa00, 0x00aaaa, 0xaa0000, 0xaa00aa, 0xaa5500, 0xaaaaaa,
      0x555555, 0x5555ff, 0x55ff55, 0x55ffff, 0xff5555, 0xff55ff, 0xffff55, 0xffffff};
  const COLORREF color = GetSysColor(index);
  const int red = color & 255, green = (color >> 8) & 255, blue = (color >> 16) & 255;
  unsigned best = 7;
  DWORD bestDistance = 0xFFFFFFFFu;
  for (unsigned i = 0; i < 16; ++i) {
    const int dr = red - (int)((rgb[i] >> 16) & 255);
    const int dg = green - (int)((rgb[i] >> 8) & 255);
    const int db = blue - (int)(rgb[i] & 255);
    const DWORD distance = (DWORD)(dr * dr + dg * dg + db * db);
    if (distance < bestDistance) {
      best = i;
      bestDistance = distance;
    }
  }
  return UNDOS_PALETTE_BRUSH(best);
}
