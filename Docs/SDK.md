# Application SDK and compiler drivers

[Project overview](../README.md)

## Application SDK and Docker image (Phase 2)

See [SDK_ROADMAP.md](../SDK_ROADMAP.md) for Docker release, cross-host validation,
and later runtime work.
Native host compiler archives remain future work in that roadmap.

The `sdk` CMake target builds and stages an application-only SDK. It can be
built without compiling the kernel, HAL, drivers, ELoader, or disk images.
Applications outside this repository use its installed CMake package; they
do not supply startup code, linker scripts, syscall glue, or freestanding
compiler/linker flags.

**This is not a full libc distribution.** The Phase 4 subset supports
`<stdio.h>`, `puts`, integer `printf`, allocation and strings. See the
[supported-function matrix](Libc.md#supported-function-matrix).
C++ constructors and cleanup work,
but a hosted C++ standard library, exceptions, threading/TLS, and FPU execution
are not supplied. Missing stdout is an explicit I/O failure, not debug-console
output. Launch a console application with the terminal's `run` command to
provide its standard streams.

### Build the Docker SDK

From the repository root, build the existing cross-toolchain image if it is
not already installed, then build the SDK image:

```sh
python3 Scripts/build.py docker-toolchain
python3 Scripts/build.py docker-sdk
```

The first command builds GCC/binutils and can take substantially longer.
`Docker/SDK/Dockerfile` is a multi-stage build: its build stage compiles and installs
the SDK and checks relocated external examples; the final image contains the
base toolchain plus `/opt/undos-sdk`, without the UnDOS source tree or SDK
build directories. It currently reuses the full toolchain image, including
that image's OS-image utilities, rather than claiming to be a minimal runtime.
Override the prerequisite with `--build-arg TOOLCHAIN_IMAGE=YOUR_IMAGE`
when using a compatible image. For reproducible release builds, use a pinned
base-image digest instead of a mutable development tag.

Docker uses Linux containers on all supported hosts. Build both prerequisite
and SDK images for the same host/container architecture: Linux amd64 for
Intel/AMD hosts or Linux arm64 for Apple Silicon/ARM hosts. Both cross-compile
to **the same i386 UnDOS target**. Native macOS/Windows compiler executables
and published multi-architecture SDK images are not produced by this target.
No image is automatically pushed to a registry.

### Build an external application

Create a directory containing just these two files:

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.27)
project(HelloUnDOS LANGUAGES C)

find_package(UnDOS CONFIG REQUIRED)
add_executable(hello main.c)
target_link_libraries(hello PRIVATE UnDOS::Application)
```

```c
/* main.c */
#include <stdio.h>

int main(void) {
    return puts("Hello from UnDOS!") == EOF ? 1 : 0;
}
```

From that application directory, the following commands work in POSIX shells
and PowerShell (Windows Docker must be in Linux-container mode):

```sh
docker run --rm --mount "type=bind,source=${PWD},target=/work" undos-sdk:dev cmake -S . -B build-undos
docker run --rm --mount "type=bind,source=${PWD},target=/work" undos-sdk:dev cmake --build build-undos
```

The output is `build-undos/hello.elf`. The image sets `CMAKE_TOOLCHAIN_FILE`
to the installed SDK toolchain, which selects `i686-elf-gcc/g++`, the SDK
sysroot, and the UnDOS CMake platform. Do not execute the resulting ELF on
the host; deploy it to UnDOS. Keep container and native build directories
separate. On Linux, add `--user "$(id -u):$(id -g)"` to Docker invocations to
avoid root-owned files in bind mounts; this option is POSIX-shell-specific.

The installed package provides:

| CMake target | Purpose |
|---|---|
| `UnDOS::Application` | Application CRT, ABI note, entry object, default layout and compile/link defaults |
| `UnDOS::GUI` | GUI library plus application defaults |
| `UnDOS::Shared` | PIC shared-library defaults, ABI note and DSO cleanup object; no application entry point |
| `UnDOS::CRT` | Low-level runtime archive; normally use Application or GUI |
| `UnDOS::FontResource` | Shared UFT1 font-resource validation helpers; link alongside Application/GUI |

For a shared library, use `add_library(message SHARED message.cpp)` and link it
to `UnDOS::Shared`; link the executable to `message` and `UnDOS::Application`.
ELoader's startup-only shared-library rules still apply. CMake gives the
library a basename-only SONAME; use an 8.3-compatible output filename for the
current FAT driver. Do not set Linux runtime paths or install a Linux ELF
interpreter. The package supplies the bare-metal GCC-specific linker options
needed to produce real `ET_DYN` objects.

The image includes C, C++/shared-library, GUI, and runtime-probe examples under
`/opt/undos-sdk/share/undos/examples`. The GUI example uses the C `windows.h`
surface. Copy an example into a mounted application directory or configure it
directly inside the container.

### C GUI API

The installed `UnDOS::GUI` target provides a C11 API in `<windows.h>` and
UnDOS-specific operations in `<undosgui/extensions.h>`. Both headers are
self-contained and do not include generated syscall declarations. C++ callers
can include them before or after the public native headers. The library exports
unsuffixed C symbols. It does not provide A/W variants or a `UNICODE`-selected
UTF-16 interface. Native applications still use the UnDOS i386 cdecl ABI; this
surface does not provide Windows binary compatibility.

The current supported-feature matrix is:

| Area | S1 behavior |
|---|---|
| Classes and windows | Class registration, creation, destruction, metadata, title, child-parent tracking and callback replacement for windows registered through this C API. Class names allow 31 UTF-8 bytes; titles allow 63. |
| Styles and show commands | Explicit translation for `GWLP_WNDPROC`, `GWLP_USERDATA`, `GWL_STYLE`, and `SW_*`. Only documented child/visible/border/caption styles and the `WS_OVERLAPPEDWINDOW` decoration combination are accepted. Native USER does not preserve independent resize/menu policy bits. |
| Messages | `GetMessage`, `PeekMessage`, `PostMessage`, callback dispatch and `WM_QUIT`. Queue filtering is unsupported. `TranslateMessage` returns `FALSE`; native input already supplies `WM_CHAR`. There is no S1 `SendMessage` or modal-loop guarantee. |
| Menus | Existing native menus: menu bars, popup submenus, string commands, separators, disabled and grayed items. Native limits are 16 items and 63 label bytes. Unsupported flag combinations fail. |
| Paint | One matched `BeginPaint`/`EndPaint` pair per native window, with the actual half-open paint rectangle, erase state, and a retained default font. There is no general `GetDC` or memory-DC API. |
| Drawing and fonts | No Windows GDI object model is exposed. `Undos*` extensions provide palette-quantized colors, a solid filled rectangle, one-pixel lines, and the current font/measurement/drawing operations. `Ellipse`, Windows `Rectangle`, and Windows `DrawText` semantics are not implemented. |
| Other Win32 areas | Controls, dialogs, accelerators, timers, clipboard, resources, common dialogs, scrolling and MDI are not implemented by S1. Their later milestones require shared implementations rather than application-local success-shaped substitutes. |

All strings use validated UTF-8 `char` sequences. S1 rejects malformed,
truncated, overlong, surrogate, and out-of-range encodings before a supported
text mutation or font call reaches the native service. Class and window titles,
menu labels, and font text use byte limits; capacities include the NUL
terminator where applicable. Text measurement and rendering accept at most
1024 bytes. The renderer uses Unicode scalar fallback but does not promise
normalization, grapheme-aware editing, bidirectional layout, shaping, kerning,
or an input method. `WM_CHAR.wParam` represents one Unicode scalar. Filesystem
path encoding and file-save behavior are unchanged.

`COLORREF` always uses Windows `0x00bbggrr` encoding. The SDK selects the
nearest color in the canonical 16-color VGA palette using squared RGB distance;
ties select the lowest palette index. High-byte/special palette encodings are
rejected. `UndosPaletteColor(index)` converts a palette index to a real
`COLORREF`, and accepts only indices 0 through 15. For class backgrounds,
`NULL` retains palette index 7 and `UNDOS_PALETTE_BRUSH(index)` is a
class-background token, not a general brush.

Use the `Undos*` API for the native font and drawing extensions. `UNDOS_FONT`
is a 32-bit native token and is distinct from a future `HFONT`. A successful
`UndosAcquireFont` makes every token value valid, including zero. A successful
`UndosSelectFont` reports separately whether a previous selection existed;
when `hadPrevious` is true, the returned previous token is owned and must be
closed with `UndosCloseFont`. `BeginPaint` selects and retains its default font
until paint cleanup. `UndosFillRectangle` is the current half-open,
solid-filled operation. `UndosMoveTo` and `UndosLineTo` are native cursor and
one-pixel line operations. These names do not claim Windows GDI object,
selection, brush, pen, or text-flag semantics.

`GetLastError` reports Windows-facing error values. It maps the native
`SyscallReturnCode` while `UndosGetLastStatus` preserves the raw native status.
Success does not clear either value. Empty `PeekMessage` returns `FALSE`
without changing the message or either error slot. `GetMessage` returns a
positive value for a message, zero for `WM_QUIT`, and -1 for an error.
`ShowWindow` returns the pre-call visibility state, so `FALSE` can indicate a
successful transition from hidden. Use `UndosShowWindow` when operation
success is required. `SetLastError(0)` before a zero-valued metadata call
disambiguates a valid zero result from failure.

### Failed window creation recovery

`CreateWindow` and `CreateWindowEx` return `NULL` when initial showing or menu
attachment fails. They preserve that setup failure in `GetLastError` and
`UndosGetLastStatus`. The SDK makes one native destroy attempt. If that attempt
also fails, the native window remains in a bounded recovery-only record and
creation still returns `NULL`. A visible residual may remain visible until
cleanup succeeds or the process exits.

Use `UndosGetWindowCreationRecovery` to inspect the first retained recovery
record in SDK table order; the order is not chronological. The record contains
the real `HWND`, the original setup error/status, and the latest cleanup
error/status. The query does not consume the record, call native services, or
change either global error value. A null output is an invalid parameter. If no
record remains, the query returns `FALSE` without changing the output or error
values. A null output sets `ERROR_INVALID_PARAMETER` and the matching raw
invalid-argument status.

Recovery-only handles are valid only for this query and `DestroyWindow`.
Ordinary window operations, including `PostMessage`, and callback dispatch
reject them before native calls. `DestroyWindow` makes one cleanup attempt
without delivering `WM_DESTROY`; a failed attempt updates the cleanup
diagnostic pair and global error slots, then retains the record. A successful
attempt removes it. Normal parent destruction also removes retained child
recovery records.

After failed creation, query and explicitly destroy retained windows. Drain at
most 64 records per pass. Stop the pass if cleanup fails; query again only
after a later controlled recovery decision. The GUI example and Task Manager
use this bounded cleanup path and report both setup and cleanup diagnostics
when cleanup remains unsuccessful. A caller may instead take its controlled
process-exit path. Recovery does not transfer menu ownership and does not
guarantee that a failed creation left no visible window.

All GUI windows use the public C callback registry and `DispatchMessage`.
Callbacks use `LRESULT CALLBACK(HWND, UINT, WPARAM, LPARAM)` and return
`DefWindowProc` for messages they do not handle. The SDK keeps bounded
process-local records, rejects unsupported class fields, and retains callback
records during reentrant dispatch and destruction. It does not provide Windows
`WM_NCCREATE`/`WM_NCDESTROY` ordering, synchronous creation data, `WM_CREATE`
veto, filtered queues, thread-local errors, or full nested-loop semantics.
`MSG.pt` is zero because the native message ABI does not provide a point.

`UNDOS_UI_CONTEXT` is zero-initialized caller-owned state. Initialize it with
`UndosInitUi`, attach it to each window that uses shared UI behavior, then
detach or destroy those windows before `UndosCloseUi`. A context owns its font
and optional display-query DC. Its presence flags are authoritative because
font and DC token zero can be valid. Do not copy an initialized context or
close it while a window or helper borrows it. `UndosRefreshUi` publishes a
new font snapshot only after its queries and resource transition succeed.

`UndosRunDialog` supports a message and up to four editable UTF-8 fields. The
title is limited to 63 bytes, the message to 4096 bytes, each label to 1024
bytes, and each value capacity to 1025 bytes including NUL. The total copied
storage is limited to 16384 bytes. It returns 1 for acceptance, 0 for cancel,
and -1 for an error; callers must treat only 1 as acceptance. Field values are
copied back only after acceptance and successful helper cleanup. The helper
uses the shared unfiltered message pump and restores owner focus.

Attached windows use the default system menu and keyboard geometry path from
`DefWindowProc`. The supported menu subset is Restore, Move, Size, Minimize,
Maximize, and Close. Close sends `WM_CLOSE` through the application callback.
Context-less windows retain the no-op default behavior. This support does not
add general controls, clipboard, or Windows GDI object semantics.

`UndosGetUiCleanupRecovery` reports retained UI-helper cleanup records without
consuming them. A retained recovery-only window accepts only the recovery query
and `DestroyWindow`; ordinary operations and dispatch are rejected. After a
failed helper operation, callers must query and explicitly retry cleanup. Drain
at most 64 records per pass and stop when one cleanup attempt fails. The
cleanup record preserves the primary operation error/status separately from
the latest cleanup error/status.

`InvalidateRect` forwards its erase request through USER InvalidateRect V2.
A false request skips background erasure unless an earlier true request remains
pending; V1 callers retain erase=true behavior. The installed `examples/gui`
project is a strict C11 consumer with UTF-8 text, explicit font ownership, and
the public C callback route. Task Manager uses the same unsuffixed API and the
UnDOS drawing extensions. Progman, File Manager, Terminal, Demo, and Fontloader
also use this single C route. The SDK does not install or build a parallel
C++ GUI callback helper.

### In-repository programs as SDK applications

The application projects below are standalone SDK consumers.
Their CMake projects do not supply a linker script,
startup objects, or freestanding flags, and do not link source-tree libraries.
Application sources still use public `undoscrt` and syscall interfaces;
this migration changes their build boundary, not their behavior.

| Application | Standalone source directory | SDK dependencies | Output |
|---|---|---|---|
| `progman` | `Apps/Progman` | `UnDOS::GUI` | `progman.elf` |
| `term` | `Apps/Terminal` | `UnDOS::GUI` | `term.elf` |
| `demo` | `Apps/Demo` | `UnDOS::GUI` | `demo.elf` |
| `winfile` | `Apps/FileManager` | `UnDOS::GUI` | `winfile.elf` |
| `taskmgr` | `Apps/TaskManager` | `UnDOS::GUI`, `UnDOS::Application` | `taskmgr.elf`, `taskprobe.elf` |
| `init` | `Apps/Init` | `UnDOS::Application`, `UnDOS::Shared` | `init.elf`, `initlib.so` |
| `abi_test` | `Tests/ABI` | `UnDOS::Application`, `UnDOS::Shared` | `abitest.elf`, `abitest.so`, `abinext.so`, `abimiss.elf` |
| `fontloader` | `Apps/Fontloader` | `UnDOS::GUI`, `UnDOS::FontResource` | `fontload.elf` |

`python3 Scripts/build.py apps --docker` first stages the SDK, then configures
and builds each application in `<build>/apps/<name>/<configuration>/`.
The `image` command stages their manifest outputs under `/System`.
The OS image build still stages configuration files and font fixtures.
The ABI project retains its deliberately unmarked `abimiss.elf` rejection
fixture and dependent-library lifecycle checks.

`Apps/TaskManager` is a standalone C++ SDK project. It uses the public
`userspace.h` syscall bindings and the C `<windows.h>` GUI surface. It does not
include kernel-private headers. The `apps` command builds it with the other
applications and includes `taskmgr.elf` and its `taskprobe.elf` accounting
probe in the application artifact manifest. The probe checks the public task snapshot
syscall, process execution accounting, blocking waits, and owned-page
allocation/free behavior. It is a diagnostic utility, not a Program Manager
launcher. FAT image paths use 8.3 names, so image assembly installs the probe
as `/System/taskprob.elf`; its build artifact remains `taskprobe.elf`.
Program Manager's Main group provides the app launcher.

Syscall 209 V1 retains its binary layout and `*Cycles` field names, but CPU
counters now count **serviced PIT0 interrupts**, not PIT2 oscillator cycles.
`sampleCycles`, CPU `totalCycles`/`idleCycles`/`systemCycles`, and process
`cpuCycles` are cumulative ticks. `clockHz` is 100 (rounded); the programmed
rate is exactly 1,193,182 / 11,931 Hz and `clockResolutionCycles` is one tick.
Tick elapsed time is `ticks * 11931 / 1193182` seconds. Executive wait-time
increments are unchanged. Masked timer periods can coalesce or be lost;
these counters do not reconstruct wall time.

IRQ0 charges the interrupted process (including its synchronous kernel work),
the exact idle thread, or unowned system work before scheduling can switch.
Task Manager divides process/system/idle tick deltas by the total tick delta;
busy is total minus idle. Approximately one-second samples have roughly
one-percentage-point quantization, regardless of displayed decimal formatting.
Process baselines use PID/incarnation and CPU baselines use CPU ID. A new
baseline, zero denominator, decreased counter or changed reset epoch shows
collecting rather than retaining stale percentages. Delayed refresh alone
does not invalidate the clock. V1 `CpuCounterDiscontinuity` is retained for
invalid/reset counters, not ordinary sampling. Current CPU accounting supports
the single-CPU HAL; it does not establish SMP safety.

The opt-in `UNDOS_TASK_MEMORY_DIAGNOSTICS` build emits `TMMEM R4` and
`TMMEMPROC` serial records at most once per five seconds after successful
snapshots, outside process/PMM locks and IRQ0. Records contain sample/tick,
PMM total/used/free/System bytes and PID-owned frames/bytes, never names or
paths. Heap live-byte statistics are unavailable. In this build, pressing
`o` in Task Manager requests at most six `TMR4` numeric CPU observations;
ordinary builds do not emit them. Serial output can perturb performance.

Run the bounded QEMU scenario with
`python3 Tools/progman-smoke.py --scenario task-manager --output build/taskmgr/runtime`.
It launches the probe, checks its serial markers, then launches Task Manager
and captures its Processes, Performance, and keyboard-navigation views. It
uses the image selected by `run-qemu.sh`; set `UNDOS_IMAGE_DIR` to an isolated
prepared image when the default image is not the intended test artifact.

The font decoder remains app-local. It uses the SDK's opt-in `kstd` headers,
installed under `sysroot/usr/include/kstd`; these headers are not added to
ordinary application include paths. They do not provide hosted C++ support,
exceptions, or an allocation runtime. Fontloader retains its allocator adapters.
ELoader remains an in-tree bootstrap image because it starts applications
before the application CRT runs. Kernel, HAL, drivers and loadable subsystems
also retain their separate kernel contracts.

To build Program Manager without the OS project, copy its directory outside the
repository and run these commands from the copied directory:

```sh
docker run --rm --mount "type=bind,source=${PWD},target=/work" undos-sdk:dev cmake -S . -B build-undos -DCMAKE_BUILD_TYPE=Debug
docker run --rm --mount "type=bind,source=${PWD},target=/work" undos-sdk:dev cmake --build build-undos
```

The result is `build-undos/progman.elf`. Deploy it with `PROGMAN.INI` to
`/System`; building it alone does not create an OS image.
The installed SDK checker accepts repeated
`--application-source /path/to/application` arguments to copy and build these
projects outside the source tree with the SDK relocated to a path containing
spaces. `--progman-source` remains a compatible shorthand. With `--output`,
the checker exports the application binaries and runtime probes.
After staging the binaries and rebuilding the image, use the bounded
`launch`, `abi`, `font`, `terminal-stdio`, and `terminal-close` scenarios in
`Tools/progman-smoke.py`. Run image consumers sequentially.

### Stage or install the SDK without building an image

With a host-compatible `i686-elf` GCC/binutils toolchain on PATH, CMake 3.27+,
Python 3, and a build tool:

```sh
cmake -S SDK -B build-sdk --toolchain "$PWD/SDK/UnDOS-toolchain.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build-sdk --target sdk --parallel
```

The result is `build-sdk/sdk/`. Set `-DUNDOS_SDK_PREFIX=/destination` when
configuring to change the staging destination. After building, a separate
installation is also supported:

```sh
cmake --install build-sdk --component SDK --prefix /destination
```

The independent SDK project replaces the old root `UNDOS_SDK_ONLY` option.
It does not download hosted-test dependencies or require GRUB/QEMU.
IDE configurations that select `i686-elf-gcc/g++` directly are also accepted:
SDK validation checks the compiler's target, not just CMake's cross-compiling
flag. Hosted tests must instead use native compilers in a separate build
directory configured from `Tests/Hosted/`, not with the old `TESTING_ONLY` switch.

The relocatable directory contains:

```text
sysroot/usr/include/       Public ABI, generated syscalls, CRT and GUI headers
sysroot/usr/lib/           Runtime/GUI archives, crt0.o, ABI/DSO objects, application.ld
bin/                      i686-undos-gcc and i686-undos-g++ SDK drivers
share/cmake/UnDOS/         Toolchain, platform module and find_package configuration
share/undos/              Examples and installed-SDK checks
```

Public headers come from the shared ABI, CRT, and GUI include trees. Do not
export kernel-private headers or source-tree include paths. Kernel-private
headers and the compiler binaries are not part of this staged directory.
Moving it is supported, provided a compatible cross-compiler stays on PATH.
For non-container applications, explicitly configure with
`--toolchain /destination/share/cmake/UnDOS/UnDOS-toolchain.cmake`.
The Docker image is the ready-to-use compiler-plus-SDK distribution.

### SDK verification

The Docker build compiles all installed examples after relocating the SDK to
a path containing spaces and checks ELF type, i386 architecture, ABI notes,
absence of Linux interpreters/TLS, shared dependency names, and exported
lifecycle symbols. Repeat from the final source-free image with:

```sh
docker run --rm undos-sdk:dev python3 /opt/undos-sdk/share/undos/check-sdk.py
```

Build external consumers outside the source tree. The installed check must also
cover the relocated SDK path containing spaces; run it in the final Docker image
after building `Docker/SDK/Dockerfile`.

For the runtime probe, create a host `build-sdk` directory and export the
probe binaries through a bind mount:

```sh
docker run --rm --mount "type=bind,source=${PWD}/build-sdk,target=/output" undos-sdk:dev python3 /opt/undos-sdk/share/undos/check-sdk.py --output /output/probe
```

Stage the exported probe directory explicitly when assembling the image, then run:

```sh
python3 Scripts/build.py image --docker --system-files build-sdk/probe
python3 Tools/progman-smoke.py --output build/sdk-smoke --scenario sdk
```

This launches an externally built SDK application and verifies heap/string
runtime functions, standard C file input and unavailable-output failures,
unoptimized C syscall bindings, shared-library initialization,
C++ destruction, atexit and
finalization using explicit debug diagnostics. Terminal stdio has a separate
`terminal-stdio` scenario. Existing image-page RWX permissions and
bare-metal toolchain executable-stack warnings are not fixed by SDK packaging.

## Compiler-driver defaults (Phase 3)

The installed SDK now includes `i686-undos-gcc` and `i686-undos-g++`. These
are relocatable Python 3 drivers over `i686-elf-gcc/g++`, not a new patched
GCC target. Put the SDK's `bin` directory and a compatible cross-toolchain on
PATH; the Docker SDK already sets both. The driver locates its sysroot relative
to its own installed location, not the current directory or source checkout.

For a source file using `<stdio.h>` and `puts`:

```sh
docker run --rm --mount "type=bind,source=${PWD},target=/work" undos-sdk:dev i686-undos-gcc hello.c -o hello.elf
```

No CMake project, linker script, CRT entry point, or freestanding flags are
required. `i686-undos-g++` likewise supports ordinary `main` and C++
construction/destruction without enabling hosted libstdc++, RTTI, or exceptions.
The [initial standard C subset](Libc.md) is documented separately; C++ standard-library headers
and floating-point execution are not provided. Standard output must actually be available;
missing streams continue to fail explicitly.

The drivers select the i386 baseline, target-only headers plus GCC's
freestanding builtin headers, the ABI note, startup/runtime objects, compiler
support library, and default application layout. They never search hosted
libc/libstdc++ headers implicitly. The CMake package and drivers implement the
same application ABI; CMake consumers may continue to use the Phase 2 toolchain
and imported targets.

Compile-only, preprocessing, assembly output, dependency generation, and
syntax checks do not add startup/link inputs. Partial links omit startup and
default libraries. Build archives with the cross-toolchain's `i686-elf-ar`:

```sh
i686-undos-gcc -c helper.c -o helper.o
i686-elf-ar rcs libhelper.a helper.o
i686-undos-gcc main.c libhelper.a -o application.elf
i686-undos-g++ -shared message.cpp -o message.so
i686-undos-g++ main.cpp ./message.so -o application.elf
```

Shared builds select PIC and DSO cleanup, omit application CRT startup, and
receive a basename-only SONAME derived from `-o` unless explicitly supplied.
Shared libraries resolve public runtime functions from the main executable;
they do not get independent copies of the process CRT. Both driver names
accept GCC-style response files, including quoted paths containing spaces.
For separately compiled shared-library objects, request `-fPIC` during their
compile step.

Low-level escape hatches deliberately allow creating nonconforming binaries:

| Option | Behavior |
|---|---|
| `-nostartfiles` | Omit ABI note and entry/DSO objects; retain default runtime libraries |
| `-nodefaultlibs` | Omit CRT/compiler support libraries; retain entry/ABI objects |
| `-nostdlib` | Omit both startup objects and default libraries |
| `-T` / `--script` through linker arguments | Replace the default application layout |
| `--undos-raw` | Pass arguments to the underlying compiler without SDK defaults |

These switches are for SDK/runtime work, not normal applications; callers
must supply a valid entry/lifecycle/note contract if they expect ELoader to
accept the resulting image. Compiler information queries such as `-dumpmachine`
describe the underlying `i686-elf` compiler rather than claiming a patched
UnDOS target.

The installed `check-sdk.py` also covers driver C/C++ compilation, compile-only
and partial-link modes, archives, response-file paths, shared SONAMEs, ABI
notes, failure propagation, and explicit startup suppression. Its exported
runtime probe is built with the drivers, so the `--scenario sdk` emulator check
exercises the command-line path as well as installed runtime objects.

### C syscall wrapper linkage

Header-defined C syscall wrappers must retain `static inline` linkage through
`undos/__shared_core.h`. Plain C `inline` can leave unresolved calls at `-O0`.
Keep both C and C++ callers in the installed-SDK probe so the wrapper is
checked through both language interfaces.
