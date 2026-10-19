# Syscall IDL

This document describes the small IDL (Interface Definition Language) used to declare syscalls, enums,
and structs shared between the kernel and userland. The IDL is defined in `tools/syscalls.txt` and
compiled by `tools/create-syscalls.py` (implemented in `tools/idlgen/`).

You should never need to read the generator source to write a new syscall — this file is the spec.

## How to (re)generate code

```
docker run --rm -v "$(pwd):/src" -w /src gcc-standalone:i686-elf \
    cmake --build build --target undos-syscalls-gen
```

or directly:

```
python3 tools/create-syscalls.py tools/syscalls.txt \
    kernel_userland_shared/include/undos/syscalls userland/ kernel/src/syscalls
```

This produces:
- `kernel_userland_shared/include/undos/syscalls/*.h` — shared wire structs, enums, structs.
- `userland/.../userspace.h` — userspace wrapper functions (e.g. `UD_SYS_CreateProcessV1(...)`).
- `kernel/src/syscalls/handler_syscall.cpp` + `__kernel_types.h` — kernel dispatch code and
  `handle_<Name>_<Version>()` declarations that you must implement.

Handwritten kernel handlers return `kernel::KernelError`, not wire-level
`SyscallReturnCode`. The generated dispatcher preserves kernel errors until
`KE_SYSCALL_Dispatch`, which converts them with `kernel::KErrorToSysReturnCode`.
Subsystem operations return `KernelError` for status-only results or
`kstd::expected<T, KernelError>` for fallible values. Normal EOF, optional absence,
and predicates are not failures. Domain validation belongs in the subsystem;
the dispatcher owns user-memory and wire-format validation. The public syscall
enum and wire layouts remain unchanged.

## Top-level declarations

The IDL file is a sequence of `enum`, `flags`, `struct`, `using`, and `syscall` declarations, in any
order — declarations may reference each other regardless of the order they appear in the file.

### enum

```
enum ProcessFlags {
    None        = 0,
    Detached    = 1,
    InheritEnv  = 2,
};
```

Each member must have an explicit numeric value. Enums are emitted as plain C-style enums and can be
referenced from fields via `enum<Name>`.

### flags

```
flags DCInitFlags : uint32 {
    Primary;
    Memory;
    Offscreen;
    NoAccel;
};
```

Like `enum`, but for bitmask values: members have **no explicit value** — the generator
automatically assigns sequential bit positions in declaration order (1st member = `0x1`, 2nd =
`0x2`, 3rd = `0x4`, ...). A required underlying integer type (`uint8`, `uint16`, `uint32`, `uint64`,
a raw C type such as `uint32_t`, or a `using` alias that resolves to a `c<T>` type) follows the `:`
and determines the emitted enum's storage type (`enum DCInitFlags : uint32_t { ... };`).

`flags` declarations are emitted as plain C-style enums, exactly like `enum`, and are referenced
from fields via `flags<Name>`. Since UnDOS already treats bitmask fields as ordinary enum values
OR'd together (see `MemoryProtectFlags`), a `flags<Name>` field is compiled identically to an
`enum<Name>` field — only the declaration itself differs (auto bit-values + mandatory underlying
type, instead of explicit values).

### using

```
using HANDLER = c<uint32_t>;
```

Declares `NAME` as an alias for an existing type expression (`c<T>`, `string`, `enum<Name>`,
`struct<Name>`, `array<T,N>`, `dynarray<T>`, `buffer<T>`, or another alias). After a `using`
declaration, `NAME` may be used as a bare field type anywhere a type is expected — as a field type
inside `syscall`/`struct` bodies, as the element type inside `array<...>`/`dynarray<...>`/
`buffer<...>`, or as the underlying type of a `flags` declaration:

```
using HANDLER = c<uint32_t>;

flags Testing : HANDLER {
    A;
    B;
};

syscall V1 GFX CombineRegion [332] {
    dest: IN HANDLER;
};
```

Here `dest: IN HANDLER;` has the same wire layout as `dest: IN c<uint32_t>;`, but generated
fields and wrapper parameters retain the `HANDLER` typedef name. Scalar aliases are emitted
as C-compatible typedefs in `__aliases.h`, included by the generated headers that use them.
For example, `using HANDLE = c<uint32_t>;` produces `typedef uint32_t HANDLE;`, with
`HANDLE` for inputs and `HANDLE*` for wrapper outputs. This is a named API type, not a
compiler-enforced strong type. Non-scalar aliases are expanded as before.

Meanwhile,
`flags Testing : HANDLER { ... }` behaves exactly as if you had written `flags Testing : uint32_t
{ ... }` directly: flags still use the fully resolved underlying integer type.

Aliases may chain (`using A = B; using B = c<uint32_t>;`) and may be declared in any order relative
to their use. A `using` that references an undefined name, forms a circular chain (`using A = B;
using B = A;`), or is used as a `flags` underlying type but does not ultimately resolve to a `c<T>`
type, is a compile-time error and aborts generation.

### struct

```
struct ProcessInfo {
    pid: c<uint32_t>;
    exitCode: c<uint32_t>;
};
```

Reusable structs, referenced from fields via `struct<Name>`. Struct fields use the same field syntax
described below (direction/mode tokens are accepted but only meaningful for syscall fields).

### syscall

```
syscall V1 PROC CreateProcess [200] {
    path: IN string;
    argument: IN string;
    flags: IN c<uint32_t>;
};
```

- `V1` — the syscall's version. Bump the version (`V2`, `V3`, ...) instead of breaking an existing
  ABI when a syscall needs to change shape; both versions can coexist (see `PROC GetInfo` V1/V2).
- `PROC` — a free-form category name, used as part of the generated struct/function names
  (`__SYS_PROC_CREATEPROCESS_V1`, `UD_SYS_CreateProcessV1`, ...).
- `CreateProcess` — the syscall name.
- `[200]` — the unique numeric syscall number dispatched on in `KE_SYSCALL_Dispatch`. **Must be
  unique across the whole file.** Existing ranges: `200`-`209` = `PROC`, `210`-`219` = `MEM`,
  `220`-`229` = `FS`. Pick the next free number in the appropriate range for a new category, or
  start a new range for a brand-new category.
- The body is a field list (see below).

## Fields

```
name: [IN|OUT] [COPY|MAP] <type>;
```

- `name` — the field name (camelCase, matches generated struct member / parameter names).
- `IN` / `OUT` (optional, defaults to `IN`) — direction of data flow between userland and kernel:
  - `IN` — supplied by the caller, read by the kernel handler.
  - `OUT` — filled in by the kernel handler and copied/mapped back to the caller.
- `COPY` / `MAP` (optional, direction/type-dependent default — see below) — how the kernel obtains
  the field's data:
  - `COPY` — the value is copied into a kernel-owned buffer once, up front. Use this for small,
    bounded, predictable-size data (paths, filenames, arguments, small structs). Copying avoids
    TOCTOU issues and keeps the kernel from holding a page mapping open while a handler runs.
  - `MAP` — the caller's memory is validated once (pointer + length) and then mapped directly via
    `kernel::borrowed_ptr<T>`; the kernel handler reads/writes it in place. Use this for large or
    variable-length data (file buffers, draw command lists, IPC messages) to avoid copying and RAM
    pressure. Unmapping is automatic (RAII) — handlers must never call `KE_VMM_MapBorrowed`/
    `KE_VMM_UnmapBorrowed` themselves.
- `<type>` — see the type table below.

### Direction/mode defaults

| Field kind                                                                    | Default direction | Default mode                     |
|-------------------------------------------------------------------------------|-------------------|----------------------------------|
| any field                                                                     | `IN`              | —                                |
| `string`, direction `IN`                                                      | —                 | `COPY`                           |
| `string`, direction `OUT`                                                     | —                 | `MAP`                            |
| `buffer<T>`                                                                   | —                 | `MAP` (always; cannot be `COPY`) |
| everything else (`c<T>`, `enum<T>`, `struct<T>`, `array<T,N>`, `dynarray<T>`) | —                 | `MAP`                            |

You rarely need to write `COPY`/`MAP` explicitly — the defaults already implement the "copy small
things, map large buffers, never enforce fixed-length buffers" policy. Only override them if a
specific field genuinely needs the other behavior.

## Field types

| IDL type       | Meaning                                                                                                                                               | Wire representation                                                   |
|----------------|-------------------------------------------------------------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------|
| `string`       | A NUL-terminated, variable-length string. Never fixed-size.                                                                                           | `const char* <name>` (or `char*` for `OUT`) + `uint32_t <name>Length` |
| `c<ctype>`     | A raw scalar C type, e.g. `c<uint32_t>`, `c<uint8_t>`.                                                                                                | `ctype <name>`                                                        |
| `enum<Name>`   | A value of a previously declared `enum Name { ... }`.                                                                                                 | `enum Name <name>`                                                    |
| `flags<Name>`  | A bitmask value of a previously declared `flags Name : <underlying> { ... }`. Same wire representation and OR-combining semantics as `enum<Name>`.    | `enum Name <name>`                                                    |
| `struct<Name>` | A value of a previously declared `struct Name { ... }`.                                                                                               | `struct Name <name>`                                                  |
| `array<T, N>`  | A fixed-size array of `N` elements of type `T`.                                                                                                       | `T <name>[N]`                                                         |
| `dynarray<T>`  | A variable-length array of `T`, always mapped (pointer + length).                                                                                     | `T* <name>` + `uint32_t <name>Length`                                 |
| `buffer<T>`    | A large, variable-length buffer, always mapped, never copied, never fixed-size. Use this for file I/O buffers, draw command lists, IPC messages, etc. | `T* <name>` + `uint32_t <name>Length`                                 |

`string`, `dynarray<T>`, and `buffer<T>` never require (or accept) a fixed maximum length in the
IDL — the kernel-side dispatcher validates the caller-supplied length against a generic size cap
(`UNDOS_MAX_COPY_SIZE` for `COPY` fields, `UNDOS_MAX_MAPPED_BUFFER_SIZE` for `MAP` fields) instead of
a per-field fixed size.

Any name declared with `using NAME = <type-expr>;` (see [`using`](#using) above) may also be used
as a bare field type wherever a type is expected, with the same meaning as the type it aliases.

## `string` OUT fields: query-length-then-fill

An `OUT string` field (e.g. `commandLine: OUT string;`) follows a two-call contract, mirroring common
"ask for size, then fill" APIs:

1. Caller passes `nullptr` for the buffer (and any value for the length pointer). The kernel handler
   populates only the required length.
2. Caller allocates a buffer of at least that length, calls again with the buffer pointer and its
   capacity in the length parameter. The kernel handler fills the buffer and updates the length to
   the actual number of bytes written.

The generated userspace wrapper exposes this as `char* name, uint32_t* nameLength` — callers set
`*nameLength` to the buffer's capacity before the call.

## Documentation comments

Any declaration (`enum`, `struct`, `syscall`, enum member, or field) may be preceded by a `/** ... */`
doc comment. It is parsed and re-emitted above the corresponding generated declaration (struct,
field, wrapper function, `handle_*` declaration, ...), so document your syscalls and fields here
rather than in the generated output.

```
/** Retrieves information about a running or exited process. */
syscall V1 PROC GetInfo [201] {
    /** Process ID to query. */
    pid: IN c<uint32_t>;
};
```

Plain `//` line comments are also supported anywhere in the file but are not carried into generated
code.

## What you write vs. what is generated

Writing a syscall in `syscalls.txt` gives you, for free:
- The shared wire struct (`__SYS_<CATEGORY>_<NAME>_<VERSION>`).
- A userspace wrapper function with a stable ABI (`UD_SYS_<Name><Version>(...)`).
- Kernel-side dispatch: validation, contiguous kernel buffer for `COPY` fields, `borrowed_ptr<T>`
  mapping for `MAP` fields, and copy-back of `OUT` fields — registered in `KE_SYSCALL_Dispatch`.

What you must still write by hand:
- The `handle_<Name>_<Version>(__KRN_<CATEGORY>_<NAME>_<VERSION>&& args, uint32_t flags)` function
  body in `kernel/src/syscalls/` — the generator only emits its declaration. Inside it, `string`
  fields are `kstd::string_view`, `MAP`-mode/`buffer<T>` fields are `kernel::borrowed_ptr<T>`
  (implicitly convertible to `kstd::span<T>`/`kstd::string_view`), and everything else is a plain
  value — you must never touch raw user pointers or call `KE_VMM_MapBorrowed`/`UnmapBorrowed`
  yourself.

## Full worked example

```
/** Reads data from an open file descriptor into `data`. */
syscall V1 FS Read [222] {
    /** Open file descriptor to read from. */
    fileDescriptor: IN c<uint32_t>;
    /** Buffer that receives the data read from the file. */
    data: OUT buffer<c<uint8_t>>;
};
```

generates a userspace wrapper roughly like:

```c
uint32_t UD_SYS_ReadV1(uint32_t fileDescriptor, uint8_t* data, uint32_t* dataLength);
```

and a kernel-side declaration you implement:

```c++
kernel::KernelError handle_Read_V1(__KRN_FS_READ_V1&& args) {
    // args.fileDescriptor: uint32_t
    // args.data: kernel::borrowed_ptr<uint8_t>, args.data_len: size (in/out)
    ...
}
```

### `flags`/`using` worked example

```
flags DCInitFlags : uint32 {
    Primary;
    Memory;
    Offscreen;
    NoAccel;
};

syscall V1 GFX CreateDC [300] {
    driverName: IN string;
    initFlags:  IN flags<DCInitFlags>;
};
```

generates `enum DCInitFlags : uint32_t { DCInitFlags_Primary = 0x00000001, DCInitFlags_Memory =
0x00000002, DCInitFlags_Offscreen = 0x00000004, DCInitFlags_NoAccel = 0x00000008, };` plus a
userspace wrapper that takes `enum DCInitFlags initFlags` exactly like an `enum<Name>` field would —
callers pass one or more values OR'd together (`DCInitFlags_Primary | DCInitFlags_Memory`).

```
using HANDLER = c<uint32_t>;

flags Testing : HANDLER {
    A;
    B;
};

syscall V1 GFX CombineRegion [332] {
    dest: IN HANDLER;
};
```

is fully resolved before codegen runs, so it generates exactly the same output as writing
`dest: IN c<uint32_t>;` and `flags Testing : uint32_t { A; B; };` directly.
