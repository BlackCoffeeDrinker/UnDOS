
#pragma once
#include <kernel/__core.hpp>

namespace kernel {
enum class MemoryRegionType : uint32_t {
  AVAILABLE = 1,   // Usable RAM for the PMM / Allocator
  RESERVED = 2,    // Hardware, ACPI tables, or bad memory
  ACPI_RECLAIM = 3,// Safe to reclaim after ACPI initialization
  BOOTLOADER = 4   // Memory used by the bootloader itself
};

struct memory_region_t {
  uint64_t base;// 64-bit to support physical memory expansion beyond 4GB (PAE/x86_64)
  uint64_t length;
  MemoryRegionType type;
};

struct boot_info_t {
  uint32_t page_size;
  uint32_t hal_more_into_addr;

  const memory_region_t *memory_map;
  size_t memory_map_count;

  uintptr_t kernel_physical_start;
  uintptr_t kernel_physical_end;
  uintptr_t kernel_virtual_start;
  uintptr_t kernel_virtual_end;

  uintptr_t hal_virtual_start;
  uintptr_t hal_virtual_end;

  const char *command_line;// Raw boot arguments string
};
}// namespace kernel
