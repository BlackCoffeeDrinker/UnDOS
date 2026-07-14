
#include "elf_loader.hpp"

#include "elf_segment_loader.hpp"

#include <Kernel.hpp>
#include <array.hpp>
#include <kernel/vfs.hpp>
#include <span.hpp>
#include <utility.hpp>

namespace {
// Program/section headers are streamed a table at a time, so a bounded
// stack-sized cap keeps each read a single fixed-size VFS read without
// requiring a heap allocation sized from untrusted header fields ahead of
// time.
constexpr size_t MAX_PROGRAM_HEADERS = 16;
constexpr size_t MAX_SECTION_HEADERS = 64;

// Reads exactly `buffer.size()` bytes starting at `offset` from `file`,
// seeking first. Returns false if the seek/read comes up short.
bool ReadExact(const kernel::KObjectPtr<kernel::KFileObject> &file, uint64_t offset, kstd::span<uint8_t> buffer) noexcept {
  if (KE_VFS_SeekFile(file, offset) != offset) {
    early_print_fmt("ReadExact: Failed to seek to offset\r\n");
    return false;
  }

  uint64_t total = 0;
  while (total < buffer.size()) {
    kstd::span chunk(buffer.data() + total, static_cast<size_t>(buffer.size() - total));
    const auto got = KE_VFS_ReadFile(file, chunk);
    if (got == 0) break;
    total += got;
  }

  return total == buffer.size();
}

// Reads a `size`-byte region from `file` at `offset` into a freshly
// KE_Malloc'd buffer that the caller owns (KE_Free it). Returns nullptr on
// allocation or short-read failure; size == 0 also returns nullptr.
uint8_t *ReadBlob(const kernel::KObjectPtr<kernel::KFileObject> &file, uint64_t offset, size_t size) noexcept {
  if (size == 0) return nullptr;
  auto *data = static_cast<uint8_t *>(KE_Malloc(size));
  if (!data) return nullptr;
  if (!ReadExact(file, offset, kstd::span(data, size))) {
    KE_Free(data);
    return nullptr;
  }
  return data;
}

// Applies every SHT_REL relocation section found in the section header
// table `shdrs` (`shnum` entries, already resident in memory) of an ELF32
// image streamed from `file`.
bool ProcessRelocationsFromVfs(const kernel::KObjectPtr<kernel::KFileObject> &file, const hal::Elf32_Shdr *shdrs, size_t shnum, uintptr_t delta, const kernel::cfunc<uintptr_t(kstd::string_view)> &resolve_symbol) noexcept {
  for (size_t i = 0; i < shnum; ++i) {
    if (shdrs[i].sh_type != hal::SHT_REL) continue;

    if (const uint32_t target_section_idx = shdrs[i].sh_info;
        target_section_idx >= shnum ||
        !(shdrs[target_section_idx].sh_flags & hal::SHF_ALLOC)) {
      // Skip this relocation section.
      continue;
    }

    if (shdrs[i].sh_link >= shnum) return false;
    const auto &sym_section = shdrs[shdrs[i].sh_link];
    if (sym_section.sh_link >= shnum) return false;
    const auto &str_section = shdrs[sym_section.sh_link];

    auto *rels = reinterpret_cast<hal::Elf32_Rel *>(ReadBlob(file, shdrs[i].sh_offset, shdrs[i].sh_size));
    auto *syms = reinterpret_cast<hal::Elf32_Sym *>(ReadBlob(file, sym_section.sh_offset, sym_section.sh_size));
    auto *strings = reinterpret_cast<char *>(ReadBlob(file, str_section.sh_offset, str_section.sh_size));

    bool ok = rels != nullptr && syms != nullptr && strings != nullptr;
    if (ok) {
      const size_t rel_count = shdrs[i].sh_size / sizeof(hal::Elf32_Rel);
      for (size_t r = 0; r < rel_count && ok; ++r) {
        if (!kernel::elf::ApplyRelocation(rels[r], syms, strings, delta, resolve_symbol)) {
          ok = false;
          early_print_fmt("ProcessRelocationsFromBlob: Failed to apply relocation\r\n");
        }
      }
    } else {
      early_print_fmt("ProcessRelocationsFromBlob: rels != nullptr && syms != nullptr && strings != nullptr\r\n");
    }

    KE_Free(rels);
    KE_Free(syms);
    KE_Free(strings);

    if (!ok) return false;
  }

  return true;
}

// Reads the section-header-string-table and, for every named section,
// invokes policy.capture_section with that section's bytes (still resident
// in a temporary buffer, freed right after the callback returns).
bool CaptureSections(const kernel::KObjectPtr<kernel::KFileObject> &file, const hal::Elf32_Ehdr &header, const hal::Elf32_Shdr *shdrs, const kernel::elf::LoadPolicy &policy) noexcept {
  if (!policy.capture_section.valid()) return true;
  if (header.e_shstrndx >= header.e_shnum) return true;

  const auto &shstrtab = shdrs[header.e_shstrndx];
  auto *shstr = ReadBlob(file, shstrtab.sh_offset, shstrtab.sh_size);
  if (!shstr) return false;

  for (size_t i = 0; i < header.e_shnum; ++i) {
    if (shdrs[i].sh_name >= shstrtab.sh_size) continue;
    const kstd::string_view name(reinterpret_cast<const char *>(shstr + shdrs[i].sh_name));
    if (name.empty()) continue;

    if (auto *data = ReadBlob(file, shdrs[i].sh_offset, shdrs[i].sh_size)) {
      policy.capture_section(policy.context, name, data, shdrs[i].sh_size);
      KE_Free(data);
    }
  }

  KE_Free(shstr);
  return true;
}
}// namespace

bool kernel::elf::ApplyRelocation(
    const hal::Elf32_Rel &rel,
    const hal::Elf32_Sym *syms,
    const char *strings,
    uintptr_t delta,
    const cfunc<uintptr_t(kstd::string_view)> &resolve_symbol) noexcept {
  const uint32_t type = hal::ELF32_R_TYPE(rel.r_info);
  const uint32_t sym_idx = hal::ELF32_R_SYM(rel.r_info);
  const auto &sym = syms[sym_idx];

  uintptr_t sym_val = 0;
  if (sym.st_shndx == 0) {// SHN_UNDEF
    const char *name = strings + sym.st_name;
    sym_val = resolve_symbol.valid() ? resolve_symbol(name) : 0;
    if (sym_val == 0) {
      early_print_fmt("Elf Loader: Unresolved symbol: {}\n\r", name);
      return false;
    }
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
    default:
      break;
  }

  return true;
}

bool kernel::elf::ProcessRelocationsFromBlob(const uint8_t *raw_blob, uintptr_t delta, const cfunc<uintptr_t(kstd::string_view)> &resolve_symbol) noexcept {
  const auto *header = reinterpret_cast<const hal::Elf32_Ehdr *>(raw_blob);
  const auto *shdrs = reinterpret_cast<const hal::Elf32_Shdr *>(raw_blob + header->e_shoff);

  for (size_t i = 0; i < header->e_shnum; ++i) {
    if (shdrs[i].sh_type == hal::SHT_REL) {
      if (const uint32_t target_section_idx = shdrs[i].sh_info;
          !(shdrs[target_section_idx].sh_flags & hal::SHF_ALLOC)) {
        continue;
      }

      const auto *rels = reinterpret_cast<const hal::Elf32_Rel *>(raw_blob + shdrs[i].sh_offset);
      const size_t rel_count = shdrs[i].sh_size / sizeof(hal::Elf32_Rel);
      const auto &sym_section = shdrs[shdrs[i].sh_link];
      const auto *syms = reinterpret_cast<const hal::Elf32_Sym *>(raw_blob + sym_section.sh_offset);
      const auto &str_section = shdrs[sym_section.sh_link];
      const auto strings = reinterpret_cast<const char *>(raw_blob + str_section.sh_offset);

      for (size_t r = 0; r < rel_count; ++r) {
        if (!ApplyRelocation(rels[r], syms, strings, delta, resolve_symbol)) {
          early_print_fmt("ProcessRelocationsFromBlob: Failed to apply relocation\r\n");
          return false;
        }
      }
    }
  }

  return true;
}

kernel::elf::LoadResult kernel::elf::LoadElfFromVfs(const kstd::string_view &path, const LoadPolicy &policy) noexcept {
  LoadResult result;

  auto file = KE_VFS_OpenFile(path, KFileObject::OpenMode::Read);
  if (!file) {
    early_print_fmt("LoadElfFromVfs: Failed to open file\r\n");
    return result;
  }

  // Step 1: read only the fixed-size ELF header.
  hal::Elf32_Ehdr header{};
  if (!ReadExact(file, 0, kstd::span(reinterpret_cast<uint8_t *>(&header), sizeof(header))) ||
      header.e_ident[0] != 0x7F || header.e_ident[1] != 'E' ||
      header.e_ident[2] != 'L' || header.e_ident[3] != 'F' ||
      header.e_phnum > MAX_PROGRAM_HEADERS || header.e_phentsize != sizeof(hal::Elf32_Phdr)) {
    KE_VFS_CloseFile(kstd::move(file));
    early_print_fmt("LoadElfFromVfs: Invalid ELF header\r\n");
    return result;
  }
  early_print_fmt("LoadElfFromVfs: Valid ELF header\r\n");
  early_print_fmt("  Entry point: 0x{:x}\r\n", header.e_entry);
  early_print_fmt("  Program headers: {} at offset 0x{:x}\r\n", header.e_phnum, header.e_phoff);
  early_print_fmt("  Section headers: {} at offset 0x{:x}\r\n", header.e_shnum, header.e_shoff);
  early_print_fmt("  Machine type: 0x{:x}\r\n", header.e_machine);

  // Step 2: read only the program header table.
  kstd::array<hal::Elf32_Phdr, MAX_PROGRAM_HEADERS> phdrs;
  const size_t phdrs_size = static_cast<size_t>(header.e_phnum) * sizeof(hal::Elf32_Phdr);
  if (!ReadExact(file, header.e_phoff, kstd::span(reinterpret_cast<uint8_t *>(phdrs.data()), phdrs_size))) {
    KE_VFS_CloseFile(kstd::move(file));
    early_print_fmt("LoadElfFromVfs: Failed to read program header table\r\n");
    return result;
  }
  early_print_fmt("  Program header table: {} entries at offset 0x{:x}\r\n", header.e_phnum, header.e_phoff);

  // Step 3 (relocatable path only): read the section header table, then
  // let the caller capture any named sections it cares about (e.g. a
  // driver's ".driver_name") before relocations are applied.
  kstd::array<hal::Elf32_Shdr, MAX_SECTION_HEADERS> shdrs;
  if (policy.relocatable) {
    if (header.e_shnum > MAX_SECTION_HEADERS || header.e_shentsize != sizeof(hal::Elf32_Shdr) ||
        !ReadExact(file, header.e_shoff, kstd::span(reinterpret_cast<uint8_t *>(shdrs.data()), static_cast<size_t>(header.e_shnum) * sizeof(hal::Elf32_Shdr)))) {
      KE_VFS_CloseFile(kstd::move(file));
      early_print_fmt("LoadElfFromVfs: Failed to read section header table\r\n");
      return result;
    }

    if (!CaptureSections(file, header, shdrs.data(), policy)) {
      KE_VFS_CloseFile(kstd::move(file));
      early_print_fmt("LoadElfFromVfs: Failed to capture sections\r\n");
      return result;
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
      KE_VFS_CloseFile(kstd::move(file));
      early_print_fmt("LoadElfFromVfs: max_vaddr <= min_vaddr\r\n");
      return result;
    }

    region_size = max_vaddr - min_vaddr;
    auto &kas = KE_VMM_GetKernelAddressSpace();
    void *assigned_ptr = KE_VMM_AllocateRegion(kas, region_size, vmm::ProtectFlags::READ | vmm::ProtectFlags::WRITE);
    if (!assigned_ptr) {
      KE_VFS_CloseFile(kstd::move(file));
      early_print_fmt("LoadElfFromVfs: Failed to allocate region\r\n");
      return result;
    }

    region_base = reinterpret_cast<uintptr_t>(assigned_ptr);
    load_delta = region_base - min_vaddr;
  }

  // Loading targets an address space that is not currently active, so
  // temporarily switch CR3 to it (drivers map into whichever AS is already
  // active -- the kernel AS -- so no switch is needed there).
  const bool switch_as = policy.target_as != nullptr;
  PhysicalAddress caller_root;
  if (switch_as) {
    caller_root = HAL_VMM_GetCurrentTranslationRoot();
    HAL_CPU_DisableInterrupts();
    HAL_VMM_SwitchAddressSpace(policy.target_as->translation_root);
  }

  bool ok = true;
  for (size_t i = 0; i < header.e_phnum && ok; ++i) {
    const auto &ph = phdrs[i];
    if (ph.p_type != hal::PT_LOAD) continue;

    uint8_t *segment_data = nullptr;
    if (ph.p_filesz > 0) {
      segment_data = ReadBlob(file, ph.p_offset, ph.p_filesz);
      if (!segment_data) {
        ok = false;
        early_print_fmt("LoadElfFromVfs: Failed to read segment data\r\n");
        break;
      }
    }

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
    if (!MapAndCopySegment(segment_virt_start, segment_data, ph.p_filesz, ph.p_memsz, perms)) {
      early_print_fmt("LoadElfFromVfs: Failed to map and copy segment\r\n");
      ok = false;
    }

    if (segment_data) KE_Free(segment_data);
  }

  if (ok && policy.relocatable) {
    ok = ProcessRelocationsFromVfs(file, shdrs.data(), header.e_shnum, load_delta, policy.resolve_symbol);
  }

  if (ok) {
    result.load_base = VirtualAddress(region_base);
    result.total_size = region_size;
    result.entry_point = VirtualAddress(header.e_entry + load_delta);
    result.ok = true;
  } else {
    early_print_fmt("LoadElfFromVfs: Failed\r\n");
  }

  if (switch_as) {
    HAL_VMM_SwitchAddressSpace(caller_root);
    HAL_CPU_EnableInterrupts();
  }

  KE_VFS_CloseFile(kstd::move(file));
  return result;
}
