"""Command-line entry point for the UnDOS IDL compiler."""

import os
import sys

from .codegen_headers import generate_headers
from .codegen_kernel import generate_kernel
from .codegen_userspace import generate_userspace
from .parser import parse_idl


def main():
    # Expect exactly 4 arguments:
    #   1. root IDL file
    #   2. output directory for definitions .h files
    #   3. output directory for userspace calling files
    #   4. output directory for kernel receiving files
    if len(sys.argv) != 5:
        print("UnDOS IDL Compiler")
        print("Usage:")
        print("    undos-idl <root.idl> <output-h-directory> <output-userspace-h-directory> <output-kernel-h-directory>")
        print("")
        print("Example:")
        print("    undos-idl kernel.idl ./generated/h/ ./generated/userspace-h/ ./generated/kernel/h")
        sys.exit(1)

    idl_path = sys.argv[1]
    out_h_dir = sys.argv[2]
    out_userspace_h_dir = sys.argv[3]
    out_kernel_h_dir = sys.argv[4]

    print("Generating IDL files...")
    print(f" Output H Directory: {out_h_dir}")
    print(f" Output Userspace H Directory: {out_userspace_h_dir}")
    print(f" Output Kernel H Directory: {out_kernel_h_dir}")

    # Validate input file
    if not os.path.isfile(idl_path):
        print(f"ERROR: IDL file not found: {idl_path}")
        sys.exit(1)

    # Validate or create output directory
    if not os.path.isdir(out_h_dir):
        try:
            os.makedirs(out_h_dir)
        except Exception as e:
            print(f"ERROR: Could not create output directory '{out_h_dir}': {e}")
            sys.exit(1)

    # Load IDL text
    with open(idl_path, "r", encoding="utf-8") as f:
        idl_text = f.read()

    # Parse IDL
    module = parse_idl(idl_text)

    # Generate .h files
    generate_headers(module, out_h_dir)
    generate_userspace(module, out_userspace_h_dir)
    generate_kernel(module, out_kernel_h_dir, "undos/syscalls")

    print(f"Generated {len(module.syscalls)} syscalls, "
          f"{len(module.enums)} enums, "
          f"{len(module.structs)} structs into '{out_h_dir}'")
