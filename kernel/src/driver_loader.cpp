
#include "driver_loader.hpp"

#include "pnp_manager.hpp"
#include "stdkrn.hpp"
#include "vmm.hpp"
#include <Kernel.hpp>
#include <kernel/elf.hpp>
#include <memory.hpp>
#include <new.hpp>

namespace {
kernel::SymbolTable g_SymbolTable;

bool MapAndCopy(uintptr_t virt_start, const uint8_t *data, size_t file_size, size_t mem_size, kernel::vmm::ProtectFlags flags) {
  const uintptr_t start_page = virt_start & ~0xFFFUL;
  const uintptr_t end_page = (virt_start + mem_size + 4095) & ~0xFFFUL;

  for (uintptr_t page = start_page; page < end_page; page += 4096) {
    auto phys = HAL_PMM_AllocateFrames(1);
    if (!phys) return false;

    if (!HAL_VMM_MapPage(kernel::VirtualAddress(page), phys, flags)) {
      HAL_PMM_FreeFrames(phys, 1);
      return false;
    }
    HAL_VMM_Flush(kernel::VirtualAddress(page));

    __builtin_memset(reinterpret_cast<void *>(page), 0, 4096);
  }

  __builtin_memcpy(reinterpret_cast<void *>(virt_start), data, file_size);
  return true;
}

void ApplyRelocation(const hal::Elf32_Rel &rel, const hal::Elf32_Sym *syms, const char *strings, uintptr_t delta) {
  const uint32_t type = hal::ELF32_R_TYPE(rel.r_info);
  const uint32_t sym_idx = hal::ELF32_R_SYM(rel.r_info);
  const auto &sym = syms[sym_idx];

  uintptr_t sym_val = 0;
  if (sym.st_shndx == 0) {// SHN_UNDEF
    const char *name = strings + sym.st_name;
    sym_val = g_SymbolTable.Resolve(name);
    if (sym_val == 0) {
      early_print_fmt("Driver Loader: Unresolved symbol: {}\n", name);
      return;
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
}

void ProcessRelocations(const uint8_t *raw_blob, uintptr_t delta) {
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
        ApplyRelocation(rels[r], syms, strings, delta);
      }
    }
  }
}
}// namespace

namespace kernel {
SymbolTable::~SymbolTable() {
  symbols.clear([](SymbolEntry *entry) {
    delete entry;
  });
}

void SymbolTable::Register(kstd::string_view name, uintptr_t address) {
  auto entry = KE_CreateObject<SymbolEntry>();
  entry->name = name;
  entry->address = address;
  symbols.insert(*entry);
}

uintptr_t SymbolTable::Resolve(kstd::string_view name) const {
  const auto *entry = symbols.find(name);
  return entry ? entry->address : 0;
}

bool KE_DRIVER_Init(const kernel::BootInfoT &boot_info) {
  for (const auto &sym: boot_info.boot_symbols) {
    if (sym.name.empty()) continue;
    g_SymbolTable.Register(sym.name, static_cast<uintptr_t>(sym.address));
  }
  return true;
}

ElfResult Driver_Load_From_Memory(const uint8_t *raw_blob, size_t blob_size, kernel::KObjectPtr<kernel::KDriverObject> &out_driver) {
  if (blob_size < sizeof(hal::Elf32_Ehdr)) return ElfResult::InvalidHeader;
  const auto *header = reinterpret_cast<const hal::Elf32_Ehdr *>(raw_blob);
  if (header->e_ident[0] != 0x7F || header->e_ident[1] != 'E' ||
      header->e_ident[2] != 'L' || header->e_ident[3] != 'F') {
    return ElfResult::InvalidHeader;
  }

  const uint64_t phdr_end = static_cast<uint64_t>(header->e_phoff) +
                            (static_cast<uint64_t>(header->e_phnum) * header->e_phentsize);
  if (phdr_end > blob_size) return ElfResult::BoundsViolation;

  const uint64_t shdr_end = static_cast<uint64_t>(header->e_shoff) +
                            (static_cast<uint64_t>(header->e_shnum) * header->e_shentsize);
  if (shdr_end > blob_size) return ElfResult::BoundsViolation;

  uintptr_t min_vaddr = 0xFFFFFFFF;
  uintptr_t max_vaddr = 0;
  const auto *phdrs = reinterpret_cast<const hal::Elf32_Phdr *>(raw_blob + header->e_phoff);

  for (size_t i = 0; i < header->e_phnum; ++i) {
    if (phdrs[i].p_type == hal::PT_LOAD) {
      if (phdrs[i].p_vaddr < min_vaddr) min_vaddr = phdrs[i].p_vaddr;
      if (phdrs[i].p_vaddr + phdrs[i].p_memsz > max_vaddr) {
        max_vaddr = phdrs[i].p_vaddr + phdrs[i].p_memsz;
      }
    }
  }

  if (max_vaddr <= min_vaddr) return ElfResult::InvalidHeader;

  const size_t total_span = max_vaddr - min_vaddr;
  auto &kas = KE_VMM_GetKernelAddressSpace();

  void *assigned_ptr = KE_VMM_AllocateRegion(kas, total_span, vmm::ProtectFlags::READ | vmm::ProtectFlags::WRITE);
  if (!assigned_ptr) return ElfResult::AllocationFailure;

  const auto assigned_virtual_base = reinterpret_cast<uintptr_t>(assigned_ptr);
  const uintptr_t load_delta = assigned_virtual_base - min_vaddr;

  for (size_t i = 0; i < header->e_phnum; ++i) {
    const auto &ph = phdrs[i];
    if (ph.p_type != hal::PT_LOAD) continue;

    auto perms = vmm::ProtectFlags::READ | vmm::ProtectFlags::WRITE;
    if (ph.p_flags & 0x1) perms = perms | vmm::ProtectFlags::EXECUTE;

    if (const uintptr_t segment_virt_start = ph.p_vaddr + load_delta;
        !MapAndCopy(segment_virt_start, raw_blob + ph.p_offset, ph.p_filesz, ph.p_memsz, perms)) {
      return ElfResult::AllocationFailure;
    }
  }

  ProcessRelocations(raw_blob, load_delta);

  // Metadata extraction (not copied to memory)
  if (header->e_shstrndx < header->e_shnum) {
    const auto *shdrs = reinterpret_cast<const hal::Elf32_Shdr *>(raw_blob + header->e_shoff);
    const auto &shstrtab = shdrs[header->e_shstrndx];
    const char *shstr = reinterpret_cast<const char *>(raw_blob + shstrtab.sh_offset);
    for (size_t i = 0; i < header->e_shnum; ++i) {
      const char *s_name = shstr + shdrs[i].sh_name;
      if (kstd::string_view(s_name) == ".driver_name") {
        const char *d_name = reinterpret_cast<const char *>(raw_blob + shdrs[i].sh_offset);
        out_driver->name = d_name;
        break;
      }
    }
  }

  out_driver->load_base = assigned_virtual_base;
  out_driver->total_size = total_span;
  out_driver->entry_point = reinterpret_cast<void (*)(KObjectPtr<KDriverObject> &)>(header->e_entry + load_delta);

  return ElfResult::Success;
}
}// namespace kernel

UNDOS_KERNEL_API void KE_DRIVER_DiscardEntryPoint(kernel::KObjectPtr<kernel::KDriverObject> &driver) {
  if (!driver || !driver->entry_point) return;

  const auto entry_addr = reinterpret_cast<uintptr_t>(driver->entry_point.fn);
  const auto page_addr = entry_addr & ~0xFFFUL;

  const kernel::PhysicalAddress phys = HAL_VMM_GetPhysicalAddress(kernel::VirtualAddress(page_addr));
  if (phys) {
    HAL_VMM_UnmapPage(kernel::VirtualAddress(page_addr));
    HAL_VMM_Flush(kernel::VirtualAddress(page_addr));
    HAL_PMM_FreeFrames(phys, 1);
  }

  driver->entry_point = nullptr;
}

UNDOS_KERNEL_API kernel::KObjectPtr<kernel::KDriverObject> KE_DRIVER_Load(const kstd::string_view &path) {
  if (path.empty()) {
    return nullptr;
  }

  kernel::PhysicalAddress module_base{0};
  size_t module_size{0};

  // Object path?
  if (path[0] == '\\') {
    if (const auto module = KE_OB_LookupObjectOfType<kernel::KModuleObject>(path)) {
      // This is a valid module path!
      module_base = module->base_physical;
      module_size = module->length;
    }
  } else {
    // Try the virtual file subsystem
  }

  // Load it
  if (!module_base || module_size <= 0) {
    return nullptr;
  }

  if (auto driver_object = kernel::CreateKObject<kernel::KDriverObject>()) {
    auto result = kernel::Driver_Load_From_Memory(
        module_base.as_ptr<const uint8_t>(),
        module_size,
        driver_object);

    early_print_fmt("Driver loaded from memory: {}\n\r", driver_object->name);

    if (result != kernel::ElfResult::Success) {
      return nullptr;
    }

    driver_object->entry_point(driver_object);
    // TODO: Unload entry point

    // Register Driver
    KE_PNP_RegisterDriver(driver_object);
    return driver_object;
  }

  return nullptr;
}
