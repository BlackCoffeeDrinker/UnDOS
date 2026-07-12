
#include "driver_loader.hpp"

#include "elf_loader.hpp"
#include "elf_segment_loader.hpp"
#include "module/symbol_table.hpp"
#include "pnp_manager.hpp"
#include "stdkrn.hpp"
#include "vmm.hpp"

#include <Kernel.hpp>
#include <kernel/elf.hpp>
#include <memory.hpp>
#include <new.hpp>
#include <system_error.hpp>

namespace {
kernel::driver::SymbolTable g_SymbolTable;

enum class ElfError {
  Success = 0,
  InvalidHeader,
  BoundsViolation,
  RelocationFailure,
  AllocationFailure
};

// Plain function pointer wrapping g_SymbolTable::Resolve, since cfunc only
// wraps C-style function pointers (no captures).
uintptr_t ResolveSymbol(kstd::string_view name) {
  return g_SymbolTable.Resolve(name);
}

// Copies a matched ".driver_name" section's bytes into the KDriverObject
// pointed to by `context`, before elf_loader frees its temporary read
// buffer.
void CaptureDriverName(void *context, kstd::string_view name, const uint8_t *data, size_t) {
  if (name != ".driver_name") return;
  auto *driver_object = static_cast<kernel::KObjectPtr<kernel::KDriverObject> *>(context);
  (*driver_object)->name = reinterpret_cast<const char *>(data);
}

ElfError Driver_Load_From_Memory(const uint8_t *raw_blob, size_t blob_size, kernel::KObjectPtr<kernel::KDriverObject> &out_driver) {
  if (blob_size < sizeof(hal::Elf32_Ehdr)) return ElfError::InvalidHeader;
  const auto *header = reinterpret_cast<const hal::Elf32_Ehdr *>(raw_blob);
  if (header->e_ident[0] != 0x7F || header->e_ident[1] != 'E' ||
      header->e_ident[2] != 'L' || header->e_ident[3] != 'F') {
    return ElfError::InvalidHeader;
  }

  // Metadata extraction (not copied to memory)
  if (header->e_shstrndx < header->e_shnum) {
    const auto *shdrs = reinterpret_cast<const hal::Elf32_Shdr *>(raw_blob + header->e_shoff);
    const auto &shstrtab = shdrs[header->e_shstrndx];
    const auto shstr = reinterpret_cast<const char *>(raw_blob + shstrtab.sh_offset);

    for (size_t i = 0; i < header->e_shnum; ++i) {
      if (const char *s_name = shstr + shdrs[i].sh_name;
          kstd::string_view(s_name) == ".driver_name") {
        const char *d_name = reinterpret_cast<const char *>(raw_blob + shdrs[i].sh_offset);
        out_driver->name = d_name;
        break;
      }
    }
  }

  const uint64_t phdr_end = static_cast<uint64_t>(header->e_phoff) +
                            (static_cast<uint64_t>(header->e_phnum) * header->e_phentsize);
  if (phdr_end > blob_size) return ElfError::BoundsViolation;

  const uint64_t shdr_end = static_cast<uint64_t>(header->e_shoff) +
                            (static_cast<uint64_t>(header->e_shnum) * header->e_shentsize);
  if (shdr_end > blob_size) return ElfError::BoundsViolation;

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

  if (max_vaddr <= min_vaddr) return ElfError::InvalidHeader;

  const size_t total_span = max_vaddr - min_vaddr;
  auto &kas = KE_VMM_GetKernelAddressSpace();

  void *assigned_ptr = KE_VMM_AllocateRegion(kas, total_span, kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE);
  if (!assigned_ptr) return ElfError::AllocationFailure;

  const auto assigned_virtual_base = reinterpret_cast<uintptr_t>(assigned_ptr);
  const uintptr_t load_delta = assigned_virtual_base - min_vaddr;

  for (size_t i = 0; i < header->e_phnum; ++i) {
    const auto &ph = phdrs[i];
    if (ph.p_type != hal::PT_LOAD) continue;

    auto perms = kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE;
    if (ph.p_flags & 0x1) perms = perms | kernel::vmm::ProtectFlags::EXECUTE;

    if (const uintptr_t segment_virt_start = ph.p_vaddr + load_delta;
        !kernel::elf::MapAndCopySegment(segment_virt_start, raw_blob + ph.p_offset, ph.p_filesz, ph.p_memsz, perms)) {
      return ElfError::AllocationFailure;
    }
  }

  if (!kernel::elf::ProcessRelocationsFromBlob(raw_blob, load_delta, kernel::cfunc<uintptr_t(kstd::string_view)>(ResolveSymbol))) {
    return ElfError::RelocationFailure;
  }

  out_driver->load_base = assigned_virtual_base;
  out_driver->total_size = total_span;
  out_driver->entry_point = reinterpret_cast<void (*)(kernel::KObjectPtr<kernel::KDriverObject> &)>(header->e_entry + load_delta);

  return ElfError::Success;
}
}// namespace

namespace kernel::driver {
void init(const BootInfoT &boot_info) {
  early_print_fmt("Loading symbols...\r\n");
  for (const auto &sym: boot_info.boot_symbols) {
    if (sym.name.empty()) continue;
    g_SymbolTable.Register(sym.name, static_cast<uintptr_t>(sym.address));
  }

  // Load all boot drivers
  early_print_fmt("Loading boot drivers...\r\n");
  const auto base = KE_OB_LookupObjectOfType<KDirectoryObject>("/System/Initial/BootModules");
  for (const auto &driver: boot_info.boot_modules) {
    if (driver.name.empty() || driver.base_physical == 0 || driver.length == 0) continue;
    auto module = CreateKObject<kernel::KModuleObject>(driver.name);
    module->base_physical = driver.base_physical;
    module->length = driver.length;
    KE_OB_InsertObject(base, module);
  }

  early_print_fmt("Loading boot drivers complete.\r\n");
}

KObjectPtr<KDriverObject> load_from_memory(PhysicalAddress module_base, size_t module_size) {
  if (auto driver_object = kernel::CreateKObject<KDriverObject>("<tmpname>")) {
    const auto ec = Driver_Load_From_Memory(
        module_base.as_ptr<const uint8_t>(),
        module_size,
        driver_object);

    if (ec != ElfError::Success) {
      early_print_fmt("Driver Loader: Failed to load ELF: {}\n\r", static_cast<uint8_t>(ec));
      return nullptr;
    }

    driver_object->entry_point(driver_object);

    // TODO At some point, enable this: this will require putting the device entry into a different section so we can unload that section completly
    if (false) {
      const auto entry_addr = reinterpret_cast<uintptr_t>(driver_object->entry_point.fn);
      const auto page_addr = entry_addr & ~0xFFFUL;

      const kernel::PhysicalAddress phys = HAL_VMM_GetPhysicalAddress(kernel::VirtualAddress(page_addr));
      if (phys) {
        HAL_VMM_UnmapPage(kernel::VirtualAddress(page_addr));
        HAL_VMM_Flush(kernel::VirtualAddress(page_addr));
        HAL_PMM_FreeFrames(phys, 1);
      }

      driver_object->entry_point = nullptr;
    }

    KE_OB_InsertObject(
        KE_OB_GetDriverDirectory(),
        driver_object);

    // Register Driver
    KE_PNP_RegisterDriver(driver_object);
    return driver_object;
  }

  return nullptr;
}
}// namespace kernel::driver

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KDriverObject> KE_DRIVER_Load(const kstd::string_view &path) noexcept {
  if (path.empty()) {
    return nullptr;
  }

  auto driver_object = kernel::CreateKObject<kernel::KDriverObject>("<tmpname>");
  if (!driver_object) {
    early_print_fmt("KE_DRIVER_Load: Failed to create driver object\r\n");
    return nullptr;
  }

  kernel::elf::LoadPolicy policy;
  policy.relocatable = true;
  policy.context = &driver_object;
  policy.resolve_symbol = kernel::cfunc<uintptr_t(kstd::string_view)>(ResolveSymbol);
  policy.capture_section = kernel::cfunc<void(void *, kstd::string_view, const uint8_t *, size_t)>(CaptureDriverName);

  const auto result = kernel::elf::LoadElfFromVfs(path, policy);
  if (!result.ok) {
    early_print_fmt("KE_DRIVER_Load: Failed to load driver from path {}\r\n", path);
    return nullptr;
  }

  driver_object->load_base = result.load_base;
  driver_object->total_size = result.total_size;
  driver_object->entry_point = reinterpret_cast<void (*)(kernel::KObjectPtr<kernel::KDriverObject> &)>(result.entry_point.value);
  driver_object->entry_point(driver_object);
  
  // TODO: Remove the entry point from memory once it's done executing
  

  KE_OB_InsertObject(KE_OB_GetDriverDirectory(), driver_object);
  KE_PNP_RegisterDriver(driver_object);
  return driver_object;
}
