#include <string.h>
#include "mounts.h"

static char mounts[UNDOS_MAX_MOUNTS][MAX_PATH];
static int mountCount;

static BOOL is_sep(char c) { return c == '/' || c == '\\'; }

static char upper(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }

int UndosMountRefresh(void) {
  char list[UNDOS_MAX_MOUNTS * MAX_PATH];
  DWORD length = GetLogicalDriveStrings(sizeof(list), list);
  const char *p = list;
  mountCount = 0;
  if (length && length < sizeof(list)) {
    while (*p && mountCount < UNDOS_MAX_MOUNTS) {
      size_t n = strlen(p), i;
      if (n < MAX_PATH && is_sep(p[0])) {
        for (i = 0; i < n; ++i) mounts[mountCount][i] = p[i] == '\\' ? '/' : p[i];
        while (i > 1 && mounts[mountCount][i - 1] == '/') --i;
        mounts[mountCount][i] = 0;
        if (i == 1 && mountCount > 0) {
          /* Keep "/" first so drive 0 is always the root. */
          lstrcpy(mounts[mountCount], mounts[0]);
          lstrcpy(mounts[0], "/");
        }
        ++mountCount;
      }
      p += n + 1;
    }
  }
  if (mountCount == 0 || lstrcmp(mounts[0], "/") != 0) {
    int i;
    if (mountCount == UNDOS_MAX_MOUNTS) --mountCount;
    for (i = mountCount; i > 0; --i) lstrcpy(mounts[i], mounts[i - 1]);
    lstrcpy(mounts[0], "/");
    ++mountCount;
  }
  return mountCount;
}

int UndosMountCount(void) {
  if (!mountCount) UndosMountRefresh();
  return mountCount;
}

LPCSTR UndosMountName(int drive) {
  if (drive < 0 || drive >= UndosMountCount()) return "";
  return mounts[drive];
}

UINT UndosDriveSet(LPSTR out, int drive) {
  LPCSTR name = UndosMountName(drive);
  UINT n = 0;
  if (!*name) {
    lstrcpy(out, "?:\\");
    return 3;
  }
  for (; *name; ++name) out[n++] = *name == '/' ? '\\' : *name;
  if (out[n - 1] != '\\') out[n++] = '\\';
  out[n] = 0;
  return n;
}

/* Length of mount `m` matched at the start of `path`, or 0. */
static size_t match(LPCSTR m, LPCSTR path) {
  size_t i = 0;
  if (m[1] == 0) return is_sep(path[0]) ? 1 : 0;
  for (; m[i]; ++i) {
    if (m[i] == '/' ? !is_sep(path[i]) : upper(m[i]) != upper(path[i])) return 0;
  }
  return (path[i] == 0 || is_sep(path[i])) ? i : 0;
}

int UndosDriveId(LPCSTR path) {
  int best = 0, i;
  size_t bestLen = 0;
  if (!path) return 0;
  for (i = 0; i < UndosMountCount(); ++i) {
    size_t n = match(mounts[i], path);
    if (n > bestLen) { bestLen = n; best = i; }
  }
  return best;
}

UINT UndosRootLength(LPCSTR path) {
  size_t n;
  if (!path || !is_sep(path[0])) return 0;
  n = match(mounts[UndosDriveId(path)], path);
  if (n > 1 && is_sep(path[n])) ++n;
  return (UINT)n;
}

BOOL UndosIsRootPath(LPCSTR path) {
  UINT n = UndosRootLength(path);
  return n && path[n] == 0;
}
