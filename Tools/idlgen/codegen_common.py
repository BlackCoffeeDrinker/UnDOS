"""Shared helpers used by every code generator module."""

from .ast_nodes import CType, EnumDecl, EnumType, FlagsDecl, FlagsType, StringType, StructDecl, StructType, Syscall

# Maps the short underlying-type names accepted in `flags NAME : uint32 { ... };`
# to their concrete C storage type. Names that are already a raw C type (e.g.
# an already-suffixed "uint32_t", resolved from a `using` alias) pass through
# unchanged.
_FLAGS_UNDERLYING_CTYPE_MAP = {
    "uint8": "uint8_t",
    "uint16": "uint16_t",
    "uint32": "uint32_t",
    "uint64": "uint64_t",
}


def flags_underlying_ctype(underlying: str) -> str:
    return _FLAGS_UNDERLYING_CTYPE_MAP.get(underlying, underlying)


def c_type_for(ftype):
    if isinstance(ftype, CType):
        return ftype.ctype
    if isinstance(ftype, StringType):
        return "char"
    if isinstance(ftype, StructType):
        return f"struct {ftype.name}"
    if isinstance(ftype, (EnumType, FlagsType)):
        return f"enum {ftype.name}"
    raise Exception(f"Unsupported type for array/dynarray element: {ftype}")


def make_filename(thing):
    if isinstance(thing, EnumDecl):
        return f"__enum_{thing.name}.h"
    if isinstance(thing, FlagsDecl):
        return f"__flags_{thing.name}.h"
    if isinstance(thing, StructDecl):
        return f"__struct_{thing.name}.h"
    if isinstance(thing, Syscall):
        return f"__syscall_{thing.name}_V{thing.version[1:]}.h"
    raise Exception(f"Unsupported type for filename: {thing}")


def wire_struct_name(sc: Syscall) -> str:
    return f"__SYS_{sc.category}_{sc.name.upper()}_{sc.version}"


def kernel_struct_name(sc: Syscall) -> str:
    return f"__KRN_{sc.category}_{sc.name.upper()}_{sc.version}"


def generate_file_header(f, include_aliases=False):
    f.write("#pragma once\n\n")
    f.write("// Generated from UnDOS IDL\n\n")
    f.write("#include <stdint.h>\n")
    f.write("#include <stddef.h>\n\n")
    if include_aliases:
        f.write("#include <undos/syscalls/__aliases.h>\n\n")


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
