
#pragma once

#include "stdkrn.hpp"

namespace kernel::process {
// Opens `path` via the VFS, parses it as an ELF32 executable, and maps its
// PT_LOAD segments into `as` (a not-yet-active address space; CR3 is switched
// to `as.translation_root` for the duration of the mapping and restored
// afterward). Returns the (load-delta adjusted) entry point on success, or a
// null VirtualAddress on any VFS/ELF-format failure.
VirtualAddress load_elf_from_vfs(vmm::AddressSpace &as, const kstd::string_view &path) noexcept;
}// namespace kernel::process
