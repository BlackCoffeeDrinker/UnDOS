"""AST node classes for the UnDOS syscall/struct IDL."""

from typing import List, Optional


# ---------- Field types ----------

class FieldType:
    pass


class StringType(FieldType):
    def __repr__(self):
        return "StringType()"


class CType(FieldType):
    def __init__(self, ctype: str):
        self.ctype = ctype

    def __repr__(self):
        return f"CType({self.ctype!r})"


class EnumType(FieldType):
    def __init__(self, name: str):
        self.name = name

    def __repr__(self):
        return f"EnumType({self.name!r})"


class StructType(FieldType):
    def __init__(self, name: str):
        self.name = name

    def __repr__(self):
        return f"StructType({self.name!r})"


class ArrayType(FieldType):
    def __init__(self, elem_type: FieldType, size: int):
        self.elem_type = elem_type
        self.size = size

    def __repr__(self):
        return f"ArrayType({self.elem_type!r}, {self.size})"


class DynArrayType(FieldType):
    def __init__(self, elem_type: FieldType):
        self.elem_type = elem_type

    def __repr__(self):
        return f"DynArrayType({self.elem_type!r})"


class BufferType(FieldType):
    """A large, variable-length buffer that is always mapped (pointer + length),
    never copied and never bounded to a fixed size. Meant for things like draw
    command lists, file read/write buffers, or IPC messages where copying would
    put pressure on a low-RAM machine."""

    def __init__(self, elem_type: FieldType):
        self.elem_type = elem_type

    def __repr__(self):
        return f"BufferType({self.elem_type!r})"


# ---------- Declarations ----------

class Field:
    def __init__(self, name: str, ftype: FieldType, direction: str = "IN",
                 mode: Optional[str] = None, doc: str = ""):
        self.name = name
        self.ftype = ftype
        self.direction = direction  # "IN" or "OUT"
        self.doc = doc  # optional documentation text (from a /** ... */ comment)
        # "COPY" or "MAP". If not specified in the IDL, pick a sensible default:
        # small/predictable-size things (string) are copied by value; buffers
        # (buffer<T>) are always mapped. dynarray keeps its historical mapped
        # behavior for backward compatibility.
        if mode is None:
            if isinstance(ftype, BufferType):
                mode = "MAP"
            elif isinstance(ftype, StringType):
                # OUT strings must write directly into the caller's buffer
                # (query-length-then-fill contract), so they stay mapped.
                # IN strings default to a one-time bounded copy to avoid
                # TOCTOU and keep the kernel from holding a page mapping open.
                mode = "MAP" if direction == "OUT" else "COPY"
            else:
                mode = "MAP"
        self.mode = mode

    def __repr__(self):
        return f"Field({self.name!r}, {self.ftype!r}, {self.direction!r}, {self.mode!r})"


class Syscall:
    def __init__(self, version: str, category: str, name: str, number: int,
                 fields: List[Field], doc: str = ""):
        self.version = version  # "V1"
        self.category = category  # "PROC"
        self.name = name  # "CreateProcess"
        self.number = number  # 200
        self.fields = fields  # List[Field]
        self.doc = doc  # optional documentation text (from a /** ... */ comment)

    def __repr__(self):
        return f"Syscall({self.version!r}, {self.category!r}, {self.name!r}, {self.number}, {self.fields!r})"


class EnumMember:
    def __init__(self, name: str, value: int, doc: str = ""):
        self.name = name
        self.value = value
        self.doc = doc

    def __repr__(self):
        return f"EnumMember({self.name!r}, {self.value})"


class EnumDecl:
    def __init__(self, name: str, members: List[EnumMember], doc: str = ""):
        self.name = name
        self.members = members
        self.doc = doc

    def __repr__(self):
        return f"EnumDecl({self.name!r}, {self.members!r})"


class StructDecl:
    def __init__(self, name: str, fields: List[Field], doc: str = ""):
        self.name = name
        self.fields = fields
        self.doc = doc

    def __repr__(self):
        return f"StructDecl({self.name!r}, {self.fields!r})"


class IDLModule:
    def __init__(self):
        self.syscalls: List[Syscall] = []
        self.enums: List[EnumDecl] = []
        self.structs: List[StructDecl] = []

    def __repr__(self):
        return f"IDLModule(syscalls={self.syscalls!r}, enums={self.enums!r}, structs={self.structs!r})"
