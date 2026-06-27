
#include <Kernel.hpp>
#include <kernel/elf.hpp>
#include "driver_loader.hpp"

namespace kernel {
enum class ElfResult { Success, InvalidHeader, BoundsViolation, AllocationFailure };
/*
ElfResult Ob_LoadDriverModule(const uint8_t* raw_blob, size_t blob_size, KDriverObject* out_driver) {
  // 1. Boundary Sanitization
  if (blob_size < sizeof(hal::Elf32_Ehdr)) return ElfResult::InvalidHeader;
  
  const auto* header = reinterpret_cast<const hal::Elf32_Ehdr *>(raw_blob);
  if (header->e_ident[0] != 0x7F || header->e_ident[1] != 'E' || 
      header->e_ident[2] != 'L'  || header->e_ident[3] != 'F') {
    return ElfResult::InvalidHeader;
  }

  // Ensure program headers are within buffer limits safely without integer overflow
  uint64_t phdr_end = static_cast<uint64_t>(header->e_phoff) + 
                      (static_cast<uint64_t>(header->e_phnum) * header->e_phentsize);
  if (phdr_end > blob_size) {
    return ElfResult::BoundsViolation;
  }

  // 2. Discover total memory span requirements
  uintptr_t min_vaddr = 0xFFFFFFFF;
  uintptr_t max_vaddr = 0;
  const auto* phdrs = reinterpret_cast<const hal::Elf32_Phdr *>(raw_blob + header->e_phoff);

  for (size_t i = 0; i < header->e_phnum; ++i) {
    if (phdrs[i].p_type == 1) { // PT_LOAD
      if (phdrs[i].p_vaddr < min_vaddr) min_vaddr = phdrs[i].p_vaddr;
      if (phdrs[i].p_vaddr + phdrs[i].p_memsz > max_vaddr) {
        max_vaddr = phdrs[i].p_vaddr + phdrs[i].p_memsz;
      }
    }
  }

  size_t total_span = max_vaddr - min_vaddr;
  
  // 3. Request a safe virtual space footprint from the Kernel VMM
  uintptr_t assigned_virtual_base = ReserveVirtualRegion(total_span); 
  uintptr_t load_delta = assigned_virtual_base - min_vaddr;

  // 4. Map, Load, and Enforce Security Protections
  for (size_t i = 0; i < header->e_phnum; ++i) {
    const auto& ph = phdrs[i];
    if (ph.p_type != 1) continue; // Skip non-loadable segments

    // Determine permissions
    auto perms = vmm::ProtectFlags::READ;
    if (ph.p_flags & 0x2) perms = perms | vmm::ProtectFlags::WRITE;   // PF_W
    if (ph.p_flags & 0x1) perms = perms | vmm::ProtectFlags::EXECUTE; // PF_X

    uintptr_t segment_virt_start = ph.p_vaddr + load_delta;
    
    // Direct Demand-Paging Mapping loop using physical allocator 
    // Ensuring page alignment bounds are padded and cleared out safely
    MapAndCopySegment(segment_virt_start, raw_blob + ph.p_offset, ph.p_filesz, ph.p_memsz, perms);
  }

  // 5. Process Relocations & Symbols
  ProcessElfRelocations(raw_blob, load_delta);

  // 6. Secure the Execution Contract
  out_driver->load_base_virtual = assigned_virtual_base;
  out_driver->total_size = total_span;
  out_driver->entry_point = reinterpret_cast<void(*)()>(header->e_entry + load_delta);

  return ElfResult::Success;
}
*/
}
