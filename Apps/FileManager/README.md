# File Manager

File Manager is the UnDOS version of Microsoft WINFILE (MIT licence, see
`winfile/LICENSE`). The source in `winfile/` is copied from
`examples/winfile/src` and edited directly where UnDOS differs. `undos/` holds
the compatibility shim, the stubs and the generated resource tables. It builds
as `winfile.elf` and is installed as `/System/winfile.elf`.

Open **File Manager** in Program Manager's **Main** group, or run
`/System/winfile.elf`.

## Layout

| Path | Contents |
|---|---|
| `winfile/` | WINFILE sources, edited for UnDOS |
| `undos/undos_compat.h`, `win32_extra.h`, `win32_decls.h` | Win32 names the SDK lacks |
| `undos/stubs.c`, `win32_stubs.c` | Stubs for out-of-scope features |
| `undos/mounts.c` | Mount table (the "drives") |
| `undos/resources.c` | Generated from `winfile/res.rc` by `Scripts/rc2c.py` (target `fileman-resources`) |

## Paths and drives

UnDOS has one Unix-style root. A "drive" is a mount point
from `GetLogicalDriveStrings` (`/`, `/MNT/CD`, ...); the drive bar lists the
mounts, and the list falls back to `/` when it is empty. Paths are absolute;
both `/` and `\` are accepted as separators.

## WINFILE parity

| Feature | Status |
|---|---|
| Frame, menus, accelerators, MDI directory windows | Ported |
| Tree and directory panes (owner-draw list) | Ported |
| Copy, Move, Delete, Rename, Create Directory | Ported, using WINFILE's dialogs |
| Properties | WINFILE's Attributes dialogs (no shell property sheet) |
| Drive bar | Lists mount points |
| Search, sort, view and filter options | Ported |
| Toolbar | Removed (as in Windows 3.1) |
| Status bar | Not shown: `CreateStatusWindow` is a stub |
| Settings (`winfile.ini`, registry) | Not saved; built-in defaults every start |
| Printing, change notification, network (WNet), format/disk copy/label (fmifs) | Stubbed; commands greyed or fail |
| Extensions | Stubbed |
| Associations, Go To Directory | Show "Not supported on UnDOS" |
| Clipboard and OLE drag and drop | Not available: Paste greyed, Cut/Copy beep |
| Font and Open File common dialogs | Not available; commands back out |
| Help (`WinHelp`) | Stubbed |
| Wide characters | None: `TCHAR` is `char` |

Not yet checked on hardware: runtime behaviour of the port as a whole.

## Build

The `apps` helper in `Scripts/build.py` builds it, and `build-hdd.sh` puts it on
`HDD_IMG.IMG`. Standalone:

```sh
cmake -S Apps/FileManager -B Apps/FileManager/build-undos \
  -DCMAKE_TOOLCHAIN_FILE=... -DCMAKE_PREFIX_PATH=PATH/TO/SDK
cmake --build Apps/FileManager/build-undos
```

## Smoke test

```sh
python3 Apps/FileManager/tests/smoke.py --image-dir PATH/TO/IMAGE --output OUT
```

It starts File Manager from Program Manager and waits for the serial marker
`filemanager: directory ready path=`. `--scenario probe` runs the filesystem
probe (`fmprobe.elf`, built with `-DFILEMANAGER_BUILD_PROBE=ON`).
