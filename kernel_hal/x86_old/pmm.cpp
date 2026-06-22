
#include <string_view.hpp>

#include <kernel/boot/boot_info.hpp>
#include <kernel/hal/early_debug.hpp>
#include <kernel/hal/physical_memory.hpp>

#include "pmm.hpp"


namespace {
uint32_t *bitmap = nullptr;
size_t total_frames = 0;
size_t bitmap_size_words = 0;
uint32_t page_size = 0;

constexpr uint32_t BITMAP_INIT = 0xFFFFFFFFu;
constexpr size_t BITMAP_WORD_SIZE = 32;

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

uintptr_t find_highest_virtual_address(const kernel::boot_info_t &boot_info) {
  uintptr_t highest_vaddr = 0;

  for (const auto &region: boot_info.mapped_memory) {
    if (region.type != kernel::MappedMemoryRegionType::None) {
      uintptr_t region_end = region.virtual_base + region.length;
      if (region_end > highest_vaddr) {
        highest_vaddr = region_end;
      }
    }
  }

  return highest_vaddr;// This is your truly safe 'boot_allocations_end'
}

uint64_t find_highest_physical_address(const kernel::boot_info_t &boot_info) {
  uint64_t max_memory = 0;

  for (const auto &region: boot_info.memory_map) {
    if (region.type == kernel::MemoryRegionType::Available) {
      if (const auto region_end = region.base + region.length;
          region_end > max_memory) {
        max_memory = region_end;
      }
    }
  }

  return max_memory;
}

}// namespace

namespace hal::x86 {
void init_pmm(const kernel::boot_info_t &boot_info) noexcept {
  uint64_t max_memory = 0;
  early_print_fmt("Parsing system memory layout with page size {}...\n\r", boot_info.page_size);

  for (size_t i = 0; i < boot_info.memory_map.size(); ++i) {
    const auto &map = boot_info.memory_map[i];
    if (map.type == kernel::MemoryRegionType::None) continue;
    early_print_fmt("Memory map region {} type: {}, base: 0x{:lx}, length: 0x{:lx}\n\r", i, map_type_to_string(map.type), map.base, map.length);
  }

  page_size = boot_info.page_size;

  // 1. Find the highest physical address to calculate total required frames
  max_memory = find_highest_physical_address(boot_info);

  early_print_fmt("Total available memory: 0x{:lx}\n\r", max_memory);

  // Explicitly cast the division result to size_t.
  total_frames = static_cast<size_t>(max_memory / page_size);
  bitmap_size_words = total_frames / 32;
  if (total_frames % 32 != 0) bitmap_size_words++;

  // 2. Find a safe spot to place the bitmap array itself!
  bitmap = reinterpret_cast<uint32_t *>(find_highest_virtual_address(boot_info));

  // Initialize the entire bitmap as "fully reserved/used" for safety
  for (size_t i = 0; i < bitmap_size_words; ++i) {
    bitmap[i] = BITMAP_INIT;
  }

  // 3. Mark only the actual available RAM regions as "free" (0)
  for (const auto &region: boot_info.memory_map) {
    if (region.type == kernel::MemoryRegionType::Available) {
      const auto start_frame = static_cast<size_t>(region.base / page_size);
      const auto frame_count = static_cast<size_t>(region.length / page_size);

      for (size_t f = 0; f < frame_count; ++f) {
        clear_bit(start_frame + f);
      }
    }
  }

  // 4. Protect the kernel memory space and the bitmap itself
  for (const auto &region: boot_info.mapped_memory) {
    if (region.type != kernel::MappedMemoryRegionType::None) {
      PMM_Reserve_Region(region.physical_base, region.length);
    }
  }

  early_print("Physical Memory Mechanism online!\n\r");
}
}// namespace hal::x86

UNDOS_HAL_API void PMM_Reserve_Region(uintptr_t base, size_t length) noexcept {
  if (page_size == 0) {
    return;
  }

  early_print_fmt("Reserving region at 0x{:x} for 0x{:x} bytes\n\r", base, length);

  const size_t start_frame = base / page_size;
  size_t frame_count = length / page_size;
  if (length % page_size != 0) frame_count++;

  for (size_t i = 0; i < frame_count; ++i) {
    set_bit(start_frame + i);
  }
}

UNDOS_HAL_API uintptr_t PMM_Allocate_Frames(size_t count) noexcept {
  if (count == 0) return 0;

  const size_t max_frames = bitmap_size_words * 32;
  size_t continuous_free = 0;
  size_t start_frame = 0;

  for (size_t frame = 0; frame < max_frames; ++frame) {
    // Optimization: If we aren't currently tracking a continuous free block,
    // and we are aligned to a word boundary, check if the entire word is full.
    if (continuous_free == 0 && (frame % 32 == 0)) {
      if (bitmap[frame / 32] == BITMAP_INIT) {
        frame += 31;// Skip this word. The loop's ++frame will advance it to the next word.
        continue;
      }
    }

    // Check if the current frame is free (0)
    if (!test_bit(frame)) {
      if (continuous_free == 0) {
        start_frame = frame;// Record where this potential block begins
      }
      continuous_free++;

      // We found a large enough contiguous block!
      if (continuous_free == count) {
        // Mark all frames in the block as allocated (1)
        for (size_t k = start_frame; k < start_frame + count; ++k) {
          set_bit(k);
        }
        return start_frame * page_size;
      }
    } else {
      // The chain broke; reset the counter and keep searching
      continuous_free = 0;
    }
  }

  early_print_fmt("Fail to allocate {} frames!\n\r", count);
  return 0;// Out of memory
}

UNDOS_HAL_API void PMM_Free_Frame(uintptr_t physical_address) noexcept {
  if (page_size == 0) return;
  const size_t frame = physical_address / page_size;
  clear_bit(frame);
}
