
#pragma once

#include "stdkrn.hpp"
#include "vmm.hpp"

#include <kernel/cfunc.hpp>
#include <kernel/elf.hpp>
#include <string_view.hpp>

// Shared VFS-streaming ELF32 loader core, used by both the kernel driver
// loader (KE_DRIVER_Load, relocatable, kernel-space) and the user-mode
// process loader (kernel::process::load_elf_from_vfs, fixed-address,
// user-space). Never materializes the whole file: the header, program
// headers, and each segment's bytes are read from the VFS on demand.
namespace kernel::elf {
struct LoadPolicy {
  // Drivers are relocatable (PIC-ish, resolved against the kernel symbol
  // table); user-mode process binaries are fixed-vaddr and non-relocatable.
  bool relocatable = false;
  // Adds ProtectFlags::USER to every mapped segment (user-mode processes).
  bool user_mode = false;
  // Address space to map into. nullptr => map into whatever address space
  // is currently active (drivers, which always load into the kernel AS).
  // Non-null => temporarily switch CR3 to *target_as for the duration of
  // the mapping, then switch back (user-mode processes, whose AS is not
  // yet active).
  vmm::AddressSpace *target_as = nullptr;
  // Opaque context forwarded to capture_section (cfunc only wraps plain
  // C-style function pointers, so callers that need to capture state must
  // do so through this pointer rather than a capturing lambda).
  void *context = nullptr;
  // Resolves an undefined (SHN_UNDEF) symbol name to an address. Required
  // when relocatable is set; ignored otherwise.
  cfunc<uintptr_t(kstd::string_view)> resolve_symbol;
  // Invoked once per named section found in the section header table
  // (relocatable path only), with the section's bytes still resident in a
  // temporary buffer. Callers that need the data (e.g. ".driver_name")
  // must copy it out before returning.
  cfunc<void(void *, kstd::string_view, const uint8_t *, size_t)> capture_section;
};

struct LoadResult {
  VirtualAddress load_base{0};
  size_t total_size{0};
  VirtualAddress entry_point{0};
  bool ok = false;
};

// Opens `path` via the VFS, parses it as an ELF32 image, allocates a region
// sized to span its PT_LOAD segments, maps and copies each segment, and
// (when policy.relocatable) applies relocations and captures named
// sections. Returns a LoadResult with ok == false on any VFS/ELF-format
// failure.
LoadResult LoadElfFromVfs(const kstd::string_view &path, const LoadPolicy &policy) noexcept;

// Applies a single ELF32 relocation record against symbol/string tables
// already resident in memory, resolving SHN_UNDEF symbols via
// `resolve_symbol`. Shared by the VFS-streaming loader above and the
// physical-memory boot-driver loader (which parses an already-mapped blob).
bool ApplyRelocation(const hal::Elf32_Rel &rel, const hal::Elf32_Sym *syms, const char *strings, uintptr_t delta, const cfunc<uintptr_t(kstd::string_view)> &resolve_symbol) noexcept;

// Applies every SHT_REL relocation section found in a fully in-memory
// ELF32 blob (section headers, symbol table, and string table are all
// already resident in `raw_blob`). Used by the physical-memory boot-driver
// loader, which never streams from the VFS.
bool ProcessRelocationsFromBlob(const uint8_t *raw_blob, uintptr_t delta, const cfunc<uintptr_t(kstd::string_view)> &resolve_symbol) noexcept;
}// namespace kernel::elf
