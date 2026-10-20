
#include "elf_loader.hpp"

#include "elf_segment_loader.hpp"

#include <Kernel.hpp>
#include <array.hpp>
#include <kernel/vfs.hpp>
#include <span.hpp>
#include <utility.hpp>

using kernel::KernelError;

namespace {
// Program/section headers are streamed a table at a time, so a bounded
// stack-sized cap keeps each read a single fixed-size VFS read without
// requiring a heap allocation sized from untrusted header fields ahead of
// time.
constexpr size_t MAX_PROGRAM_HEADERS = 16;
constexpr size_t MAX_SECTION_HEADERS = 64;

void CloseFileAfterFailure(const kernel::KObjectPtr<kernel::KIOObject> &file) noexcept {
  const auto status = KE_VFS_CloseFile(file);
  if (status) {
    KE_Print("LoadElfFromVfs: failed to close file during cleanup: {}",
             status.message());
  }
}

// Reads exactly `buffer.size()` bytes starting at `offset` from `file`,
// seeking first. Returns false if the seek/read comes up short.
kstd::error_code ReadExact(const kernel::KObjectPtr<kernel::KIOObject> &file, uint64_t offset, kstd::span<uint8_t> buffer) noexcept {
  const auto seek_status = KE_VFS_SeekFile(file, offset);
  if (!seek_status) {
    KE_Print("ReadExact: Failed to seek to offset");
    return seek_status.error();
  }
  if (*seek_status != offset) return kernel::KernelError::IoError;

  uint64_t total = 0;
  while (total < buffer.size()) {
    kstd::span chunk(buffer.data() + total, static_cast<size_t>(buffer.size() - total));
    const auto got = KE_VFS_ReadFile(file, chunk);
    if (!got) return got.error();
    if (*got == 0) break;
    if (*got > chunk.size()) return kernel::KernelError::InvalidData;
    total += *got;
  }

  return total == buffer.size() ? kernel::KernelError::Success : kernel::KernelError::UnexpectedEndOfFile;
}

// Reads a `size`-byte region from `file` at `offset` into a freshly
// KE_Malloc'd buffer that the caller owns (KE_Free it). Allocation and
// short-read failures are returned as kstd::error_code; size == 0 succeeds with a
// null pointer because there is no data to read.
 kstd::expected<uint8_t *, kstd::error_code> ReadBlob(const kernel::KObjectPtr<kernel::KIOObject> &file, uint64_t offset, size_t size) noexcept {
  if (size == 0) return static_cast<uint8_t *>(nullptr);
  auto *data = static_cast<uint8_t *>(KE_Malloc(size));
  if (!data) return kstd::unexpected(kernel::KernelError::OutOfMemory);
  const auto status = ReadExact(file, offset, kstd::span(data, size));
  if (status) {
    KE_Free(data);
    return kstd::unexpected(status);
  }
  return data;
}

// Applies every SHT_REL relocation section found in the section header
// table `shdrs` (`shnum` entries, already resident in memory) of an ELF32
// image streamed from `file`.
kstd::error_code ProcessRelocationsFromVfs(const kernel::KObjectPtr<kernel::KIOObject> &file, const hal::Elf32_Shdr *shdrs, size_t shnum, uintptr_t delta, const kernel::cfunc<kstd::expected<uintptr_t, kstd::error_code>(kstd::string_view)> &resolve_symbol) noexcept {
  // Relocation sections normally share one symbol/string table; keep them
  // resident across sections instead of re-reading them from disk each time.
  size_t loaded_sym_index = shnum;
  hal::Elf32_Sym *syms = nullptr;
  char *strings = nullptr;
  kstd::error_code status = kernel::KernelError::Success;

  for (size_t i = 0; i < shnum && !status; ++i) {
    if (shdrs[i].sh_type != hal::SHT_REL) continue;

    if (const uint32_t target_section_idx = shdrs[i].sh_info;
        target_section_idx >= shnum ||
        !(shdrs[target_section_idx].sh_flags & hal::SHF_ALLOC)) {
      // Skip this relocation section.
      continue;
    }

    if (shdrs[i].sh_link >= shnum) {
      status = kernel::KernelError::InvalidData;
      break;
    }
    const auto &sym_section = shdrs[shdrs[i].sh_link];
    if (sym_section.sh_link >= shnum) {
      status = kernel::KernelError::InvalidData;
      break;
    }
    const auto &str_section = shdrs[sym_section.sh_link];

    if (loaded_sym_index != shdrs[i].sh_link) {
      KE_Free(syms);
      KE_Free(strings);
      auto symbol_data = ReadBlob(file, sym_section.sh_offset, sym_section.sh_size);
      if (!symbol_data) {
        status = symbol_data.error();
        break;
      }
      auto string_data = ReadBlob(file, str_section.sh_offset, str_section.sh_size);
      if (!string_data) {
        KE_Free(*symbol_data);
        status = string_data.error();
        break;
      }
      syms = reinterpret_cast<hal::Elf32_Sym *>(*symbol_data);
      strings = reinterpret_cast<char *>(*string_data);
      loaded_sym_index = shdrs[i].sh_link;
    }

    auto rel_data = ReadBlob(file, shdrs[i].sh_offset, shdrs[i].sh_size);
    if (!rel_data) {
      status = rel_data.error();
    } else if (!syms || !strings) {
      KE_Free(*rel_data);
      status = kernel::KernelError::InvalidData;
    } else {
      auto *rels = reinterpret_cast<hal::Elf32_Rel *>(*rel_data);
      const size_t rel_count = shdrs[i].sh_size / sizeof(hal::Elf32_Rel);
      for (size_t r = 0; r < rel_count && !status; ++r) {
        status = kernel::elf::ApplyRelocation(rels[r], syms, strings, delta, resolve_symbol);
        if (status) {
          KE_Print("ProcessRelocationsFromBlob: Failed to apply relocation type {} at {x}: {}",
                   hal::ELF32_R_TYPE(rels[r].r_info), rels[r].r_offset, status.message());
        }
      }
      KE_Free(rels);
    }
  }

  KE_Free(syms);
  KE_Free(strings);
  return status;
}

// Reads the section-header-string-table and, for every named section,
// invokes policy.capture_section with that section's bytes (still resident
// in a temporary buffer, freed right after the callback returns).
kstd::error_code CaptureSections(const kernel::KObjectPtr<kernel::KIOObject> &file, const hal::Elf32_Ehdr &header, const hal::Elf32_Shdr *shdrs, const kernel::elf::LoadPolicy &policy) noexcept {
  if (!policy.capture_section.valid()) return kernel::KernelError::Success;
  if (header.e_shstrndx >= header.e_shnum) return kernel::KernelError::InvalidData;

  const auto &shstrtab = shdrs[header.e_shstrndx];
  auto shstr_result = ReadBlob(file, shstrtab.sh_offset, shstrtab.sh_size);
  if (!shstr_result) return shstr_result.error();
  auto *shstr = *shstr_result;

  for (size_t i = 0; i < header.e_shnum; ++i) {
    if (shdrs[i].sh_name >= shstrtab.sh_size) continue;
    const kstd::string_view name(reinterpret_cast<const char *>(shstr + shdrs[i].sh_name));
    if (name.empty()) continue;
    if (!policy.capture_section_name.empty() && name != policy.capture_section_name) continue;

    auto data = ReadBlob(file, shdrs[i].sh_offset, shdrs[i].sh_size);
    if (!data) {
      KE_Free(shstr);
      return data.error();
    }
    policy.capture_section(policy.context, name, *data, shdrs[i].sh_size);
    KE_Free(*data);
  }

  KE_Free(shstr);
  return kernel::KernelError::Success;
}
}// namespace

kstd::error_code kernel::elf::ApplyRelocation(
    const hal::Elf32_Rel &rel,
    const hal::Elf32_Sym *syms,
    const char *strings,
    uintptr_t delta,
    const cfunc<kstd::expected<uintptr_t, kstd::error_code>(kstd::string_view)> &resolve_symbol) noexcept {
  if (!syms || !strings) return KernelError::InvalidArgument;
  const uint32_t type = hal::ELF32_R_TYPE(rel.r_info);
  const uint32_t sym_idx = hal::ELF32_R_SYM(rel.r_info);
  const auto &sym = syms[sym_idx];

  uintptr_t sym_val = 0;
  if (sym.st_shndx == 0) {// SHN_UNDEF
    const char *name = strings + sym.st_name;
    if (!resolve_symbol.valid()) return KernelError::UnresolvedSymbol;
    const auto resolved = resolve_symbol(name);
    if (!resolved) {
      KE_Print("Elf Loader: Unresolved symbol: {}", name);
      return resolved.error() == KernelError::NotFound ? KernelError::UnresolvedSymbol : resolved.error();
    }
    sym_val = *resolved;
  } else {
    sym_val = sym.st_value + delta;
  }

  auto *target = reinterpret_cast<uintptr_t *>(rel.r_offset + delta);

  switch (type) {
    case hal::R_386_32:
      if (sym.st_shndx == 0) {
        *target = sym_val;
      } else {
        *target += delta;
      }
      break;
    case hal::R_386_PC32:
    case hal::R_386_PLT32:
      if (sym.st_shndx == 0) {
        *target = sym_val - reinterpret_cast<uintptr_t>(target) - 4;
      } else {
        // Internal PC-relative calls are invariant under linear translation
      }
      break;
    case hal::R_386_RELATIVE:
      *target += delta;
      break;
    case hal::R_386_GOTOFF:
    case hal::R_386_GOTPC:
      // Invariant under linear translation of the whole image (as in Stage 1.5)
      break;
    default:
      return KernelError::Unsupported;
  }

  return KernelError::Success;
}

kstd::error_code kernel::elf::ProcessRelocationsFromBlob(const uint8_t *raw_blob, uintptr_t delta, const cfunc<kstd::expected<uintptr_t, kstd::error_code>(kstd::string_view)> &resolve_symbol) noexcept {
  if (!raw_blob) return KernelError::InvalidArgument;
  const auto *header = reinterpret_cast<const hal::Elf32_Ehdr *>(raw_blob);
  const auto *shdrs = reinterpret_cast<const hal::Elf32_Shdr *>(raw_blob + header->e_shoff);

  for (size_t i = 0; i < header->e_shnum; ++i) {
    if (shdrs[i].sh_type == hal::SHT_REL) {
      if (shdrs[i].sh_info >= header->e_shnum || shdrs[i].sh_link >= header->e_shnum) {
        return KernelError::InvalidData;
      }
      if (const uint32_t target_section_idx = shdrs[i].sh_info;
          !(shdrs[target_section_idx].sh_flags & hal::SHF_ALLOC)) {
        continue;
      }

      const auto *rels = reinterpret_cast<const hal::Elf32_Rel *>(raw_blob + shdrs[i].sh_offset);
      const size_t rel_count = shdrs[i].sh_size / sizeof(hal::Elf32_Rel);
      const auto &sym_section = shdrs[shdrs[i].sh_link];
      if (sym_section.sh_link >= header->e_shnum) return KernelError::InvalidData;
      const auto *syms = reinterpret_cast<const hal::Elf32_Sym *>(raw_blob + sym_section.sh_offset);
      const auto &str_section = shdrs[sym_section.sh_link];
      const auto strings = reinterpret_cast<const char *>(raw_blob + str_section.sh_offset);

      for (size_t r = 0; r < rel_count; ++r) {
        const auto status = ApplyRelocation(rels[r], syms, strings, delta, resolve_symbol);
        if (status) {
          KE_Print("ProcessRelocationsFromBlob: Failed to apply relocation");
          return status;
        }
      }
    }
  }

  return KernelError::Success;
}

kstd::expected<kernel::elf::LoadResult, kstd::error_code>
kernel::elf::LoadElfFromVfs(const kstd::string_view &path, const LoadPolicy &policy) noexcept {
  LoadResult result;
  if (path.empty()) return kstd::unexpected(KernelError::InvalidArgument);
  auto file = KE_VFS_OpenFile(path, KIOObject::OpenMode::Read);
  if (!file) {
    KE_Print("LoadElfFromVfs: Failed to open file");
    return kstd::unexpected(file.error());
  }
  auto &fileObject = *file;

  // Step 1: read only the fixed-size ELF header.
  hal::Elf32_Ehdr header{};
  const auto header_status = ReadExact(fileObject, 0, kstd::span(reinterpret_cast<uint8_t *>(&header), sizeof(header)));
  if (header_status ||
      header.e_ident[0] != 0x7F || header.e_ident[1] != 'E' ||
      header.e_ident[2] != 'L' || header.e_ident[3] != 'F' ||
      header.e_phnum > MAX_PROGRAM_HEADERS || header.e_phentsize != sizeof(hal::Elf32_Phdr)) {
    CloseFileAfterFailure(fileObject);
    KE_Print("LoadElfFromVfs: Invalid ELF header");
    return kstd::unexpected(!header_status ? KernelError::InvalidExecutable : header_status);
  }

  // KE_Print("LoadElfFromVfs: Valid ELF header");
  // KE_Print("  Entry point: 0x{:x}", header.e_entry);
  // KE_Print("  Program headers: {} at offset 0x{:x}", header.e_phnum, header.e_phoff);
  // KE_Print("  Section headers: {} at offset 0x{:x}", header.e_shnum, header.e_shoff);
  // KE_Print("  Machine type: 0x{:x}", header.e_machine);

  // Step 2: read only the program header table.
  kstd::array<hal::Elf32_Phdr, MAX_PROGRAM_HEADERS> phdrs{};
  const size_t phdrs_size = static_cast<size_t>(header.e_phnum) * sizeof(hal::Elf32_Phdr);
  const auto phdr_status = ReadExact(fileObject, header.e_phoff, kstd::span(reinterpret_cast<uint8_t *>(phdrs.data()), phdrs_size));
  if (phdr_status) {
    CloseFileAfterFailure(fileObject);
    KE_Print("LoadElfFromVfs: Failed to read program header table");
    return kstd::unexpected(phdr_status);
  }
  //KE_Print("  Program header table: {} entries at offset 0x{:x}", header.e_phnum, header.e_phoff);

  // Step 3 (relocatable path only): read the section header table, then
  // let the caller capture any named sections it cares about (e.g. a
  // driver's ".driver_name") before relocations are applied.
  kstd::array<hal::Elf32_Shdr, MAX_SECTION_HEADERS> shdrs{};
  if (policy.relocatable) {
    if (header.e_shnum > MAX_SECTION_HEADERS || header.e_shentsize != sizeof(hal::Elf32_Shdr)) {
      CloseFileAfterFailure(fileObject);
      KE_Print("LoadElfFromVfs: Failed to read section header table");
      return kstd::unexpected(KernelError::InvalidExecutable);
    }
    const auto shdr_status = ReadExact(fileObject, header.e_shoff, kstd::span(reinterpret_cast<uint8_t *>(shdrs.data()), static_cast<size_t>(header.e_shnum) * sizeof(hal::Elf32_Shdr)));
    if (shdr_status) {
      CloseFileAfterFailure(fileObject);
      return kstd::unexpected(shdr_status);
    }

    const auto capture_status = CaptureSections(fileObject, header, shdrs.data(), policy);
    if (capture_status) {
      CloseFileAfterFailure(fileObject);
      KE_Print("LoadElfFromVfs: Failed to capture sections");
      return kstd::unexpected(capture_status);
    }
  }

  // Kernel driver path (no target address space supplied): the image is
  // relocatable, so allocate a fresh kernel region sized to span its
  // PT_LOAD segments and translate every vaddr by the resulting delta.
  // User-mode process path (a target address space is supplied): binaries
  // are fixed-vaddr, so segments are mapped at their own p_vaddr with no
  // delta and no region allocation.
  uintptr_t load_delta = 0;
  uintptr_t region_base = 0;
  size_t region_size = 0;
  if (policy.target_as == nullptr) {
    uintptr_t min_vaddr = 0xFFFFFFFF;
    uintptr_t max_vaddr = 0;
    for (size_t i = 0; i < header.e_phnum; ++i) {
      if (phdrs[i].p_type != hal::PT_LOAD) continue;
      if (phdrs[i].p_vaddr < min_vaddr) min_vaddr = phdrs[i].p_vaddr;
      if (phdrs[i].p_vaddr + phdrs[i].p_memsz > max_vaddr) max_vaddr = phdrs[i].p_vaddr + phdrs[i].p_memsz;
    }
    if (max_vaddr <= min_vaddr) {
      CloseFileAfterFailure(fileObject);
      KE_Print("LoadElfFromVfs: max_vaddr <= min_vaddr");
      return kstd::unexpected(KernelError::InvalidExecutable);
    }

    region_size = max_vaddr - min_vaddr;

    // One BackingObject (Kind::KernelModule, since it's a file-backed
    // image) + one VAD represents this loaded module's entire address
    // range, registered under \Memory for inspection via the Object
    // Manager.
    static uint32_t g_next_module_id = 0;
    kstd::static_string<64> backing_name;
    kstd::format(backing_name, "KernelModule{}", g_next_module_id++);

    const auto backing = kernel::CreateKObject<KMemoryBackingObject>(backing_name);
    if (!backing) {
      CloseFileAfterFailure(fileObject);
      KE_Print("LoadElfFromVfs: Failed to create backing object");
      return kstd::unexpected(backing.error());
    }
    backing.value()->kind = KMemoryBackingObject::Kind::KernelModule;
    backing.value()->file = fileObject;
    backing.value()->size = region_size;
    const auto insert_status = KE_OB_InsertObject(KE_OB_GetMemoryDirectory(), *backing);
    if (insert_status) {
      CloseFileAfterFailure(fileObject);
      return kstd::unexpected(insert_status);
    }

    auto &kas = KE_VMM_GetKernelAddressSpace();
    auto vad = KE_VMM_MapBackingObject(kas, *backing, region_size, vmm::ProtectFlags::READ | vmm::ProtectFlags::WRITE);
    if (!vad) {
      CloseFileAfterFailure(fileObject);
      KE_Print("LoadElfFromVfs: Failed to allocate region");
      return kstd::unexpected(vad.error());
    }

    region_base = (*vad)->start.value;
    load_delta = region_base - min_vaddr;
  }

  // Step 1 — Read every PT_LOAD segment's file bytes from the VFS *before*
  // switching to the target address space / disabling interrupts. Disk
  // reads typically complete via an IRQ (e.g. the ATA driver), so doing
  // them while interrupts are disabled -- as used to happen inside the
  // mapping loop below -- means the completion interrupt can never fire and
  // ReadBlob silently hands back stale/incomplete data instead of the real
  // file content. Reading everything up-front, in the caller's own address
  // space with interrupts still enabled, avoids that entirely.
  kstd::array<uint8_t *, MAX_PROGRAM_HEADERS> segment_data{};
  kstd::error_code status = KernelError::Success;
  for (size_t i = 0; i < header.e_phnum && !status; ++i) {
    const auto &ph = phdrs[i];
    if (ph.p_type != hal::PT_LOAD || ph.p_filesz == 0) continue;

    auto data = ReadBlob(fileObject, ph.p_offset, ph.p_filesz);
    if (!data) {
      status = data.error();
      KE_Print("LoadElfFromVfs: Failed to read segment data");
    } else segment_data[i] = *data;
  }

  // Loading targets an address space that is not currently active, so
  // temporarily switch CR3 to it (drivers map into whichever AS is already
  // active -- the kernel AS -- so no switch is needed there).
  const bool switch_as = !status && policy.target_as != nullptr;
  PhysicalAddress caller_root;
  uintptr_t irq_state = 0;
  if (switch_as) {
    caller_root = HAL_VMM_GetCurrentTranslationRoot();
    irq_state = HAL_CPU_SaveAndDisableInterrupts();
    HAL_VMM_SwitchAddressSpace(policy.target_as->translation_root);
  }

  for (size_t i = 0; i < header.e_phnum && !status; ++i) {
    const auto &ph = phdrs[i];
    if (ph.p_type != hal::PT_LOAD) continue;

    vmm::ProtectFlags perms;
    if (policy.target_as == nullptr) {
      // Mirrors Driver_Load_From_Memory's always-writable mapping (needed
      // so relocations can be patched in place after mapping).
      perms = vmm::ProtectFlags::READ | vmm::ProtectFlags::WRITE;
    } else {
      perms = vmm::ProtectFlags::READ;
      if (policy.user_mode) perms = perms | vmm::ProtectFlags::USER;
      if (ph.p_flags & 0x2) perms = perms | vmm::ProtectFlags::WRITE;
    }
    if (ph.p_flags & 0x1) perms = perms | vmm::ProtectFlags::EXECUTE;

    const uintptr_t segment_virt_start = ph.p_vaddr + load_delta;
    status = MapAndCopySegment(segment_virt_start, segment_data[i], ph.p_filesz, ph.p_memsz, perms);
    if (status) {
      KE_Print("LoadElfFromVfs: Failed to map and copy segment");
    }
  }

  if (switch_as) {
    HAL_VMM_SwitchAddressSpace(caller_root);
    HAL_CPU_RestoreInterrupts(irq_state);
  }

  for (size_t i = 0; i < header.e_phnum; ++i) {
    if (segment_data[i]) KE_Free(segment_data[i]);
  }

  if (!status && policy.relocatable) {
    status = ProcessRelocationsFromVfs(fileObject, shdrs.data(), header.e_shnum, load_delta, policy.resolve_symbol);
  }

  if (!status) {
    result.load_base = VirtualAddress(region_base);
    result.total_size = region_size;
    result.entry_point = VirtualAddress(header.e_entry + load_delta);
  } else {
    KE_Print("LoadElfFromVfs: Failed");
    if (region_size != 0) {
      auto &kas = KE_VMM_GetKernelAddressSpace();
      const uintptr_t start = region_base & ~uintptr_t(0xFFF);
      const uintptr_t end = (region_base + region_size + 0xFFF) & ~uintptr_t(0xFFF);
      for (uintptr_t address = start; address < end; address += 0x1000) {
        const VirtualAddress page(address);
        const auto phys = HAL_VMM_GetPhysicalAddress(page);
        if (phys) {
          HAL_VMM_UnmapPage(page);
          HAL_SMP_ShootdownTlb(page);
          HAL_PMM_FreeFrames(phys, 1);
        }
      }
      const auto cleanup_status = KE_VMM_FreeRegion(kas, reinterpret_cast<void *>(region_base));
      if (cleanup_status) {
        KE_Print("LoadElfFromVfs: failed to release module region: {}",
                 cleanup_status.message());
      }
    }
  }

  const auto close_status = KE_VFS_CloseFile(fileObject);
  if (status) return kstd::unexpected(status);
  if (close_status) return kstd::unexpected(close_status);
  return result;
}
