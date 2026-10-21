"""Generates the kernel-side decoupled argument structs (__kernel_types.h)
and the syscall dispatch table (handler_syscall.cpp)."""

import os

from .ast_nodes import ArrayType, BufferType, CType, DynArrayType, EnumType, FlagsType, IDLModule, StringType, StructType
from .codegen_common import c_type_for, kernel_struct_name, make_filename, wire_struct_name
from .template import Template


def generate_kernel_types(module: IDLModule, out_h_dir: str, out_h_syscall_dir: str):
    """Generates the kernel-only struct definitions that decouple the kernel-side
    view of syscall arguments from the wire (userspace) struct.

    Fields whose mode is MAP (large buffers, and OUT strings that must be
    filled in place) are represented with kernel::borrowed_ptr so that the
    sub-mapping of user memory is automatically released (leak-proof) and the
    kernel processes the buffer directly without an extra copy.

    Fields whose mode is COPY (small, predictable-size things like IN
    strings) are laid out contiguously right after the wire struct by the
    caller, so no extra mapping/allocation is needed: the field becomes a
    non-owning kstd::string_view directly into the wire mapping that is
    already held for the duration of the dispatch call."""
    filename = os.path.join(out_h_dir, "__kernel_types.h")
    with open(filename, "w", encoding="utf-8") as f:
        template = Template(f, type="header")

        template.write_include_system("stdint.h")
        template.write_include_system("undos/syscalls/__aliases.h")
        template.write_include_system("kernel/memory/borrowed_ptr.hpp")
        template.write_include_system("string_view.hpp")

        for enum in module.enums:
            enum_filename = os.path.join(out_h_syscall_dir, make_filename(enum))
            template.write_include_system(enum_filename)
        template.write_newline()

        for flags in module.flags:
            flags_filename = os.path.join(out_h_syscall_dir, make_filename(flags))
            template.write_include_system(flags_filename)
        template.write_newline()

        for struct in module.structs:
            struct_filename = os.path.join(out_h_syscall_dir, make_filename(struct))
            template.write_include_system(struct_filename)
        template.write_newline()

        for sc in module.syscalls:
            template.write_doc(sc.doc)
            template.begin_syscall_struct(kernel_struct_name(sc), type="kernel")

            for field in sc.fields:
                template.write_doc(field.doc, indent="    ")
                if isinstance(field.ftype, StringType):
                    if field.mode == "COPY":
                        template.write_kernel_field("kstd::string", field.name, extra='{}')
                    else:
                        template.write_kernel_mapped_array_type('char', field.name)
                elif isinstance(field.ftype, CType):
                    template.write_kernel_field(field.ftype.ctype, field.name)
                elif isinstance(field.ftype, StructType):
                    template.write_kernel_struct(field.ftype.name, field.name)
                elif isinstance(field.ftype, EnumType):
                    template.write_kernel_enum(field.ftype.name, field.name)
                elif isinstance(field.ftype, FlagsType):
                    template.write_kernel_enum(field.ftype.name, field.name)
                elif isinstance(field.ftype, ArrayType):
                    template.write_kernel_array(c_type_for(field.ftype.elem_type), field.name, str(field.ftype.size))
                elif isinstance(field.ftype, DynArrayType):
                    template.write_kernel_borrowed_ptr(c_type_for(field.ftype.elem_type), field.name)
                elif isinstance(field.ftype, BufferType):
                    template.write_kernel_mapped_array_type(c_type_for(field.ftype.elem_type), field.name)
            template.end_syscall_struct()


def generate_kernel_handle_definition(module: IDLModule, out_h_dir: str, out_h_syscall_dir: str):
    filename = os.path.join(out_h_dir, "syscalls.hpp")
    with open(filename, "w", encoding="utf-8") as f:
        template = Template(f, type="header")
        template.write_include_system("stdint.h")
        template.write_include_system("kernel/kerror.hpp")
        template.write_include_system("kernel/memory/borrowed_ptr.hpp")
        template.write_include_system("string_view.hpp")

        template.write_include_local("stdkrn.hpp")
        template.write_include_local("__kernel_types.h")
        template.write_newline()

        providers = [sc for sc in module.syscalls if sc.provider]
        if providers:
            f.write("#include <kernel/kobject/KModuleObject.hpp>\n\n")
            f.write("namespace kernel::syscall {\n")
            f.write("// Binds every syscall provided by `module` (its exported handle_<Name>_<Version>\n")
            f.write("// functions) into the dispatch slot table. All-or-nothing; fails with NotFound when a\n")
            f.write("// declared handler is not exported and AlreadyExists when a slot is already bound.\n")
            f.write("kstd::error_code BindProvider(const KObjectPtr<KModuleObject> &module);\n")
            f.write("}// namespace kernel::syscall\n\n")

        for sc in module.syscalls:
            template.write_doc(sc.doc)
            if sc.provider:
                f.write(f"// Provided by module '{sc.provider}': it must export (UNDOS_MODULE_EXPORT)\n")
                f.write(f"//   kstd::error_code handle_{sc.name}_{sc.version}({kernel_struct_name(sc)} &&args);\n\n")
                continue
            template.write_method_def(name = f"handle_{sc.name}_{sc.version}", return_type = "kstd::error_code", args = [
                {'name': "args", 'type': f"{kernel_struct_name(sc)} &&"}
            ])
        template.write_newline()


def generate_kernel(module: IDLModule, out_h_dir: str, out_h_syscall_dir: str):
    generate_kernel_types(module, out_h_dir, out_h_syscall_dir)
    generate_kernel_handle_definition(module, out_h_dir, out_h_syscall_dir)

    filename = os.path.join(out_h_dir, "handler_syscall.cpp")
    with open(filename, "w", encoding="utf-8") as f:
        template = Template(f, type="source")
        

        template.write_include_local("stdkrn.hpp")
        template.write_newline()
        template.write_include_system("memory.hpp")
        template.write_include_system("stddef.h")
        template.write_newline()
        template.write_include_local("syscalls.hpp")
        template.write_include_local("KErrorToSysReturnCode.hpp")
        template.write_newline()

        for enum in module.enums:
            filename = os.path.join(out_h_syscall_dir, make_filename(enum))
            template.write_include_system(filename)
        template.write_newline()
        for flags in module.flags:
            filename = os.path.join(out_h_syscall_dir, make_filename(flags))
            template.write_include_system(filename)
        template.write_newline()
        for struct in module.structs:
            filename = os.path.join(out_h_syscall_dir, make_filename(struct))
            template.write_include_system(filename)
        template.write_newline()
        for sc in module.syscalls:
            filename = os.path.join(out_h_syscall_dir, make_filename(sc))
            template.write_include_system(filename)
        template.write_newline()

        # Bounds used for the one-time pointer+length validation done in each
        # Dispatch_* function below. COPY fields are meant for small,
        # predictable-size data (paths, filenames, arguments, small structs);
        # MAP fields are for large buffers and are never fixed-length, but are
        # still bounded here to something a single syscall can reasonably map.
        template.write_constant("size_t", "UNDOS_MAX_COPY_SIZE", "4096")
        template.write_constant("size_t", "UNDOS_MAX_MAPPED_BUFFER_SIZE", "64 * 1024 * 1024")
        template.write_newline()

        # Small helper used by every Dispatch_* below to write a single OUT
        # field back into the caller's (userspace) struct: maps just the
        # bytes at `userPtr`, copies `length` bytes from `kernelPtr` into
        # them, and unmaps again. This is the one-time, per-field version of
        # KE_VMM_CopyToUserspace(as, userPtr, kernelPtr, length) described by
        # the syscall codegen spec.
        f.write("static kstd::error_code KE_VMM_CopyToUserspace(kernel::vmm::AddressSpace &as, void *userPtr, const void *kernelPtr, size_t length) {\n")
        f.write("  if (length == 0) return kernel::KernelError::Success;\n")
        f.write("  if (userPtr == nullptr) [[unlikely]] return kernel::KernelError::InvalidAddress;\n")
        f.write("  const auto mapped = KE_VMM_MapBorrowed(userPtr, length, as);\n")
        f.write("  if (!mapped) [[unlikely]] return mapped.error();\n")
        f.write("  kstd::memcpy(*mapped, kernelPtr, length);\n")
        f.write("  return KE_VMM_UnmapBorrowed(*mapped, length, as);\n")
        f.write("}\n\n")

        providers = [sc for sc in module.syscalls if sc.provider]
        if providers:
            f.write("// One slot per module-provided syscall, indexed at generation time so dispatch is a\n")
            f.write("// single load and null check. An empty slot means the provider is not loaded.\n")
            f.write("namespace {\n")
            f.write("struct ProviderSlot {\n  kstd::string_view provider;\n  kstd::string_view symbol;\n  kernel::VirtualAddress handler;\n};\n\n")
            f.write("ProviderSlot g_providerSlots[] = {\n")
            for sc in providers:
                f.write(f'    {{"{sc.provider}", "handle_{sc.name}_{sc.version}", kernel::VirtualAddress{{0}}}},\n')
            f.write("};\n}// namespace\n\n")
            f.write("namespace kernel::syscall {\n")
            f.write("kstd::error_code BindProvider(const KObjectPtr<KModuleObject> &module) {\n")
            f.write("  const kstd::string_view moduleName(module->name);\n")
            f.write("  for (const auto &slot: g_providerSlots) {\n")
            f.write("    if (slot.provider != moduleName) continue;\n")
            f.write("    if (slot.handler.value != 0) return KernelError::AlreadyExists;\n")
            f.write("    if (!module->exports.Find(slot.symbol)) return KernelError::NotFound;\n")
            f.write("  }\n")
            f.write("  for (auto &slot: g_providerSlots) {\n")
            f.write("    if (slot.provider != moduleName) continue;\n")
            f.write("    slot.handler = *module->exports.Find(slot.symbol);\n")
            f.write("  }\n")
            f.write("  return KernelError::Success;\n")
            f.write("}\n}// namespace kernel::syscall\n\n")

        slot_index = {sc.number: i for i, sc in enumerate(providers)}

        for sc in module.syscalls:
            wire_name = wire_struct_name(sc)
            krn_name = kernel_struct_name(sc)

            copy_in_string_fields = [
                field for field in sc.fields
                if isinstance(field.ftype, StringType) and field.mode == "COPY" and field.direction == "IN"
            ]

            f.write(f"static kstd::error_code Dispatch_{sc.name}_{sc.version}(void *userData, size_t size, uint32_t flags) {{\n")
            f.write(f"  if (size < sizeof({wire_name})) return kernel::KernelError::InvalidSize;\n\n")
            f.write("  const auto currentThread = KE_SCHED_GetCurrentThread();\n")
            f.write("  if (!currentThread) return kernel::KernelError::NoCurrentThread;\n")
            f.write("  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;\n")
            f.write("  auto &address_space = currentThread->owner_process->address_space;\n\n")

            # Step 1: map only the struct header, copy it out, unmap right away.
            f.write(f"  {wire_name} headerCopy {{}};\n")
            f.write(f"  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof({wire_name}), address_space)) {{\n")
            f.write(f"    kstd::memcpy(&headerCopy, *mapped, sizeof({wire_name}));\n")
            f.write(f"    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof({wire_name}), address_space); error) return error;\n")
            f.write("  } else {\n")
            f.write("    return mapped.error();\n")
            f.write("  }\n\n")

            # Step 2: validate every pointer + length in the copied header, once.
            for field in sc.fields:
                if isinstance(field.ftype, (StringType, DynArrayType, BufferType)):
                    f.write(f"  if (headerCopy.{field.name} != nullptr) {{\n")
                    f.write(f"    if (headerCopy.{field.name}Length > UNDOS_MAX_COPY_SIZE) return kernel::KernelError::InvalidSize;\n")
                    if not (isinstance(field.ftype, StringType) and field.mode == "COPY" and field.direction == "IN"):
                        f.write(f"    if (headerCopy.{field.name}Length == 0) return kernel::KernelError::InvalidArgument;\n")
                    f.write(f"  }}\n\n")

            # Step 4/5: build the decoupled kernel-side struct from the
            # rewritten kernel struct (never from the raw user pointer).
            f.write(f"  {krn_name} args {{}};\n")
            for field in sc.fields:
                if isinstance(field.ftype, StringType):
                    if field.mode == "COPY" and field.direction == "IN":
                        # Already rewritten to point inside kbuf above; just
                        # take a non-owning view of it.
                        f.write(f"  if (headerCopy.{field.name} != nullptr && headerCopy.{field.name}Length > 0) {{\n")
                        f.write(f"    args.{field.name}.resize(headerCopy.{field.name}Length);\n")
                        f.write(f"    if (args.{field.name}.data() == nullptr || args.{field.name}.size() != headerCopy.{field.name}Length) return kernel::KernelError::OutOfMemory;\n")
                        f.write(f"    if (const auto mapped = KE_VMM_MapBorrowed(const_cast<char *>(headerCopy.{field.name}), headerCopy.{field.name}Length, address_space)) {{\n")
                        f.write(f"      kstd::memcpy(args.{field.name}.data(), *mapped, headerCopy.{field.name}Length);\n")
                        f.write(f"      if (const auto error = KE_VMM_UnmapBorrowed(*mapped, headerCopy.{field.name}Length, address_space); error) return error;\n")
                        f.write(f"    }} else {{\n")
                        f.write(f"      return mapped.error();\n")
                        f.write(f"    }}\n")
                        f.write(f"  }}\n")
                    else:
                        # MAP (typically OUT strings): keep the user buffer
                        # mapped for the handler's lifetime so it can write
                        # directly into it (MDL-style, via borrowed_ptr).
                        f.write(f"  if (headerCopy.{field.name} != nullptr) {{\n")
                        f.write(
                            f"    const auto mapped = KE_VMM_MapBorrowed(const_cast<char *>(headerCopy.{field.name}), headerCopy.{field.name}Length, address_space);\n")
                        f.write("    if (!mapped) return mapped.error();\n")
                        f.write(f"    args.{field.name} = kernel::borrowed_ptr<char>(static_cast<char *>(*mapped), headerCopy.{field.name}Length, address_space);\n")
                        f.write(f"    args.{field.name}_len = headerCopy.{field.name}Length;\n")
                        f.write("  }\n")
                    continue
                if field.direction == "OUT":
                    continue
                if isinstance(field.ftype, DynArrayType) or isinstance(field.ftype, BufferType):
                    f.write(f"  if (headerCopy.{field.name} != nullptr) {{\n")
                    f.write(f"    if (const auto mapped = KE_VMM_MapBorrowed(const_cast<{c_type_for(field.ftype.elem_type)} *>(headerCopy.{field.name}), headerCopy.{field.name}Length * sizeof({c_type_for(field.ftype.elem_type)}), address_space)) {{\n")
                    f.write(f"      args.{field.name} = kernel::borrowed_ptr<{c_type_for(field.ftype.elem_type)}>(static_cast<{c_type_for(field.ftype.elem_type)} *>(*mapped), headerCopy.{field.name}Length, address_space);\n")
                    f.write(f"    }} else {{\n")
                    f.write(f"      return mapped.error();\n")
                    f.write(f"    }}\n")
                    f.write("  }\n")
                elif isinstance(field.ftype, ArrayType):
                    f.write(f"  kstd::memcpy(args.{field.name}, headerCopy.{field.name}, sizeof(args.{field.name}));\n")
                else:
                    f.write(f"  args.{field.name} = headerCopy.{field.name};\n")

            # OUT buffer<T> fields: mapped up-front (MDL-style) so the
            # handler can write directly into them; no extra copy needed,
            # unmapping happens automatically via borrowed_ptr's RAII.
            for field in sc.fields:
                if field.direction != "OUT" or not isinstance(field.ftype, BufferType):
                    continue
                f.write(f"  if (headerCopy.{field.name} != nullptr) {{\n")
                f.write(
                    f"    const auto mapped = KE_VMM_MapBorrowed(headerCopy.{field.name}, headerCopy.{field.name}Length * sizeof({c_type_for(field.ftype.elem_type)}), address_space);\n")
                f.write("    if (!mapped) return mapped.error();\n")
                f.write(f"    args.{field.name} = kernel::borrowed_ptr<{c_type_for(field.ftype.elem_type)}>(static_cast<{c_type_for(field.ftype.elem_type)} *>(*mapped), headerCopy.{field.name}Length, address_space);\n")
                f.write(f"    args.{field.name}_len = headerCopy.{field.name}Length;\n")
                f.write("  }\n")

            f.write("\n  (void)flags;\n")
            if sc.provider:
                f.write(f"  const auto handler = g_providerSlots[{slot_index[sc.number]}].handler;\n")
                f.write("  if (handler.value == 0) return kernel::KernelError::Unsupported;\n")
                f.write(f"  const kstd::error_code ret = handler.as_ptr<kstd::error_code({krn_name} &&)>()(kstd::move(args));\n\n")
            else:
                f.write(f"  const kstd::error_code ret = handle_{sc.name}_{sc.version}(kstd::move(args));\n\n")

            # Step 6: copy every OUT (non-buffer) field back to the original
            # user struct, one field at a time; buffer<T>/OUT-string content
            # was already written directly into the caller's memory via
            # borrowed_ptr, only their length needs reporting back here.
            for field in sc.fields:
                if field.direction != "OUT":
                    continue
                if isinstance(field.ftype, StringType):
                    f.write(f"  // content was written directly into the mapped user buffer; report the actual/required length.\n")
                    f.write(f"  {{\n    const auto len = static_cast<uint32_t>(args.{field.name}_len);\n")
                    f.write(f"    if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof({wire_name}, {field.name}Length), &len, sizeof(len)); error) return error;\n")
                    f.write(f"  }}\n")
                elif isinstance(field.ftype, DynArrayType):
                    f.write(f"  /* OUT dynarray fields must be copied back manually by the handler */\n")
                elif isinstance(field.ftype, BufferType):
                    f.write(f"  // buffer was mapped and filled by the kernel directly; report actual length written.\n")
                    f.write(f"  {{\n")
                    f.write(f"    const auto len = static_cast<uint32_t>(args.{field.name}_len);\n")
                    f.write(f"    if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof({wire_name}, {field.name}Length), &len, sizeof(len)); error) return error;\n")
                    f.write(f"  }}\n")
                elif isinstance(field.ftype, ArrayType):
                    f.write(
                        f"  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof({wire_name}, {field.name}), args.{field.name}, sizeof(args.{field.name})); error) return error;\n")
                else:
                    f.write(
                        f"  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof({wire_name}, {field.name}), &args.{field.name}, sizeof(args.{field.name})); error) return error;\n")

            f.write("  return ret;\n")
            f.write("}\n\n")

        f.write("\n")

        f.write("UNDOS_KERNEL_API_DEF uint32_t KE_SYSCALL_Dispatch(uint32_t number, uint32_t size, void *data, uint32_t flags) noexcept {\n")
        f.write("  switch (number) {\n")
        for sc in module.syscalls:
            f.write(f"    case {sc.number}: return kernel::KErrorToSysReturnCode(Dispatch_{sc.name}_{sc.version}(data, size, flags));\n")
        f.write("  }\n")
        f.write("  return SyscallReturnCode_UNKNOWN;\n")
        f.write("}\n")
        f.write("\n\n")
