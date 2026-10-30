# Building and developing UnDOS

[Project overview](../README.md)

Repository commands in this guide run from the repository root.
For external applications, use the [SDK guide](SDK.md).

## Repository layout and build entry points

The repository has independent CMake projects and a thin root IDE launcher.
Python 3 commands orchestrate builds on macOS, Windows, and Linux. Docker
supplies the cross-toolchain and image utilities; native cross-builds require
equivalent tools on PATH. Native hosted tests use the host compiler.

| Directory | Ownership |
|---|---|
| `Docker/Toolchain`, `Docker/SDK` | Cross-toolchain and application SDK images |
| `Kernel/` | Kernel source, private API, HAL, drivers, subsystems and bootstrap loader |
| `CRT/` | Application startup, lifecycle and C library |
| `Apps/` | Independent SDK-consuming applications |
| `SDK/` | SDK producer, CMake package, compiler drivers, GUI and opt-in KStd headers |
| `ABI/` | Public shared headers, ABI note and syscall IDL inputs |
| `Tests/Hosted`, `Tests/ABI` | Native tests and SDK-built ABI fixtures |
| `Image/` | Manifest-based image assembly, GRUB template and boot tools |
| `BuildSupport/`, `Tools/`, `Scripts/` | Shared CMake helpers, development tools and orchestration |

From the repository root:

```sh
python3 Scripts/build.py docker-toolchain
python3 Scripts/build.py docker-sdk
python3 Scripts/build.py sdk --docker
python3 Scripts/build.py kernel --docker
python3 Scripts/build.py apps --docker
python3 Scripts/build.py image --docker
python3 Scripts/build.py hosted
```

Use `Scripts/build.py` for orchestration, not compiler dependency management.

`apps` stages the SDK before building applications. `image` builds the kernel,
SDK and applications, then assembles `build/image/os.iso` and
`build/image/fat32.img`. Kernel and SDK builds remain independent.
Keep component dependencies within their ownership boundaries. Do not add
application dependencies to the kernel or kernel dependencies to the SDK
producer. `BuildSupport/` owns shared CMake helpers, and `ABI/IDL/` owns syscall
inputs.
Use `--config Release`, `--jobs N`, or `--build-root PATH` as needed.
Repeated `--cmake-arg=-DNAME=VALUE` arguments reach CMake configuration.
With `--docker`, build roots and extra input directories must be inside the
repository bind mount. Omit `--docker` to use tools installed on the host.

Build trees are separated by component and configuration:
`build/kernel/Debug`, `build/sdk-build/Debug`, `build/sdk/Debug`,
`build/apps/<name>/Debug`, and `build/hosted/Debug`. Image assembly consumes
generated artifact manifests and starts with fresh staging trees. It never
reuses files left in the old `bin/filesystem` tree. Supply additional runtime
probes explicitly with `--system-files build/probe`; each file in that directory
is staged under `/System`.

Syscall bindings and dispatch code are generated under
`<component-build-dir>/generated/syscalls/`, not in the source tree. Each
component builds its own copy and the SDK installs its generated public headers.
See [Syscall IDL](../SYSCALLS_IDL.md) for regeneration commands.

For CLion with a Docker toolchain, open the **repository root**, not `Kernel/`
or `SDK/`. CLion mounts the opened project directory; opening a component
alone omits required sibling directories. The root CMake launcher adds only
one component, selected with `-DUNDOS_COMPONENT=Kernel` (the default), `SDK`,
`CRT`, or `Hosted`. Use a separate build directory and profile for each.
The Python commands and standalone component entry points remain independent.

For Kernel, SDK, and CRT profiles, select the Docker cross-toolchain with
`i686-elf-gcc/g++`. The launcher selects the matching toolchain file before
compiler detection. Kernel/HAL images use `BuildSupport/i686-toolchain.cmake`;
SDK/CRT use `SDK/UnDOS-toolchain.cmake`. Do not exchange their shared-library
link modes. Do not reuse a macOS/native cache or pass
`CMAKE_OSX_ARCHITECTURES`/`-arch` flags to the cross-compiler.
For `Hosted`, select native host compilers, not the i386 cross-compiler.
If CLion reports compiler-information errors from `cc`/`c++` rather than
`i686-elf-gcc/g++`, check the profile's selected toolchain and reset its CMake
cache. Old component caches cannot be reused as root workspace caches.

Standalone component configuration still works when the entire repository
is available to CMake, for example in the `Scripts/build.py` Docker bind mount.
Applications such as `Apps/Progman/` can also be opened independently against
an installed SDK because they do not depend on sibling source directories.

## Kernel error conventions

Stage 1.5 exports only default/protected global or weak ELF symbols, excluding
image-private hidden/internal definitions. Duplicate exports retain the first
definition, matching boot relocation lookup. Boot handoff and driver lookup
preserve symbol names up to 255 bytes; longer names fail explicitly rather than
being truncated into false duplicates.

Kernel operations report status with `kstd::error_code`; operations producing a
value use `kstd::expected<T, kstd::error_code>`. `kernel::KernelError` names the
kernel category's values and converts implicitly to an error code; other
subsystems can use their own categories. Check status codes with their boolean
conversion (zero means success), and compare specific failures with their
category-aware error codes. Propagate the original error
instead of replacing it with `false`, `nullptr`, a negative integer, or a
successful zero-byte transfer. Predicates and optional lookup results still
represent ordinary false/absent outcomes, and EOF remains a successful empty read.
Subsystems own their domain validation, including size and format limits; syscall
handlers adapt ABI arguments and resolve the caller's handles.

The kernel error category has permanent, allocation-free storage: error codes
may retain its reference for the kernel's lifetime. It does not register a
static destructor or depend on the application's `atexit` runtime.
The `kstd` generic and system categories also use permanent, allocation-free
storage without destructor registration, including when default-constructing
or clearing an error code.
Cross-image callers use the exported C-linkage `KE_ERROR_GetCategory` entry
point through the existing inline C++ helpers, so LTO cannot discard the category
accessor required by the HAL or drivers. It returns a non-null pointer at the
linkage boundary; C++ callers retain the reference-based category interface.
Freestanding library contract violations use the C-linkage `KE_RUNTIME_Panic`
hook, including pure-virtual calls; HAL and driver images do not depend on a
mangled C++ panic export.

Handwritten syscall handlers and generated internal dispatch functions return
`kstd::error_code`. Only the public dispatch boundary converts errors to the unchanged
`SyscallReturnCode` ABI through `kernel::KErrorToSysReturnCode`. The
mapper checks the category before interpreting its value;
unrecognized categories or errors without a truthful ABI equivalent return
`UNKNOWN`.

## Kernel log formatting

`KE_Print(message)` sends plain text directly to `KE_PrintBuffer`, without parsing
braces or using a formatting buffer. String literals and fixed arrays use a
bounded string view; dynamic strings and explicit views are also supported.

`KE_Print("Device {} at {x}", name, address)` packages arguments into a stack
array for a shared parser and integer conversion routine. Named output sinks
keep the formatter independent of the variadic argument pack. Formatting remains
allocation-free for early boot. The default 256-byte output buffer can be changed
with `KE_Print<Size>`; overflowing output ends in `...`, while an exact fit is
not truncated. Each call submits its message to `KE_PrintBuffer` once.

## How to build the OS

Build the toolchain once with `python3 Scripts/build.py docker-toolchain`.
Build the OS with `python3 Scripts/build.py image --docker`.
Use `run-qemu.sh` to run it, or `debug-and-run-for-15-seconds.sh` to build a
debug image and run it. Both wrappers bound QEMU execution to 15 seconds.

## Selecting the root volume

The generated GRUB configuration selects the root volume with
`root=UUID=1234-5678`. The UUID is the FAT volume serial number written as
eight hexadecimal digits, with a hyphen after the fourth digit. Hexadecimal
letters can use either case. For example, serial `0x12345678` is
`1234-5678`.

UUID selection probes native Volume devices after Plug and Play discovery has
settled. It supports FAT12, FAT16, and FAT32 volumes with a valid serial field.
The kernel requires exactly one matching volume. A malformed UUID, no match,
multiple matches, or a volume read failure stops boot before the early boot
volume is unmounted. Cloned disks can have the same serial, so booting with
more than one clone connected is ambiguous.

To select a volume by its device name instead, set `root=<device-name>`. This
exact-name path remains available. A UUID selector requires `rootfs=FAT`;
`rootfs` and `rootops` continue to apply to a named root.

## Direct GRUB loader

Fresh images load the kernel and HAL with the `undos_direct` GRUB module.
The archived Stage 1.5 sources remain in the tree, but normal kernel builds
and fresh images do not require or stage `undos-boot`.

The module build requires the prepared GRUB 2.14 source and i386-pc target
support under `build/grub-direct-loader/`. It checks the pinned Alpine package
and source hashes, the recovered target configuration, the shipped GRUB file
set, and the matching module options. It builds only `undos_direct.mod` with
the pinned Alpine x86 compiler environment. It does not rebuild or replace
the GRUB runtime. Every `grub-direct` route rebuilds the kernel target's
BootInfo layout probe before it builds the module. The provenance manifest
binds the probe source, included layout headers, target flags, object, fixture,
kernel manifest, and kernel/HAL artifact hashes. The module build and image
assembly reject missing or stale probe evidence.

Run the commands from the repository root:

```sh
python3 Scripts/build.py grub-direct --docker --build-root build/grub-direct-loader
python3 Scripts/build.py image --docker --build-root build/grub-direct-loader \
  --direct-loader-manifest build/grub-direct-loader/manifest-Debug.json

python3 Tools/test-grub-direct-probe-gate.py \
  --probe-manifest build/grub-direct-loader/grub-direct-loader/bootinfo-layout-Debug.json \
  --kernel-manifest build/grub-direct-loader/kernel/Debug/artifacts-Debug.json \
  --direct-manifest build/grub-direct-loader/manifest-Debug.json \
  --apps-manifest build/grub-direct-loader/apps/artifacts-Debug.json \
  --output build/grub-direct-loader/probe-gate-tests
```

The probe-gate test accepts current evidence and rejects missing evidence or a
changed layout input before it publishes a module manifest. It also checks
that image assembly preserves existing staging when probe evidence is stale.
Use a new output directory for each run.

To rebuild the kernel, HAL, and direct loader, then publish a validated refresh
of the existing private HDD image, run this command from any directory:

```sh
/path/to/UnDOS/build-hdd.sh
```

The script requires Python 3, Docker with an available daemon, mtools, and
`lsof`. It preserves the current GRUB menu and existing backups. It creates no
backup, does not build applications, and does not boot-test the published
image. If durability or hash verification fails after replacement, the script
reports that the image was already replaced; it does not roll back.

The GRUB source/configuration preparation is an explicit prerequisite. The
module command fails if the matching prepared files are missing. It does not
substitute another GRUB release or target compiler. The generated module is
marked `GPLv3+`. Current task authorization permits private local testing and
the checked local HDD update only. Do not distribute the module or image.

The direct handoff uses GRUB's generic relocation preparation API and a
module-owned transition. It does not call GRUB's CPU-specific relocator boot
tail. The preflight executes CPUID only when the EFLAGS.ID test proves it is
available. The baseline path uses no CR4 instruction. A newer-CPU cleanup path
reads CR4 only when CPUID reports PSE or PAE support, then clears only those
supported bits. The QEMU 486 and Pentium runs exercise emulator profiles; they
do not establish physical 386/486 compatibility. Physical CPU results remain
`NOT_YET_VERIFIED`.

For a new HDD image, use the task-local updater after building the kernel, HAL
and module manifests:

```sh
python3 Tools/update-grub-direct-hdd.py prepare \
  --image "$PWD/HDD_IMG.IMG" \
  --candidate "$PWD/build/grub-direct-loader/hdd-update/candidate.img" \
  --kernel-manifest build/grub-direct-loader/kernel/Debug/artifacts-Debug.json \
  --direct-manifest build/grub-direct-loader/manifest-Debug.json
```

The updater checks the image geometry and volume ID, compares installed GRUB
files with the recovered set, and preserves the original files and boot area.
It adds only the direct module, kernel, HAL and a direct menu entry. Boot a
disposable copy of the candidate through `run-qemu.sh` before publication.
The publish command requires the exact boot-tested copy, its successful serial
log and a task-local backup path. It retains the original image backup.

```sh
python3 Tools/update-grub-direct-hdd.py publish \
  --image "$PWD/HDD_IMG.IMG" \
  --candidate "$PWD/build/grub-direct-loader/hdd-update/candidate.img" \
  --boot-image "$PWD/build/grub-direct-loader/hdd-update/qemu-direct/fat32.img" \
  --report "$PWD/build/grub-direct-loader/hdd-update/candidate-report.json" \
  --boot-log "$PWD/build/grub-direct-loader/hdd-update/qemu-direct/serial.log" \
  --backup "$PWD/build/grub-direct-loader/hdd-update/HDD_IMG.original.backup"
```

To update the previously published task-owned entry, use `prepare-update`.
This mode checks the recorded prior candidate, its exact menu block and
payload hashes before replacing only those task-owned bytes. It preserves the
historical entries, current default and timeout. It does not overwrite the
original image backup.

```sh
python3 Tools/update-grub-direct-hdd.py prepare-update \
  --image "$PWD/HDD_IMG.IMG" \
  --candidate "$PWD/build/grub-direct-loader/hdd-update/candidate-cpu-safe.img" \
  --report "$PWD/build/grub-direct-loader/hdd-update/candidate-cpu-safe-report.json" \
  --baseline-report "$PWD/build/grub-direct-loader/hdd-update/candidate-report.json" \
  --kernel-manifest build/grub-direct-loader/cpu-safe-build/kernel/Debug/artifacts-Debug.json \
  --direct-manifest build/grub-direct-loader/manifest-Debug.json

python3 Tools/test-grub-direct-hdd.py \
  --image build/grub-direct-loader/hdd-update/candidate-cpu-safe.img \
  --output build/grub-direct-loader/hdd-update/qemu-cpu-safe \
  --cpu 486 --memory 32

python3 Tools/update-grub-direct-hdd.py publish-update \
  --image "$PWD/HDD_IMG.IMG" \
  --candidate "$PWD/build/grub-direct-loader/hdd-update/candidate-cpu-safe.img" \
  --boot-image "$PWD/build/grub-direct-loader/hdd-update/qemu-cpu-safe/fat32.img" \
  --report "$PWD/build/grub-direct-loader/hdd-update/candidate-cpu-safe-report.json" \
  --boot-log "$PWD/build/grub-direct-loader/hdd-update/qemu-cpu-safe/serial.log" \
  --baseline-report "$PWD/build/grub-direct-loader/hdd-update/candidate-report.json" \
  --backup "$PWD/build/grub-direct-loader/hdd-update/HDD_IMG.IMG.original.bak" \
  --snapshot "$PWD/build/grub-direct-loader/hdd-update/HDD_IMG.IMG.pre-cpu-safe.bak"
```

The update entry is labelled **UnDOS - direct loader (CPU-safe handoff
diagnostic)** and has menu ID `undos-direct-cpu-safe`. Select it from the GRUB
menu for the private HDD test. The HDD default remains unchanged. The update
retains the original image backup and creates a separate snapshot of the
currently published image before replacement.

The approved loader-only diagnostic build also adds a manual
**UnDOS - handoff STOP diagnostics** submenu. Its label includes the first
eight hexadecimal digits of the module hash. It leaves the existing default,
timeout, CPU-safe entry and historical entries unchanged. Each named STOP is
an indefinite hold. Photograph the visible stage, then reset or power-cycle.
The loader checks for BIOS mode 3, 80 columns and active page 0, writes a
direct-VGA banner, and waits for Enter before it starts a diagnostic run.
Every entry is manual. A transient preparation marker permits `boot` only
after the diagnostic command has successfully prepared its loader. Module
load and preparation failures stay at a GRUB prompt and do not call `boot`.
This path does not include a kernel-entry hook and does not redesign serial
output.

To prepare a diagnostic-only candidate from the already published CPU-safe
HDD, use a new candidate and report path. The original task backup must exist;
publication creates a separate immediate snapshot and does not overwrite
either retained backup:

```sh
python3 Tools/update-grub-direct-hdd.py prepare-diagnostics \
  --image "$PWD/HDD_IMG.IMG" \
  --candidate "$PWD/build/grub-direct-loader/hdd-update/candidate-diagnostics.img" \
  --report "$PWD/build/grub-direct-loader/hdd-update/candidate-diagnostics-report.json" \
  --baseline-report "$PWD/build/grub-direct-loader/hdd-update/candidate-report.json" \
  --current-report "$PWD/build/grub-direct-loader/hdd-update/candidate-cpu-safe-report.json" \
  --kernel-manifest build/grub-direct-loader/cpu-safe-build/kernel/Debug/artifacts-Debug.json \
  --direct-manifest build/grub-direct-loader/manifest-Debug.json

python3 Tools/test-grub-direct-hdd.py \
  --image build/grub-direct-loader/hdd-update/candidate-diagnostics.img \
  --output build/grub-direct-loader/hdd-update/qemu-diagnostics-normal \
  --cpu 486 --memory 32

python3 Tools/test-grub-direct-diagnostics.py \
  --image build/grub-direct-loader/hdd-update/candidate-diagnostics.img \
  --output build/grub-direct-loader/hdd-update/qemu-diagnostics-command \
  --stop command --cpu 486 --memory 32

python3 Tools/test-grub-direct-diagnostics.py \
  --image build/grub-direct-loader/hdd-update/candidate-diagnostics.img \
  --output build/grub-direct-loader/hdd-update/qemu-diagnostics-load-error \
  --stop command --cpu 486 --memory 32 --expect-module-failure

python3 Tools/update-grub-direct-hdd.py publish-diagnostics \
  --image "$PWD/HDD_IMG.IMG" \
  --candidate "$PWD/build/grub-direct-loader/hdd-update/candidate-diagnostics.img" \
  --boot-image "$PWD/build/grub-direct-loader/hdd-update/qemu-diagnostics-normal/fat32.img" \
  --report "$PWD/build/grub-direct-loader/hdd-update/candidate-diagnostics-report.json" \
  --boot-log "$PWD/build/grub-direct-loader/hdd-update/qemu-diagnostics-normal/serial.log" \
  --baseline-report "$PWD/build/grub-direct-loader/hdd-update/candidate-report.json" \
  --current-report "$PWD/build/grub-direct-loader/hdd-update/candidate-cpu-safe-report.json" \
  --backup "$PWD/build/grub-direct-loader/hdd-update/HDD_IMG.IMG.original.bak" \
  --snapshot "$PWD/build/grub-direct-loader/hdd-update/HDD_IMG.IMG.pre-diagnostics.bak"
```

Run `Tools/test-grub-direct-diagnostics.py` once for each STOP name
(`command`, `loaded`, `callback`, `profile`, `prepared`, `copied`,
`parameters`, `segments`, `cleanup`, `paging`, `frame`, and `jump`) before
publication. The harness reads the VGA text aperture to confirm the manual
visibility prompt and exact selected stage. It also captures the screen,
checks a stable instruction pointer and decoded hold jump, and records serial
output from a disposable HDD copy. The `--expect-module-failure` case corrupts
only that disposable copy and verifies that the error prompt remains visible
without a kernel boot. A captured QEMU stop is diagnostic evidence only; it
does not prove physical 386/486 compatibility or successful kernel entry.

Run the hosted loader and atomic checks through CTest:

```sh
cmake -S Tests/Hosted -B build/grub-direct-loader/hosted
cmake --build build/grub-direct-loader/hosted --target GrubDirectTests AtomicCmpxchgTests
ctest --test-dir build/grub-direct-loader/hosted -R '^(GrubDirectTests|AtomicCmpxchgTests)$' --output-on-failure
```

`AtomicCmpxchgTests` compiles the same CMPXCHG loop bodies used by the HAL.
On Apple Silicon, CMake targets x86-64 so macOS can run the harness through
its x86 translation support.

Run bounded direct-loader error/recovery and lifecycle scenarios against a
fresh image:

```sh
python3 Tools/test-grub-direct-recovery.py \
  --image-dir build/grub-direct-loader/image \
  --output build/grub-direct-loader/recovery-malformed \
  --scenario malformed-recovery --cpu 486 --memory 32
python3 Tools/test-grub-direct-recovery.py \
  --image-dir build/grub-direct-loader/image \
  --output build/grub-direct-loader/recovery-reload \
  --scenario reload --cpu 486 --memory 32
```

The reload scenario prepares the direct loader, replaces it with a command
that fails to open its kernel input, then prepares and boots a valid image.
This tests read-failure cleanup and loader replacement; it does not inject
every allocation, registration, or generic-relocator preparation failure.

The direct loader accepts ELF32 i386 kernel and HAL images, bounds normalized
memory-map handling to 512 MiB, and keeps the existing BootInfo ABI. It uses
GRUB's generic relocation-copy engine followed by its own source-built
transition. It does not replace the shipped GRUB runtime. The transition
avoids CR4 on the baseline path and clears supported PAE/PSE bits only on the
CPUID-proven path. No strict no-CR4 emulator or physical 386/486 result is
available here. QEMU runs do not establish hardware compatibility or prove
the complete cause of reported physical boot failures. Physical 386/486
compatibility remains `NOT_YET_VERIFIED`.

## Bounded GUI development workflow

Use the wrapper scripts rather than invoking QEMU directly:

```sh
./debug-and-run-for-15-seconds.sh
```

`run-qemu.sh` defaults to headless display plus serial output. For visual
inspection set `UNDOS_DISPLAY` to an available QEMU backend, for example
`UNDOS_DISPLAY=cocoa ./run-qemu.sh` on macOS. `UNDOS_VGA=std` selects the VGA
fallback instead of Cirrus. `UNDOS_QMP_SOCKET=/absolute/path/to/socket` enables
a local QMP socket for screenshot capture and input injection. All modes retain
the 15-second runtime bound. `UNDOS_QEMU_CPU=486` is the default; set it to
`pentium` to test the other emulated CPU profile. `UNDOS_QEMU_BOOT=c` boots
the task-local `fat32.img` as a hard disk without attaching the ISO;
`UNDOS_QEMU_BOOT=d` preserves the default ISO boot. A serial-only boot is not a
visual GUI check. `UNDOS_QEMU_MEMORY` accepts `8`, `16`, or `32` MiB for
bounded low-memory scenarios and defaults to `32`.
`python3 Tools/progman-smoke.py --output build/grub-direct-loader/smoke --vga cirrus`
uses that wrapper to capture shell/menu/application checkpoints and verify a
quoted argument reaches the demo; use `--vga std` for the fallback path. Inspect
the generated PPM captures as well as the serial log.
Additional `--scenario` choices are `editing` (create and launch a program),
`failure` (missing executable and recovery), `windows` (system menu and group
states), `terminal` (help and exit), `terminal-stdio` (foreground child streams
and input/EOF), `terminal-close` (closing with blocked child input), `multiple` (mouse double-click,
independent instances, and closing one instance), and `font` (user-mode
TrueType decode, kernel registration, and private-face release). Run these sequentially:
they share the same writable disk image and must not run concurrently.

For the cacheless font-rendering contract, use a separate hosted build root
and run the full suite:

```sh
cmake -S Tests/Hosted -B build/remove-glyph-cache/hosted/Debug
cmake --build build/remove-glyph-cache/hosted/Debug -j4
ctest --test-dir build/remove-glyph-cache/hosted/Debug --output-on-failure
ctest --test-dir build/remove-glyph-cache/hosted/Debug -R 'font|USER|menu|GFX|chrome text' --output-on-failure
python3 Scripts/build.py kernel --docker --build-root build/remove-glyph-cache --jobs 4
python3 Scripts/build.py apps --docker --build-root build/remove-glyph-cache --jobs 4
python3 Scripts/build.py image --docker --build-root build/remove-glyph-cache --jobs 4
```

Then run `menu-font` and `launch` sequentially on each VGA adapter. These
scenarios share writable image state and must not overlap. Each command uses
the bounded QEMU wrapper through `Tools/progman-smoke.py`:

```sh
UNDOS_IMAGE_DIR="$PWD/build/remove-glyph-cache/image" python3 Tools/progman-smoke.py --scenario menu-font --vga cirrus --output build/remove-glyph-cache/smoke/menu-font-cirrus
UNDOS_IMAGE_DIR="$PWD/build/remove-glyph-cache/image" python3 Tools/progman-smoke.py --scenario menu-font --vga std --output build/remove-glyph-cache/smoke/menu-font-std
UNDOS_IMAGE_DIR="$PWD/build/remove-glyph-cache/image" python3 Tools/progman-smoke.py --scenario launch --vga cirrus --output build/remove-glyph-cache/smoke/launch-cirrus
UNDOS_IMAGE_DIR="$PWD/build/remove-glyph-cache/image" python3 Tools/progman-smoke.py --scenario launch --vga std --output build/remove-glyph-cache/smoke/launch-std
```

Inspect the PPM captures and serial logs. On macOS, convert captures for
viewing with `sips -s format png <capture.ppm> --out <capture.png>`. Smoke
results are functional checks, not performance evidence. The 386/486 cost of
repeated non-pinned rendering remains `NOT_YET_VERIFIED`.

For terminal input or standard-handle changes, run the hosted `[tty]` and
`[standard-handles]` selectors. Also run the SDK terminal example with
`--scenario terminal-stdio` and `--scenario terminal-close` in separate
invocations. These image checks share writable state and each uses the bounded
15-second QEMU wrapper.

Hosted tests can be configured independently:

```sh
cmake -S Tests/Hosted -B build-hosted
cmake --build build-hosted --target KernelTests -j4
build-hosted/tests/KernelTests '[progman],[user],[gfx]'
```

See [the Program Manager plan](../TO_PROGRMAN.md) for work-package contracts
and remaining release gates.

## Adding a New Cross-Binary Function

1. Declare the function interface prototype in a header inside `kernel_headers` using either `UNDOS_KERNEL_API` or `UNDOS_HAL_API`.
2. Implement the function inside its native project target normally.
3. You can reference and call this function instantly from the alternate target module. Stage 1.5 will automatically cross-stitch the references when parsing the
   `.symtab` layouts during boot.

## Kernel memory and ownership

UnDOS uses C++ types and Resource Acquisition Is Initialization (RAII) to make
ownership explicit and reduce leaks and use-after-free errors. Avoid raw owning
pointers. Prefer references for borrowed objects; use pointers when absence
(`nullptr`) is valid. Raw pointers still exist at low-level boundaries, so this
is a development rule, not a claim that the kernel is raw-pointer-free or that
C++ guarantees memory safety.

### Addresses are not ownership

Use `VirtualAddress` for an address in a virtual address space and
`PhysicalAddress` for a physical memory address. These are distinct tagged
types, defined in [address.hpp](../Kernel/API/include/kernel/memory/address.hpp),
so APIs can distinguish the two instead of accepting an unclassified integer
or pointer. Neither type owns memory, frees an allocation, validates access,
or keeps a mapping alive.

Converting with `from_ptr()` or `as_ptr<T>()` changes the representation, not
the mapping or lifetime. A physical address must have an appropriate virtual
mapping before CPU code can dereference the memory. A `VirtualAddress` is not
necessarily accessible in the current address space.

### Kernel object ownership: `KObjectPtr<T>`

`KObjectPtr<T>` is a reference-counted handle to an Object Manager object.
Constructing it from a raw pointer or copying it retains the object through
`KE_OB_Retain`; moving it transfers the reference. Destruction or replacement
releases the reference through `KE_OB_Release`. The Object Manager frees the
object when its last reference is released.

Keep a `KObjectPtr<T>` when retaining an object beyond a borrow. For example,
driver objects use `KObjectPtr<KDriverObject>`. Pass handles as `const &` when
the callee only borrows them, to avoid unnecessary atomic reference-count
updates. `get()` returns a borrowed raw pointer; do not keep that pointer after
the owning reference is gone. `release()` transfers responsibility for the
held reference without decrementing it; use it only for an explicit ownership
handoff.

Reference counting manages participating object references. It does not make
borrowed pointers safe after release, prevent reference cycles, or synchronize
access to the object's mutable state.

See [KObjectPtr](../Kernel/API/include/kernel/fwd/KObjectPtr.hpp),
[the shared pointer wrapper](../Kernel/API/include/kernel/fwd/KPtr.hpp), and
[Object Manager retain/release](../Kernel/Source/src/object_manager.cpp).

### Device references: `KDevicePtr<T>`

`KDevicePtr<T>` provides typed access to device objects through the same
wrapper, but its retain and release hooks are currently no-ops. It is
**non-owning**: copying it does not extend device lifetime, and destroying it
does not free a device. The device must remain alive for every use of the
handle; do not infer ownership from the pointer-like syntax.

See [KDevicePtr](../Kernel/API/include/kernel/fwd/KDevicePtr.hpp) and
[the driver guide](WritingADriver.md).

## Code Generation Invariant Rules

> [!IMPORTANT]
> When generating code modifications or additions for the UnDOS codebase, you must adhere strictly to these constraints:

* **No hosted standard library headers:** Do not include `<iostream>`, `<string>`, `<vector>`, or another hosted standard header. Use the header-only equivalents provided by `<strfmt.hpp>` or `SDK/KStd` in the `kstd` namespace.
* **PMM Initialization Placement:** Any allocation arrays, tracking structures, or page frame bitmaps generated for memory subsystem design must calculate their initial
  position pointers starting exactly at `boot_info->kernel_virtual_end`. Do not use hardcoded markers.
* **C-linkage exports:** Functions with API visibility must use `extern "C"` so the Stage 1.5 symbol map can match their names exactly.
* **Exported API names:** Use `<Module>_<SYSTEM>_<Name>` with `KE` or `HAL` as the module prefix, an uppercase system name, and a camel-case function name. For example, `KE_VMM_AllocateRegion`, `HAL_IO_Out8`, or `KE_OB_InsertObject`. Do not wrap exported functions in C++ namespaces.
* **References and pointers:** Prefer references. Use a pointer only when `nullptr` is a valid and expected argument or result.
* **Smart pointers:** Pass `KObjectPtr<T>` as `const &` when possible to avoid unnecessary atomic reference-count updates.
* **Driver state:** Do not store mutable driver state in global variables. Put it in a context structure, register it with `KE_IO_CreateDeviceWithContext<CONTEXT_STRUCT_NAME>` when creating the FDO, and access it through `pdo->deviceExtension.as<CONTEXT_STRUCT_NAME>()`.

## Minimal kernel text output

`KE_PrintBuffer` keeps its timestamped line format and writes through the HAL
normal text writer. The current x86 HAL sends normal, emergency, and raw debug
output to COM1 at 115200 baud, 8N1, with no flow control. The kernel logger
does not access VGA memory. The UART writer uses bounded polling and reports
partial output through `KE_ConsoleGetOutputStatus`; output failures do not stop
boot or recursively log.

The serial driver leaves COM1 configuration to the HAL. It configures other
assigned UARTs with compatible settings and uses nonblocking I/O. The legacy
Stage 1.5 path uses the same baud and a bounded transmit poll.

`kernel_core_main` now continues to create `/System/init.elf` after its device
and display setup. The HDD refresh command boots a disposable candidate through
`run-qemu.sh` before publication. It requires the kernel startup, root
selection, font readiness, and Program Manager readiness messages, plus a
valid nonuniform QMP screenshot of at least 640x480. It retains the serial log,
screenshots, and candidate report under `build/bios-chs-fallback/`. This check
does not verify a physical 486; that requires a human hardware test.

A future platform-capability console may need runtime routing or graphics
takeover integration. Those behaviors are not implemented.
