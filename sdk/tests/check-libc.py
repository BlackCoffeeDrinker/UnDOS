#!/usr/bin/env python3
"""Exercise the actual freestanding libc against controlled streams and memory."""

import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--cxx", default="c++")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    crt = root / "userland/crt"
    # Do not interpose the host process's own libc or its startup allocator.
    symbols = """
        malloc calloc realloc free abs labs llabs atoi atol strtol strtoul getenv system abort
        memcpy memmove memset memcmp strlen strcmp strncmp strcpy strncpy strcat strchr strrchr
        memchr strncat strstr strspn strcspn strpbrk strtok strerror
        stdin stdout stderr fopen fclose fflush setvbuf setbuf feof ferror clearerr
        fgetc getc getchar ungetc fgets fputc putc putchar fputs puts fread fwrite fseek ftell
        printf fprintf vprintf vfprintf snprintf vsnprintf perror
        exit _Exit atexit __cxa_atexit __cxa_finalize __dso_handle environ
    """.split()
    flags = ["-fno-builtin", "-Wall", "-Wextra", "-Werror", "-pedantic-errors",
             f"-I{crt / 'libc/include'}", f"-I{crt / 'include'}",
             f"-I{root / 'kernel_userland_shared/include'}",
             *[f"-D{name}=undos_test_{name}" for name in symbols]]
    sources = [crt / "heap.c", crt / "string.c",
               *[crt / "libc" / f"{name}.c" for name in ("stdlib", "string", "stdio", "format")],
               root / "sdk/tests/libc-backend.c"]
    with tempfile.TemporaryDirectory(prefix="undos-libc-") as directory:
        build = Path(directory)
        for optimize in ("-O0", "-O2"):
            process = build / "process.o"
            lifecycle = [
                "-Dmain=undos_test_app_main",
                "-D__preinit_array_start=undos_test_preinit_start",
                "-D__preinit_array_end=undos_test_preinit_end",
                "-D__init_array_start=undos_test_init_start",
                "-D__init_array_end=undos_test_init_end",
                "-D__fini_array_start=undos_test_fini_start",
                "-D__fini_array_end=undos_test_fini_end",
            ]
            subprocess.run([args.cxx, "-std=c++20", optimize, *flags, *lifecycle,
                            "-c", str(crt / "process.cpp"), "-o", str(process)], check=True)
            test = build / "test.o"
            subprocess.run([args.cc, "-std=c11", optimize, *flags, "-c",
                            str(root / "sdk/tests/libc-test.c"), "-o", str(test)], check=True)
            objects = []
            for index, source in enumerate(sources):
                obj = build / f"libc-{index}.o"
                subprocess.run([args.cc, "-std=c11", optimize, *flags, "-c",
                                str(source), "-o", str(obj)], check=True)
                objects.append(str(obj))
            executable = build / "libc-test"
            subprocess.run([args.cxx, "-std=c++20", "-fno-exceptions", "-fno-rtti", optimize,
                            *objects, str(test), str(process),
                            str(root / "sdk/tests/libc-lifecycle.S"), "-o", str(executable)], check=True)
            result = subprocess.run([str(executable)])
            if result.returncode:
                raise RuntimeError(f"{optimize} libc test failed near line {result.returncode} "
                                   "(exit status is line number modulo 256)")
            print(f"Controlled-stream libc tests passed ({optimize})")


if __name__ == "__main__":
    main()
