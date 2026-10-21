# File Manager

File Manager is an original WINFILE-style UnDOS application. It builds as the
standalone SDK consumer `winfile.elf`. It does not use Windows code or resources.

## Use the application

Open **File Manager** in Program Manager's **Main** group. You can also launch
`/System/winfile.elf` from the Run dialog. An optional first argument selects
the initial directory, for example `/System`.

The left pane is a lazily expanded directory tree. Click a folder to browse it.
Click its `+`/`-` marker, double-click it, or use Left/Right to expand or collapse
it. The right pane shows names, byte sizes and FAT attributes. Select entries
with the mouse or arrow keys. Double-click a folder to enter it; double-click
a native `.ELF` executable to launch it. Other files have no application
association.

Use the File and View menus, toolbar, or these keys:

| Key | Action |
|---|---|
| Enter | Open the selected folder or run the selected executable |
| Backspace | Go to the parent directory |
| Tab | Switch keyboard focus between panes |
| Home / End | Select the first / last entry in the pane |
| Page Up / Page Down | Change file pages or move through the tree |
| F1 | Help |
| F2 | Rename |
| F3 | Enter an absolute directory path |
| F4 | Show properties |
| F5 | Refresh the directory and reset the cached tree |
| F6 / F7 | Copy / move a file |
| F8 | Create a directory |
| F10 | Open the File menu |
| Delete | Delete the selected file or empty directory after confirmation |

The Copy, Move, Rename, Delete and Properties commands operate on the right-pane
selection. Destinations can be absolute paths or names relative to the current
directory. Give the complete destination filename; the application does not
implicitly append a filename to a destination folder.

## Supported behavior and limits

- File copy and move work on the same mounted FAT volume. Directory creation,
  empty-directory deletion and same-parent directory renaming are supported.
  Recursive directory copy/deletion, cross-parent directory moves and
  cross-volume operations are not supported.
- Names must be valid ASCII FAT 8.3 names. Long-name entries are shown through
  their short aliases; mutating an entry with attached long-name records is
  rejected. FAT12, FAT16 and mirrored FAT32 are supported. ExFAT and FAT32
  volumes with disabled FAT mirroring are not supported for mutations.
- Existing destinations are never overwritten. Read-only sources cannot be
  moved, renamed or deleted. Open source entries and open directory handles
  block mutations. Unrelated open files, such as the system registry hive,
  do not block mutations.
- The file pane has 64 entries per page. Prev/Next and Page Up/Page Down expose
  further pages. Sorting applies to the current page and keeps directories
  first. Scroll controls and arrow keys expose rows below the visible area.
- The tree holds 256 folders. Collapse branches or use Path if that limit is
  reached. Paths are limited to 255 bytes. Timestamps, disk formatting,
  attribute editing, multi-selection and file associations are not implemented.
- Operations are synchronous and have no undo or crash-consistency guarantee.
  After a device write error, the FAT driver disables further mutations until
  remount. Check the filesystem offline before re-enabling writes.

## Build

Use an SDK rebuilt from this revision. An older `undos-sdk:dev` image does not
contain the new directory and file-operation bindings. From the repository root:

```sh
python3 Scripts/build.py apps --docker
python3 Scripts/build.py image --docker
```

The `apps` helper builds `build/apps/FileManager/Debug/winfile.elf`. The `image`
helper includes it at `/System/winfile.elf` and stages the default Program
Manager entry. The optional filesystem probe is not installed by these helpers.

To build only File Manager against the staged SDK:

```sh
python3 Scripts/build.py sdk --docker
docker run --rm --mount "type=bind,source=${PWD},target=/src" -w /src \
  gcc-standalone:i686-elf cmake -S Apps/FileManager \
  -B Apps/FileManager/build-undos \
  --toolchain /src/build/sdk/Debug/share/cmake/UnDOS/UnDOS-toolchain.cmake
docker run --rm --mount "type=bind,source=${PWD},target=/src" -w /src \
  gcc-standalone:i686-elf cmake --build Apps/FileManager/build-undos
```

The result is `Apps/FileManager/build-undos/winfile.elf`. The project can also be
copied outside the repository and configured against an installed updated SDK.
Its CMake project does not depend on source-tree headers.

## Developer checks

The host-only model checks are independent of the SDK:

```sh
cmake -S Apps/FileManager/tests -B Apps/FileManager/.build/model
cmake --build Apps/FileManager/.build/model
ctest --test-dir Apps/FileManager/.build/model --output-on-failure
```

Enable `FILEMANAGER_BUILD_PROBE=ON` to build `fmprobe.elf`. Stage both executables
into a **disposable image**. The probe creates `/FMPROBE`, copies the system's
`PROGMAN.INI`, verifies its bytes, moves/renames it and deletes its test entries.
It never replaces an existing `/FMPROBE`; a failed probe can leave that folder
for inspection.

```sh
python3 Apps/FileManager/tests/smoke.py --image-dir PATH/TO/IMAGE \
  --output Apps/FileManager/.build/probe-smoke --scenario probe
python3 Apps/FileManager/tests/smoke.py --image-dir PATH/TO/IMAGE \
  --output Apps/FileManager/.build/ui-smoke --scenario ui
```

Run these sequentially. Both use `run-qemu.sh` and retain its 15-second bound.
The UI scenario captures directory navigation, tree, menu and help screenshots.
Inspect those captures; a success marker alone does not establish visual
correctness. Hosted VFS/FAT integration coverage lives in
`Tests/Hosted/tests/vfs_fat_integration_test.cpp`.
