"""Shared helpers used by every code generator module."""

from .ast_nodes import CType, EnumDecl, StringType, StructDecl, Syscall


def c_type_for(ftype):
    if isinstance(ftype, CType):
        return ftype.ctype
    if isinstance(ftype, StringType):
        return "char"
    raise Exception(f"Unsupported type for array/dynarray element: {ftype}")


def make_filename(thing):
    if isinstance(thing, EnumDecl):
        return f"__enum_{thing.name}.h"
    if isinstance(thing, StructDecl):
        return f"__struct_{thing.name}.h"
    if isinstance(thing, Syscall):
        return f"__syscall_{thing.name}_V{thing.version[1:]}.h"
    raise Exception(f"Unsupported type for filename: {thing}")


def wire_struct_name(sc: Syscall) -> str:
    return f"__SYS_{sc.category}_{sc.name.upper()}_{sc.version}"


def kernel_struct_name(sc: Syscall) -> str:
    return f"__KRN_{sc.category}_{sc.name.upper()}_{sc.version}"


def generate_file_header(f):
    f.write("#pragma once\n\n")
    f.write("// Generated from UnDOS IDL\n\n")
    f.write("#include <stdint.h>\n")
    f.write("#include <stddef.h>\n\n")


def write_doc_comment(f, doc: str, indent: str = ""):
    """Writes `doc` (possibly multi-line, possibly empty) as a Doxygen-style
    `/** ... */` comment block above whatever follows. No-op if `doc` is empty."""
    if not doc:
        return
    lines = doc.split("\n")
    if len(lines) == 1:
        f.write(f"{indent}/** {lines[0]} */\n")
        return
    f.write(f"{indent}/**\n")
    for line in lines:
        f.write(f"{indent} * {line}\n")
    f.write(f"{indent} */\n")
