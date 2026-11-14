#ifndef UNDOS_WINBASE_H
#define UNDOS_WINBASE_H

/* kernel32-style file API and ShellExecute. Included by windows.h.
   UnDOS has a single root: paths are absolute ("/DIR/FILE"), '\\' is
   accepted as a separator, and ':' is not a valid name character. There is no
   GetLogicalDrives; GetDriveType/GetVolumeInformation take mount points. */

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* HANDLE matches the UnDOS syscall headers (a 32-bit object handle) so
   both can be included together; Win32 code treats it as opaque. */
typedef uint32_t HANDLE;
typedef HANDLE *LPHANDLE, *PHANDLE;
#define INVALID_HANDLE_VALUE ((HANDLE)0xFFFFFFFFu)
#define MAX_PATH 260
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)

#define ERROR_FILE_NOT_FOUND 2
#define ERROR_PATH_NOT_FOUND 3
#define ERROR_ACCESS_DENIED 5
#define ERROR_NOT_SAME_DEVICE 17
#define ERROR_NO_MORE_FILES 18
#define ERROR_FILE_EXISTS 80
#define ERROR_INVALID_NAME 123
#define ERROR_DIR_NOT_EMPTY 145
#define ERROR_ALREADY_EXISTS 183
#define ERROR_FILENAME_EXCED_RANGE 206
#define ERROR_DIRECTORY 267
#define ERROR_BAD_LENGTH 24

#define FILE_ATTRIBUTE_READONLY 0x00000001
#define FILE_ATTRIBUTE_HIDDEN 0x00000002
#define FILE_ATTRIBUTE_SYSTEM 0x00000004
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_ARCHIVE 0x00000020
#define FILE_ATTRIBUTE_NORMAL 0x00000080

#define FILE_CASE_PRESERVED_NAMES 0x00000002
#define FILE_READ_ONLY_VOLUME 0x00080000

#define DRIVE_UNKNOWN 0
#define DRIVE_NO_ROOT_DIR 1
#define DRIVE_REMOVABLE 2
#define DRIVE_FIXED 3
#define DRIVE_REMOTE 4
#define DRIVE_CDROM 5
#define DRIVE_RAMDISK 6

#define MOVEFILE_REPLACE_EXISTING 0x00000001
#define MOVEFILE_COPY_ALLOWED 0x00000002

typedef struct _FILETIME {
  DWORD dwLowDateTime;
  DWORD dwHighDateTime;
} FILETIME, *PFILETIME, *LPFILETIME;

typedef struct _SYSTEMTIME {
  WORD wYear;
  WORD wMonth;
  WORD wDayOfWeek;
  WORD wDay;
  WORD wHour;
  WORD wMinute;
  WORD wSecond;
  WORD wMilliseconds;
} SYSTEMTIME, *PSYSTEMTIME, *LPSYSTEMTIME;

typedef struct _WIN32_FIND_DATAA {
  DWORD dwFileAttributes;
  FILETIME ftCreationTime;
  FILETIME ftLastAccessTime;
  FILETIME ftLastWriteTime;
  DWORD nFileSizeHigh;
  DWORD nFileSizeLow;
  DWORD dwReserved0;
  DWORD dwReserved1;
  char cFileName[MAX_PATH];
  char cAlternateFileName[14];
} WIN32_FIND_DATAA, WIN32_FIND_DATA, *LPWIN32_FIND_DATA;

HANDLE FindFirstFile(LPCSTR pattern, LPWIN32_FIND_DATA data);
BOOL FindNextFile(HANDLE find, LPWIN32_FIND_DATA data);
BOOL FindClose(HANDLE find);
DWORD GetFileAttributes(LPCSTR path);
typedef enum _GET_FILEEX_INFO_LEVELS {
  GetFileExInfoStandard,
  GetFileExMaxInfoLevel
} GET_FILEEX_INFO_LEVELS;
typedef struct _WIN32_FILE_ATTRIBUTE_DATA {
  DWORD dwFileAttributes;
  FILETIME ftCreationTime;
  FILETIME ftLastAccessTime;
  FILETIME ftLastWriteTime;
  DWORD nFileSizeHigh;
  DWORD nFileSizeLow;
} WIN32_FILE_ATTRIBUTE_DATA, *LPWIN32_FILE_ATTRIBUTE_DATA;
BOOL GetFileAttributesEx(LPCSTR path, GET_FILEEX_INFO_LEVELS level, void *result);
BOOL SetFileAttributes(LPCSTR path, DWORD attributes);
BOOL CopyFile(LPCSTR source, LPCSTR destination, BOOL failIfExists);
BOOL MoveFile(LPCSTR source, LPCSTR destination);
BOOL MoveFileEx(LPCSTR source, LPCSTR destination, DWORD flags);
BOOL DeleteFile(LPCSTR path);
BOOL CreateDirectory(LPCSTR path, void *security);
BOOL RemoveDirectory(LPCSTR path);
/* Fills buffer with the mount points ("/\0/MNT/CD\0\0"). Returns the
   length without the final NUL, or the size needed when buffer is too small. */
DWORD GetLogicalDriveStrings(DWORD size, LPSTR buffer);
UINT GetDriveType(LPCSTR root);
BOOL GetVolumeInformation(LPCSTR root, LPSTR volumeName, DWORD volumeSize,
                          DWORD *serial, DWORD *maxComponent, DWORD *flags,
                          LPSTR fileSystemName, DWORD fileSystemSize);
BOOL FileTimeToLocalFileTime(const FILETIME *fileTime, LPFILETIME localTime);
BOOL LocalFileTimeToFileTime(const FILETIME *localTime, LPFILETIME fileTime);
BOOL FileTimeToSystemTime(const FILETIME *fileTime, LPSYSTEMTIME systemTime);
BOOL SystemTimeToFileTime(const SYSTEMTIME *systemTime, LPFILETIME fileTime);
LONG CompareFileTime(const FILETIME *first, const FILETIME *second);
BOOL DosDateTimeToFileTime(WORD date, WORD time, LPFILETIME fileTime);
BOOL FileTimeToDosDateTime(const FILETIME *fileTime, WORD *date, WORD *time);

/* Current directory. It lives in the SDK, per process; relative paths and
   "." / ".." components resolve against it. */
DWORD GetCurrentDirectory(DWORD size, LPSTR buffer);
BOOL SetCurrentDirectory(LPCSTR path);
DWORD GetFullPathName(LPCSTR path, DWORD size, LPSTR buffer, LPSTR *filePart);

/* Strings. TCHAR is char, so these are the A forms. */
int lstrlen(LPCSTR text);
LPSTR lstrcpy(LPSTR destination, LPCSTR source);
LPSTR lstrcpyn(LPSTR destination, LPCSTR source, int length);
LPSTR lstrcat(LPSTR destination, LPCSTR source);
int lstrcmp(LPCSTR a, LPCSTR b);
int lstrcmpi(LPCSTR a, LPCSTR b);
int wsprintf(LPSTR buffer, LPCSTR format, ...);
int wvsprintf(LPSTR buffer, LPCSTR format, __builtin_va_list args);
LPSTR CharUpper(LPSTR text);
LPSTR CharLower(LPSTR text);
DWORD CharUpperBuff(LPSTR text, DWORD length);
DWORD CharLowerBuff(LPSTR text, DWORD length);
LPSTR CharNext(LPCSTR text);
LPSTR CharPrev(LPCSTR start, LPCSTR current);
BOOL IsCharAlpha(char c);
BOOL IsCharAlphaNumeric(char c);
BOOL IsCharUpper(char c);
BOOL IsCharLower(char c);

/* Memory. Moveable blocks never move: the handle is the address, and
   Lock returns it unchanged. */
#define LMEM_FIXED 0x0000
#define LMEM_MOVEABLE 0x0002
#define LMEM_ZEROINIT 0x0040
#define LPTR (LMEM_FIXED | LMEM_ZEROINIT)
#define LHND (LMEM_MOVEABLE | LMEM_ZEROINIT)
#define GMEM_FIXED 0x0000
#define GMEM_MOVEABLE 0x0002
#define GMEM_ZEROINIT 0x0040
#define GMEM_DDESHARE 0x2000
#define GMEM_SHARE 0x2000
#define GPTR (GMEM_FIXED | GMEM_ZEROINIT)
#define GHND (GMEM_MOVEABLE | GMEM_ZEROINIT)
#define HEAP_ZERO_MEMORY 0x00000008
HLOCAL LocalAlloc(UINT flags, SIZE_T size);
HLOCAL LocalReAlloc(HLOCAL memory, SIZE_T size, UINT flags);
HLOCAL LocalFree(HLOCAL memory);
LPVOID LocalLock(HLOCAL memory);
BOOL LocalUnlock(HLOCAL memory);
SIZE_T LocalSize(HLOCAL memory);
HLOCAL LocalHandle(LPCVOID memory);
HGLOBAL GlobalAlloc(UINT flags, SIZE_T size);
HGLOBAL GlobalReAlloc(HGLOBAL memory, SIZE_T size, UINT flags);
HGLOBAL GlobalFree(HGLOBAL memory);
LPVOID GlobalLock(HGLOBAL memory);
BOOL GlobalUnlock(HGLOBAL memory);
SIZE_T GlobalSize(HGLOBAL memory);
HGLOBAL GlobalHandle(LPCVOID memory);
HANDLE GetProcessHeap(void);
LPVOID HeapAlloc(HANDLE heap, DWORD flags, SIZE_T size);
LPVOID HeapReAlloc(HANDLE heap, DWORD flags, LPVOID memory, SIZE_T size);
BOOL HeapFree(HANDLE heap, DWORD flags, LPVOID memory);
SIZE_T HeapSize(HANDLE heap, DWORD flags, LPCVOID memory);
#define MoveMemory(destination, source, length) memmove((destination), (source), (length))
#define CopyMemory(destination, source, length) memcpy((destination), (source), (length))
#define FillMemory(destination, length, fill) memset((destination), (fill), (length))
#define ZeroMemory(destination, length) memset((destination), 0, (length))

/* Threads and synchronisation. Thread and event handles are kernel handles.
   GetLastError is per process, not per thread. */
#define INFINITE 0xFFFFFFFFu
#define WAIT_OBJECT_0 0x00000000u
#define WAIT_ABANDONED 0x00000080u
#define WAIT_TIMEOUT 0x00000102u
#define WAIT_FAILED 0xFFFFFFFFu
#define STILL_ACTIVE 0x00000103u
#define CREATE_SUSPENDED 0x00000004u
typedef struct _SECURITY_ATTRIBUTES {
  DWORD nLength;
  LPVOID lpSecurityDescriptor;
  BOOL bInheritHandle;
} SECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;
typedef DWORD(WINAPI *LPTHREAD_START_ROUTINE)(LPVOID parameter);
typedef struct _CRITICAL_SECTION {
  LONG LockCount;
  LONG RecursionCount;
  DWORD OwningThread;
  HANDLE LockSemaphore;
} CRITICAL_SECTION, *LPCRITICAL_SECTION, *PCRITICAL_SECTION;
HANDLE CreateThread(LPSECURITY_ATTRIBUTES attributes, SIZE_T stackSize,
                    LPTHREAD_START_ROUTINE routine, LPVOID parameter,
                    DWORD flags, LPDWORD threadId);
void ExitThread(DWORD code) __attribute__((noreturn));
BOOL GetExitCodeThread(HANDLE thread, LPDWORD code);
DWORD GetCurrentThreadId(void);
DWORD WaitForSingleObject(HANDLE handle, DWORD milliseconds);
void Sleep(DWORD milliseconds);
HANDLE CreateEvent(LPSECURITY_ATTRIBUTES attributes, BOOL manualReset,
                   BOOL initialState, LPCSTR name);
BOOL SetEvent(HANDLE event);
BOOL ResetEvent(HANDLE event);
BOOL CloseHandle(HANDLE handle);
void InitializeCriticalSection(LPCRITICAL_SECTION section);
void DeleteCriticalSection(LPCRITICAL_SECTION section);
void EnterCriticalSection(LPCRITICAL_SECTION section);
BOOL TryEnterCriticalSection(LPCRITICAL_SECTION section);
void LeaveCriticalSection(LPCRITICAL_SECTION section);
/* Implemented in the SDK; 386-safe (no CMPXCHG/XADD). */
LONG UndosInterlockedAdd(LONG volatile *target, LONG delta);
LONG UndosInterlockedExchange(LONG volatile *target, LONG value);
LONG UndosInterlockedCompareExchange(LONG volatile *target, LONG value, LONG comparand);
static inline LONG InterlockedIncrement(LONG volatile *value) { return UndosInterlockedAdd(value, 1); }
static inline LONG InterlockedDecrement(LONG volatile *value) { return UndosInterlockedAdd(value, -1); }
static inline LONG InterlockedExchange(LONG volatile *target, LONG value) {
  return UndosInterlockedExchange(target, value);
}
static inline LONG InterlockedCompareExchange(LONG volatile *target, LONG value, LONG comparand) {
  return UndosInterlockedCompareExchange(target, value, comparand);
}

/* Shell */
#define SE_ERR_FNF 2
#define SE_ERR_PNF 3
#define SE_ERR_ACCESSDENIED 5
#define SE_ERR_OOM 8
#define SE_ERR_NOASSOC 31
HINSTANCE ShellExecute(HWND window, LPCSTR operation, LPCSTR file,
                       LPCSTR parameters, LPCSTR directory, int show);
HINSTANCE FindExecutable(LPCSTR file, LPCSTR directory, LPSTR result);

/* UnDOS extension: in-memory association table used by ShellExecute and
   FindExecutable in place of the registry/WIN.INI [Extensions] section.
   extension is given without the dot ("TXT"); NULL program removes it. */
BOOL UndosSetAssociation(LPCSTR extension, LPCSTR program);

#ifdef __cplusplus
}
#endif

#endif
