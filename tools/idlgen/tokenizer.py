"""Tokenizer for the UnDOS syscall/struct IDL."""

import re
from typing import List

# ---------- Tokenizer ----------

TOKEN_REGEX = [
    ("WHITESPACE", r"[ \t\r\n]+"),

    # Documentation comments: /** ... */ immediately preceding a
    # syscall/enum/struct/field declaration become that declaration's doc
    # string. Must come before the generic block/line comment patterns.
    ("DOCCOMMENT", r"/\*\*(?s:.*?)\*/"),

    # Regular (non-doc) comments are discarded.
    ("BLOCKCOMMENT", r"/\*(?s:.*?)\*/"),
    ("COMMENT", r"//[^\n]*"),

    ("SYSCALL", r"\bsyscall\b"),
    ("V", r"\bV[0-9]+\b"),

    # Field direction tokens (must come before IDENT)
    ("IN", r"\bIN\b"),
    ("OUT", r"\bOUT\b"),

    # Field transfer-mode tokens: COPY (copy small data by value, no mapping)
    # or MAP (map user memory once and hand the kernel a pointer+length view).
    # Must come before IDENT for the same reason as IN/OUT.
    ("COPY", r"\bCOPY\b"),
    ("MAP", r"\bMAP\b"),

    # Combined forms FIRST
    ("CKW", r"c<\s*[A-Za-z_][A-Za-z0-9_]*\s*>"),
    ("ENUMKW", r"enum<\s*[A-Za-z_][A-Za-z0-9_]*\s*>"),
    ("STRUCTKW", r"struct<\s*[A-Za-z_][A-Za-z0-9_]*\s*>"),

    ("ARRAYKW", r"\barray<"),
    ("DYNARRAYKW", r"\bdynarray<"),
    ("BUFFERKW", r"\bbuffer<"),

    # Base keywords and identifiers
    ("ENUM", r"\benum\b"),
    ("STRUCT", r"\bstruct\b"),
    ("STRINGKW", r"\bstring\b"),
    ("IDENT", r"[A-Za-z_][A-Za-z0-9_]*"),
    ("NUMBER", r"[0-9]+"),

    # Punctuation
    ("LBRACE", r"\{"),
    ("RBRACE", r"\}"),
    ("LBRACKET", r"\["),
    ("RBRACKET", r"\]"),
    ("LPAREN", r"\("),
    ("RPAREN", r"\)"),
    ("COLON", r":"),
    ("SEMICOLON", r";"),
    ("COMMA", r","),
    ("EQ", r"="),
    ("LT", r"<"),
    ("GT", r">"),
]

MASTER_REGEX = re.compile("|".join(f"(?P<{name}>{pattern})" for name, pattern in TOKEN_REGEX))


class Token:
    def __init__(self, kind: str, value: str, pos: int):
        self.kind = kind
        self.value = value
        self.pos = pos

    def __repr__(self):
        return f"Token({self.kind}, {self.value!r}, {self.pos})"


def clean_doc_comment(raw: str) -> str:
    """Strips the /** and */ delimiters and any leading '*' line decoration
    from a doc comment, returning the plain documentation text."""
    text = raw.strip()
    if text.startswith("/**"):
        text = text[3:]
    if text.endswith("*/"):
        text = text[:-2]
    lines = []
    for line in text.split("\n"):
        line = line.strip()
        if line.startswith("*"):
            line = line[1:].strip()
        lines.append(line)
    # Drop leading/trailing blank lines produced by the delimiters.
    while lines and lines[0] == "":
        lines.pop(0)
    while lines and lines[-1] == "":
        lines.pop()
    return "\n".join(lines)


def tokenize(text: str) -> List[Token]:
    tokens = []
    for m in MASTER_REGEX.finditer(text):
        kind = m.lastgroup
        value = m.group()
        pos = m.start()
        if kind in ("WHITESPACE", "COMMENT", "BLOCKCOMMENT"):
            continue
        tokens.append(Token(kind, value, pos))
    return tokens
