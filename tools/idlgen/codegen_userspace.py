"""Generates the userspace syscall wrapper functions (userspace.h)."""

import os

from .ast_nodes import (
    ArrayType,
    BufferType,
    CType,
    DynArrayType,
    EnumType,
    FlagsType,
    IDLModule,
    StringType,
    StructType,
)
from .codegen_common import c_type_for, generate_file_header, write_doc_comment


def generate_userspace(module: IDLModule, out_h_dir: str):
    filename = os.path.join(out_h_dir, "userspace.h")
    with open(filename, "w", encoding="utf-8") as f:
        generate_file_header(f)
        f.write("#include <undos/syscalls.h>\n")
        f.write("\n")
        
        # No libc (<string.h>) here: userspace wrappers must also work in
        # freestanding/nostdlib callers (e.g. the ELF loader), so string
        # length/copy are done with plain inline loops instead.

        for sc in module.syscalls:
            struct_name = f"__SYS_{sc.category}_{sc.name.upper()}_{sc.version}"

            # Wrapper function
            wrapper_name = f"UD_SYS_{sc.name}{sc.version}"
            write_doc_comment(f, sc.doc)
            f.write(f"UNDOS_SYSCALL_DEF uint32_t {wrapper_name}(")

            # Build argument list
            args = []
            for field in sc.fields:
                is_out = field.direction == "OUT"
                if isinstance(field.ftype, StringType):
                    if is_out:
                        args.append(f"char* {field.name}")
                        args.append(f"uint32_t* {field.name}Length")
                    else:
                        args.append(f"const char* {field.name}")
                elif isinstance(field.ftype, CType):
                    if is_out:
                        args.append(f"{field.ftype.ctype}* {field.name}")
                    else:
                        args.append(f"{field.ftype.ctype} {field.name}")
                elif isinstance(field.ftype, StructType):
                    if is_out:
                        args.append(f"struct {field.ftype.name}* {field.name}")
                    else:
                        args.append(f"const struct {field.ftype.name}* {field.name}")
                elif isinstance(field.ftype, (EnumType, FlagsType)):
                    if is_out:
                        args.append(f"enum {field.ftype.name}* {field.name}")
                    else:
                        args.append(f"enum {field.ftype.name} {field.name}")
                elif isinstance(field.ftype, ArrayType):
                    args.append(f"const {c_type_for(field.ftype.elem_type)}* {field.name}")
                elif isinstance(field.ftype, DynArrayType):
                    args.append(f"const {c_type_for(field.ftype.elem_type)}* {field.name}")
                    args.append(f"uint32_t {field.name}Length")
                elif isinstance(field.ftype, BufferType):
                    if is_out:
                        args.append(f"{c_type_for(field.ftype.elem_type)}* {field.name}")
                        args.append(f"uint32_t* {field.name}Length")
                    else:
                        args.append(f"const {c_type_for(field.ftype.elem_type)}* {field.name}")
                        args.append(f"uint32_t {field.name}Length")

            f.write(", ".join(args))
            f.write(")\n{\n")


            inline_fields = [
                field for field in sc.fields
                if isinstance(field.ftype, StringType) and field.mode == "COPY" and field.direction == "IN"
            ]

            f.write(f"    uint32_t ret = 0;\n")

            f.write(f"    struct {struct_name} __wire;\n")

            for field in sc.fields:
                if field in inline_fields:
                    f.write(f"    __wire.{field.name} = {field.name};\n")
                    f.write(f"    __wire.{field.name}Length = {field.name} ? 1 : 0;\n")
                    f.write(f"     while ({field.name}[__wire.{field.name}Length]) __wire.{field.name}Length++;\n")
                    continue
                if field.direction == "OUT":
                    if isinstance(field.ftype, StringType):
                        f.write(f"    __wire.{field.name} = {field.name};\n")
                        f.write(f"    __wire.{field.name}Length = {field.name}Length ? *{field.name}Length : 0;\n")
                    elif isinstance(field.ftype, DynArrayType):
                        f.write(f"    __wire.{field.name} = {field.name};\n")
                        f.write(f"    __wire.{field.name}Length = {field.name}Length;\n")
                    elif isinstance(field.ftype, BufferType):
                        f.write(f"    __wire.{field.name} = {field.name};\n")
                        f.write(f"    __wire.{field.name}Length = {field.name}Length ? *{field.name}Length : 0;\n")
                    continue
                if isinstance(field.ftype, StringType):
                    f.write(f"    {{ uint32_t __l = 0; while ({field.name}[__l]) __l++; __wire.{field.name}Length = __l; }}\n")
                    f.write(f"    __wire.{field.name} = {field.name};\n")
                elif isinstance(field.ftype, CType):
                    f.write(f"    __wire.{field.name} = {field.name};\n")
                elif isinstance(field.ftype, StructType):
                    f.write(f"    __wire.{field.name} = *{field.name};\n")
                elif isinstance(field.ftype, (EnumType, FlagsType)):
                    f.write(f"    __wire.{field.name} = {field.name};\n")
                elif isinstance(field.ftype, ArrayType):
                    f.write(f"    for (uint32_t __i = 0; __i < sizeof(__wire.{field.name}); __i++) ((uint8_t*)__wire.{field.name})[__i] = ((const uint8_t*){field.name})[__i];\n")
                elif isinstance(field.ftype, DynArrayType):
                    f.write(f"    __wire.{field.name} = {field.name};\n")
                    f.write(f"    __wire.{field.name}Length = {field.name}Length;\n")
                elif isinstance(field.ftype, BufferType):
                    f.write(f"    __wire.{field.name} = {field.name};\n")
                    f.write(f"    __wire.{field.name}Length = {field.name}Length;\n")

            # Syscall assembly
            size_arg = "__size" if inline_fields else f"sizeof(struct {struct_name})"
            f.write("\n    asm volatile(\n")
            f.write('        "int $0x80"\n')
            f.write("        : \"=a\"(ret)\n")
            f.write(f"        : \"a\"({sc.number}), \"b\"(sizeof __wire), \"c\"(&__wire), \"d\"(0)\n")
            f.write("        : \"memory\");\n\n")

            # Copy back OUT fields
            for field in sc.fields:
                if field.direction != "OUT":
                    continue
                if isinstance(field.ftype, StringType):
                    f.write(f"    if ({field.name}Length) *{field.name}Length = __wire.{field.name}Length;\n")
                elif isinstance(field.ftype, (CType, StructType, EnumType, FlagsType)):
                    f.write(f"    *{field.name} = __wire.{field.name};\n")
                elif isinstance(field.ftype, ArrayType):
                    f.write(f"    for (uint32_t __i = 0; __i < sizeof(__wire.{field.name}); __i++) ((uint8_t*){field.name})[__i] = ((const uint8_t*)__wire.{field.name})[__i];\n")
                elif isinstance(field.ftype, DynArrayType):
                    f.write(f"    /* user must copy __wire.{field.name} manually */\n")
                elif isinstance(field.ftype, BufferType):
                    f.write(f"    /* buffer was mapped and filled by the kernel directly; report actual length written */\n")
                    f.write(f"    if ({field.name}Length) *{field.name}Length = __wire.{field.name}Length;\n")

            f.write("    return ret;\n")
            f.write("}\n\n")
