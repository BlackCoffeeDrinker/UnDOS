"""Focused regression coverage for public scalar aliases in syscall generation."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from idlgen.ast_nodes import CType
from idlgen.codegen_headers import generate_headers
from idlgen.codegen_kernel import generate_kernel
from idlgen.codegen_userspace import generate_userspace
from idlgen.parser import parse_idl
from idlgen.resolver import ResolverError, resolve_aliases


class ScalarAliasTests(unittest.TestCase):
    def test_scalar_aliases_preserve_names_and_flatten_typedefs(self):
        module = parse_idl("""
            using SECOND = HANDLE;
            using HANDLE = c<uint32_t>;
            using HANDLES = buffer<HANDLE>;
            flags Access : SECOND { Read; };
            struct Holder { handle: HANDLE; };
            syscall V1 TEST Example [1] {
                input: IN SECOND;
                output: OUT HANDLE;
                handles: IN HANDLES;
                fixed: IN array<HANDLE, 2>;
            };
        """)
        resolve_aliases(module)
        fields = module.syscalls[0].fields
        self.assertEqual(fields[0].ftype.ctype, "SECOND")
        self.assertEqual(fields[1].ftype.ctype, "HANDLE")
        self.assertEqual(fields[2].ftype.elem_type.ctype, "HANDLE")
        self.assertEqual(fields[3].ftype.elem_type.ctype, "HANDLE")
        self.assertEqual(module.flags[0].underlying, "uint32_t")
        self.assertEqual(module.usings[0].aliased.ctype, "uint32_t")
        with tempfile.TemporaryDirectory(prefix="undos-idl-") as directory:
            root = Path(directory)
            headers = root / "undos/syscalls"
            headers.mkdir(parents=True)
            generate_headers(module, str(headers))
            generate_userspace(module, str(root))
            generate_kernel(module, str(root), "undos/syscalls")
            aliases = (headers / "__aliases.h").read_text()
            self.assertIn("typedef uint32_t HANDLE;", aliases)
            self.assertIn("typedef uint32_t SECOND;", aliases)
            self.assertNotIn("typedef uint32_t HANDLES;", aliases)
            wire = (headers / "__syscall_Example_V1.h").read_text()
            self.assertIn("#include <undos/syscalls/__aliases.h>", wire)
            self.assertIn("SECOND input;", wire)
            self.assertIn("HANDLE output;", wire)
            self.assertIn("const HANDLE* handles;", wire)
            self.assertIn("HANDLE fixed[2];", wire)
            wrapper = (root / "userspace.h").read_text()
            self.assertIn("SECOND input, HANDLE* output, const HANDLE* handles", wrapper)
            kernel = (root / "__kernel_types.h").read_text()
            self.assertIn("#include <undos/syscalls/__aliases.h>", kernel)
            self.assertIn("HANDLE output;", kernel)
            generated = {path: path.read_bytes() for path in root.rglob("*") if path.is_file()}
            generate_headers(module, str(headers))
            generate_userspace(module, str(root))
            generate_kernel(module, str(root), "undos/syscalls")
            self.assertEqual(generated, {path: path.read_bytes()
                                        for path in root.rglob("*") if path.is_file()})
            # The typedef header must be usable by ordinary C and C++ callers.
            source = '#include <undos/syscalls/__aliases.h>\n'
            source += 'typedef char handle_size[sizeof(HANDLE) == 4 ? 1 : -1];\n'
            source += 'HANDLE echo(HANDLE value) { return value; }\n'
            for compiler, language, standard in (("cc", "c", "c11"), ("c++", "c++", "c++20")):
                subprocess.run([compiler, "-x", language, f"-std={standard}",
                                "-Wall", "-Wextra", "-Werror", "-pedantic-errors",
                                "-fsyntax-only", f"-I{root}", "-"],
                               input=source, text=True, check=True)

    def test_invalid_aliases_still_fail(self):
        for text in ("using A = B; using B = A;", "using A = Missing;",
                     "using A = string; flags F : A { One; };"):
            with self.subTest(text=text), self.assertRaises(ResolverError):
                resolve_aliases(parse_idl(text))

    def test_object_handles_keep_existing_wire_types(self):
        root = Path(__file__).resolve().parents[2]
        text = (root / "tools/syscalls.txt").read_text()
        module = parse_idl(text, base_dir=str(root / "tools"))
        resolve_aliases(module)
        old = parse_idl(text.replace("using HANDLE = c<uint32_t>;", "")
                       .replace(": IN HANDLE;", ": IN c<uint32_t>;")
                       .replace(": OUT HANDLE;", ": OUT c<uint32_t>;"),
                       base_dir=str(root / "tools"))
        resolve_aliases(old)
        for syscall, previous in zip(module.syscalls, old.syscalls):
            self.assertEqual(syscall.number, previous.number)
            self.assertEqual(len(syscall.fields), len(previous.fields))
            for field, before in zip(syscall.fields, previous.fields):
                self.assertEqual((field.name, field.direction, field.mode),
                                 (before.name, before.direction, before.mode))
                actual = CType("uint32_t") if isinstance(field.ftype, CType) and field.ftype.ctype == "HANDLE" else field.ftype
                self.assertEqual(repr(actual), repr(before.ftype))


if __name__ == "__main__":
    unittest.main()
