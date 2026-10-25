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
the 15-second runtime bound. A serial-only boot is not a visual GUI check.
`python3 Tools/progman-smoke.py --output /tmp/undos-smoke --vga cirrus`
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
