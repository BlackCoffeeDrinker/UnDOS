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

Init establishes the active display path before starting the shell. Program
Manager starts maximized; restoring it uses its 600×400 default frame at
(30, 30). Both Cirrus and the planar VGA fallback use Source Sans 3 and the
classic palette.
The VGA target advertises one 640x480 nominal-60-Hz mode with four one-bit
planes. It does not advertise packed-pixel depths or other VGA timings.
Program Manager icon labels, File Manager panes and toolbars, dialogs, and
Terminal use the published font through the SDK font-object API. Text-dependent
row, control, and cell geometry uses font metrics and measured runs. Editing
fields show a measured suffix so newly typed text remains visible. Font-change
notifications reflow application-owned layouts without blocking publication.
Shared window decorations follow the NT 3.51 style: centered captions,
dark-blue active title bars, beveled gray frames, a system-menu box, and
triangle minimize/maximize controls. Maximized windows show paired restore
arrows; closing remains available through the system menu or Alt+F4.

File Manager uses its default 680-pixel width at (30, 30), with a font-derived
height for twelve initial list rows. Neither application queries display
topology to choose startup geometry. Resizing File Manager to a narrow client
area wraps its toolbar into two rows. The compact toolbar shifts the path field
and list down by one button row. Painting, list hit testing, and scrolling use
the same client-area layout.
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

## User-controlled display mode

Adapter discovery does not select a graphics mode. `init` reads
`/System/DISPLAY.CFG` before starting the shell. The file contains four
whitespace-separated decimal values: `width height bpp refreshHz`, initially
`640 480 8 60`; refresh zero prefers 60 Hz. Missing, unreadable or malformed
configuration is an explicit startup error. Init matches the complete
resolution, depth, and fixed-refresh preference against monitor-approved
target modes. An unsupported preference is an explicit startup error; init
does not silently select a different refresh or mode.

The GFX API models one logical desktop source, physical targets, and paths.
Calls 350–356 query the generation-tagged topology, enumerate source modes,
targets, target modes, and active paths, validate a complete one-path
proposal, and commit its process-local validation token. The manager publishes
immutable heap-backed records for one generation. Each query retains that
snapshot while writing output; replacing it cannot invalidate an in-flight
reader. Snapshot allocation, reclamation and driver mode enumeration occur
outside spinlocks and interrupt-disabled regions. Driver callback passes use
an auto-reset dispatcher event as a blocking serialization gate, not a yielding
spinlock. Refresh stages mode observations before taking USER scene/drawing
locks, then rechecks generation before applying the lifecycle update.

Enumeration calls 351–354 take `queryOnly`, `firstIndex`, and output capacity,
and return separate `required` and `written` counts. First call with
`queryOnly=1` and zero capacity (including when the pointer is null), allocate the required record storage, then fill
with `queryOnly=0` against the same generation. Undersized fills return
`INVALID_SIZE`, report `required`, leave `written=0`, and write no records.
Empty ranges succeed with both counts zero; an index beyond the end is
invalid. A generation already stale when the call acquires its snapshot
returns `RETRY` with no records. A mapped fill only pins an already published
snapshot; it performs no driver polling or snapshot construction. If that
generation is dirty and has no current published snapshot, it returns `RETRY`
without writing. An unmapped topology or size query can build the replacement.
Init uses this two-pass contract and checked
dynamic caller storage rather than fixed mode arrays.

Every topology
record class is limited to 64 records: sources, source modes, targets,
aggregate target modes, paths, and results each have a separate limit.
These are admission policies, not collection-storage capacities. Snapshot
record allocations use checked counts and a derived maximum byte budget.
Commit verifies result capacity before
programming and returns actual target state and an outcome record after a
hardware attempt. Validation reserves target resources before it returns a
token. Closing or terminating with an unused token releases those
reservations. A stale-generation commit consumes the token and releases its
reservation without programming hardware, even when result capacity is also
insufficient. Freshness and capacity precedence are decided together under
callback/topology serialization, including invalidation after initial snapshot
acquisition. Prior activation is observed inside the scene/drawing/callback
transaction. Active-target state queries also use the callback gate; internal
transaction helpers do not recursively acquire it.
Commit always reports `required=1`; current-token insufficient
capacity returns `INVALID_SIZE`, preserves the token, and does not change
hardware. It preallocates an unpublished replacement snapshot before any
hardware attempt. Allocation failure returns `OUT_OF_MEMORY` and preserves
the token and reservations. After an attempt, the preallocated snapshot
reports actual state, including degraded or unknown outcomes, without another
snapshot allocation.
Known disconnected targets remain visible but cannot be selected. Connector,
monitor, mode-list, and resource notifications invalidate old snapshots and
tokens. Reconnecting a known connector preserves its target identity.
PnP removal retires its targets and active path without accessing removed
hardware; outstanding tokens retain objects until cleanup. Tokens are
process-local and cannot be used by another process.
Applications may query and request mode changes; there is no exclusive owner
or authorization requirement. Mode changes do not save configuration.
EDID/default-monitor limits remain kernel-enforced; the old
`System\Console\Desktop` preference is no longer used.

Adaptive-sync fields are descriptive only and currently report unsupported.
Variable-refresh programming, cloning, and extended-desktop presentation are
not implemented; USER continues to present through a single active path.

The Cirrus backend computes fixed-refresh timings using integer GTF and PLL
selection for two identified chips:

| Chip identity (CR27 shifted right by two) | VRAM from SR0F bits 4:3 | Supported depths | Software pixel-clock ceilings |
|---|---|---|---|
| GD5430 (`0x28`, including QEMU ISA Cirrus) | `08`: 512 KiB; `10`: 1 MiB; `18`: 2 MiB. Bit 7 does not double capacity. | 8, 15, 16, 24 bpp | 80, 50, 50, 28.5 MHz respectively |
| GD5434 (`0x2a`) | Same base encodings; bit 7 doubles capacity. | 8, 15, 16, 24, 32 bpp | 80 MHz |

GD5430 uses the conservative Xorg five-bit PLL divider profile (SR1E bit 7
clear), with a 28.636–111 MHz internal oscillator search range and an 85.5 MHz
hardware-output ceiling. Its 15/16-bpp hardware clock equals the pixel clock,
including on 1-MiB boards; 24 bpp requires three times the pixel clock.
Banked depth programming uses SR07 `01`/`07`/`07`/`05` and hidden-DAC
`00`/`c0`/`c1`/`c5` for 8/15/16/24 bpp respectively.
GD5434 retains its six-bit divider profile and 135 MHz depth-adjusted
hardware-clock ceiling. Both profiles require at most 5,000 ppm PLL error and
a complete aligned surface within decoded VRAM. Unknown chip identities or
memory encodings do not produce Cirrus modes.

The GD5430 profile follows upstream
[Xorg Alpine memory/depth limits and programming](https://github.com/NetBSD/xsrc/blob/d2b10af68fa17c493922c0288503c4e10054925b/external/mit/xf86-video-cirrus/dist/src/alp_driver.c)
and its [conservative PLL search](https://github.com/NetBSD/xsrc/blob/d2b10af68fa17c493922c0288503c4e10054925b/external/mit/xf86-video-cirrus/dist/src/CirrusClk.c).
The hardware-clock depth factors also follow the
[Linux Alpine implementation](https://github.com/torvalds/linux/blob/d89e1d186ec9271bf78e62771779e9fc03bf0fc3/drivers/video/fbdev/cirrusfb.c).
[QEMU v9.2.0 ISA Cirrus](https://github.com/qemu/qemu/blob/v9.2.0/hw/display/cirrus_vga_isa.c)
selects GD5430 with 2 MiB; the PCI GD5446 model is not supported by this profile.
These are conservative upstream-derived software constraints, not vendor
electrical certification. Emulator register behavior does not establish
physical-board timing, PLL stability, or VRAM wiring; those remain
NOT_YET_VERIFIED.

Drivers report connector or resource changes through `KE_GFX_RefreshAdapter`
or the PnP `Changed` notification. Call refresh in preemptible context,
outside programming and USER/topology locks. Refresh discovers new heads,
retains known connectors, and disables an active mode that is no longer
supported. Commit and refresh use USER scene → drawing → topology lock
order. Validation, reservation cleanup, and generation observation use
topology serialization. `KE_PNP_ReportRemovedDevice` marks the retained
device stack removed and queues stop notifications. It first drains USER
drawing and retires matching targets under the same lock order, before
removed-device I/O becomes invalid. Call it before hardware/resource
teardown and outside USER/topology locks. Queries observe generations but
do not retire the desktop. The removal report does not free device objects
or reclaim driver resources.

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
UnDOS-owned user-mode TrueType decoder and kernel VM, rasterizer, and face
registry. On `main`, each `Realization::glyph()` call serializes loading and
rasterization under its realization mutex; the shared glyph cache remains on
the `glyph-cache` branch. Default-font publication pins printable ASCII and
replacement masks in `UiGlyphs`, so USER chrome can draw those masks without
VM work under the scene lock. The kernel independently validates uploaded UFT1
resources.
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
