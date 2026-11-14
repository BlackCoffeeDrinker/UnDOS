/* UnDOS has one Unix-style root. WINFILE's DRIVE is an
   index into a mount table built from GetLogicalDriveStrings ("/",
   "/MNT/CD", ...). WINFILE keeps paths in backslash form ("\MNT\CD\X"),
   which the SDK accepts. */
#ifndef UNDOS_MOUNTS_H
#define UNDOS_MOUNTS_H

#include <windows.h>

#define UNDOS_MAX_MOUNTS 26

/* Rebuilds the table; an empty mount list falls back to "/". */
int UndosMountRefresh(void);
int UndosMountCount(void);
/* Display name ("/", "/MNT/CD"), or "" when the drive is not mounted. */
LPCSTR UndosMountName(int drive);
/* Writes the drive's root ("\" or "\MNT\CD\"); an unmounted drive gets
   "?:\" so every file call on it fails. Returns the length written. */
UINT UndosDriveSet(LPSTR out, int drive);
/* Longest mount that prefixes `path`; 0 (the root) when none does. */
int UndosDriveId(LPCSTR path);
/* Length of the root part of `path` including its trailing separator. */
UINT UndosRootLength(LPCSTR path);
BOOL UndosIsRootPath(LPCSTR path);

#endif
