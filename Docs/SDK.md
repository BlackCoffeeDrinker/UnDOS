# Application SDK and compiler drivers

[Project overview](../README.md)

## Application SDK and Docker image (Phase 2)

See [SDK_ROADMAP.md](../SDK_ROADMAP.md) for Docker release, cross-host validation,
and later runtime work.

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
| `UnDOS::KStd` | Opt-in header-only `kstd` utilities; not a hosted C++ standard library |

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

`UnDOS::GUI` exposes a C API through `<windows.h>`. It follows familiar Win32
names and callback patterns for the window, message-loop, and paint operations
that UnDOS currently supports. The initial API includes class registration,
window creation and management, message dispatch, paint lifecycle, and basic
2D drawing.

The existing `<undosgui/gui.hpp>` helper remains available for current C++
applications during migration. New GUI applications should use the C API.

This is a source-oriented subset, not a Win32 implementation. The API is
ANSI-only and uses the UnDOS cdecl ABI. It accepts only unfiltered `GetMessage`
and `PeekMessage` calls. `CW_USEDEFAULT` selects position `(0, 0)` and size
`320 x 200`. Unsupported extended styles fail instead of being ignored. Only
`WS_CHILD`, `WS_VISIBLE`, `WS_BORDER`, `WS_CAPTION`, and
`WS_OVERLAPPEDWINDOW` style bits are accepted. Window-class backgrounds use
the UnDOS palette; `NULL` selects palette index 7, and
`UNDOS_PALETTE_BRUSH(index)` encodes an index from 0 through 15. Drawing accepts
palette indices or packed RGB colors, which map to the canonical VGA palette.
`WNDCLASSA` style, extra-byte, icon, and cursor fields have no effect.
`MSG.pt` is zero because the native message ABI does not provide a point.
Device contexts are available only during `BeginPaint`/`EndPaint`.
`ShowWindow` returns operation success, not the previous visibility state.
`InvalidateRect` ignores its erase argument. `MoveToEx` requires a null
previous-point argument. `GetLastError` returns the last UnDOS syscall status.
`HMENU` supports menu bars, popup submenus, string commands, separators, and
disabled or grayed items. Each menu can contain up to 16 items, and each label
can contain up to 63 bytes. Menus support mouse selection, arrow-key navigation,
Enter or Space activation, Escape, F10, and Alt+Down. Selecting a command posts
`WM_COMMAND` with the item ID in `wParam` and zero in `lParam`. `DestroyMenu`
destroys the menu and its submenu tree, and detaches it from windows.
Unicode APIs, common controls, and general `GetDC` access are not provided.
Keyboard input already arrives as `WM_CHAR`; `TranslateMessage` does not
synthesize messages.

The installed `examples/gui` project builds as a C-only application. It is a
small reference for the supported callback and paint pattern.

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
| `init` | `Apps/Init` | `UnDOS::Application`, `UnDOS::Shared` | `init.elf`, `initlib.so` |
| `abi_test` | `Tests/ABI` | `UnDOS::Application`, `UnDOS::Shared` | `abitest.elf`, `abitest.so`, `abinext.so`, `abimiss.elf` |
| `fontloader` | `Apps/Fontloader` | `UnDOS::Application`, `UnDOS::FontResource`, `UnDOS::KStd` | `fontload.elf` |

`python3 Scripts/build.py apps --docker` first stages the SDK, then configures
and builds each application in `<build>/apps/<name>/<configuration>/`.
The `image` command stages their manifest outputs under `/System`.
The OS image build still stages configuration files and font fixtures.
The ABI project retains its deliberately unmarked `abimiss.elf` rejection
fixture and dependent-library lifecycle checks.

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

Kernel-private headers and the compiler binaries are not part of this staged
directory. Moving it is supported, provided a compatible cross-compiler stays
on PATH. For non-container applications, explicitly configure with
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
