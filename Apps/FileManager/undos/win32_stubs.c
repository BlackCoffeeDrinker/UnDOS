/* UnDOS WINFILE port: link stubs for the Win32 calls declared in
   win32_decls.h. Each fails with ERROR_CALL_NOT_IMPLEMENTED (or does the
   minimal harmless thing) so WINFILE takes its fallback path. */
#include "winfile.h"
#include "mpr.h"
#include <undosgui/extensions.h>
#include <undoscrt/stdio.h>

INT APIENTRY WinMain(HINSTANCE, HINSTANCE, LPSTR, INT);
#ifndef INVALID_FILE_SIZE
#define INVALID_FILE_SIZE ((DWORD)0xFFFFFFFF)
#endif
#ifndef SM_CYSCREEN
#define SM_CYSCREEN 1
#endif
#define NI(ret) do { SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return ret; } while (0)

/* Entry point: UnDOS apps start at main(). The resource tables generated
   from res.rc must be registered before WinMain loads menus and strings. */
extern const UNDOS_RESOURCE winfile_resources[];
extern const UINT winfile_resources_count;
int main(void) {
  if (!UndosRegisterResources(winfile_resources, winfile_resources_count)) {
    crt_printf("filemanager: resource registration failed error=%u\n", (unsigned)GetLastError());
    return 1;
  }
  return WinMain(0, 0, "", SW_SHOWNORMAL);
}

/* WINFILE helpers that were #if 0 in the copied source. UnDOS has no network
   drives, and its FAT driver keeps long names. */
DWORD IsNetDrive(INT drive) { (void)drive; return 0; }
BOOL IsLFNDrive(LPTSTR szDrive) { (void)szDrive; return TRUE; }

/* Go To Directory (wfgoto.cpp) and Associate (wfassoc.c) are not ported. */
static INT_PTR CALLBACK unsupported_dialog(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
  (void)wParam; (void)lParam;
  if (msg == WM_INITDIALOG) {
    MessageBox(hDlg, "Not supported on UnDOS.", "File Manager", MB_OK);
    EndDialog(hDlg, IDCANCEL);
    return TRUE;
  }
  return FALSE;
}
INT_PTR CALLBACK GotoDirDlgProc(HWND h, UINT m, WPARAM w, LPARAM l) { return unsupported_dialog(h, m, w, l); }
INT_PTR CALLBACK AssociateDlgProc(HWND h, UINT m, WPARAM w, LPARAM l) { return unsupported_dialog(h, m, w, l); }
DWORD StartBuildingDirectoryTrie() { return 0; }
VOID SetCurrentPathOfWindow(LPWSTR szPath) { (void)szPath; }

/* WNet */
DWORD WNetGetDirectoryTypeA(LPSTR a, LPDWORD b, BOOL c) { (void)a; (void)c; if (b) *b = 0; return WN_NOT_SUPPORTED; }
DWORD WNetPropertyDialogA(HWND a, DWORD b, DWORD c, LPSTR d, DWORD e) { (void)a; (void)b; (void)c; (void)d; (void)e; return WN_NOT_SUPPORTED; }
DWORD WNetGetPropertyTextA(DWORD a, DWORD b, LPSTR c, LPSTR d, DWORD e, DWORD f) { (void)a; (void)b; (void)c; (void)e; (void)f; if (d) *d = 0; return WN_NOT_SUPPORTED; }
DWORD WNetGetConnection2A(LPSTR a, LPVOID b, LPDWORD c) { (void)a; (void)b; (void)c; return WN_NOT_SUPPORTED; }
DWORD WNetOpenEnum(DWORD a, DWORD b, DWORD c, LPVOID d, LPHANDLE e) { (void)a; (void)b; (void)c; (void)d; if (e) *e = 0; return WN_NOT_SUPPORTED; }
DWORD WNetEnumResource(HANDLE a, LPDWORD b, LPVOID c, LPDWORD d) { (void)a; (void)b; (void)c; (void)d; return WN_NOT_SUPPORTED; }
DWORD WNetGetLastError(LPDWORD a, LPSTR b, DWORD c, LPSTR d, DWORD e) { (void)c; (void)e; if (a) *a = 0; if (b) *b = 0; if (d) *d = 0; return WN_SUCCESS; }

/* Registry: none on UnDOS. */
LONG RegOpenKey(HKEY a, LPCSTR b, HKEY *c) { (void)a; (void)b; if (c) *c = 0; return ERROR_CALL_NOT_IMPLEMENTED; }
LONG RegCloseKey(HKEY a) { (void)a; return ERROR_INVALID_HANDLE; }
LONG RegEnumKey(HKEY a, DWORD b, LPSTR c, DWORD d) { (void)a; (void)b; (void)c; (void)d; return ERROR_NO_MORE_ITEMS; }
LONG RegQueryValueEx(HKEY a, LPCSTR b, LPDWORD c, LPDWORD d, LPBYTE e, LPDWORD f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return ERROR_CALL_NOT_IMPLEMENTED; }

/* Shell */
DWORD SHFormatDrive(HWND a, UINT b, UINT c, UINT d) { (void)a; (void)b; (void)c; (void)d; return SHFMT_NOFORMAT; }
UINT ExtractIconEx(LPCSTR a, int b, HICON *c, HICON *d, UINT e) { (void)a; (void)b; (void)e; if (c) *c = 0; if (d) *d = 0; return 0; }
LPSTR PathFindExtension(LPCSTR p)
{
  const char *dot = NULL;
  for (const char *s = p; *s; ++s) {
    if (*s == '.') dot = s;
    else if (*s == '/' || *s == '\\' || *s == ' ') dot = NULL;
  }
  return (LPSTR)(dot ? dot : p + strlen(p));
}
BOOL PathFileExists(LPCSTR p) { return GetFileAttributes(p) != INVALID_FILE_ATTRIBUTES; }
BOOL PathAppend(LPSTR p, LPCSTR more)
{
  size_t n = strlen(p);
  if (n && p[n - 1] != '\\' && p[n - 1] != '/') { if (n + 1 >= MAX_PATH) return FALSE; p[n++] = '\\'; p[n] = 0; }
  while (*more == '\\' || *more == '/') ++more;
  if (n + strlen(more) >= MAX_PATH) return FALSE;
  strcat(p, more);
  return TRUE;
}
VOID MenuHelp(UINT a, WPARAM b, LPARAM c, HMENU d, HINSTANCE e, HWND f, UINT *g) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; }
VOID GetEffectiveClientRect(HWND h, LPRECT r, const INT *info) { (void)info; GetClientRect(h, r); }

/* Help, version, locale */
BOOL WinHelp(HWND a, LPCSTR b, UINT c, ULONG_PTR d) { (void)a; (void)b; (void)c; (void)d; NI(FALSE); }
DWORD GetFileVersionInfoSize(LPCSTR a, LPDWORD b) { (void)a; if (b) *b = 0; NI(0); }
BOOL GetFileVersionInfo(LPCSTR a, DWORD b, DWORD c, LPVOID d) { (void)a; (void)b; (void)c; (void)d; NI(FALSE); }
BOOL VerQueryValue(LPCVOID a, LPCSTR b, LPVOID *c, PUINT d) { (void)a; (void)b; if (c) *c = NULL; if (d) *d = 0; NI(FALSE); }
DWORD VerLanguageName(DWORD a, LPSTR b, DWORD c) { (void)a; if (b && c) *b = 0; NI(0); }
BOOL GetVersionEx(LPOSVERSIONINFO v)
{
  if (!v) NI(FALSE);
  v->dwMajorVersion = 3; v->dwMinorVersion = 10; v->dwBuildNumber = 0; v->dwPlatformId = 0;
  v->szCSDVersion[0] = 0;
  return TRUE;
}
static int copy_out(LPCSTR s, LPSTR d, int n)
{
  int len = (int)strlen(s) + 1;
  if (n == 0) return len;
  if (!d || n < len) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return 0; }
  memcpy(d, s, (size_t)len);
  return len;
}
int GetLocaleInfo(LCID l, DWORD t, LPSTR d, int n)
{
  (void)l;
  switch (t & 0xFFFF) {
  case LOCALE_SDECIMAL: return copy_out(".", d, n);
  case LOCALE_STHOUSAND: return copy_out(",", d, n);
  default: return copy_out("", d, n);
  }
}
int GetNumberFormat(LCID l, DWORD f, LPCSTR v, const NUMBERFMT *fmt, LPSTR d, int n)
{
  /* Plain integer grouping with ',' (WINFILE formats sizes without decimals). */
  char out[64]; int len = 0, digits = 0; const char *e;
  (void)l; (void)f; (void)fmt;
  for (e = v; *e >= '0' && *e <= '9'; ++e) ++digits;
  for (const char *s = v; s < e && len < 60; ++s) {
    out[len++] = *s;
    if ((e - s - 1) % 3 == 0 && s + 1 < e) out[len++] = ',';
  }
  out[len] = 0;
  return copy_out(out, d, n);
}
int GetDateFormat(LCID l, DWORD f, const SYSTEMTIME *t, LPCSTR fmt, LPSTR d, int n)
{
  char out[32]; (void)l; (void)f; (void)fmt;
  if (!t) NI(0);
  wsprintf(out, "%d/%d/%d", t->wMonth, t->wDay, t->wYear);
  return copy_out(out, d, n);
}
int GetTimeFormat(LCID l, DWORD f, const SYSTEMTIME *t, LPCSTR fmt, LPSTR d, int n)
{
  char out[32]; (void)l; (void)f; (void)fmt;
  if (!t) NI(0);
  wsprintf(out, "%d:%02d:%02d", t->wHour, t->wMinute, t->wSecond);
  return copy_out(out, d, n);
}
int CompareString(LCID l, DWORD f, LPCSTR a, int na, LPCSTR b, int nb)
{
  char x[MAX_PATH], y[MAX_PATH]; int r; (void)l;
  lstrcpyn(x, a, na < 0 || na >= MAX_PATH ? MAX_PATH : na + 1);
  lstrcpyn(y, b, nb < 0 || nb >= MAX_PATH ? MAX_PATH : nb + 1);
  r = (f & NORM_IGNORECASE) ? lstrcmpi(x, y) : lstrcmp(x, y);
  return r < 0 ? 1 : r > 0 ? 3 : 2;
}
LCID GetUserDefaultLCID(void) { return 0x0409; }
LCID GetThreadLocale(void) { return 0x0409; }
BOOL SetThreadLocale(LCID l) { return l == 0x0409; }
LANGID SetThreadUILanguage(LANGID l) { (void)l; return 0x0409; }
int WideCharToMultiByte(UINT cp, DWORD f, LPCSTR s, int ns, LPSTR d, int nd, LPCSTR def, LPBOOL used)
{
  int len = ns < 0 ? (int)strlen(s) + 1 : ns; (void)cp; (void)f; (void)def;
  if (used) *used = FALSE;
  if (nd == 0) return len;
  if (nd < len) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return 0; }
  memcpy(d, s, (size_t)len);
  return len;
}
BOOL IsDBCSLeadByte(BYTE c) { (void)c; return FALSE; }
DWORD FormatMessage(DWORD a, LPCVOID b, DWORD c, DWORD e, LPSTR d, DWORD n, va_list *g)
{
  (void)a; (void)b; (void)e; (void)g;
  if (!d || n < 16) NI(0);
  return (DWORD)wsprintf(d, "Error %lu", (unsigned long)c);
}
HRESULT VarDateFromStr(LPCSTR a, LCID b, ULONG c, DATE *d) { (void)a; (void)b; (void)c; if (d) *d = 0; return (HRESULT)0x80004001L; }
INT VariantTimeToSystemTime(DATE a, SYSTEMTIME *b) { (void)a; (void)b; return FALSE; }
HRESULT StringCchPrintf(LPSTR d, size_t n, LPCSTR fmt, ...)
{
  char tmp[1025]; va_list ap;
  va_start(ap, fmt); wvsprintf(tmp, fmt, ap); va_end(ap);
  if (!n) return (HRESULT)0x80070057L;
  lstrcpyn(d, tmp, (int)n);
  return strlen(tmp) < n ? 0 : (HRESULT)0x8007007AL;
}

/* Process, module, environment */
UINT SetErrorMode(UINT m) { (void)m; return 0; }
UINT GetSystemDirectory(LPSTR d, UINT n) { int r = copy_out("/System", d, (int)n); return r ? (UINT)r - 1 : 8; }
HMODULE GetModuleHandle(LPCSTR n) { (void)n; return NULL; }
DWORD GetModuleFileName(HMODULE m, LPSTR d, DWORD n) { int r; (void)m; r = copy_out("/System/FILEMAN.ELF", d, (int)n); return r ? (DWORD)r - 1 : 0; }
DWORD GetEnvironmentVariable(LPCSTR a, LPSTR b, DWORD c) { (void)a; (void)b; (void)c; SetLastError(203 /* ERROR_ENVVAR_NOT_FOUND */); return 0; }
DWORD ExpandEnvironmentStrings(LPCSTR s, LPSTR d, DWORD n) { return (DWORD)copy_out(s, d, (int)n); }
LPSTR GetCommandLine(void) { static char cmd[] = "FILEMAN.ELF"; return cmd; }
VOID ExitProcess(UINT code) { exit((int)code); }
HANDLE GetCurrentThread(void) { return (HANDLE)-2; }
BOOL SetThreadPriority(HANDLE h, int p) { (void)h; (void)p; return TRUE; }
BOOL FreeResource(HGLOBAL h) { (void)h; return FALSE; }

/* Files */
HANDLE CreateFile(LPCSTR a, DWORD b, DWORD c, LPVOID d, DWORD e, DWORD f, HANDLE g) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; NI(INVALID_HANDLE_VALUE); }
BOOL DeviceIoControl(HANDLE a, DWORD b, LPVOID c, DWORD d, LPVOID e, DWORD f, LPDWORD g, LPVOID h) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h; NI(FALSE); }
DWORD GetCompressedFileSize(LPCSTR a, LPDWORD b) { (void)a; if (b) *b = 0; NI(INVALID_FILE_SIZE); }
BOOL GetFileInformationByHandle(HANDLE a, LPBY_HANDLE_FILE_INFORMATION b) { (void)a; (void)b; NI(FALSE); }
BOOL GetDiskFreeSpaceEx(LPCSTR a, PULARGE_INTEGER b, PULARGE_INTEGER c, PULARGE_INTEGER d) { (void)a; (void)b; (void)c; (void)d; NI(FALSE); }
DWORD GetShortPathName(LPCSTR s, LPSTR d, DWORD n) { int r = copy_out(s, d, (int)n); return r ? (DWORD)r - 1 : (DWORD)strlen(s) + 1; }
HANDLE FindFirstFileEx(LPCSTR a, FINDEX_INFO_LEVELS b, LPVOID c, FINDEX_SEARCH_OPS d, LPVOID e, DWORD f) { (void)b; (void)d; (void)e; (void)f; return FindFirstFile(a, (LPWIN32_FIND_DATA)c); }
BOOL CreateDirectoryEx(LPCSTR t, LPCSTR a, LPVOID s) { (void)t; return CreateDirectory(a, s); }
BOOL CopyFileEx(LPCSTR a, LPCSTR b, LPPROGRESS_ROUTINE c, LPVOID d, LPBOOL e, DWORD f) { (void)c; (void)d; (void)e; (void)f; return CopyFile(a, b, FALSE); }
BOOL CreateHardLink(LPCSTR a, LPCSTR b, LPVOID c) { (void)a; (void)b; (void)c; NI(FALSE); }

/* Windows and menus */
BOOL TrackPopupMenu(HMENU a, UINT b, int c, int d, int e, HWND f, const RECT *g) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; NI(FALSE); }
int GetMenuString(HMENU a, UINT b, LPSTR c, int d, UINT e) { (void)a; (void)b; (void)e; if (c && d) *c = 0; NI(0); }
HMENU GetSystemMenu(HWND a, BOOL b) { (void)a; (void)b; return NULL; }
HDWP BeginDeferWindowPos(int n) { (void)n; return (HDWP)1; }
HDWP DeferWindowPos(HDWP d, HWND h, HWND after, int x, int y, int cx, int cy, UINT f)
{
  SetWindowPos(h, after, x, y, cx, cy, f);
  return d;
}
BOOL EndDeferWindowPos(HDWP d) { (void)d; return TRUE; }
BOOL GetWindowPlacement(HWND h, WINDOWPLACEMENT *p)
{
  if (!p) NI(FALSE);
  p->flags = 0; p->showCmd = SW_SHOWNORMAL;
  p->ptMinPosition.x = p->ptMinPosition.y = -1;
  p->ptMaxPosition.x = p->ptMaxPosition.y = -1;
  return GetWindowRect(h, &p->rcNormalPosition);
}
HWND GetActiveWindow(void) { return hwndFrame; }
HWND SetActiveWindow(HWND h) { (void)h; return hwndFrame; }
BOOL SetForegroundWindow(HWND h) { (void)h; return TRUE; }
HWND GetLastActivePopup(HWND h) { return h; }
HWND GetDesktopWindow(void) { return NULL; }
HWND GetNextWindow(HWND h, UINT c) { return GetWindow(h, c); }
BOOL EnumChildWindows(HWND p, WNDENUMPROC fn, LPARAM l)
{
  for (HWND c = GetWindow(p, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
    if (!fn(c, l)) return FALSE;
  return TRUE;
}
int GetClassName(HWND a, LPSTR b, int c) { (void)a; if (b && c) *b = 0; NI(0); }
int InternalGetWindowText(HWND h, LPSTR s, int n) { return GetWindowText(h, s, n); }
BOOL IsWindowUnicode(HWND h) { (void)h; return FALSE; }
BOOL GetUpdateRect(HWND h, LPRECT r, BOOL e) { (void)e; if (r) GetClientRect(h, r); return TRUE; }
UINT RegisterWindowMessage(LPCSTR s) { (void)s; static UINT next = 0xC000; return next++; }
BOOL SystemParametersInfo(UINT a, UINT b, PVOID c, UINT d)
{
  (void)b; (void)d;
  if (a == SPI_GETWORKAREA && c) {
    SetRect((LPRECT)c, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    return TRUE;
  }
  NI(FALSE);
}
BOOL SetCursorPos(int x, int y) { (void)x; (void)y; NI(FALSE); }
HOOKPROC SetWindowsHook(int a, HOOKPROC b) { (void)a; (void)b; return NULL; }
LRESULT DefHookProc(int a, WPARAM b, LPARAM c, HOOKPROC *d) { (void)a; (void)b; (void)c; (void)d; return 0; }

/* GDI */
int GetDeviceCaps(HDC a, int i) { (void)a; return (i == LOGPIXELSX || i == LOGPIXELSY) ? 96 : 0; }
int MulDiv(int a, int b, int c) { return c ? (int)(((long long)a * b) / c) : -1; }
HFONT CreateFont(int a, int b, int c, int d, int e, DWORD f, DWORD g, DWORD h, DWORD i, DWORD j, DWORD k, DWORD l, DWORD m, LPCSTR n)
{
  (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h; (void)i; (void)j; (void)k; (void)l; (void)m; (void)n;
  return (HFONT)GetStockObject(SYSTEM_FONT);
}
HFONT CreateFontIndirect(const LOGFONT *lf) { (void)lf; return (HFONT)GetStockObject(SYSTEM_FONT); }
HBITMAP CreateDIBitmap(HDC dc, const BITMAPINFOHEADER *h, DWORD init, const void *bits, const BITMAPINFO *bi, UINT usage)
{
  HBITMAP bmp;
  if (!h) NI(NULL);
  /* A full-colour bitmap: a memory DC's compatible bitmap would be monochrome. */
  bmp = CreateBitmap(h->biWidth, h->biHeight < 0 ? -h->biHeight : h->biHeight, 1, 32, NULL);
  if (bmp && (init & CBM_INIT) && bits && bi)
    SetDIBits(dc, bmp, 0, (UINT)(h->biHeight < 0 ? -h->biHeight : h->biHeight), bits, bi, usage);
  return bmp;
}
DWORD SetLayout(HDC a, DWORD b) { (void)a; (void)b; return 0; }
DWORD GetLayout(HDC a) { (void)a; return 0; }
HICON LoadIcon(HINSTANCE a, LPCSTR b) { (void)a; (void)b; return NULL; }
BOOL DrawIcon(HDC a, int b, int c, HICON d) { (void)a; (void)b; (void)c; (void)d; NI(FALSE); }
BOOL DrawIconEx(HDC a, int b, int c, HICON d, int e, int f, UINT g, HBRUSH h, UINT i) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h; (void)i; NI(FALSE); }
BOOL DestroyIcon(HICON a) { (void)a; return TRUE; }
