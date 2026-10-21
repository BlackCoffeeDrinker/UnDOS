"""Recursive-descent parser for the UnDOS syscall/struct IDL."""

import os
from typing import List, Optional, Set

from .ast_nodes import (
    AliasType,
    ArrayType,
    BufferType,
    CType,
    DynArrayType,
    EnumDecl,
    EnumMember,
    EnumType,
    Field,
    FieldType,
    FlagMember,
    FlagsDecl,
    FlagsType,
    IDLModule,
    StringType,
    StructDecl,
    StructType,
    Syscall,
    UsingDecl,
)
from .tokenizer import Token, clean_doc_comment, tokenize


class ParserError(Exception):
    pass


class Parser:
    def __init__(self, tokens: List[Token], base_dir: str = ".", visited: Optional[Set[str]] = None):
        self.tokens = tokens
        self.pos = 0
        # Directory used to resolve relative `include "...";` paths
        # (the directory of the file currently being parsed).
        self.base_dir = base_dir
        # Absolute paths of every file already included anywhere in this
        # parse, shared across nested includes so a file can't be pulled in
        # twice (and diamond/cyclic includes don't recurse forever).
        self.visited = visited if visited is not None else set()

    def current(self) -> Optional[Token]:
        if self.pos < len(self.tokens):
            return self.tokens[self.pos]
        return None

    def match(self, kind: str) -> Token:
        tok = self.current()
        if tok is None or tok.kind != kind:
            raise ParserError(f"Expected {kind}, got {tok}")
        self.pos += 1
        return tok

    def try_match(self, kind: str) -> Optional[Token]:
        tok = self.current()
        if tok is not None and tok.kind == kind:
            self.pos += 1
            return tok
        return None

    def take_pending_doc(self) -> str:
        """Consumes and returns a documentation comment (/** ... */)
        immediately preceding the current position, if any."""
        tok = self.current()
        if tok is not None and tok.kind == "DOCCOMMENT":
            self.pos += 1
            return clean_doc_comment(tok.value)
        return ""

    # Field/parameter names that happen to spell a keyword (most notably
    # "flags", already widely used as a plain field name for enum<Name>
    # bitmask fields before `flags`/`using` were introduced) must still be
    # accepted as identifiers in name position.
    _IDENT_LIKE_KEYWORDS = ("FLAGS", "USING")

    def match_ident_like(self) -> Token:
        tok = self.current()
        if tok is not None and (tok.kind == "IDENT" or tok.kind in self._IDENT_LIKE_KEYWORDS):
            self.pos += 1
            return tok
        raise ParserError(f"Expected IDENT, got {tok}")

    def parse_syscall(self, doc: str = "") -> Syscall:
        self.match("SYSCALL")

        version_tok = self.match("V")
        version = version_tok.value

        category_tok = self.match("IDENT")
        category = category_tok.value

        name_tok = self.match("IDENT")
        name = name_tok.value

        self.match("LBRACKET")
        number_tok = self.match("NUMBER")
        number = int(number_tok.value)
        self.match("RBRACKET")

        provider = ""
        tok = self.current()
        if tok is not None and tok.kind == "IDENT" and tok.value == "provider":
            self.match("IDENT")
            provider = self.match("IDENT").value

        self.match("LBRACE")
        fields = self.parse_field_list()
        self.match("RBRACE")
        self.match("SEMICOLON")

        return Syscall(version, category, name, number, fields, doc, provider)

    def parse_module(self) -> IDLModule:
        module = IDLModule()
        while self.current() is not None:
            doc = self.take_pending_doc()
            tok = self.current()
            if tok is None:
                break
            if tok.kind == "SYSCALL":
                module.syscalls.append(self.parse_syscall(doc))
            elif tok.kind == "ENUM":
                module.enums.append(self.parse_enum(doc))
            elif tok.kind == "STRUCT":
                module.structs.append(self.parse_struct_decl(doc))
            elif tok.kind == "FLAGS":
                module.flags.append(self.parse_flags(doc))
            elif tok.kind == "USING":
                module.usings.append(self.parse_using(doc))
            elif tok.kind == "INCLUDE":
                included = self.parse_include()
                module.syscalls.extend(included.syscalls)
                module.enums.extend(included.enums)
                module.structs.extend(included.structs)
                module.flags.extend(included.flags)
                module.usings.extend(included.usings)
            else:
                raise ParserError(f"Unexpected token at top level: {tok}")
        return module

    # ---- include ----

    def parse_include(self) -> "IDLModule":
        """Parses `include "path";`, resolves `path` relative to the
        directory of the file currently being parsed, and recursively
        parses it, returning its declarations as an IDLModule to be merged
        into the including module. Already-included files (tracked in
        `self.visited`, shared across the whole include tree) are skipped
        and yield an empty module, so a file can be included more than once
        (e.g. via two different includers) without duplicating anything."""
        self.match("INCLUDE")
        path_tok = self.match("STRING")
        self.match("SEMICOLON")

        raw_path = path_tok.value[1:-1]  # strip surrounding quotes
        resolved_path = os.path.normpath(os.path.join(self.base_dir, raw_path))

        if resolved_path in self.visited:
            return IDLModule()
        self.visited.add(resolved_path)

        if not os.path.isfile(resolved_path):
            raise ParserError(f"Included file not found: {resolved_path}")

        with open(resolved_path, "r", encoding="utf-8") as f:
            included_text = f.read()

        included_tokens = tokenize(included_text)
        included_parser = Parser(included_tokens, base_dir=os.path.dirname(resolved_path), visited=self.visited)
        return included_parser.parse_module()

    def parse_field_list(self):
        fields = []
        while True:
            doc = self.take_pending_doc()
            tok = self.current()
            if tok is None or tok.kind == "RBRACE":
                break

            name_tok = self.match_ident_like()
            self.match("COLON")

            direction = "IN"
            if self.try_match("OUT"):
                direction = "OUT"
            elif self.try_match("IN"):
                direction = "IN"

            mode = None
            if self.try_match("COPY"):
                mode = "COPY"
            elif self.try_match("MAP"):
                mode = "MAP"

            ftype = self.parse_field_type()
            self.match("SEMICOLON")

            fields.append(Field(name_tok.value, ftype, direction, mode, doc))

        return fields

    # ---- syscall V1 PROC Name [number] { fields }; ----

    def parse_field_type(self) -> FieldType:
        tok = self.current()
        if tok is None:
            raise ParserError("Unexpected end of input in field type")

        # string
        if tok.kind == "STRINGKW":
            self.match("STRINGKW")
            return StringType()

        # c<...>
        if tok.kind == "CKW":
            raw = tok.value  # e.g. "c<uint32_t>"
            inner = raw[2:-1].strip()  # strip "c<" and ">"
            self.match("CKW")
            return CType(inner)

        # enum<...>
        if tok.kind == "ENUMKW":
            raw = tok.value  # e.g. "enum<ProcessFlags>"
            inner = raw[5:-1].strip()  # strip "enum<" and ">"
            self.match("ENUMKW")
            return EnumType(inner)

        # struct<...>
        if tok.kind == "STRUCTKW":
            raw = tok.value  # e.g. "struct<ProcessInfo>"
            inner = raw[7:-1].strip()  # strip "struct<" and ">"
            self.match("STRUCTKW")
            return StructType(inner)

        # array<T, N>
        if tok.kind == "ARRAYKW":
            self.match("ARRAYKW")  # "array<"
            elem_type = self.parse_field_type()
            self.match("COMMA")
            size_tok = self.match("NUMBER")
            size = int(size_tok.value)
            self.match("GT")
            return ArrayType(elem_type, size)

        # dynarray<T>
        if tok.kind == "DYNARRAYKW":
            self.match("DYNARRAYKW")  # "dynarray<"
            elem_type = self.parse_field_type()
            self.match("GT")
            return DynArrayType(elem_type)

        # buffer<T>
        if tok.kind == "BUFFERKW":
            self.match("BUFFERKW")  # "buffer<"
            elem_type = self.parse_field_type()
            self.match("GT")
            return BufferType(elem_type)

        # flags<...>
        if tok.kind == "FLAGSKW":
            raw = tok.value  # e.g. "flags<DCInitFlags>"
            inner = raw[6:-1].strip()  # strip "flags<" and ">"
            self.match("FLAGSKW")
            return FlagsType(inner)

        # Bare identifier: assume it's a `using`-declared alias name. Its
        # validity is checked later by the alias-resolution pass, not here.
        if tok.kind == "IDENT":
            self.match("IDENT")
            return AliasType(tok.value)

        raise ParserError(f"Unknown field type token: {tok}")

    def parse_field_type_inside_angle(self) -> FieldType:
        # Reuse parse_field_type but expect CKW/STRINGKW/etc.
        tok = self.current()
        if tok is None:
            raise ParserError("Unexpected end of input in angle-bracket type")

        if tok.kind == "STRINGKW":
            self.match("STRINGKW")
            return StringType()

        if tok.kind == "CKW":
            self.match("CKW")
            ctype_tok = self.match("IDENT")
            ctype = ctype_tok.value
            self.match("GT")
            return CType(ctype)

        if tok.kind == "ENUMKW":
            self.match("ENUMKW")
            name_tok = self.match("IDENT")
            name = name_tok.value
            self.match("GT")
            return EnumType(name)

        if tok.kind == "STRUCT":
            self.match("STRUCT")
            self.match("LT")
            name_tok = self.match("IDENT")
            name = name_tok.value
            self.match("GT")
            return StructType(name)

        raise ParserError(f"Unsupported type inside angle brackets: {tok}")

    # ---- enum ----

    def parse_enum(self, doc: str = "") -> EnumDecl:
        self.match("ENUM")
        name_tok = self.match("IDENT")
        name = name_tok.value

        self.match("LBRACE")
        members = []
        while True:
            member_doc = self.take_pending_doc()
            tok = self.current()
            if tok is None or tok.kind == "RBRACE":
                break
            ident_tok = self.match("IDENT")
            self.match("EQ")
            value = 0
            hexnumber = self.try_match("HEXNUMBER")
            if hexnumber:
                value = int(hexnumber.value, 16)
            else:
                value_tok = self.match("NUMBER")
                value = int(value_tok.value)
            members.append(EnumMember(ident_tok.value, value, member_doc))
            if self.try_match("COMMA"):
                continue
            else:
                break
        self.match("RBRACE")
        self.match("SEMICOLON")
        return EnumDecl(name, members, doc)

    # ---- struct ----

    def parse_struct_decl(self, doc: str = "") -> StructDecl:
        self.match("STRUCT")
        name_tok = self.match("IDENT")
        name = name_tok.value

        self.match("LBRACE")
        fields = self.parse_field_list()
        self.match("RBRACE")
        self.match("SEMICOLON")
        return StructDecl(name, fields, doc)

    # ---- flags ----

    def parse_flags(self, doc: str = "") -> FlagsDecl:
        self.match("FLAGS")
        name_tok = self.match("IDENT")
        name = name_tok.value

        self.match("COLON")
        underlying_tok = self.match("IDENT")
        underlying = underlying_tok.value

        self.match("LBRACE")
        members = []
        while True:
            member_doc = self.take_pending_doc()
            tok = self.current()
            if tok is None or tok.kind == "RBRACE":
                break
            ident_tok = self.match("IDENT")
            self.match("SEMICOLON")
            members.append(FlagMember(ident_tok.value, member_doc))
        self.match("RBRACE")
        self.match("SEMICOLON")
        return FlagsDecl(name, underlying, members, doc)

    # ---- using ----

    def parse_using(self, doc: str = "") -> UsingDecl:
        self.match("USING")
        name_tok = self.match("IDENT")
        name = name_tok.value

        self.match("EQ")
        aliased = self.parse_field_type()
        self.match("SEMICOLON")
        return UsingDecl(name, aliased, doc)


# ---------- Convenience API ----------

def parse_idl(text: str, base_dir: str = ".") -> IDLModule:
    tokens = tokenize(text)
    parser = Parser(tokens, base_dir=base_dir)
    return parser.parse_module()
