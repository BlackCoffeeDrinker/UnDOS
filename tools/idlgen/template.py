class Template:
    def __init__(self, f: file, type: str = ""):
        self.f = f
        self.type = type
        self.f.write("// Generated from UnDOS IDL\n\n")
        if self.type == "header":
            self.f.write("#pragma once\n\n")

    def __write(self, s: str):
        self.f.write(s)

    def write_doc(self, doc: str, indent: str = ""):
        """Writes `doc` (possibly multi-line, possibly empty) as a Doxygen-style
        `/** ... */` comment block above whatever follows. No-op if `doc` is empty."""
        if not doc:
            return

        lines = doc.split("\n")
        if len(lines) == 1:
            self.__write(f"{indent}/** {lines[0]} */\n")
            return

        self.__write(f"{indent}/**\n")
        for line in lines:
            self.__write(f"{indent} * {line}\n")
        self.__write(f"{indent} */\n")

    def write_newline(self):
        self.__write("\n")

    def write_include_system(self, filename):
        self.__write(f"#include <{filename}>\n")

    def write_include_local(self, filename):
        self.__write(f"#include \"{filename}\"\n")

    def write_constant(self, type: str, name: str, value: str):
        self.__write(f"constexpr {type} {name} = {value};\n")

    def begin_syscall_struct(self, name: str, type: str = ""):
        self.__write(f"struct {name} {{\n")
    def end_syscall_struct(self):
        self.__write("};\n\n")

    def write_kernel_field(self, type, name, extra="", default_value=""):
        after = ""
        if len(extra) > 0:
            after = f" {extra}"
        if len(default_value) > 0:
            after += f" = {default_value}"
        self.__write(f"    {type} {name}{after};\n")

    def write_kernel_borrowed_ptr(self, type, name):
        self.write_kernel_field(f"kernel::borrowed_ptr<{type}>", name, default_value="nullptr")

    def write_kernel_copy_string(self, name: str):
        self.write_kernel_field("kstd::string_view", name, extra="{}")

    def write_kernel_mapped_array_type(self, type: str, name: str):
        self.write_kernel_borrowed_ptr(type, name)
        self.write_kernel_field("size_t", f"{name}_len", default_value="0")

    def write_kernel_struct(self, struct_name: str, name: str):
        self.write_kernel_field(f"struct {struct_name}", name)

    def write_kernel_enum(self, enum_type: str, name: str):
        self.write_kernel_field(f"enum {enum_type}", name)

    def write_kernel_array(self, array_of_type: str, array_name: str, max_elements: str):
        self.write_kernel_field(f"kstd::array<{array_of_type}, {max_elements}>", array_name)

    def write_kernel_handle_def(self, name: str, version: int, struct_name: str):
        self.__write(f"uint32_t handle_{name}_{version}({struct_name}&& args, uint32_t flags);\n")
