"""Generates the plain C/C++ struct/enum definition headers (one per
enum/struct/syscall, plus a syscalls.h that includes them all)."""

import os

from .ast_nodes import (
    ArrayType,
    BufferType,
    CType,
    DynArrayType,
    EnumType,
    IDLModule,
    StringType,
    StructType,
    Syscall,
)
from .codegen_common import c_type_for, generate_file_header, make_filename, write_doc_comment


def generate_headers(module: IDLModule, out_h_dir: str):
    # ---- Enums ----
    for enum in module.enums:
        filename = os.path.join(out_h_dir, make_filename(enum))
        with open(filename, "w", encoding="utf-8") as f:
            generate_file_header(f)
            write_doc_comment(f, enum.doc)
            f.write(f"enum {enum.name} {{\n")
            for member in enum.members:
                write_doc_comment(f, member.doc, indent="    ")
                f.write(f"    {enum.name}_{member.name} = {member.value},\n")
            f.write("};\n")

    # ---- Structs ----
    for struct in module.structs:
        filename = os.path.join(out_h_dir, make_filename(struct))
        with open(filename, "w", encoding="utf-8") as f:
            generate_file_header(f)
            write_doc_comment(f, struct.doc)
            f.write(f"struct {struct.name} {{\n")
            for field in struct.fields:
                write_doc_comment(f, field.doc, indent="    ")
                f.write(f"    {c_type_for(field.ftype)} {field.name};\n")
            f.write("};\n")

    # ---- Syscalls ----
    for sc in module.syscalls:
        filename = os.path.join(out_h_dir, make_filename(sc))
        with open(filename, "w", encoding="utf-8") as f:
            generate_file_header(f)
            write_syscall_struct_to_file(f, sc)

    include_all_file = os.path.join(out_h_dir, "syscalls.h")
    with open(include_all_file, "w", encoding="utf-8") as f:
        f.write("#pragma once\n\n")
        for enum in module.enums:
            f.write(f"#include \"{make_filename(enum)}\"\n")
        f.write("\n")
        for struct in module.structs:
            f.write(f"#include \"{make_filename(struct)}\"\n")
        f.write("\n")
        for sc in module.syscalls:
            f.write(f"#include \"{make_filename(sc)}\"\n")


def write_syscall_struct_to_file(file, sc: Syscall):
    struct_name = f"__SYS_{sc.category}_{sc.name.upper()}_{sc.version}"

    # Struct definition
    write_doc_comment(file, sc.doc)
    file.write(f"struct {struct_name} {{\n")
    for field in sc.fields:
        write_doc_comment(file, field.doc, indent="    ")
        if isinstance(field.ftype, StringType):
            if field.direction == "OUT":
                file.write(f"    char* {field.name};\n")
            else:
                file.write(f"    const char* {field.name};\n")
            file.write(f"    uint32_t {field.name}Length;\n")
        elif isinstance(field.ftype, CType):
            file.write(f"    {field.ftype.ctype} {field.name};\n")
        elif isinstance(field.ftype, StructType):
            file.write(f"    struct {field.ftype.name} {field.name};\n")
        elif isinstance(field.ftype, EnumType):
            file.write(f"    enum {field.ftype.name} {field.name};\n")
        elif isinstance(field.ftype, ArrayType):
            file.write(f"    {c_type_for(field.ftype.elem_type)} {field.name}[{field.ftype.size}];\n")
        elif isinstance(field.ftype, DynArrayType):
            file.write(f"    const {c_type_for(field.ftype.elem_type)}* {field.name};\n")
            file.write(f"    uint32_t {field.name}Length;\n")
        elif isinstance(field.ftype, BufferType):
            if field.direction == "OUT":
                file.write(f"    {c_type_for(field.ftype.elem_type)}* {field.name};\n")
            else:
                file.write(f"    const {c_type_for(field.ftype.elem_type)}* {field.name};\n")
            file.write(f"    uint32_t {field.name}Length;\n")

    file.write("};\n\n")
    file.write(f"constexpr size_t   __SYS_{sc.category}_{sc.name.upper()}_{sc.version}_SIZE = sizeof(struct {struct_name});\n")
    file.write(f"constexpr uint32_t __SYS_{sc.category}_{sc.name.upper()}_{sc.version}_CALLNUMBER = {sc.number};\n")
    file.write("\n")
