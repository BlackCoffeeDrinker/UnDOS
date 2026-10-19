#!/usr/bin/env python3
"""UnDOS application defaults over the SDK's i686-elf GCC toolchain."""

import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys


def expand_response_files(arguments, depth=0):
    if depth > 16:
        raise ValueError("response files are nested more than 16 levels")
    expanded = []
    for argument in arguments:
        if argument.startswith("@"):
            text = Path(argument[1:]).read_text()
            expanded.extend(expand_response_files(shlex.split(text, comments=False), depth + 1))
        else:
            expanded.append(argument)
    return expanded


def main():
    name = Path(sys.argv[0]).name
    compiler_name = "i686-elf-g++" if name.endswith("g++") else "i686-elf-gcc"
    compiler = shutil.which(compiler_name)
    if not compiler:
        raise ValueError(f"{compiler_name} was not found on PATH")
    arguments = expand_response_files(sys.argv[1:])
    if "--undos-raw" in arguments:
        os.execv(compiler, [compiler, *[arg for arg in arguments if arg != "--undos-raw"]])

    sdk = Path(__file__).resolve().parent.parent
    sysroot = sdk / "sysroot"
    library = sysroot / "usr/lib"
    if not (library / "libundoscrt.a").is_file():
        raise ValueError(f"SDK runtime is missing from {library}")

    informational = not arguments or any(
        arg in ("--version", "-dumpmachine", "-dumpversion", "-dumpfullversion",
                "-print-search-dirs", "-print-libgcc-file-name", "--help", "-dumpspecs")
        or arg.startswith(("-print-file-name=", "-print-prog-name=", "--help="))
        for arg in arguments
    )
    if informational:
        os.execv(compiler, [compiler, *arguments])

    # Keep host libc/libstdc++ headers out, but retain GCC's freestanding headers.
    includes = subprocess.run([compiler, "-print-file-name=include"], check=True,
                              capture_output=True, text=True).stdout.strip()
    if not Path(includes).is_dir():
        raise ValueError(f"compiler builtin include directory does not exist: {includes}")
    defaults = [
        f"--sysroot={sysroot}", "-m32", "-march=i386", "-ffreestanding",
        "-fno-stack-protector", "-nostdinc", "-isystem", includes,
        "-isystem", str(sysroot / "usr/include"),
    ]
    if compiler_name.endswith("g++"):
        defaults.extend(["-fno-exceptions", "-fno-rtti", "-fuse-cxa-atexit"])

    linker_arguments = []
    for index, argument in enumerate(arguments):
        if argument.startswith("-Wl,"):
            linker_arguments.extend(argument[4:].split(","))
        elif argument == "-Xlinker" and index + 1 < len(arguments):
            linker_arguments.append(arguments[index + 1])
    shared = "-shared" in arguments or any(
        arg in ("-shared", "--shared") for arg in linker_arguments)
    defaults.extend(["-fPIC", "-fvisibility=default"] if shared else ["-fno-pic", "-fno-pie"])
    compile_only = any(arg in ("-c", "-S", "-E", "-M", "-MM", "-fsyntax-only")
                       for arg in arguments)
    partial = "-r" in arguments or any(
        arg in ("-r", "--relocatable") for arg in linker_arguments)
    if compile_only:
        os.execv(compiler, [compiler, *defaults, *arguments])
    if partial:
        os.execv(compiler, [compiler, *defaults, "-nostdlib", *arguments])

    link_defaults = ["-nostdlib", "-Wl,--hash-style=sysv", "-Wl,--no-dynamic-linker",
                     "-L", str(library)]
    start = []
    end = []
    if "-nostdlib" not in arguments and "-nostartfiles" not in arguments:
        start.append(str(library / "undos-abi-note.o"))
        start.append(str(library / ("undos-dso.o" if shared else "crt0.o")))
    if shared:
        # Bare-metal GCC's specs do not forward -shared to ld.
        link_defaults.append("-Wl,-shared")
        if not any(arg in ("-soname", "--soname", "-h") or arg.startswith("--soname=")
                   for arg in linker_arguments):
            output = "a.out"
            for index, argument in enumerate(arguments):
                if argument == "-o" and index + 1 < len(arguments):
                    output = arguments[index + 1]
                elif argument.startswith("-o") and len(argument) > 2:
                    output = argument[2:]
            link_defaults.extend(["-Xlinker", "-soname", "-Xlinker", Path(output).name])
    else:
        link_defaults.extend(["-no-pie", "-Wl,--export-dynamic"])
        custom_script = any(arg == "--script" or arg.startswith(("-T", "--script="))
                            for arg in [*arguments, *linker_arguments])
        if not custom_script:
            link_defaults.extend(["-Xlinker", "-T", "-Xlinker", str(library / "application.ld")])
    if "-nostdlib" not in arguments and "-nodefaultlibs" not in arguments:
        if not shared:
            end.append("-lundoscrt")
        end.append("-lgcc")
    os.execv(compiler, [compiler, *defaults, *link_defaults, *start, *arguments, *end])


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"{Path(sys.argv[0]).name}: error: {error}", file=sys.stderr)
        sys.exit(1)
