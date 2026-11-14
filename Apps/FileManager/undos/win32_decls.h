/* UnDOS WINFILE port: Win32 calls WINFILE names that the UnDOS SDK does not
   provide. Each is stubbed in stubs.c and fails with
   ERROR_CALL_NOT_IMPLEMENTED (or does nothing), so WINFILE takes its
   fallback path. Generally useful ones move into the SDK with tests. */
#ifndef UNDOS_WIN32_DECLS_H
#define UNDOS_WIN32_DECLS_H

typedef BOOL *LPBOOL;
typedef UINT *PUINT;

#ifndef isalpha
#define isalpha(c) ((((unsigned)(c) | 0x20) - 'a') < 26u)
#endif

typedef DWORD (CALLBACK *LPPROGRESS_ROUTINE)(LARGE_INTEGER, LARGE_INTEGER, LARGE_INTEGER, LARGE_INTEGER, DWORD, DWORD, HANDLE, HANDLE, LPVOID);
typedef BOOL (CALLBACK *WNDENUMPROC)(HWND, LPARAM);
typedef LRESULT (CALLBACK *HOOKPROC)(int, WPARAM, LPARAM);
typedef double DATE;

/* WNet (out of scope) */
#define WN_SUCCESS 0
#define WN_NOT_SUPPORTED 50
#define WNTYPE_FILE 3
#define WNPS_FILE 0
#define WNPS_DIR 1
#define WNPS_MULT 2
#define WNFMT_MULTILINE 0x01
#define WNFMT_ABBREVIATED 0x02
#define RESOURCETYPE_DISK 1
#define RESOURCEUSAGE_CONNECTABLE 1
#define RESOURCE_REMEMBERED 3
DWORD WNetOpenEnum(DWORD, DWORD, DWORD, LPVOID, LPHANDLE);
DWORD WNetEnumResource(HANDLE, LPDWORD, LPVOID, LPDWORD);
DWORD WNetGetLastError(LPDWORD, LPSTR, DWORD, LPSTR, DWORD);

/* Registry (none on UnDOS) */
#define HKEY_CLASSES_ROOT ((HKEY)0x80000000u)
#define HKEY_LOCAL_MACHINE ((HKEY)0x80000002u)
#define REG_SZ 1
#define REG_EXPAND_SZ 2
#define REG_BINARY 3
#define REG_DWORD 4
#define REG_MULTI_SZ 7
#define RRF_RT_REG_SZ 0x02
#define RRF_RT_REG_EXPAND_SZ 0x04
#define RRF_RT_REG_BINARY 0x08
#define RRF_RT_REG_DWORD 0x10
#define RRF_RT_REG_MULTI_SZ 0x20
#define RRF_RT_DWORD 0x18
#define RRF_RT_ANY 0xFFFF
LONG RegOpenKey(HKEY, LPCSTR, HKEY *);
LONG RegCloseKey(HKEY);
LONG RegEnumKey(HKEY, DWORD, LPSTR, DWORD);
LONG RegQueryValueEx(HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);

/* Shell */
#define SHFMT_ID_DEFAULT 0xFFFF
#define SHFMT_ERROR 0xFFFFFFFFL
#define SHFMT_CANCEL 0xFFFFFFFEL
#define SHFMT_NOFORMAT 0xFFFFFFFDL
#define TB_ENABLEBUTTON (WM_USER + 1)
#define TB_CUSTOMIZE (WM_USER + 27)
DWORD SHFormatDrive(HWND, UINT, UINT, UINT);
UINT ExtractIconEx(LPCSTR, int, HICON *, HICON *, UINT);
LPSTR PathFindExtension(LPCSTR);
BOOL PathFileExists(LPCSTR);
BOOL PathAppend(LPSTR, LPCSTR);
VOID MenuHelp(UINT, WPARAM, LPARAM, HMENU, HINSTANCE, HWND, UINT *);
VOID GetEffectiveClientRect(HWND, LPRECT, const INT *);

/* Help, version, locale */
BOOL WinHelp(HWND, LPCSTR, UINT, ULONG_PTR);
DWORD GetFileVersionInfoSize(LPCSTR, LPDWORD);
BOOL GetFileVersionInfo(LPCSTR, DWORD, DWORD, LPVOID);
BOOL VerQueryValue(LPCVOID, LPCSTR, LPVOID *, PUINT);
DWORD VerLanguageName(DWORD, LPSTR, DWORD);
BOOL GetVersionEx(LPOSVERSIONINFO);
int GetLocaleInfo(LCID, DWORD, LPSTR, int);
int GetNumberFormat(LCID, DWORD, LPCSTR, const NUMBERFMT *, LPSTR, int);
int GetDateFormat(LCID, DWORD, const SYSTEMTIME *, LPCSTR, LPSTR, int);
int GetTimeFormat(LCID, DWORD, const SYSTEMTIME *, LPCSTR, LPSTR, int);
int CompareString(LCID, DWORD, LPCSTR, int, LPCSTR, int);
LCID GetUserDefaultLCID(void);
LCID GetThreadLocale(void);
BOOL SetThreadLocale(LCID);
LANGID SetThreadUILanguage(LANGID);
int WideCharToMultiByte(UINT, DWORD, LPCSTR, int, LPSTR, int, LPCSTR, LPBOOL);
BOOL IsDBCSLeadByte(BYTE);
DWORD FormatMessage(DWORD, LPCVOID, DWORD, DWORD, LPSTR, DWORD, va_list *);
HRESULT VarDateFromStr(LPCSTR, LCID, ULONG, DATE *);
INT VariantTimeToSystemTime(DATE, SYSTEMTIME *);
HRESULT StringCchPrintf(LPSTR, size_t, LPCSTR, ...);
#define StrNCpy lstrcpyn

/* Process, module, environment */
UINT SetErrorMode(UINT);
UINT GetSystemDirectory(LPSTR, UINT);
HMODULE GetModuleHandle(LPCSTR);
DWORD GetModuleFileName(HMODULE, LPSTR, DWORD);
DWORD GetEnvironmentVariable(LPCSTR, LPSTR, DWORD);
DWORD ExpandEnvironmentStrings(LPCSTR, LPSTR, DWORD);
LPSTR GetCommandLine(void);
VOID ExitProcess(UINT);
HANDLE GetCurrentThread(void);
BOOL SetThreadPriority(HANDLE, int);
BOOL FreeResource(HGLOBAL);

/* Files */
HANDLE CreateFile(LPCSTR, DWORD, DWORD, LPVOID, DWORD, DWORD, HANDLE);
BOOL DeviceIoControl(HANDLE, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD, LPVOID);
DWORD GetCompressedFileSize(LPCSTR, LPDWORD);
BOOL GetFileInformationByHandle(HANDLE, LPBY_HANDLE_FILE_INFORMATION);
BOOL GetDiskFreeSpaceEx(LPCSTR, PULARGE_INTEGER, PULARGE_INTEGER, PULARGE_INTEGER);
DWORD GetShortPathName(LPCSTR, LPSTR, DWORD);
HANDLE FindFirstFileEx(LPCSTR, FINDEX_INFO_LEVELS, LPVOID, FINDEX_SEARCH_OPS, LPVOID, DWORD);
BOOL CreateDirectoryEx(LPCSTR, LPCSTR, LPVOID);
BOOL CopyFileEx(LPCSTR, LPCSTR, LPPROGRESS_ROUTINE, LPVOID, LPBOOL, DWORD);
BOOL CreateHardLink(LPCSTR, LPCSTR, LPVOID);

/* Windows and menus */
BOOL TrackPopupMenu(HMENU, UINT, int, int, int, HWND, const RECT *);
int GetMenuString(HMENU, UINT, LPSTR, int, UINT);
HMENU GetSystemMenu(HWND, BOOL);
HDWP BeginDeferWindowPos(int);
HDWP DeferWindowPos(HDWP, HWND, HWND, int, int, int, int, UINT);
BOOL EndDeferWindowPos(HDWP);
BOOL GetWindowPlacement(HWND, WINDOWPLACEMENT *);
HWND GetActiveWindow(void);
HWND SetActiveWindow(HWND);
BOOL SetForegroundWindow(HWND);
HWND GetLastActivePopup(HWND);
HWND GetDesktopWindow(void);
HWND GetNextWindow(HWND, UINT);
BOOL EnumChildWindows(HWND, WNDENUMPROC, LPARAM);
int GetClassName(HWND, LPSTR, int);
int InternalGetWindowText(HWND, LPSTR, int);
BOOL IsWindowUnicode(HWND);
BOOL GetUpdateRect(HWND, LPRECT, BOOL);
UINT RegisterWindowMessage(LPCSTR);
BOOL SystemParametersInfo(UINT, UINT, PVOID, UINT);
BOOL SetCursorPos(int, int);
HOOKPROC SetWindowsHook(int, HOOKPROC);
LRESULT DefHookProc(int, WPARAM, LPARAM, HOOKPROC *);

/* GDI */
int GetDeviceCaps(HDC, int);
int MulDiv(int, int, int);
HFONT CreateFont(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, LPCSTR);
HFONT CreateFontIndirect(const LOGFONT *);
HBITMAP CreateDIBitmap(HDC, const BITMAPINFOHEADER *, DWORD, const void *, const BITMAPINFO *, UINT);
DWORD SetLayout(HDC, DWORD);
DWORD GetLayout(HDC);
HICON LoadIcon(HINSTANCE, LPCSTR);
BOOL DrawIcon(HDC, int, int, HICON);
BOOL DrawIconEx(HDC, int, int, HICON, int, int, UINT, HBRUSH, UINT);
BOOL DestroyIcon(HICON);

#endif
