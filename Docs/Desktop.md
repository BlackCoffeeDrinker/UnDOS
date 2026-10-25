# Desktop and graphical environment

[Project overview](../README.md)

Build and run instructions are in the [development guide](Development.md).
Repository commands below run from the repository root.

## Graphical User Interface

The GUI is heavily inspired by GDI and the classic Windows 3.1 / Workgroups environment:

* Basic primitive window management and dirty rect z-ordering.
* Lightweight software pixel rendering pipeline optimized for retro VGAM/SVGA contexts.
* Simple, predictable, keyboard/mouse event focus structures.

## Native Program Manager session

Normal startup uses `init.elf` as a small supervisor for
`/System/progman.elf`. Before starting the shell, init reads
`/System/FONTS.INI` and publishes the selected system-font backend. The shipped
file selects `/System/Fonts/SSR.TTF` and `/System/Fonts/SSB.TTF` at 11 pt,
which converts to 15 ppem at 96 DPI. A `Backend=bitmap` setting selects the
built-in bitmap face without loading external files. Missing, unreadable, or
invalid configuration and failed publication stop desktop startup with an
explicit serial diagnostic. The shell loads
`/System/PROGMAN.INI` and opens native program-group windows. This is a native
ELF shell, not Windows binary compatibility. The `demo.elf` notes/argument
exerciser and windowed `term.elf` are included in the initial disk image.

The strict ASCII `/System/FONTS.INI` profile accepts one `[Fonts]` section and
a required case-sensitive `Backend` key. `Backend=bitmap` selects the built-in
face and does not accept face paths or a point size. `Backend=truetype`
requires one each of the case-sensitive `Regular`, `Bold`, and `SizePt` keys.
The file is limited to 1023 bytes. Each absolute font path is 1–255 bytes;
quote paths that contain spaces. The parser rejects duplicate or unknown
sections and keys. It does not support escapes or inline comments. `SizePt`
is a base-10 integer from 1 through 192. Init converts the value with integer
rounding at 96 DPI: `ppem = (SizePt * 96 + 36) / 72`. Image assembly installs
the shipped `Apps/Init/FONTS.INI` as `/System/FONTS.INI`; an image overlay may
replace it.

Program Manager uses the classic gray MDI workspace, white group interiors,
32-pixel original program icons, and a compact File/Options/Window/Help menu
bar. Main opens at startup; the other configured groups appear as minimized
group icons. There is no taskbar or bottom status/help strip.

The shell requires a primary display of at least 640x480. Both Cirrus and
the planar VGA fallback use Source Sans 3 and the classic palette.
Program Manager icon labels, File Manager panes and toolbars, dialogs, and
Terminal use the published font through the SDK font-object API. Text-dependent
row, control, and cell geometry uses font metrics and measured runs. Editing
fields show a measured suffix so newly typed text remains visible. Font-change
notifications reflow application-owned layouts without blocking publication.
Shared window decorations follow the NT 3.51 style: centered captions,
dark-blue active title bars, beveled gray frames, a system-menu box, and
triangle minimize/maximize controls. Maximized windows show paired restore
arrows; closing remains available through the system menu or Alt+F4.

File Manager sizes its outer window to the active display. On a 640-pixel-wide
display it uses a 580-pixel window and wraps its nine toolbar buttons into two
rows; on a 1024-pixel-wide display it retains the 680-pixel window and one-row
toolbar. The compact toolbar shifts the path field and list down by one button
row. Painting, list hit testing, and scrolling use the same client-area layout.
Text that does not fit a narrower column is shortened with an ellipsis.

Use arrow keys and Enter to select/run a program, Tab to open the next group,
and Page Up/Page Down to reach additional icons. F10 opens the menu; F1 opens
About. The File menu provides Run, group/program creation, properties, and
deletion. The Window menu reopens groups and arranges their windows.

**Settings are session-only.** The shipped configuration is read-only; editing
does not write FAT or claim to persist across reboot. Closing a group hides its
view without deleting its entries. The terminal has `help`, `clear`, `echo`,
`run`, and `exit` commands. `run /System/APP.elf [ARGS]` connects the child's
stdin, stdout, and stderr to the terminal's virtual TTY. While the child runs,
keyboard input goes to it, Enter commits a line, and Ctrl+D sends EOF (or
commits a partially typed line). The command prompt returns when it exits.
Closing the terminal closes its TTY source, releasing a child waiting for
input with EOF; it does not forcibly terminate the child.
The foreground child uses an independent blocking TTY reader. Keep terminal
source/output reads nonblocking for the GUI loop. Keep the i8042 decoding
worker above idle priority so polling applications cannot starve keyboard
delivery.

Configuration records are bounded and versioned. Groups must precede their
items; IDs must be nonzero and unique across both kinds of record:

```text
version=1
group|1|Main
item|10|1|Terminal|/System/term.elf||0
item|11|1|Welcome|/System/demo.elf|"two words" second|1
```

There are at most six groups and 32 programs. Titles occupy at most 31
characters, executable paths 95, and argument strings 127. `|`, CR, and LF are
reserved delimiters. Use absolute executable paths and 8.3-compatible image
filenames. An invalid configuration is reported and opens an explicitly empty
recovery session rather than silently substituting successful-looking data.

Run `/System/init.elf --diagnostic` to select the preserved subsystem probes
instead of normal session supervision.

## User-controlled primary display

Adapter discovery does not select a graphics mode. `init` reads
`/System/DISPLAY.CFG` before starting the shell. The file contains four
whitespace-separated decimal values: `width height bpp refreshHz`, initially
`640 480 8 60`; refresh zero accepts any refresh rate. Missing, unreadable or
malformed configuration is an explicit startup error. If the requested mode
is not supported, init logs the mismatch and selects the first enumerated
mode (including the planar VGA fallback).

`EnumDisplayModesV1` (350) enumerates monitor-approved modes; its zero-based
ordinal is distinct from the returned opaque `modeId`. `available=0` marks
the end. `GetDisplayModeV1` (351) reports active/inactive state, generation and
current mode. `SetDisplayModeV1` (352) activates or switches the primary display.
Any application, including a future Control Panel, may request a switch:
there is no exclusive owner or authorization requirement yet. Mode changes
do not save configuration. EDID/default-monitor limits remain kernel-enforced;
the old `System\Console\Desktop` preference is no longer used.

`CreateDC(Primary)` opens the active root drawing surface, not a mode-control
lease. It returns `UNSUPPORTED` before activation rather than selecting mode
zero implicitly. Firmware output may remain visible until activation. Memory
DCs work independently. Existing screen DC handles survive mode changes, but
pixels are lost: USER refreshes clipping, pointer position, window placement
and desktop painting. Each window receives a coalesced `DisplayChange`
notification even if its message ring is full. Program Manager refits its
frame and workspace. Allocation failures preserve the previous mode; output
enable failure is explicit and queryable as an inactive programmed mode.

After building the image, `python3 Tools/progman-smoke.py --scenario display
--output /tmp/undos-display` exercises a second process switching resolutions
and back while retaining a root DC. Repeat with `--vga std` for the single-mode
VGA path. Both use the bounded QEMU wrapper.

## Split TrueType infrastructure

The [split font implementation](../TTF_SPLIT_USER_KERNEL.md) has an
UnDOS-owned user-mode TrueType decoder and kernel VM, rasterizer, face registry
and glyph cache. The kernel independently validates uploaded UFT1 resources.
Process tables hold handles; the published default retains both faces after
Font Loader exits. No FreeType dependency is used.

Run `/System/fontload.elf` with argument `/System/Fonts/FTEST.TTF` from the
Program Manager Run dialog to exercise decoding, registration and cleanup.
FTEST is original geometric test data, not the startup font. That one-file
command tests registration only. `/System/fontload.elf --preview` republishes
the packaged Source Sans 3 pair at 12 ppem and displays both weights.
The unmodified regular/bold fonts and their SIL Open Font License are packaged
as `SSR.TTF`, `SSB.TTF` and `OFLSS.TXT` under `/System/Fonts`.

Use `python3 Tools/progman-smoke.py --output build/font-default --scenario
font-desktop` to capture the default 11-pt startup with Program Manager and
File Manager. The `font-config`, `font-config-missing`,
`font-config-invalid`, and `font-config-missing-face` scenarios require a
separate test image selected with `UNDOS_IMAGE_DIR`; they verify the effective
configuration and fail-fast startup paths.

Menu bars, popup rows, captions, and minimized-window labels use the published
font's measured ink and metrics. Font publication prepares geometry before
commit. USER rejects a candidate with `OutOfRange` if cumulative descendant
shifts exceed its signed 16-bit window-coordinate range, and retains the old
font and geometry. Oversized menus retain their logical row heights, clip to
the display viewport, and scroll keyboard selection into view. Applications
use the same SDK font-object contract for layout and drawing.
