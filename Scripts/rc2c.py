#!/usr/bin/env python3
"""Convert a Win32 .rc script into an UnDOS resource table (C source).

The script runs the host C preprocessor over the .rc file (with RC_INVOKED
defined), so #include/#ifdef and numeric IDs from the app's headers resolve
as they would under rc.exe. Everything else stays symbolic: style flags,
VK_ codes and ID expressions are written into the generated C as
expressions, and the C compiler evaluates them against <windows.h> and the
app's headers.

Output per resource type:
  MENU          Win32 MENUITEMTEMPLATE (normal, version 0), UTF-16 text.
  DIALOG(EX)    Win32 DLGTEMPLATE / DLGTEMPLATEEX, UTF-16 text.
  ACCELERATORS  ACCEL array.
  STRINGTABLE   One NUL-terminated string per ID (UnDOS LoadString format).
  BITMAP        The .bmp file bytes without the 14-byte BITMAPFILEHEADER (as rc.exe).
  ICON/CURSOR   The .ico/.cur file bytes (not parsed; loaders may ignore).

The result defines `const UNDOS_RESOURCE <symbol>[]` and `<symbol>_count`.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

TYPE_ORDINALS = {"MENU": "RT_MENU", "DIALOG": "RT_DIALOG", "DIALOGEX": "RT_DIALOG",
                 "ACCELERATORS": "RT_ACCELERATOR", "BITMAP": "RT_BITMAP",
                 "ICON": "RT_ICON", "CURSOR": "RT_CURSOR"}
LOAD_OPTIONS = {"PRELOAD", "LOADONCALL", "MOVEABLE", "FIXED", "DISCARDABLE", "PURE", "IMPURE", "SHARED",
                "NONSHARED"}
BLOCK_OPEN = {"BEGIN", "{"}
BLOCK_CLOSE = {"END", "}"}

TOKEN_RE = re.compile(r'''
    (?P<ws>\s+)
  | (?P<str>L?"(?:[^"\\\n]|\\.|"")*")
  | (?P<num>0[xX][0-9A-Fa-f]+[lLuU]*|\d+[lLuU]*)
  | (?P<id>[A-Za-z_][A-Za-z0-9_]*)
  | (?P<op>[|&~+\-*/(),{}<>!^%.:;=\[\]'#\\?@])
''', re.X)


class RcError(Exception):
    pass


def tokenize(text):
    tokens = []
    pos = 0
    for line in text.splitlines():
        if line.lstrip().startswith("#"):
            continue  # pragmas and anything the preprocessor left behind
        pos = 0
        while pos < len(line):
            m = TOKEN_RE.match(line, pos)
            if not m:
                raise RcError("cannot tokenize: %r" % line[pos:])
            pos = m.end()
            if m.lastgroup == "ws":
                continue
            tokens.append((m.lastgroup, m.group()))
    return tokens


def rc_string(token):
    """Decode an rc string literal ("" is a quote; C escapes apply)."""
    body = token[1:] if token.startswith("L") else token
    body = body[1:-1].replace('""', '"')
    out = []
    i = 0
    while i < len(body):
        c = body[i]
        if c == "\\" and i + 1 < len(body):
            n = body[i + 1]
            simple = {"n": "\n", "t": "\t", "r": "\r", "a": "\a", "\\": "\\", '"': '"', "0": "\0"}
            if n in "xX":
                m = re.match(r"[0-9A-Fa-f]{1,4}", body[i + 2:])
                out.append(chr(int(m.group(), 16)))
                i += 2 + len(m.group())
                continue
            if n in simple:
                out.append(simple[n])
                i += 2
                continue
        out.append(c)
        i += 1
    return "".join(out)


def c_literal(text):
    data = text.encode("utf-8")
    parts = []
    for b in data:
        ch = chr(b)
        if ch == "\\":
            parts.append("\\\\")
        elif ch == '"':
            parts.append('\\"')
        elif 32 <= b < 127 and ch != "?":
            parts.append(ch)
        else:
            parts.append("\\%03o" % b)
    return '"' + "".join(parts) + '"'


class Parser:
    def __init__(self, tokens):
        self.t = tokens
        self.i = 0

    def peek(self, k=0):
        j = self.i + k
        return self.t[j] if j < len(self.t) else (None, None)

    def next(self):
        tok = self.peek()
        self.i += 1
        return tok

    def expect(self, value):
        kind, text = self.next()
        if text != value:
            raise RcError("expected %r, got %r near token %d" % (value, text, self.i))

    def accept(self, value):
        if self.peek()[1] == value:
            self.i += 1
            return True
        return False

    def at_end(self):
        return self.i >= len(self.t)

    def expr(self, stop=(",",)):
        """Collect tokens of one expression up to a top-level stop token."""
        depth = 0
        parts = []
        while not self.at_end():
            kind, text = self.peek()
            if depth == 0 and (text in stop or text in BLOCK_OPEN or text in BLOCK_CLOSE and text != ")"):
                break
            if depth == 0 and kind == "str":
                break
            # Two operands in a row: the expression ended at the previous one
            # (rc statements are not terminated, e.g. "IDM_OPEN MENUITEM ...").
            if depth == 0 and parts and (parts[-1] == ")" or re.match(r"\w", parts[-1])) and \
                    (kind in ("id", "num") or text == "("):
                break
            if text == "(":
                depth += 1
            elif text == ")":
                if depth == 0:
                    break
                depth -= 1
            self.next()
            parts.append(text)
        if not parts:
            raise RcError("empty expression near token %d (%r)" % (self.i, self.peek()[1]))
        return " ".join(parts)

    def style_expr(self, base=None):
        """rc style: terms joined by |, each optionally prefixed by NOT."""
        include, exclude = [], []
        if base:
            include.append(base)
        while True:
            negate = self.accept("NOT")
            term = self.term()
            (exclude if negate else include).append(term)
            if not self.accept("|"):
                break
        value = "(" + (" | ".join(include) if include else "0") + ")"
        if exclude:
            value = "(%s & ~(%s))" % (value, " | ".join(exclude))
        return value

    def term(self):
        kind, text = self.next()
        if text == "(":
            depth, parts = 1, ["("]
            while depth:
                k, t = self.next()
                if t is None:
                    raise RcError("unbalanced parenthesis")
                depth += t == "("
                depth -= t == ")"
                parts.append(t)
            return " ".join(parts)
        if text in ("-", "~"):
            return text + self.term()
        if kind not in ("id", "num"):
            raise RcError("bad style term %r" % text)
        return text


class Words:
    """A WORD array whose entries are C expressions."""

    def __init__(self):
        self.items = []

    def word(self, expr):
        self.items.append("RC_W(%s)" % expr)

    def dword(self, expr):
        self.items.append("RC_LO(%s)" % expr)
        self.items.append("RC_HI(%s)" % expr)

    def text(self, value):
        for ch in value:
            cp = ord(ch)
            if cp > 0xFFFF:
                cp -= 0x10000
                self.items.append("0x%04X" % (0xD800 | (cp >> 10)))
                self.items.append("0x%04X" % (0xDC00 | (cp & 0x3FF)))
            else:
                self.items.append("0x%04X" % cp if cp >= 128 or ch in "\\'" else "'%s'" % ch if ch.isprintable() else "0x%04X" % cp)
        self.items.append("0")

    def sz_or_ord(self, value):
        if value is None:
            self.items.append("0")
        elif isinstance(value, str):
            self.text(value)
        else:
            self.items.append("0xFFFF")
            self.word(value[0])

    def align4(self):
        if len(self.items) % 2:
            self.items.append("0")


CONTROL_CLASSES = {"BUTTON": 0x80, "EDIT": 0x81, "STATIC": 0x82, "LISTBOX": 0x83, "SCROLLBAR": 0x84,
                   "COMBOBOX": 0x85}

# keyword: (class, default style, has text, needs size)
SHORTHAND = {
    "LTEXT": ("STATIC", "SS_LEFT | WS_GROUP", True),
    "CTEXT": ("STATIC", "SS_CENTER | WS_GROUP", True),
    "RTEXT": ("STATIC", "SS_RIGHT | WS_GROUP", True),
    "ICON": ("STATIC", "SS_ICON", True),
    "PUSHBUTTON": ("BUTTON", "BS_PUSHBUTTON | WS_TABSTOP", True),
    "DEFPUSHBUTTON": ("BUTTON", "BS_DEFPUSHBUTTON | WS_TABSTOP", True),
    "CHECKBOX": ("BUTTON", "BS_CHECKBOX | WS_TABSTOP", True),
    "AUTOCHECKBOX": ("BUTTON", "BS_AUTOCHECKBOX | WS_TABSTOP", True),
    "STATE3": ("BUTTON", "BS_3STATE | WS_TABSTOP", True),
    "AUTO3STATE": ("BUTTON", "BS_AUTO3STATE | WS_TABSTOP", True),
    "RADIOBUTTON": ("BUTTON", "BS_RADIOBUTTON", True),
    "AUTORADIOBUTTON": ("BUTTON", "BS_AUTORADIOBUTTON", True),
    "GROUPBOX": ("BUTTON", "BS_GROUPBOX", True),
    "EDITTEXT": ("EDIT", "ES_LEFT | WS_BORDER | WS_TABSTOP", False),
    "LISTBOX": ("LISTBOX", "LBS_NOTIFY | WS_BORDER", False),
    "COMBOBOX": ("COMBOBOX", "CBS_SIMPLE | WS_TABSTOP", False),
    "SCROLLBAR": ("SCROLLBAR", "SBS_HORZ", False),
}


class Converter:
    def __init__(self, source_dir, symbol):
        self.source_dir = source_dir
        self.symbol = symbol
        self.blobs = []      # C definitions
        self.entries = []    # (type expr, name expr, data symbol, size expr)
        self.count = 0

    def name_expr(self, text, kind):
        if kind == "str":
            return c_literal(rc_string(text))
        if kind == "num" or text.startswith("("):
            return "MAKEINTRESOURCE(%s)" % text
        return c_literal(text)  # an undefined identifier names the resource by string

    def new_symbol(self, prefix):
        self.count += 1
        return "%s_%s_%d" % (self.symbol, prefix, self.count)

    def add_words(self, rtype, name, words):
        sym = self.new_symbol("tpl")
        rows = ",\n".join("  " + ", ".join(words.items[i:i + 8]) for i in range(0, len(words.items), 8))
        self.blobs.append("static const WORD %s[] __attribute__((aligned(4))) = {\n%s\n};" % (sym, rows))
        self.entries.append((rtype, name, sym, "sizeof(%s)" % sym))

    def add_file(self, rtype, name, filename):
        path = os.path.join(self.source_dir, filename.replace("\\", "/"))
        with open(path, "rb") as f:
            data = f.read()
        if rtype == "RT_BITMAP" and data[:2] == b"BM":
            data = data[14:]  # like rc.exe: RT_BITMAP holds the DIB without BITMAPFILEHEADER
        sym = self.new_symbol("file")
        rows = ",\n".join("  " + ", ".join("0x%02X" % b for b in data[i:i + 16]) for i in range(0, len(data), 16))
        self.blobs.append("/* %s */\nstatic const BYTE %s[] = {\n%s\n};" % (os.path.basename(path), sym, rows))
        self.entries.append((rtype, name, sym, str(len(data))))

    # --- top level -----------------------------------------------------
    def run(self, p):
        while not p.at_end():
            kind, text = p.peek()
            if text == "STRINGTABLE":
                p.next()
                self.skip_options(p)
                self.stringtable(p)
                continue
            if text == "LANGUAGE":
                p.next(); p.term(); p.expect(","); p.term()
                continue
            kind2, text2 = p.peek(1)
            if kind in ("id", "num", "str") and text2 in TYPE_ORDINALS:
                p.next(); p.next()
                name = self.name_expr(text, kind)
                getattr(self, "res_" + text2.lower())(p, name, text2)
                continue
            if kind in ("id", "num") and text2 in ("VERSIONINFO", "RCDATA", "DLGINIT", "TOOLBAR", "MENUEX"):
                p.next(); p.next()
                while p.peek()[1] not in BLOCK_OPEN:
                    p.next()
                self.skip_block(p)
                continue
            p.next()  # C declarations from headers, stray tokens

    def skip_options(self, p):
        while p.peek()[1] in LOAD_OPTIONS:
            p.next()

    def skip_block(self, p):
        if p.next()[1] not in BLOCK_OPEN:
            raise RcError("expected BEGIN")
        depth = 1
        while depth:
            text = p.next()[1]
            if text is None:
                raise RcError("unterminated block")
            depth += text in BLOCK_OPEN
            depth -= text in BLOCK_CLOSE

    def file_resource(self, p, name, rtype):
        self.skip_options(p)
        kind, text = p.next()
        filename = rc_string(text) if kind == "str" else text
        while p.peek()[1] == ".":  # unquoted file name such as wbbitmap.bmp
            p.next()
            filename += "." + p.next()[1]
        self.add_file(TYPE_ORDINALS[rtype], name, filename)

    res_bitmap = file_resource
    res_icon = file_resource
    res_cursor = file_resource

    # --- STRINGTABLE ---------------------------------------------------
    def stringtable(self, p):
        if p.next()[1] not in BLOCK_OPEN:
            raise RcError("expected BEGIN after STRINGTABLE")
        while p.peek()[1] not in BLOCK_CLOSE:
            ident = p.expr()
            p.accept(",")
            kind, text = p.next()
            if kind != "str":
                raise RcError("string expected for ID %s, got %r" % (ident, text))
            value = rc_string(text)
            while p.peek()[0] == "str":
                value += rc_string(p.next()[1])
            sym = self.new_symbol("str")
            self.blobs.append("static const char %s[] = %s;" % (sym, c_literal(value)))
            self.entries.append(("RT_STRING", "MAKEINTRESOURCE(%s)" % ident, sym, "sizeof(%s)" % sym))
        p.next()

    # --- ACCELERATORS --------------------------------------------------
    def res_accelerators(self, p, name, rtype):
        self.skip_options(p)
        if p.next()[1] not in BLOCK_OPEN:
            raise RcError("expected BEGIN after ACCELERATORS")
        rows = []
        while p.peek()[1] not in BLOCK_CLOSE:
            kind, text = p.peek()
            options = set()
            if kind == "str":
                p.next()
                event = rc_string(text)
                if event.startswith("^") and len(event) == 2:
                    key = "%d" % (ord(event[1].upper()) - 0x40)
                elif len(event) == 1:
                    key = "%d" % ord(event)
                else:
                    raise RcError("bad accelerator key %r" % text)
                virtkey_char = event
            else:
                key = p.expr()
                virtkey_char = None
            p.expect(",")
            cmd = p.expr()
            while p.accept(","):
                options.add(p.next()[1])
            if "VIRTKEY" in options and virtkey_char is not None:
                key = "%d" % ord(virtkey_char.upper())
            flags = ["FVIRTKEY" for o in options if o == "VIRTKEY"]
            for o, f in (("SHIFT", "FSHIFT"), ("CONTROL", "FCONTROL"), ("ALT", "FALT"), ("NOINVERT", "FNOINVERT")):
                if o in options:
                    flags.append(f)
            rows.append("  {(BYTE)(%s), (WORD)(%s), (WORD)(%s)}" % (" | ".join(flags) or "0", key, cmd))
        p.next()
        sym = self.new_symbol("accel")
        self.blobs.append("static const ACCEL %s[] = {\n%s\n};" % (sym, ",\n".join(rows)))
        self.entries.append(("RT_ACCELERATOR", name, sym, "sizeof(%s)" % sym))

    # --- MENU ----------------------------------------------------------
    def res_menu(self, p, name, rtype):
        self.skip_options(p)
        words = Words()
        words.word("0")
        words.word("0")
        self.menu_items(p, words)
        self.add_words("RT_MENU", name, words)

    def menu_items(self, p, words):
        if p.next()[1] not in BLOCK_OPEN:
            raise RcError("expected BEGIN in MENU")
        items = []
        while p.peek()[1] not in BLOCK_CLOSE:
            keyword = p.next()[1]
            if keyword == "MENUITEM":
                if p.accept("SEPARATOR"):
                    items.append(("item", [], "0", ""))
                    continue
                text = rc_string(p.next()[1])
                p.expect(",")
                ident = p.expr()
                opts = []
                while p.accept(","):
                    opts.append(p.next()[1])
                items.append(("item", opts, ident, text))
            elif keyword == "POPUP":
                text = rc_string(p.next()[1])
                opts = []
                while p.accept(","):
                    opts.append(p.next()[1])
                sub = Words()
                self.menu_items(p, sub)
                items.append(("popup", opts, sub, text))
            else:
                raise RcError("unexpected %r in MENU" % keyword)
        p.next()
        flag_names = {"CHECKED": "MF_CHECKED", "GRAYED": "MF_GRAYED", "INACTIVE": "MF_DISABLED",
                      "MENUBARBREAK": "MF_MENUBARBREAK", "MENUBREAK": "MF_MENUBREAK", "HELP": "MF_HELP"}
        for index, (kind, opts, payload, text) in enumerate(items):
            flags = [flag_names[o] for o in opts if o in flag_names]
            if kind == "popup":
                flags.append("MF_POPUP")
            if index == len(items) - 1:
                flags.append("MF_END")
            words.word(" | ".join(flags) or "0")
            if kind == "item":
                words.word(payload)
                words.text(text)
            else:
                words.text(text)
                words.items.extend(payload.items)

    # --- DIALOG / DIALOGEX ---------------------------------------------
    def res_dialog(self, p, name, rtype):
        ex = rtype == "DIALOGEX"
        self.skip_options(p)
        x = p.expr(); p.expect(",")
        y = p.expr(); p.expect(",")
        cx = p.expr(); p.expect(",")
        cy = p.expr()
        help_id = "0"
        if ex and p.accept(","):
            help_id = p.expr()
        style, exstyle, caption, font, menu, klass = None, "0", "", None, None, None
        while p.peek()[1] not in BLOCK_OPEN:
            keyword = p.next()[1]
            if keyword == "STYLE":
                style = p.style_expr()
            elif keyword == "EXSTYLE":
                exstyle = p.style_expr()
            elif keyword == "CAPTION":
                caption = rc_string(p.next()[1])
            elif keyword == "FONT":
                size = p.expr(); p.expect(",")
                face = rc_string(p.next()[1])
                extra = []
                while p.accept(","):
                    extra.append(p.expr())
                font = (size, face, extra)
            elif keyword == "MENU":
                k, t = p.next()
                menu = rc_string(t) if k == "str" else [t]
            elif keyword == "CLASS":
                k, t = p.next()
                klass = rc_string(t) if k == "str" else [t]
            elif keyword in ("LANGUAGE", "CHARACTERISTICS", "VERSION"):
                p.expr()
                if p.accept(","):
                    p.expr()
            else:
                raise RcError("unexpected %r in dialog header" % keyword)
        if style is None:
            style = "(WS_POPUP | WS_BORDER | WS_SYSMENU)"
        if caption:
            style = "(%s | WS_CAPTION)" % style
        if font:
            style = "(%s | DS_SETFONT)" % style

        controls = self.dialog_controls(p, ex)
        words = Words()
        if ex:
            words.word("1"); words.word("0xFFFF")
            words.dword(help_id); words.dword(exstyle); words.dword(style)
        else:
            words.dword(style); words.dword(exstyle)
        words.word(str(len(controls)))
        for v in (x, y, cx, cy):
            words.word(v)
        words.sz_or_ord(menu)
        words.sz_or_ord(klass)
        words.text(caption)
        if font:
            size, face, extra = font
            words.word(size)
            if ex:
                weight = extra[0] if len(extra) > 0 else "0"
                italic = extra[1] if len(extra) > 1 else "0"
                charset = extra[2] if len(extra) > 2 else "1"
                words.word(weight)
                words.items.append("RC_W((%s) | ((%s) << 8))" % (italic, charset))
            words.text(face)
        for c in controls:
            words.align4()
            if ex:
                words.dword(c["help"]); words.dword(c["exstyle"]); words.dword(c["style"])
            else:
                words.dword(c["style"]); words.dword(c["exstyle"])
            for v in (c["x"], c["y"], c["cx"], c["cy"]):
                words.word(v)
            if ex:
                words.dword(c["id"])
            else:
                words.word(c["id"])
            words.sz_or_ord(c["class"])
            words.sz_or_ord(c["text"])
            data = c["data"]
            words.word(str(len(data) * 2))
            for d in data:
                words.word(d)
        self.add_words("RT_DIALOG", name, words)

    res_dialogex = res_dialog

    def dialog_controls(self, p, ex):
        if p.next()[1] not in BLOCK_OPEN:
            raise RcError("expected BEGIN in dialog")
        controls = []
        while p.peek()[1] not in BLOCK_CLOSE:
            keyword = p.next()[1]
            c = {"exstyle": "0", "help": "0", "data": []}
            if keyword == "CONTROL":
                c["text"] = self.control_text(p)
                p.expect(",")
                c["id"] = p.expr(); p.expect(",")
                k, t = p.next()
                klass = rc_string(t) if k == "str" else t
                c["class"] = [str(CONTROL_CLASSES[klass.upper()])] if klass.upper() in CONTROL_CLASSES else klass
                p.expect(",")
                c["style"] = p.style_expr("WS_CHILD | WS_VISIBLE")
                p.expect(",")
                self.control_rect(p, c, True)
                if p.accept(","):
                    c["exstyle"] = p.style_expr()
                    if ex and p.accept(","):
                        c["help"] = p.expr()
            elif keyword in SHORTHAND:
                klass, base, has_text = SHORTHAND[keyword]
                c["class"] = [str(CONTROL_CLASSES[klass])]
                c["text"] = self.control_text(p) if has_text else ""
                if has_text:
                    p.expect(",")
                c["id"] = p.expr(); p.expect(",")
                self.control_rect(p, c, keyword != "ICON")
                c["style"] = "(WS_CHILD | WS_VISIBLE | %s)" % base
                if p.accept(","):
                    c["style"] = p.style_expr("WS_CHILD | WS_VISIBLE | " + base)
                    if p.accept(","):
                        c["exstyle"] = p.style_expr()
                        if ex and p.accept(","):
                            c["help"] = p.expr()
            else:
                raise RcError("unexpected %r in dialog body" % keyword)
            if p.peek()[1] == "{":
                p.next()
                while not p.accept("}"):
                    c["data"].append(p.expr(stop=(",", "}")))
                    p.accept(",")
            controls.append(c)
        p.next()
        return controls

    def control_text(self, p):
        kind, text = p.peek()
        if kind == "str":
            p.next()
            return rc_string(text)
        return [p.expr()]  # ordinal, e.g. an icon ID

    def control_rect(self, p, c, sized):
        c["x"] = p.expr(); p.expect(",")
        c["y"] = p.expr()
        c["cx"] = c["cy"] = "0"
        # ICON's size is optional: ICON text, id, x, y [, cx, cy [, style]].
        if sized or p.accept(","):
            if sized:
                p.expect(",")
            c["cx"] = p.expr(); p.expect(",")
            c["cy"] = p.expr()

    # --- output --------------------------------------------------------
    def emit(self, headers, source_name):
        out = ["/* Generated by Scripts/rc2c.py from %s. Do not edit. */" % source_name,
               "#include <windows.h>", "#include <undosgui/extensions.h>"]
        out += ['#include "%s"' % h for h in headers]
        out += ["", "#define RC_W(x) ((WORD)(x))",
                "#define RC_LO(x) ((WORD)((DWORD)(x) & 0xFFFFu))",
                "#define RC_HI(x) ((WORD)(((DWORD)(x) >> 16) & 0xFFFFu))", ""]
        out += self.blobs
        out.append("")
        out.append("const UNDOS_RESOURCE %s[] = {" % self.symbol)
        for rtype, name, sym, size in self.entries:
            out.append("  {%s, %s, %s, (DWORD)(%s)}," % (rtype, name, sym, size))
        out.append("};")
        out.append("const UINT %s_count = sizeof(%s) / sizeof(%s[0]);" % (self.symbol, self.symbol, self.symbol))
        return "\n".join(out) + "\n"


def preprocess(rc_path, includes, defines, extra_headers, cc):
    with open(rc_path, encoding="utf-8-sig") as f:
        text = f.read()
    # rc.exe accepts backslashes in #include paths; the host cpp does not.
    text = re.sub(r'(#\s*include\s*")([^"]*)(")', lambda m: m.group(1) + m.group(2).replace("\\", "/") + m.group(3),
                  text)
    with tempfile.NamedTemporaryFile("w", suffix=".rc.c", delete=False, encoding="utf-8") as tmp:
        for h in extra_headers:
            tmp.write('#include "%s"\n' % h)
        tmp.write('#line 1 "%s"\n' % rc_path)
        tmp.write(text)
        name = tmp.name
    try:
        cmd = [cc, "-E", "-P", "-x", "c", "-w", "-DRC_INVOKED", "-D_WIN32", "-I", os.path.dirname(rc_path)]
        for inc in includes:
            cmd += ["-I", inc]
        for d in defines:
            cmd.append("-D" + d)
        cmd.append(name)
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode:
            raise RcError("preprocessor failed:\n" + result.stderr)
        return result.stdout
    finally:
        os.unlink(name)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rc", help="input .rc file")
    ap.add_argument("-o", "--output", required=True, help="output C file")
    ap.add_argument("-I", dest="includes", action="append", default=[], help="preprocessor include directory")
    ap.add_argument("-D", dest="defines", action="append", default=[], help="preprocessor define")
    ap.add_argument("--pre-include", action="append", default=[],
                    help="header included before the .rc (for IDs the .rc uses without including)")
    ap.add_argument("--header", action="append", default=[], help="header #included by the generated C")
    ap.add_argument("--symbol", default="undos_resources", help="name of the generated table")
    ap.add_argument("--cc", default=os.environ.get("CC", "cc"), help="host C compiler used as preprocessor")
    args = ap.parse_args()
    try:
        text = preprocess(os.path.abspath(args.rc), args.includes, args.defines, args.pre_include, args.cc)
        conv = Converter(os.path.dirname(os.path.abspath(args.rc)), args.symbol)
        conv.run(Parser(tokenize(text)))
        output = conv.emit(args.header, os.path.basename(args.rc))
    except RcError as e:
        sys.stderr.write("rc2c: %s\n" % e)
        return 1
    with open(args.output, "w", encoding="utf-8") as f:
        f.write(output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
