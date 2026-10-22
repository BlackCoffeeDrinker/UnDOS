"""Command-line entry point for the UnDOS IDL compiler."""

import argparse
import os

from .codegen_common import make_filename
from .codegen_headers import generate_headers
from .codegen_kernel import generate_kernel
from .codegen_userspace import generate_userspace
from .parser import ParserError, parse_idl
from .resolver import ResolverError, resolve_aliases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list-outputs", action="store_true",
                        help="List output paths for CMake without generating files")
    parser.add_argument("idl_path")
    parser.add_argument("out_h_dir")
    parser.add_argument("out_userspace_h_dir")
    parser.add_argument("out_kernel_h_dir")
    args = parser.parse_args()
    idl_path = args.idl_path
    out_h_dir = args.out_h_dir
    out_userspace_h_dir = args.out_userspace_h_dir
    out_kernel_h_dir = args.out_kernel_h_dir

    # Load IDL text
    with open(idl_path, "r", encoding="utf-8") as f:
        idl_text = f.read()

    # Parse IDL (relative `include "...";` directives are resolved against
    # the root file's own directory)
    try:
        module = parse_idl(idl_text, base_dir=os.path.dirname(os.path.abspath(idl_path)))
    except ParserError as e:
        parser.error(str(e))

    # Resolve `using` aliases before any codegen runs
    try:
        resolve_aliases(module)
    except ResolverError as e:
        parser.error(str(e))

    if args.list_outputs:
        outputs = [os.path.join(out_h_dir, name) for name in ("__aliases.h", "syscalls.h")]
        outputs += [os.path.join(out_h_dir, make_filename(item))
                    for item in (*module.enums, *module.flags, *module.structs, *module.syscalls)]
        outputs.append(os.path.join(out_userspace_h_dir, "userspace.h"))
        outputs += [os.path.join(out_kernel_h_dir, name)
                    for name in ("__kernel_types.h", "syscalls.hpp", "handler_syscall.cpp")]
        print("\n".join(path.replace(os.sep, "/") for path in outputs))
        return

    print("Generating IDL files...")
    print(f" Output H Directory: {out_h_dir}")
    print(f" Output Userspace H Directory: {out_userspace_h_dir}")
    print(f" Output Kernel H Directory: {out_kernel_h_dir}")
    for directory in (out_h_dir, out_userspace_h_dir, out_kernel_h_dir):
        os.makedirs(directory, exist_ok=True)

    # Generate .h files
    generate_headers(module, out_h_dir)
    generate_userspace(module, out_userspace_h_dir)
    generate_kernel(module, out_kernel_h_dir, "undos/syscalls")

    print(f"Generated {len(module.syscalls)} syscalls, "
          f"{len(module.enums)} enums, "
          f"{len(module.structs)} structs into '{out_h_dir}'")
