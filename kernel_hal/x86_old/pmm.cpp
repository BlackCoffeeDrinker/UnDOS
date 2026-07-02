
#include <string_view.hpp>

#include <kernel/boot/boot_info.hpp>

using kernel::PhysicalAddress;
using kernel::VirtualAddress;

#include "pmm.hpp"

namespace {
uint32_t *bitmap = nullptr;
size_t total_frames = 0;
size_t bitmap_size_words = 0;

constexpr uint32_t PAGE_SIZE = 4096;
constexpr uint32_t BITMAP_INIT = 0xFFFFFFFFu;
constexpr size_t BITMAP_WORD_SIZE = 32;
constexpr size_t ISA_DMA_LIMIT = 0x1000000;// 16 MB

// Helper inline functions for bit manipulation
constexpr size_t frame_to_word(size_t frame) noexcept { return frame / BITMAP_WORD_SIZE; }
constexpr size_t frame_to_bit(size_t frame) noexcept { return frame % BITMAP_WORD_SIZE; }

void set_bit(size_t frame) noexcept { bitmap[frame_to_word(frame)] |= (1u << frame_to_bit(frame)); }
void clear_bit(size_t frame) noexcept { bitmap[frame_to_word(frame)] &= ~(1u << frame_to_bit(frame)); }
bool test_bit(size_t frame) noexcept { return (bitmap[frame_to_word(frame)] & (1u << frame_to_bit(frame))) != 0; }

kstd::string_view map_type_to_string(kernel::MemoryRegionType type) noexcept {
  switch (type) {
    case kernel::MemoryRegionType::Available:
      return "Available";
    case kernel::MemoryRegionType::Reserved:
      return "Reserved";
    case kernel::MemoryRegionType::ACPI_Reclaim:
      return "ACPI Reclaim";
    case kernel::MemoryRegionType::None:
      return "Not used";
  }
  return "Unknown";
}

VirtualAddress find_highest_virtual_address(const kernel::BootInfoT &boot_info) {
  VirtualAddress highest_vaddr = 0;

  for (const auto &region: boot_info.mapped_memory) {
    if (region.type != kernel::MappedMemoryRegionType::None) {
      if (VirtualAddress region_end = region.virtual_base + region.length;
          region_end > highest_vaddr) {
        highest_vaddr = region_end;
      }
    }
  }

  return highest_vaddr;// This is your truly safe 'boot_allocations_end'
}

uint64_t find_highest_physical_address(const kernel::BootInfoT &boot_info) {
  uint64_t max_memory = 0;

  for (const auto &region: boot_info.memory_regions) {
    if (region.type == kernel::MemoryRegionType::Available) {
      if (const uint64_t region_end = static_cast<uintptr_t>(region.base) + region.length;
          region_end > max_memory) {
        max_memory = region_end;
      }
    }
  }

  return max_memory;
}

}// namespace

namespace hal::x86 {
void init_pmm(const kernel::BootInfoT &boot_info) noexcept {
  uint64_t max_memory = 0;
  early_print_fmt("Parsing system memory layout with page size {}...\n\r", boot_info.page_size);

  for (size_t i = 0; i < boot_info.memory_regions.size(); ++i) {
    const auto &map = boot_info.memory_regions[i];
    if (map.type == kernel::MemoryRegionType::None) continue;
    early_print_fmt("Memory map region {} type: {}, base: 0x{:lx}, length: 0x{:lx}\n\r", i, map_type_to_string(map.type), static_cast<uintptr_t>(map.base), map.length);
  }

  // 1. Find the highest physical address to calculate total required frames
  max_memory = find_highest_physical_address(boot_info);

  early_print_fmt("Total available memory: 0x{:lx}\n\r", max_memory);

  // Explicitly cast the division result to size_t.
  total_frames = static_cast<size_t>(max_memory / PAGE_SIZE);
  bitmap_size_words = total_frames / 32;
  if (total_frames % 32 != 0) bitmap_size_words++;

  // 2. Find a safe spot to place the bitmap array itself!
  bitmap = find_highest_virtual_address(boot_info).as_ptr<uint32_t>();

  // Initialize the entire bitmap as "fully reserved/used" for safety
  for (size_t i = 0; i < bitmap_size_words; ++i) {
    bitmap[i] = BITMAP_INIT;
  }

  // 3. Mark only the actual available RAM regions as "free" (0)
  for (const auto &region: boot_info.memory_regions) {
    if (region.type == kernel::MemoryRegionType::Available) {
      const auto start_frame = static_cast<uintptr_t>(region.base) / PAGE_SIZE;
      const auto frame_count = static_cast<size_t>(region.length / PAGE_SIZE);

      for (size_t f = 0; f < frame_count; ++f) {
        clear_bit(start_frame + f);
      }
    }
  }

  // 4. Protect the kernel memory space and the bitmap itself
  for (const auto &region: boot_info.mapped_memory) {
    if (region.type != kernel::MappedMemoryRegionType::None) {
      HAL_PMM_ReserveRegion(region.physical_base, region.length);
    }
  }

  early_print("Physical Memory Mechanism online!\n\r");
}
}// namespace hal::x86

UNDOS_HAL_API void HAL_PMM_ReserveRegion(PhysicalAddress base, size_t length) noexcept {
  early_print_fmt("Reserving region at 0x{:x} for 0x{:x} bytes\n\r", static_cast<uintptr_t>(base), length);

  const size_t start_frame = static_cast<uintptr_t>(base) / PAGE_SIZE;
  size_t frame_count = length / PAGE_SIZE;
  if (length % PAGE_SIZE != 0) frame_count++;

  for (size_t i = 0; i < frame_count; ++i) {
    set_bit(start_frame + i);
  }
}

UNDOS_HAL_API PhysicalAddress HAL_PMM_AllocateFrames(size_t count) noexcept {
  if (count == 0) return 0;

  const size_t max_frames = bitmap_size_words * 32;
  constexpr size_t dma_frames = ISA_DMA_LIMIT / PAGE_SIZE;

  size_t continuous_free = 0;
  size_t start_frame = 0;

  // Prefer allocation above ISA DMA region
  for (size_t frame = dma_frames; frame < max_frames; ++frame) {
    if (continuous_free == 0 && (frame % 32 == 0)) {
      if (bitmap[frame / 32] == BITMAP_INIT) {
        frame += 31;
        continue;
      }
    }

    if (!test_bit(frame)) {
      if (continuous_free == 0) start_frame = frame;
      continuous_free++;
      if (continuous_free == count) {
        for (size_t k = start_frame; k < start_frame + count; ++k) set_bit(k);
        return start_frame * PAGE_SIZE;
      }
    } else {
      continuous_free = 0;
    }
  }

  // Fallback to DMA region if needed
  // For now, let's allow it as a fallback to avoid OOM if memory is tight,
  // but ISA DMA drivers should use the dedicated function.
  continuous_free = 0;
  for (size_t frame = 0; frame < dma_frames && frame < max_frames; ++frame) {
    if (!test_bit(frame)) {
      if (continuous_free == 0) start_frame = frame;
      continuous_free++;
      if (continuous_free == count) {
        for (size_t k = start_frame; k < start_frame + count; ++k) set_bit(k);
        return start_frame * PAGE_SIZE;
      }
    } else {
      continuous_free = 0;
    }
  }

  early_print_fmt("Fail to allocate {} frames!\n\r", count);
  return 0;
}

UNDOS_HAL_API PhysicalAddress HAL_PMM_AllocateFramesDMA(size_t count) noexcept {
  if (count == 0) return 0;

  const size_t max_frames = bitmap_size_words * 32;
  constexpr size_t dma_frames = ISA_DMA_LIMIT / PAGE_SIZE;

  size_t continuous_free = 0;
  size_t start_frame = 0;

  for (size_t frame = 0; frame < dma_frames && frame < max_frames; ++frame) {
    if (!test_bit(frame)) {
      if (continuous_free == 0) start_frame = frame;
      continuous_free++;
      if (continuous_free == count) {
        for (size_t k = start_frame; k < start_frame + count; ++k) set_bit(k);
        return start_frame * PAGE_SIZE;
      }
    } else {
      continuous_free = 0;
    }
  }

  return 0;
}

UNDOS_HAL_API void HAL_PMM_FreeFrames(PhysicalAddress base, size_t count) noexcept {
  const size_t start_frame = static_cast<uintptr_t>(base) / PAGE_SIZE;
  for (size_t i = 0; i < count; ++i) {
    clear_bit(start_frame + i);
  }
}
