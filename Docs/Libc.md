# Initial standard C library

[Project overview](../README.md)

For installation and compiler setup, see the [SDK guide](SDK.md).
Repository test commands run from the repository root.

## Initial standard C library (Phase 4)

The SDK extends the existing CRT rather than importing a separate libc. This
keeps one allocator, stream state and exit lifecycle per process, with no new
third-party dependency or additional upstream license/distribution obligations.
The SDK installs the public C headers. Do not add the libc header directory to
kernel or hosted-test include paths.
The libc implementation, allocator and memory/string primitives are C sources
compiled by the C compiler, including the legacy `crt_*` stdio helpers.
Core library code uses C11; syscall-facing code uses GNU C23 to consume the
existing generated syscall declarations and inline
assembly. Its private backend interface has C linkage, including when used by
the existing C++ CRT lifecycle/constructor support.
New code remains part of the repository under its existing project terms;
this is not a vendored or separately licensed libc port.

An ordinary hello-world source now builds through either the driver or the
CMake package, with no extra link flags:

```c
#include <stdio.h>

int main(void) {
    return puts("Hello from UnDOS!") == EOF ? 1 : 0;
}
```

```sh
i686-undos-gcc hello.c -o hello.elf
```

### Supported-function matrix

| Header | Initial implementation |
|---|---|
| `stdio.h` | `FILE`, `stdin`, `stdout`, `stderr`, `EOF`; `fopen` (read-only `r`/`rb`), `fclose`; `fread`, `fwrite`, `fgetc`/`getc`/`getchar`, one-byte `ungetc`, `fgets`, `fputc`/`putc`/`putchar`, `fputs`, `puts`; `feof`, `ferror`, `clearerr`; `fflush`, `setvbuf`, `setbuf`; `printf`, `fprintf`, `vprintf`, `vfprintf`, `snprintf`, `vsnprintf`, `perror` |
| `stdlib.h` | `malloc`, `calloc`, `realloc`, `free`; `abs`, `labs`, `llabs`; `atoi`, `atol`, `strtol`, `strtoul`; `atexit`, `exit`, `_Exit`; `EXIT_SUCCESS`, `EXIT_FAILURE`; limited `abort`, `getenv`, `system` as described below |
| `string.h` | `memcpy`, `memmove`, `memset`, `memcmp`, `memchr`; `strlen`, `strcmp`, `strncmp`, `strcpy`, `strncpy`, `strcat`, `strncat`, `strchr`, `strrchr`, `strstr`, `strspn`, `strcspn`, `strpbrk`, `strtok`, `strerror` |
| `errno.h` | Process-global `errno`; `EIO`, `EBADF`, `ENOMEM`, `EINVAL`, `EMFILE`, `ERANGE`, `ENOSYS`, `EOVERFLOW` |
| `stdint.h`, `stddef.h`, `stdarg.h`, `limits.h` | Reused from the selected cross-GCC's freestanding builtin headers; the probe checks i386 ILP32, fixed-width integers, 8-bit bytes, and size limits |

Keep this matrix aligned with the implemented and tested function set. This
table is an explicit subset, **not full ISO C or POSIX conformance**.
Unlisted facilities such as `scanf`, `sprintf`, floating conversions,
locale/wide-character I/O, signals, time, threads/TLS, and hosted C++ libraries
are not supplied. `abort` terminates unsuccessfully without cleanup, but has
no SIGABRT delivery. ABI v1's environment is empty: valid `getenv` queries
return null. `system(NULL)` reports no command processor; non-null commands
fail with `ENOSYS`.

### Formatting, streams and errors

Formatting supports `%s`, `%c`, `%%`, `%d`/`%i`, `%u`, `%o`, `%x`/`%X`, and
`%p` (hexadecimal with `0x`, including null). Integer length modifiers
`hh`, `h`, `l`, `ll`, `z`, `t`, `j`, flags `-+ #0`, decimal width/precision,
and `*` width/precision are supported. String precision limits bytes.
Wide strings/characters, floating conversions, and `%n` fail with `ENOSYS`;
unknown or incomplete conversions fail with `EINVAL`. Formatting can emit a
prefix before an error. Success returns the number of bytes produced;
`snprintf` returns the length that would have been written, terminates whenever
capacity is nonzero, and allows `(NULL, 0)` length queries. Output-count overflow
fails with `EOVERFLOW`. There is no legacy `crt_printf` 512-byte limit.

`puts` adds a newline; `fputs` does not. Streams start unbuffered. Output
supports full/line buffering selected before the first I/O; a supplied buffer
must remain valid until close/exit. A null buffer selects up to `BUFSIZ` bytes
of internal storage. Input is unbuffered; requesting buffered input fails with
`ENOSYS`. `fflush(NULL)` flushes all open output streams. Normal return/`exit`
runs callbacks/destructors then flushes output, while `_Exit` bypasses both.
Flush errors set the stream error flag and `errno`; they do not replace the
requested process exit status. `fclose` attempts flush and closes even on error.

Standard handles come from the startup runtime queries, never assumed handles
0/1/2. Missing handles fail with `EBADF`; errors returned by the current
kernel I/O API generally map to `EIO` because it does not distinguish all
filesystem failure reasons. `WriteV2` (syscall 226) preserves `WriteV1` and
reports actual bytes accepted, so stdio retries short writes and fails on
zero-progress writes. The dispatcher accepts `WriteV2` buffers up to
`UNDOS_MAX_MAPPED_BUFFER_SIZE` (64 MiB); the target may accept fewer bytes.
Bulk stdio still splits transfers into requests of at most 4096 bytes. This SDK requires a kernel
with that additive syscall.
The original `crt_*` APIs remain available and unchanged.

`fread`/`fwrite` return complete element counts and reject multiplication
overflow. EOF and error flags are distinct and sticky until `clearerr`;
successful `ungetc` clears EOF. A successful zero-byte backend read marks EOF.
The current stream API cannot distinguish temporarily empty device queues
from EOF, so interactive readiness/blocking is not a libc guarantee.
Use filesystem streams or a controlled input provider for deterministic input.

Only read-only `fopen` is supported: write/append/update modes fail with
`ENOSYS` rather than pretending that VFS creation/truncation guarantees exist.
`fseek`/`ftell` also fail with `ENOSYS`; filesystem mutation, seek semantics,
and POSIX file descriptors/process APIs are not promised. Paths follow the
existing VFS rules (including short filenames); the libc does not create a
working-directory abstraction. VFS read failures propagate through the syscall
status separately from successful EOF or short reads.

The allocator reuses the CRT page arenas/free list, now with 16-byte alignment.
Zero-size allocation returns null; failed growth preserves the original
allocation. Size overflow/allocation failure sets `ENOMEM`. Arenas grow in
at least 64 KiB chunks and remain mapped until process teardown. Stream storage
is a fixed `FOPEN_MAX` (16) slot table with a 256-byte internal buffer per slot;
formatting uses bounded stack buffers rather than allocating a formatted copy.
The initial library is not thread-safe (`errno`, allocator, streams, `strtok`,
and callback tables have process-global state).

### Coverage and terminal integration

`python3 SDK/tests/check-libc.py` compiles the actual library and CRT lifecycle
with controlled I/O/page providers at `-O0` and `-O2`, using an ordinary C test
caller. The library and controlled backend are compiled separately by the C
compiler as strict C11 with pedantic errors; only the existing C++ lifecycle
module uses the C++ compiler. It verifies exact hello-style output/newlines, input/EOF/errors, short
and zero-progress writes, partial flush recovery, truncation and integer limits,
allocation failure/overflow, stream exhaustion, normal-exit callback/flush
ordering, and `_Exit` bypass. It is also registered as hosted CTest
`libc_streams`. Test symbols are renamed to avoid interposing the host libc.
Run the registered test with
`ctest --test-dir <host-build> -R libc_streams --output-on-failure`.

The installed SDK checker builds the standard hello example and a C runtime
probe. Export its input fixture as `libcin.txt`, a supported short filename.
`--scenario sdk` exercises formatting, allocation, file input, the real
versioned write syscall and unavailable stdout in UnDOS. **Valid stdout output
is verified in the controlled-stream harness**, not inferred from debug output.
Program Manager children may lack stdout. The `terminal-stdio` scenario runs the
installed SDK's `terminal` example (`ttytest.elf`) through `run` and exercises
real terminal stdout/stderr, line input, Ctrl+D, and repeated launches.
The separate `terminal-close` scenario closes the terminal while a child waits
for input, keeping each emulator run within 15 seconds. Stage `ttytest.elf` from the SDK
checker alongside the other probe binaries with `--system-files`, then run:

```sh
python3 Tools/progman-smoke.py --output build/terminal-stdio --scenario terminal-stdio
python3 Tools/progman-smoke.py --output build/terminal-close --scenario terminal-close
```

Inspect the captured child-output/response screenshots as well as the serial
markers. No debug-console fallback is used to make `puts` appear successful.
