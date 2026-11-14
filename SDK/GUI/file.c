#include <windows.h>
#include <undoscrt/string.h>

#include "private/backend.h"
#include "private/ui_internal.h"

/* kernel32-style file API over the UnDOS IO syscalls. UnDOS has a single
   Unix-style root, so paths are absolute ("/DIR/A.TXT"); '\\' is accepted
   as a separator, and ':' is not a valid name character. Mount points
   ("/", "/MNT/CD") are the volume roots. File times come from FAT's local-time DOS stamps. */

#define UI_FIND_LIMIT 8
#define UI_UNDOS_PATH 256

typedef struct FindRecord {
  BOOL used;
  uint32_t directory;
  uint32_t index;
  char pattern[MAX_PATH];
} FindRecord;

static FindRecord finds[UI_FIND_LIMIT];

static BOOL fail(DWORD error) {
  ui_core_failure(UI_STATUS_INVALID_ARGUMENT, error);
  return FALSE;
}

static char upper(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 32) : c; }

static BOOL exists(const char *path, uint32_t *attributes);
static DWORD missing_error(const char *path);

/* Per-process current directory; relative paths resolve against it. */
static char currentDirectory[UI_UNDOS_PATH] = "/";

/* Appends one path, resolving "." and "..", onto out[0..*n). */
static BOOL append_components(const char *p, char *out, size_t *n) {
  while (*p) {
    while (*p == '/' || *p == '\\') ++p;
    if (!*p) break;
    const char *start = p;
    while (*p && *p != '/' && *p != '\\') ++p;
    const size_t length = (size_t)(p - start);
    if (length == 1 && start[0] == '.') continue;
    if (length == 2 && start[0] == '.' && start[1] == '.') {
      while (*n > 1 && out[*n - 1] != '/') --*n;
      if (*n > 1) --*n;
      continue;
    }
    if (*n + length + 2 >= UI_UNDOS_PATH) return fail(ERROR_FILENAME_EXCED_RANGE);
    if (out[*n - 1] != '/') out[(*n)++] = '/';
    memcpy(out + *n, start, length);
    *n += length;
  }
  return TRUE;
}

/* Converts a Win32 path to an absolute UnDOS path without a trailing
   separator (except the root). Relative paths resolve against the current
   directory; a path containing ':' is rejected. */
static BOOL undos_path(LPCSTR path, char *out) {
  if (!path || !path[0]) return fail(ERROR_PATH_NOT_FOUND);
  if (strchr(path, ':')) return fail(ERROR_INVALID_NAME);
  size_t n = 1;
  out[0] = '/';
  if (path[0] != '\\' && path[0] != '/' &&
      !append_components(currentDirectory, out, &n))
    return FALSE;
  if (!append_components(path, out, &n)) return FALSE;
  out[n] = 0;
  return TRUE;
}

DWORD GetCurrentDirectory(DWORD size, LPSTR buffer) {
  const DWORD length = (DWORD)strlen(currentDirectory);
  if (!buffer || size <= length) return length + 1;
  memcpy(buffer, currentDirectory, length + 1);
  return length;
}

BOOL SetCurrentDirectory(LPCSTR path) {
  char native[UI_UNDOS_PATH];
  if (!undos_path(path, native)) return FALSE;
  uint32_t attributes = FILE_ATTRIBUTE_DIRECTORY;
  if (strcmp(native, "/") != 0 && !exists(native, &attributes))
    return fail(missing_error(native));
  if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) return fail(ERROR_DIRECTORY);
  memcpy(currentDirectory, native, strlen(native) + 1);
  return TRUE;
}

DWORD GetFullPathName(LPCSTR path, DWORD size, LPSTR buffer, LPSTR *filePart) {
  char native[UI_UNDOS_PATH];
  if (filePart) *filePart = NULL;
  if (!undos_path(path, native)) return 0;
  const DWORD length = (DWORD)strlen(native);
  if (!buffer || size <= length) return length + 1;
  memcpy(buffer, native, length + 1);
  if (filePart && length > 1) *filePart = strrchr(buffer, '/') + 1;
  return length;
}

static uint32_t query(const char *path, UiFileInfo *info) {
  uint32_t handle = 0;
  uint32_t status = ui_backend_file_open(path, &handle);
  if (status != UI_STATUS_OK) return status;
  status = ui_backend_file_information(handle, info);
  (void)ui_backend_file_close(handle);
  return status;
}

static BOOL exists(const char *path, uint32_t *attributes) {
  UiFileInfo info = {0};
  if (query(path, &info) != UI_STATUS_OK) return FALSE;
  if (attributes) *attributes = info.attributes;
  return TRUE;
}

/* Packed DOS stamp (date << 16 | time) to FILETIME; 0 stays 0. */
static void dos_file_time(uint32_t stamp, FILETIME *out) {
  out->dwLowDateTime = out->dwHighDateTime = 0;
  if (stamp) (void)DosDateTimeToFileTime((WORD)(stamp >> 16), (WORD)stamp, out);
}

static BOOL parent_exists(const char *path) {
  char parent[UI_UNDOS_PATH];
  memcpy(parent, path, strlen(path) + 1);
  char *slash = strrchr(parent, '/');
  if (!slash) return FALSE;
  if (slash == parent) slash[1] = 0;
  else *slash = 0;
  uint32_t attributes = 0;
  return exists(parent, &attributes) && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static DWORD missing_error(const char *path) {
  return parent_exists(path) ? ERROR_FILE_NOT_FOUND : ERROR_PATH_NOT_FOUND;
}

static DWORD public_attributes(uint32_t attributes) {
  const DWORD known = FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN |
                      FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_DIRECTORY |
                      FILE_ATTRIBUTE_ARCHIVE;
  const DWORD result = attributes & known;
  return result ? result : FILE_ATTRIBUTE_NORMAL;
}

DWORD GetFileAttributes(LPCSTR path) {
  char native[UI_UNDOS_PATH];
  if (!undos_path(path, native)) return INVALID_FILE_ATTRIBUTES;
  uint32_t attributes = 0;
  if (strcmp(native, "/") == 0) return FILE_ATTRIBUTE_DIRECTORY;
  if (!exists(native, &attributes)) {
    fail(missing_error(native));
    return INVALID_FILE_ATTRIBUTES;
  }
  return public_attributes(attributes);
}

BOOL GetFileAttributesEx(LPCSTR path, GET_FILEEX_INFO_LEVELS level, void *result) {
  if (level != GetFileExInfoStandard || !result) return fail(ERROR_INVALID_PARAMETER);
  char native[UI_UNDOS_PATH];
  if (!undos_path(path, native)) return FALSE;
  WIN32_FILE_ATTRIBUTE_DATA *data = (WIN32_FILE_ATTRIBUTE_DATA *)result;
  memset(data, 0, sizeof(*data));
  if (strcmp(native, "/") == 0) {
    data->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
    return TRUE;
  }
  UiFileInfo info = {0};
  if (query(native, &info) != UI_STATUS_OK) return fail(missing_error(native));
  data->dwFileAttributes = public_attributes(info.attributes);
  dos_file_time(info.creationTime, &data->ftCreationTime);
  dos_file_time(info.lastAccessTime, &data->ftLastAccessTime);
  dos_file_time(info.lastWriteTime, &data->ftLastWriteTime);
  data->nFileSizeLow = (DWORD)info.size;
  data->nFileSizeHigh = (DWORD)(info.size >> 32);
  return TRUE;
}

BOOL SetFileAttributes(LPCSTR path, DWORD attributes) {
  const DWORD current = GetFileAttributes(path);
  if (current == INVALID_FILE_ATTRIBUTES) return FALSE;
  const DWORD settable = FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN |
                         FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE;
  // Win32 ignores bits it cannot set, and FILE_ATTRIBUTE_NORMAL means none.
  const DWORD wanted = attributes & settable;
  if ((current & settable) == wanted) return TRUE;
  char native[UI_UNDOS_PATH];
  if (!undos_path(path, native)) return FALSE;
  if (strcmp(native, "/") == 0) return fail(ERROR_ACCESS_DENIED);
  const uint32_t status = ui_backend_file_set_attributes(native, wanted);
  if (status == UI_STATUS_OK) return TRUE;
  // Open entries, read-only volumes and long-name-only entries all deny.
  ui_core_failure(status, status == UI_STATUS_NOT_FOUND ? missing_error(native)
                                                        : ERROR_ACCESS_DENIED);
  return FALSE;
}

/* DOS-style wildcard match, case-insensitive. "*.*" also matches names
   without an extension, as on FAT. */
static BOOL wildcard(const char *pattern, const char *name) {
  if (strcmp(pattern, "*.*") == 0 || strcmp(pattern, "*") == 0) return TRUE;
  if (*pattern == 0) return *name == 0;
  if (*pattern == '*') {
    for (const char *n = name;; ++n) {
      if (wildcard(pattern + 1, n)) return TRUE;
      if (!*n) return FALSE;
    }
  }
  if (*name == 0) {
    // "NAME.*" and "NAME." match "NAME".
    return (pattern[0] == '.' && (pattern[1] == 0 ||
                                  (pattern[1] == '*' && pattern[2] == 0)));
  }
  if (*pattern == '?' || upper(*pattern) == upper(*name))
    return wildcard(pattern + 1, name + 1);
  return FALSE;
}

static BOOL next_match(FindRecord *find, LPWIN32_FIND_DATA data) {
  for (;;) {
    char name[MAX_PATH];
    UiFileInfo info = {0};
    uint32_t found = 0;
    const uint32_t status = ui_backend_file_read_directory(
        find->directory, find->index++, name, sizeof(name), &info, &found);
    if (status != UI_STATUS_OK) {
      ui_core_failure(status, ui_core_error_from_status(status));
      return FALSE;
    }
    if (!found) return fail(ERROR_NO_MORE_FILES);
    if (!wildcard(find->pattern, name)) continue;
    memset(data, 0, sizeof(*data));
    data->dwFileAttributes = public_attributes(info.attributes);
    data->nFileSizeLow = (DWORD)info.size;
    data->nFileSizeHigh = (DWORD)(info.size >> 32);
    dos_file_time(info.creationTime, &data->ftCreationTime);
    dos_file_time(info.lastAccessTime, &data->ftLastAccessTime);
    dos_file_time(info.lastWriteTime, &data->ftLastWriteTime);
    memcpy(data->cFileName, name, strlen(name) + 1);
    return TRUE;
  }
}

HANDLE FindFirstFile(LPCSTR pattern, LPWIN32_FIND_DATA data) {
  char native[UI_UNDOS_PATH];
  if (!data) {
    fail(ERROR_INVALID_PARAMETER);
    return INVALID_HANDLE_VALUE;
  }
  if (!undos_path(pattern, native)) return INVALID_HANDLE_VALUE;
  char *slash = strrchr(native, '/');
  const char *mask = slash[1] ? slash + 1 : "*.*";
  if (strlen(mask) >= MAX_PATH) {
    fail(ERROR_FILENAME_EXCED_RANGE);
    return INVALID_HANDLE_VALUE;
  }
  FindRecord *find = NULL;
  size_t slot = 0;
  for (; slot < UI_FIND_LIMIT && !find; ++slot)
    if (!finds[slot].used) find = &finds[slot];
  if (!find) {
    fail(ERROR_NOT_ENOUGH_MEMORY);
    return INVALID_HANDLE_VALUE;
  }
  memcpy(find->pattern, mask, strlen(mask) + 1);
  if (slash == native) slash[1] = 0;
  else *slash = 0;
  uint32_t attributes = 0;
  if (!exists(native, &attributes) || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
    if (strcmp(native, "/") != 0) {
      fail(ERROR_PATH_NOT_FOUND);
      return INVALID_HANDLE_VALUE;
    }
  }
  const uint32_t status = ui_backend_file_open(native, &find->directory);
  if (status != UI_STATUS_OK) {
    fail(ERROR_PATH_NOT_FOUND);
    return INVALID_HANDLE_VALUE;
  }
  find->used = TRUE;
  find->index = 0;
  if (!next_match(find, data)) {
    const DWORD error = GetLastError();
    (void)ui_backend_file_close(find->directory);
    find->used = FALSE;
    fail(error == ERROR_NO_MORE_FILES ? ERROR_FILE_NOT_FOUND : error);
    return INVALID_HANDLE_VALUE;
  }
  return (HANDLE)(0x46490000u + slot);
}

static FindRecord *find_record(HANDLE handle) {
  const uint32_t slot = (uint32_t)handle - 0x46490001u;
  if (((uint32_t)handle & 0xFFFF0000u) != 0x46490000u || slot >= UI_FIND_LIMIT ||
      !finds[slot].used) {
    fail(ERROR_INVALID_HANDLE);
    return NULL;
  }
  return &finds[slot];
}

BOOL FindNextFile(HANDLE handle, LPWIN32_FIND_DATA data) {
  FindRecord *find = find_record(handle);
  if (!find) return FALSE;
  if (!data) return fail(ERROR_INVALID_PARAMETER);
  return next_match(find, data);
}

BOOL FindClose(HANDLE handle) {
  FindRecord *find = find_record(handle);
  if (!find) return FALSE;
  (void)ui_backend_file_close(find->directory);
  find->used = FALSE;
  return TRUE;
}

static BOOL operate(uint32_t operation, const char *source,
                    const char *destination) {
  const uint32_t status = ui_backend_file_operate(operation, source, destination);
  if (status == UI_STATUS_OK) return TRUE;
  // The kernel reports open entries, busy volumes and unsupported cases
  // (directory copies, cross-parent directory moves) the same way.
  ui_core_failure(status, status == UI_STATUS_NOT_FOUND
                              ? missing_error(source)
                              : status == UI_STATUS_OUT_OF_MEMORY
                                    ? ERROR_NOT_ENOUGH_MEMORY
                                    : ERROR_ACCESS_DENIED);
  return FALSE;
}

BOOL CopyFile(LPCSTR source, LPCSTR destination, BOOL failIfExists) {
  char from[UI_UNDOS_PATH], to[UI_UNDOS_PATH];
  if (!undos_path(source, from) || !undos_path(destination, to)) return FALSE;
  uint32_t attributes = 0;
  if (!exists(from, &attributes)) return fail(missing_error(from));
  if (attributes & FILE_ATTRIBUTE_DIRECTORY) return fail(ERROR_ACCESS_DENIED);
  uint32_t target = 0;
  if (exists(to, &target)) {
    if (failIfExists) return fail(ERROR_FILE_EXISTS);
    if (target & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY))
      return fail(ERROR_ACCESS_DENIED);
    if (!operate(UI_FILE_DELETE, to, NULL)) return FALSE;
  } else if (!parent_exists(to)) {
    return fail(ERROR_PATH_NOT_FOUND);
  }
  return operate(UI_FILE_COPY, from, to);
}

BOOL MoveFileEx(LPCSTR source, LPCSTR destination, DWORD flags) {
  char from[UI_UNDOS_PATH], to[UI_UNDOS_PATH];
  if (flags & ~(DWORD)(MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED))
    return fail(ERROR_INVALID_PARAMETER);
  if (!undos_path(source, from) || !undos_path(destination, to)) return FALSE;
  uint32_t attributes = 0;
  if (!exists(from, &attributes)) return fail(missing_error(from));
  uint32_t target = 0;
  if (exists(to, &target)) {
    if (!(flags & MOVEFILE_REPLACE_EXISTING) ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY))
      return fail(ERROR_ALREADY_EXISTS);
    if (target & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY))
      return fail(ERROR_ACCESS_DENIED);
    if (!operate(UI_FILE_DELETE, to, NULL)) return FALSE;
  } else if (!parent_exists(to)) {
    return fail(ERROR_PATH_NOT_FOUND);
  }
  return operate(UI_FILE_MOVE, from, to);
}

BOOL MoveFile(LPCSTR source, LPCSTR destination) {
  return MoveFileEx(source, destination, 0);
}

BOOL DeleteFile(LPCSTR path) {
  char native[UI_UNDOS_PATH];
  if (!undos_path(path, native)) return FALSE;
  uint32_t attributes = 0;
  if (!exists(native, &attributes)) return fail(missing_error(native));
  if (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY))
    return fail(ERROR_ACCESS_DENIED);
  return operate(UI_FILE_DELETE, native, NULL);
}

BOOL CreateDirectory(LPCSTR path, void *security) {
  (void)security;
  char native[UI_UNDOS_PATH];
  if (!undos_path(path, native)) return FALSE;
  if (exists(native, NULL)) return fail(ERROR_ALREADY_EXISTS);
  if (!parent_exists(native)) return fail(ERROR_PATH_NOT_FOUND);
  return operate(UI_FILE_MKDIR, native, NULL);
}

BOOL RemoveDirectory(LPCSTR path) {
  char native[UI_UNDOS_PATH];
  if (!undos_path(path, native)) return FALSE;
  uint32_t attributes = 0;
  if (!exists(native, &attributes)) return fail(missing_error(native));
  if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) return fail(ERROR_DIRECTORY);
  if (attributes & FILE_ATTRIBUTE_READONLY) return fail(ERROR_ACCESS_DENIED);
  uint32_t handle = 0, found = 0;
  UiFileInfo info = {0};
  char name[MAX_PATH];
  if (ui_backend_file_open(native, &handle) == UI_STATUS_OK) {
    const uint32_t status = ui_backend_file_read_directory(
        handle, 0, name, sizeof(name), &info, &found);
    (void)ui_backend_file_close(handle);
    if (status == UI_STATUS_OK && found) return fail(ERROR_DIR_NOT_EMPTY);
  }
  return operate(UI_FILE_DELETE, native, NULL);
}

/* Mount points stand in for drive roots: UnDOS has one tree, and the kernel
   reports which volume is mounted at which path. */
static BOOL is_mount_point(const char *native) {
  char name[UI_UNDOS_PATH];
  for (uint32_t index = 0;; ++index) {
    uint32_t found = 0;
    if (ui_backend_query_mount(index, name, sizeof(name), &found) != UI_STATUS_OK ||
        !found)
      return FALSE;
    if (strcmp(name, native) == 0) return TRUE;
  }
}

DWORD GetLogicalDriveStrings(DWORD size, LPSTR buffer) {
  char name[UI_UNDOS_PATH];
  DWORD length = 0;
  for (uint32_t index = 0;; ++index) {
    uint32_t found = 0;
    const uint32_t status = ui_backend_query_mount(index, name, sizeof(name), &found);
    if (status != UI_STATUS_OK) {
      ui_core_failure(status, ui_core_error_from_status(status));
      return 0;
    }
    if (!found) break;
    const DWORD n = (DWORD)strlen(name) + 1;
    if (buffer && length + n < size) memcpy(buffer + length, name, n);
    length += n;
  }
  // Win32: on success the count excludes the final NUL; if the buffer is
  // too small the required size including it is returned.
  if (!buffer || length + 1 > size) return length + 1;
  buffer[length] = 0;
  return length;
}

UINT GetDriveType(LPCSTR root) {
  char native[UI_UNDOS_PATH];
  if (!root) root = "/";
  if (!undos_path(root, native)) return DRIVE_NO_ROOT_DIR;
  // NOT_YET_VERIFIED: the kernel does not report the device kind, so every
  // mounted volume is reported as fixed.
  return is_mount_point(native) ? DRIVE_FIXED : DRIVE_NO_ROOT_DIR;
}

BOOL GetVolumeInformation(LPCSTR root, LPSTR volumeName, DWORD volumeSize,
                          DWORD *serial, DWORD *maxComponent, DWORD *flags,
                          LPSTR fileSystemName, DWORD fileSystemSize) {
  char native[UI_UNDOS_PATH];
  if (!root) root = "/";
  if (!undos_path(root, native)) return FALSE;
  char label[32], fileSystem[16];
  uint32_t number = 0, component = 0, volume = 0;
  const uint32_t status = ui_backend_volume_information(
      native, label, sizeof(label), fileSystem, sizeof(fileSystem), &number,
      &component, &volume);
  if (status == UI_STATUS_NOT_FOUND) return fail(ERROR_PATH_NOT_FOUND);
  if (status != UI_STATUS_OK) {
    ui_core_failure(status, ui_core_error_from_status(status));
    return FALSE;
  }
  if ((volumeName && strlen(label) >= volumeSize) ||
      (fileSystemName && strlen(fileSystem) >= fileSystemSize))
    return fail(ERROR_BAD_LENGTH);
  if (volumeName) memcpy(volumeName, label, strlen(label) + 1);
  if (fileSystemName) memcpy(fileSystemName, fileSystem, strlen(fileSystem) + 1);
  if (serial) *serial = number;
  if (maxComponent) *maxComponent = component;
  if (flags)
    *flags = (volume & UI_VOLUME_CASE_PRESERVED ? FILE_CASE_PRESERVED_NAMES : 0) |
             (volume & UI_VOLUME_READ_ONLY ? FILE_READ_ONLY_VOLUME : 0);
  return TRUE;
}

/* FILETIME counts 100 ns intervals since 1601-01-01 UTC. UnDOS has no time
   zones, so local time equals UTC. */
#define TICKS_PER_SECOND 10000000ull
#define SECONDS_PER_DAY 86400ull

BOOL FileTimeToLocalFileTime(const FILETIME *fileTime, LPFILETIME localTime) {
  if (!fileTime || !localTime) return fail(ERROR_INVALID_PARAMETER);
  *localTime = *fileTime;
  return TRUE;
}

BOOL LocalFileTimeToFileTime(const FILETIME *localTime, LPFILETIME fileTime) {
  return FileTimeToLocalFileTime(localTime, fileTime);
}

static BOOL leap(uint32_t year) {
  return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static uint32_t month_days(uint32_t year, uint32_t month) {
  static const uint8_t days[12] = {31, 28, 31, 30, 31, 30,
                                   31, 31, 30, 31, 30, 31};
  return days[month - 1] + (month == 2 && leap(year));
}

BOOL FileTimeToSystemTime(const FILETIME *fileTime, LPSYSTEMTIME systemTime) {
  if (!fileTime || !systemTime) return fail(ERROR_INVALID_PARAMETER);
  const uint64_t ticks = ((uint64_t)fileTime->dwHighDateTime << 32) |
                         fileTime->dwLowDateTime;
  if (ticks >> 63) return fail(ERROR_INVALID_PARAMETER);
  uint64_t seconds = ticks / TICKS_PER_SECOND;
  uint64_t days = seconds / SECONDS_PER_DAY;
  seconds %= SECONDS_PER_DAY;
  systemTime->wMilliseconds = (WORD)((ticks % TICKS_PER_SECOND) / 10000);
  systemTime->wHour = (WORD)(seconds / 3600);
  systemTime->wMinute = (WORD)(seconds / 60 % 60);
  systemTime->wSecond = (WORD)(seconds % 60);
  systemTime->wDayOfWeek = (WORD)((days + 1) % 7);  // 1601-01-01 was a Monday.
  uint32_t year = 1601;
  for (;;) {
    const uint32_t length = leap(year) ? 366 : 365;
    if (days < length) break;
    days -= length;
    ++year;
  }
  uint32_t month = 1;
  while (days >= month_days(year, month)) days -= month_days(year, month++);
  systemTime->wYear = (WORD)year;
  systemTime->wMonth = (WORD)month;
  systemTime->wDay = (WORD)(days + 1);
  return TRUE;
}

BOOL SystemTimeToFileTime(const SYSTEMTIME *systemTime, LPFILETIME fileTime) {
  if (!systemTime || !fileTime) return fail(ERROR_INVALID_PARAMETER);
  const SYSTEMTIME *t = systemTime;
  if (t->wYear < 1601 || t->wYear > 30827 || t->wMonth < 1 || t->wMonth > 12 ||
      t->wDay < 1 || t->wDay > month_days(t->wYear, t->wMonth) ||
      t->wHour > 23 || t->wMinute > 59 || t->wSecond > 59 ||
      t->wMilliseconds > 999)
    return fail(ERROR_INVALID_PARAMETER);
  uint64_t days = 0;
  for (uint32_t year = 1601; year < t->wYear; ++year) days += leap(year) ? 366 : 365;
  for (uint32_t month = 1; month < t->wMonth; ++month)
    days += month_days(t->wYear, month);
  days += t->wDay - 1u;
  const uint64_t seconds = days * SECONDS_PER_DAY + t->wHour * 3600u +
                           t->wMinute * 60u + t->wSecond;
  const uint64_t ticks = seconds * TICKS_PER_SECOND + t->wMilliseconds * 10000ull;
  fileTime->dwLowDateTime = (DWORD)ticks;
  fileTime->dwHighDateTime = (DWORD)(ticks >> 32);
  return TRUE;
}

BOOL DosDateTimeToFileTime(WORD date, WORD time, LPFILETIME fileTime) {
  if (!fileTime) return fail(ERROR_INVALID_PARAMETER);
  const SYSTEMTIME t = {.wYear = (WORD)(1980 + (date >> 9)),
                        .wMonth = (WORD)((date >> 5) & 15),
                        .wDay = (WORD)(date & 31),
                        .wHour = (WORD)(time >> 11),
                        .wMinute = (WORD)((time >> 5) & 63),
                        .wSecond = (WORD)((time & 31) * 2)};
  return SystemTimeToFileTime(&t, fileTime);
}

BOOL FileTimeToDosDateTime(const FILETIME *fileTime, WORD *date, WORD *time) {
  SYSTEMTIME t;
  if (!date || !time || !FileTimeToSystemTime(fileTime, &t))
    return fail(ERROR_INVALID_PARAMETER);
  if (t.wYear < 1980 || t.wYear > 2107) return fail(ERROR_INVALID_PARAMETER);
  *date = (WORD)(((t.wYear - 1980) << 9) | (t.wMonth << 5) | t.wDay);
  *time = (WORD)((t.wHour << 11) | (t.wMinute << 5) | (t.wSecond / 2));
  return TRUE;
}

LONG CompareFileTime(const FILETIME *first, const FILETIME *second) {
  const uint64_t a = ((uint64_t)first->dwHighDateTime << 32) | first->dwLowDateTime;
  const uint64_t b = ((uint64_t)second->dwHighDateTime << 32) | second->dwLowDateTime;
  return a < b ? -1 : a > b ? 1 : 0;
}

void ui_file_reset(void) {
  for (size_t i = 0; i < UI_FIND_LIMIT; ++i)
    if (finds[i].used) (void)ui_backend_file_close(finds[i].directory);
  memset(finds, 0, sizeof(finds));
}
