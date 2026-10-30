# Application ABI

[Project overview](../README.md)

Repository commands in this reference run from the repository root.

## Application ABI v1 (Phase 1)

This is the application contract for the planned UnDOS SDK on macOS, Windows,
and Linux hosts. It is **not the kernel/HAL ABI**. The startup, loader, runtime
lifecycle, ABI identification, and capability queries are implemented in-tree.
The installable sysroot, external CMake package, Docker SDK, and compiler-driver
defaults are described in [the SDK guide](SDK.md). The initial standard
C library subset is described in [the C library reference](Libc.md).
This is not complete hosted ISO C/POSIX support.
When changing this ABI, keep this document, the loader, CRT, syscall IDL inputs,
and conformance probes consistent.

### CPU and C calling convention

Applications use little-endian i386 System V C conventions and ILP32:
`int`, `long`, and pointers are 32 bits, `long long` is 64 bits, and
`long double` uses the i386 12-byte storage representation. Use the i386
instruction baseline, not i686 instructions such as CMOV. At C function call
sites ESP is 16-byte aligned before CALL; the callee sees its return address
at ESP and arguments starting at ESP+4. EBX, ESI, EDI, EBP, and ESP are
callee-preserved, EAX/ECX/EDX are scratch, and the direction flag must be clear.
This does not imply Linux executable or syscall compatibility.

Floating-point operations use the x87 ABI, not a software-float calling
convention. `UD_SYS_QueryFpuV1` returns `FpuCapability_Unavailable`,
`FpuCapability_Native`, or `FpuCapability_Emulated`. Availability means usable,
per-thread managed support, not merely an installed coprocessor.
**It currently returns Unavailable**, including on CPUs with an FPU: native
x87 initialization/context preservation and the planned kernel FPU emulator
are not implemented. Do not execute floating-point code while unavailable.
Future emulation will retain this ABI and query, rather than requiring
applications to adopt another floating-point calling convention.

### Executables, shared libraries, and ABI identification

The main application is an ELF32 little-endian `EM_386` `ET_EXEC`. Startup-loaded
shared libraries are `ET_DYN`, referenced by `DT_NEEDED`. ELoader performs eager
binding; no Linux interpreter or lazy-binding trampoline is used. `PT_INTERP`
and `PT_TLS` are rejected. TLS, `dlopen`/`dlclose`, exception unwinding, and a
complete C++ standard runtime are outside v1.

Every application and shared library must contain exactly one allocated
UnDOS ABI note covered by both a `PT_NOTE` and a `PT_LOAD` segment:

| ELF note field | Value |
|---|---|
| Name | `UnDOS` including its terminating NUL (`namesz = 6`, padded to 4 bytes) |
| Type | `1` |
| Descriptor | Two little-endian uint32 values: ABI version `1`, flags `0` |
| Descriptor size | `8` |

Missing, malformed, duplicate, or incompatible UnDOS notes reject before
application initialization. Unknown non-UnDOS notes are permitted.
All in-tree application/library images migrate together: **there is no legacy
binary allowlist**, because UnDOS has not shipped. ELoader itself is a kernel-
bootstrapped component, not a legacy application exemption; its private
kernel handoff is updated together with the CRT.

Dynamic objects use the System V hash table (`DT_HASH`) and ELF32 REL
relocations. Supported types are `R_386_NONE`, `R_386_32`, `R_386_PC32`,
`R_386_COPY`, `R_386_GLOB_DAT`, `R_386_JMP_SLOT`, and `R_386_RELATIVE`.
RELA relocation tables and unsupported relocation types reject.
RELR, symbol versioning, and indirect-function (IFUNC) symbols are unsupported
and reject rather than being interpreted as ordinary symbols.
Libraries are searched in the executable's directory, then `/System`;
dependency names cannot contain directory traversal. Dependency cycles reject.
Constructors run dependency-before-dependent, not merely reverse file
discovery order; finalization reverses initialization order.

Current loader limits are eight objects including the main executable,
32 direct dependencies per object, 16 program headers per object, and 4 MiB
of combined mapped image pages. Application mappings start at or above
`0x00010000`, below the `0xC0000000` kernel boundary, and cannot overlap ELoader
at `0x08000000` through its page-rounded linked end. Libraries occupy fixed
16 MiB virtual slots in the window starting at `0x10000000` (the first
dependency currently uses `0x11000000`). These are virtual ranges, not physical
RAM reservations. The initial stack allocation remains 16 KiB, including
startup data; no automatic growth, ASLR, or stack-guard guarantee is made.
**Image pages currently map user read/write/execute**; ELF segment flags
validate entry/callback addresses but do not yet establish W^X protections.
i386 also does not provide an NX bit.

### Versioned process entry and arguments

The SDK-owned `_start` receives ESP aligned to 16 bytes, a zero reserved word
at ESP, and a pointer to `UnDOSStartupInfoV1` at ESP+4. There is no return
address to a caller and no Linux auxiliary vector. `_start` clears DF and
calls the CRT; normal applications implement `main(int argc, char **argv)`.
Other entry registers are unspecified.

`ABI/include/undos/abi.h` defines the public 48-byte
structure. Every field is a fixed-width uint32; address fields are process
virtual addresses, not host-sized pointers:

| Offset | Field | Contract |
|---|---|---|
| 0 | `size` | Exactly 48 for v1 |
| 4 | `version` | `1` |
| 8 | `argc` | Argument count, including argv[0] |
| 12 | `argv` | Address of argc+1 pointers, ending in NULL |
| 16 | `envp` | Address of an empty, NULL-terminated environment |
| 20 | `initialize_shared` | Loader callback, invoked once by the CRT |
| 24 | `finalize_shared` | Loader callback for normal-exit library cleanup |
| 28 | `flags` | Zero |
| 32 | `reserved[4]` | All zero |

Callbacks take no arguments and use the same C calling convention. The
startup-event handle is private loader bookkeeping, cleared from the entry
frame before application entry, and is not part of this public structure.
Startup strings and pointer arrays remain valid for the process lifetime.
The environment is exposed as `environ` but contains no inherited variables;
working-directory and relative-path API guarantees are deferred.

`argv[0]` preserves the executable path verbatim. Paths are nonempty and at
most 255 bytes; the command argument string is at most 1023 bytes. At most
32 arguments, including argv[0], are accepted. ASCII whitespace separates
arguments except inside double quotes; quotes are removed and adjacent quoted
and unquoted fragments concatenate. `""` is an empty argument. Backslashes
are literal: there is no escaping or shell expansion. Unmatched quotes,
embedded NULs, and overflow reject rather than truncate.

### File management extensions

`ReadDirectoryV1` (227) enumerates short-name directory entries through a readable
VFS directory handle. Its output name uses query-length-then-fill, including the
terminating NUL. `GetFileInformationV1` (229) returns the byte size and FAT
attribute bits for an open file or directory.

`CopyFileV1` (230), `MoveFileV1` (231), `DeleteFileV1` (232) and
`CreateDirectoryV1` (233) provide separate file-management operations.
Destinations are never overwritten.
Deletion supports only empty directories; directory renames work only within
the same parent. Paths are absolute and at most 255 bytes. Cross-volume and
recursive operations are unsupported. Open source entries and open directory
handles block mutations.
The exact field and failure contracts are in `ABI/IDL/syscalls.txt`; application
behavior and filesystem limits are described in
[File Manager](../Apps/FileManager/README.md).

### Runtime initialization, exit, and standard streams

The CRT validates startup size/version/reserved fields, caches standard
handles, exposes the empty environment, runs executable preinitializers,
invokes shared-library initializers, runs executable constructors, then calls
`main`. Static executable arrays are owned by the CRT; the loader owns
shared-library `DT_INIT`/`DT_INIT_ARRAY` and `DT_FINI_ARRAY`/`DT_FINI`.
Linker scripts retain initialization/finalization arrays and accommodate
the cross-compiler's `.ctors`/`.dtors` sections.

Returning from main calls `crt_exit` (`exit` is also supplied). Normal exit
runs registered `atexit`/`__cxa_atexit` callbacks in reverse registration
order, executable finalizers in reverse array order, then library finalizers
in reverse dependency order. `__cxa_finalize` prevents repeated callbacks and
supports DSO-specific selection. Registration is bounded to 128 pending
callbacks and returns failure when full; threading/TLS runtime guarantees
are not implied. Current CRT output is unbuffered, so no pending stream
buffers need flushing. `_Exit`/`crt_immediate_exit` and the Exit syscall
bypass cleanup. Reentrant normal exit takes the immediate path.

Zero means success. All 32 exit-status bits are preserved through
`UD_SYS_WaitProcessV1`; there is no implicit POSIX low-eight-bit truncation.
Loader failure exits with status 255 and is reported as process-creation
failure when it occurs before the readiness handshake. Readiness covers
mapping, relocation, dependency ordering, and loader lifecycle validation,
not completion of constructors or main.

Object-manager APIs use `HANDLE`, a C-compatible typedef of `uint32_t`, for
process, event, I/O and TTY handles. It is an opaque process-local token, not a
pointer; zero remains a valid handle. This naming does not change syscall wire
layouts or make integers and handles distinct compiler types. Graphics, window
and font-resource identifiers remain separate APIs.

**Never assume that handles 0/1/2 are standard streams.** Query
`UD_SYS_GetStdHandleV1` with `StdHandleSelector_Stdin`,
`StdHandleSelector_Stdout`, or `StdHandleSelector_Stderr`, then use the returned
opaque process-local handle. Closing a standard handle clears its mapping;
ordinary handle reuse does not recreate that mapping. Inheritable standard
streams preserve their identities when child handle values change.
Invalid selectors and unavailable streams return `INVALID_ARGUMENT`.
The CRT caches these queries at startup; unavailable streams do not prevent
GUI applications from starting. CRT output functions return `-1` on missing
streams, write failures, or formatting overflow; they do not silently fall
back to debug output. Explicit diagnostic output can use the debug syscall.
The terminal uses `UD_SYS_CreateProcessV2` (208) to supply explicit caller-local
stdin/stdout/stderr handles. It validates read/write access before launch and
creates independent child handles without inheriting unrelated handles.
`CreateProcessV1` keeps its existing inheritance behavior. TTY stdin waits for
committed input or EOF rather than treating an empty input queue as EOF.

### Graphics and text rendering

Default-font publication retains printable ASCII and the Terminal replacement
masks for that publication. `UiGlyphs` pins these masks so USER chrome can draw
them without VM execution, rasterization, or mask allocation under the USER
scene lock. Prepare other glyphs before acquiring the scene lock.

On `main`, every `Realization::glyph()` call takes the realization mutex and
loads and rasterizes a new mask. Hinted realizations execute eligible glyph
programs; unhinted realizations do not execute glyph programs. `main` has no
shared glyph cache. The shared-cache implementation is preserved on the
`glyph-cache` branch. These rendering details do not change the ABI wire
contracts.

### ABI conformance workflow

Build the image with `python3 Scripts/build.py image --docker`. Then run the
hosted `[application-abi]`, `[process-startup]`, and `[standard-handles]` tests.
The real startup assembly has an additional bounded opcode/alignment check:

```sh
python3 Tests/Hosted/tests/process_startup_trampoline_test.py --build-dir build/abi-trampoline
python3 Tools/progman-smoke.py --output build/abi-smoke --scenario abi
```

The smoke test uses the bounded `run-qemu.sh` wrapper and launches
`init --abi-test` through Program Manager. Its child checks quoted/empty arguments, initialized data
and BSS, empty environment, standard-handle queries, FPU capability reporting,
cross-library symbol resolution, dependency initialization, C++ destruction,
atexit, reverse finalization, immediate exit without cleanup, full-width exit
codes, and rejection of an
unmarked executable. The dependency fixture deliberately places a dependent
after its dependency in discovery order, so reverse-discovery initialization
would fail. Run emulator scenarios sequentially because they share the disk.
