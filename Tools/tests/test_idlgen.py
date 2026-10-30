"""Focused regression coverage for public scalar aliases in syscall generation."""

from pathlib import Path
import shutil
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
    def test_empty_output_buffers_reach_handler_without_empty_mapping(self):
        module = parse_idl("""
            syscall V1 TEST Output [1] { values: OUT buffer<c<uint32_t>>; };
            syscall V1 TEST Input [2] { values: IN buffer<c<uint32_t>>; };
            syscall V1 TEST Text [3] { value: OUT string; };
        """)
        with tempfile.TemporaryDirectory(prefix="undos-idl-") as directory:
            generate_kernel(module, directory, "undos/syscalls")
            dispatch = (Path(directory) / "handler_syscall.cpp").read_text()
            output = dispatch.split("static kstd::error_code Dispatch_Output_V1(")[1].split(
                "static kstd::error_code Dispatch_Input_V1(")[0]
            self.assertNotIn("valuesLength == 0", output)
            self.assertIn("headerCopy.values != nullptr && headerCopy.valuesLength > 0", output)
            self.assertIn("valuesLength > UNDOS_MAX_MAPPED_BUFFER_SIZE / sizeof(uint32_t)", output)
            self.assertIn("if (!mapped) return mapped.error();", output)
            mapping = output.index("if (headerCopy.values != nullptr && headerCopy.valuesLength > 0)")
            self.assertLess(output.index("args.values_capacity = headerCopy.valuesLength;"), mapping)
            self.assertGreater(output.index("args.values_len = headerCopy.valuesLength;"), mapping)
            self.assertIn("size_t values_capacity", (Path(directory) / "__kernel_types.h").read_text())
            self.assertIn("if (headerCopy.valuesLength == 0) return kernel::KernelError::InvalidArgument;",
                          dispatch.split("static kstd::error_code Dispatch_Input_V1(")[1])
            self.assertIn("if (headerCopy.valueLength == 0) return kernel::KernelError::InvalidArgument;",
                          dispatch.split("static kstd::error_code Dispatch_Text_V1(")[1])

    def test_input_buffers_forward_element_counts_to_handlers(self):
        module = parse_idl("""
            syscall V1 TEST Input [1] {
                values: IN buffer<c<uint32_t>>;
            };
        """)
        with tempfile.TemporaryDirectory(prefix="undos-idl-") as directory:
            root = Path(directory)
            generate_kernel(module, str(root), "undos/syscalls")
            dispatch = (root / "handler_syscall.cpp").read_text()
            assignment = "args.values_len = headerCopy.valuesLength;"
            self.assertIn(assignment, dispatch)
            self.assertIn("headerCopy.valuesLength > UNDOS_MAX_MAPPED_BUFFER_SIZE / sizeof(uint32_t)",
                          dispatch)
            self.assertLess(dispatch.index(assignment),
                            dispatch.index("handle_Input_V1(kstd::move(args))"))
            self.assertIn("size_t values_len", (root / "__kernel_types.h").read_text())

    def test_cli_lists_complete_outputs_and_creates_build_directories(self):
        script = Path(__file__).resolve().parents[1] / "create-syscalls.py"
        with tempfile.TemporaryDirectory(prefix="undos-idl-") as directory:
            root = Path(directory)
            idl = root / "example.txt"
            idl.write_text("""
                using HANDLE = c<uint32_t>;
                enum Kind { File = 1, };
                flags Access : uint32 { Read; };
                struct Info { handle: HANDLE; };
                syscall V1 TEST Example [1] { info: OUT struct<Info>; };
            """)
            output = root / "build with spaces"
            command = [sys.executable, str(script), str(idl),
                       str(output / "include/undos/syscalls"), str(output / "include"),
                       str(output / "kernel/syscalls")]
            listed = subprocess.check_output([*command, "--list-outputs"], text=True).splitlines()
            self.assertFalse(output.exists())
            subprocess.run(command, check=True, capture_output=True)
            generated = {path.as_posix(): path.read_bytes()
                         for path in output.rglob("*") if path.is_file()}
            self.assertEqual(set(listed), set(generated))
            subprocess.run(command, check=True, capture_output=True)
            self.assertEqual(generated, {path.as_posix(): path.read_bytes()
                                         for path in output.rglob("*") if path.is_file()})

    def test_cmake_generates_incrementally_and_tracks_idl_changes(self):
        repository = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory(prefix="undos-idl-cmake-") as directory:
            root = Path(directory)
            for name in ("BuildSupport", "Tools/idlgen", "ABI/IDL"):
                shutil.copytree(repository / name, root / name,
                                ignore=shutil.ignore_patterns("__pycache__"))
            shutil.copy2(repository / "Tools/create-syscalls.py", root / "Tools")
            (root / "CMakeLists.txt").write_text("""
                cmake_minimum_required(VERSION 3.27)
                project(SyscallGenerationTest LANGUAGES CXX)
                set(TESTING_ONLY ON)
                include(BuildSupport/Project.cmake)
                add_executable(probe probe.cpp)
                target_include_directories(probe PRIVATE "${UNDOS_SYSCALL_INCLUDE_DIR}")
                add_dependencies(probe undos-syscalls-gen)
            """)
            (root / "probe.cpp").write_text(
                '#include <undos/syscalls/__aliases.h>\n'
                'static_assert(sizeof(HANDLE) == 4);\nint main() { return 0; }\n')
            build = root / "build with spaces"

            def run(*args):
                result = subprocess.run(args, text=True, stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT)
                self.assertEqual(result.returncode, 0, result.stdout)

            run("cmake", "-S", str(root), "-B", str(build))
            run("cmake", "--build", str(build), "--parallel", "2")
            generated = build / "generated/syscalls"
            original = {p: p.stat().st_mtime_ns for p in generated.rglob("*") if p.is_file()}
            run("cmake", "--build", str(build))
            self.assertEqual(original, {p: p.stat().st_mtime_ns for p in original})
            missing = generated / "include/undos/syscalls/__aliases.h"
            missing.unlink()
            run("cmake", "--build", str(build))
            self.assertTrue(missing.exists())
            idl = root / "ABI/IDL/syscalls.txt"
            idl.write_text(idl.read_text() + "\nsyscall V1 TEST BuildProbe [9999] { };\n")
            run("cmake", "--build", str(build))
            self.assertTrue((generated / "include/undos/syscalls/__syscall_BuildProbe_V1.h").is_file())
            self.assertIn("UD_SYS_BuildProbeV1", (generated / "include/userspace.h").read_text())
            self.assertFalse((root / "ABI/userspace.h").exists())
            self.assertFalse((root / "ABI/include/undos/syscalls").exists())
            self.assertFalse((root / "Kernel").exists())

    def test_kernel_errors_are_converted_only_at_public_dispatch(self):
        module = parse_idl("""
            syscall V1 TEST Example [1] {
                value: OUT c<uint32_t>;
            };
        """)
        with tempfile.TemporaryDirectory(prefix="undos-idl-") as directory:
            root = Path(directory)
            generate_kernel(module, str(root), "undos/syscalls")
            declarations = (root / "syscalls.hpp").read_text()
            self.assertIn("kstd::error_code handle_Example_V1(", declarations)
            dispatch = (root / "handler_syscall.cpp").read_text()
            self.assertIn("static kstd::error_code Dispatch_Example_V1(", dispatch)
            self.assertIn("return kernel::KernelError::InvalidSize;", dispatch)
            self.assertIn("return kernel::KernelError::NoCurrentThread;", dispatch)
            self.assertIn("return kernel::KernelError::NoCurrentProcess;", dispatch)
            self.assertIn("const kstd::error_code ret = handle_Example_V1(", dispatch)
            self.assertIn("if (!mapped) [[unlikely]] return mapped.error();", dispatch)
            self.assertIn("if (const auto error = KE_VMM_CopyToUserspace(", dispatch)
            self.assertIn("; error) return error;", dispatch)
            self.assertNotIn("kernel::KernelError handle_", declarations)
            self.assertNotIn("static kernel::KernelError Dispatch_", dispatch)
            self.assertIn("case 1: return kernel::KErrorToSysReturnCode(Dispatch_Example_V1(", dispatch)
            self.assertNotIn("return SyscallReturnCode_INVALID_", dispatch)

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
        text = (root / "ABI/IDL/syscalls.txt").read_text()
        module = parse_idl(text, base_dir=str(root / "ABI/IDL"))
        resolve_aliases(module)
        old = parse_idl(text.replace("using HANDLE = c<uint32_t>;", "")
                       .replace(": IN HANDLE;", ": IN c<uint32_t>;")
                       .replace(": OUT HANDLE;", ": OUT c<uint32_t>;"),
                       base_dir=str(root / "ABI/IDL"))
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
