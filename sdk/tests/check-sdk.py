#!/usr/bin/env python3
"""Build installed examples after relocating the SDK, then inspect their ABI."""

import argparse
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


def run(*args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)


def inspect(path, expected_type):
    data = path.read_bytes()
    if data[:7] != b"\x7fELF\x01\x01\x01":
        raise RuntimeError(f"{path}: not little-endian ELF32")
    kind, machine = struct.unpack_from("<HH", data, 16)
    if (kind, machine) != (expected_type, 3):
        raise RuntimeError(f"{path}: wrong ELF type or architecture")
    phoff = struct.unpack_from("<I", data, 28)[0]
    phsize, phcount = struct.unpack_from("<HH", data, 42)
    segments = [
        struct.unpack_from("<8I", data, phoff + i * phsize)
        for i in range(phcount)
    ]
    if any(p[0] in (3, 7) for p in segments):
        raise RuntimeError(f"{path}: unexpected PT_INTERP or PT_TLS")
    if expected_type == 2 and min(p[2] for p in segments if p[0] == 1) < 0x10000:
        raise RuntimeError(f"{path}: application maps below 64 KiB")
    matches = 0
    for segment in segments:
        if segment[0] != 4:
            continue
        offset, limit = segment[1], segment[1] + segment[4]
        while offset < limit:
            namesz, descsz, note_type = struct.unpack_from("<3I", data, offset)
            offset += 12
            name = data[offset:offset + namesz]
            offset += (namesz + 3) & ~3
            descriptor = data[offset:offset + descsz]
            offset += (descsz + 3) & ~3
            if name == b"UnDOS\0":
                if note_type != 1 or descriptor != struct.pack("<2I", 1, 0):
                    raise RuntimeError(f"{path}: incompatible ABI note")
                matches += 1
    if matches != 1:
        raise RuntimeError(f"{path}: expected exactly one ABI note")
    print(f"ABI verified: {path.name}")


def check_drivers(sdk, root, output):
    cc = str(sdk / "bin/i686-undos-gcc")
    cxx = str(sdk / "bin/i686-undos-g++")
    work = root / "driver build"
    work.mkdir()
    source = sdk / "share/undos/examples"
    run(cc, str(source / "hello/main.c"), "-o", str(work / "hello.elf"))
    inspect(work / "hello.elf", 2)
    run(cc, str(source / "terminal/main.c"), "-o", str(work / "ttytest.elf"))
    inspect(work / "ttytest.elf", 2)

    # Non-linking modes must not introduce entry objects, libraries or notes.
    hello = str(source / "hello/main.c")
    run(cc, "-E", hello, "-o", str(work / "hello.i"))
    run(cc, "-S", hello, "-o", str(work / "hello.s"))
    run(cc, "-M", hello, "-MF", str(work / "hello.d"))
    run(cc, "-c", hello, "-o", str(work / "hello.o"))
    undefined = run("i686-elf-nm", "-u", str(work / "hello.o"),
                    capture_output=True, text=True).stdout
    if "crt_start" in undefined or "_start" in undefined:
        raise RuntimeError("Compile-only driver mode injected startup code")
    run(cc, "-r", str(work / "hello.o"), "-o", str(work / "partial.o"))
    run(cc, str(work / "partial.o"), "-o", str(work / "partial.elf"))
    inspect(work / "partial.elf", 2)
    run(cc, "-fsyntax-only", hello)

    # Archive order and user quoting survive response-file expansion.
    (work / "value.c").write_text("int archived_value(void) { return 73; }\n")
    (work / "main.c").write_text(
        "int archived_value(void);\nint main(void) { return archived_value() != 73; }\n")
    run(cc, "-c", str(work / "value.c"), "-o", str(work / "value.o"))
    run("i686-elf-ar", "rcs", str(work / "libvalue.a"), str(work / "value.o"))
    response = work / "arguments.rsp"
    response.write_text(
        f'"{work / "main.c"}" "{work / "libvalue.a"}" -o "{work / "archive.elf"}"\n')
    run(cc, f"@{response}")
    inspect(work / "archive.elf", 2)

    # Explicit low-level modes omit the normal startup/default archives.
    run(cc, "-nostdlib", str(work / "hello.o"), "-Wl,-e,0",
        "-Wl,--unresolved-symbols=ignore-all", "-o", str(work / "raw.elf"))
    sections = run("i686-elf-readelf", "-S", str(work / "raw.elf"),
                   capture_output=True, text=True).stdout
    if ".note.undos" in sections:
        raise RuntimeError("-nostdlib unexpectedly injected an ABI note")
    for option, has_start, has_libraries in (
        ("-nostartfiles", False, True),
        ("-nodefaultlibs", True, False),
        ("-nostdlib", False, False),
    ):
        trace = run(cc, "-###", option, hello,
                    capture_output=True, text=True).stderr
        if ("undos-abi-note.o" in trace) != has_start or ("-lundoscrt" in trace) != has_libraries:
            raise RuntimeError(f"{option} did not suppress the expected defaults")
    run(cc, "--undos-raw", "-dumpmachine")
    failed = subprocess.run([cc, "-c", str(work / "missing.c")], capture_output=True)
    original = subprocess.run(["i686-elf-gcc", "-c", str(work / "missing.c")],
                              capture_output=True)
    if failed.returncode == 0 or failed.returncode != original.returncode:
        raise RuntimeError("Compiler failure was not propagated")
    (work / "hosted.cpp").write_text("#include <vector>\n")
    hosted = subprocess.run([cxx, "-E", str(work / "hosted.cpp")], capture_output=True)
    if hosted.returncode == 0:
        raise RuntimeError("Driver exposed hosted C++ standard-library headers")

    # Shared construction, C wrappers and cleanup use the same installed SDK.
    probe = source / "probe"
    run(cxx, "-O2", "-g", "-shared", str(probe / "library.cpp"),
        "-o", str(work / "sdkmsg.so"))
    run(cc, "-O2", "-g", "-c", str(probe / "query.c"), "-o", str(work / "query.o"))
    run(cc, "-O0", "-g", "-c", str(probe / "libc.c"), "-o", str(work / "libc.o"))
    run(cxx, "-O2", "-g", "-c", str(probe / "main.cpp"), "-o", str(work / "main.o"))
    run(cxx, str(work / "main.o"), str(work / "query.o"), str(work / "libc.o"),
        str(work / "sdkmsg.so"), "-lundos_font_resource", "-o", str(work / "sdkcheck.elf"))
    inspect(work / "sdkcheck.elf", 2)
    inspect(work / "sdkmsg.so", 3)
    dynamic = run("i686-elf-readelf", "-d", str(work / "sdkcheck.elf"),
                  capture_output=True, text=True).stdout
    if "Shared library: [sdkmsg.so]" not in dynamic:
        raise RuntimeError("Driver did not provide a basename-only shared-library SONAME")
    if output:
        output.mkdir(parents=True, exist_ok=True)
        for name in ("sdkcheck.elf", "sdkmsg.so", "ttytest.elf"):
            shutil.copy2(work / name, output / name)
        (output / "libcin.txt").write_text("input 73\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--output", type=Path, help="Copy the runtime probe ELF files here")
    args = parser.parse_args()
    sdk = args.sdk.resolve()
    with tempfile.TemporaryDirectory(prefix="undos-sdk-") as directory:
        root = Path(directory)
        relocated = root / "relocated sdk"
        shutil.copytree(sdk, relocated)
        if (relocated / "sysroot/usr/include/kernel").exists():
            raise RuntimeError("SDK must not install kernel-private headers")
        toolchain = relocated / "share/cmake/UnDOS/UnDOS-toolchain.cmake"
        environment = dict(os.environ, CMAKE_TOOLCHAIN_FILE=str(toolchain))
        for example in ("hello", "shared", "gui", "probe", "terminal"):
            source = relocated / "share/undos/examples" / example
            build = root / f"{example} build"
            run("cmake", "-S", str(source), "-B", str(build),
                "-DCMAKE_BUILD_TYPE=Debug", env=environment)
            run("cmake", "--build", str(build), "--parallel", "2", env=environment)
            executable = "sdkcheck" if example == "probe" else example
            inspect(build / f"{executable}.elf", 2)
            if example == "probe":
                inspect(build / "sdkmsg.so", 3)
                if args.output:
                    args.output.mkdir(parents=True, exist_ok=True)
                    for name in ("sdkcheck.elf", "sdkmsg.so"):
                        shutil.copy2(build / name, args.output / name)
            if example == "shared":
                inspect(build / "message.so", 3)
                dynamic = run("i686-elf-readelf", "-d", str(build / "shared.elf"),
                              capture_output=True, text=True).stdout
                if "Shared library: [message.so]" not in dynamic:
                    raise RuntimeError("Expected basename-only message.so dependency")
                symbols = run("i686-elf-readelf", "--dyn-syms", "--wide",
                              str(build / "shared.elf"), capture_output=True, text=True).stdout
                for symbol in ("__cxa_atexit", "__cxa_finalize"):
                    if symbol not in symbols:
                        raise RuntimeError(f"Missing exported runtime symbol {symbol}")
        check_drivers(relocated, root, args.output)
    print("Installed SDK relocation, C/C++ examples, shared library and ABI checks passed")


if __name__ == "__main__":
    main()
