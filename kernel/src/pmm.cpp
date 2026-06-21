
#include "Core.hpp"

#include "pmm.hpp"

namespace kernel::pmm {
static uint32_t *bitmap = nullptr;
static size_t total_frames = 0;
static size_t bitmap_size_words = 0;
static uint32_t page_size = 0;

constexpr uint32_t BITMAP_INIT = 0xFFFFFFFFu;

// Helper inline functions for bit manipulation
inline void set_bit(size_t frame) noexcept { bitmap[frame / 32] |= (1u << (frame % 32)); }
inline void clear_bit(size_t frame) noexcept { bitmap[frame / 32] &= ~(1u << (frame % 32)); }
inline bool test_bit(size_t frame) noexcept { return (bitmap[frame / 32] & (1u << (frame % 32))) != 0; }

void init(const boot_info_t &boot_info) {
  uint64_t max_memory = 0;

  page_size = boot_info.page_size;

  // 1. Find the highest physical address to calculate total required frames
  for (size_t i = 0; i < boot_info.memory_map_count; ++i) {
    if (const auto &region = boot_info.memory_map[i];
        region.type == MemoryRegionType::AVAILABLE) {
      if (const auto region_end = region.base + region.length;
          region_end > max_memory) {
        max_memory = region_end;
      }
    }
  }

  arch::early_print_fmt("Total available memory: 0x{:x}\n\r", max_memory);

  // Explicitly cast the division result to size_t.
  // On 32-bit non-PAE hardware, total frames will comfortably fit in 32 bits (max 1,048,576 frames for 4GB).
  total_frames = static_cast<size_t>(max_memory / page_size);
  bitmap_size_words = total_frames / 32;
  if (total_frames % 32 != 0) bitmap_size_words++;

  // 2. Find a safe spot to place the bitmap array itself!
  bitmap = reinterpret_cast<uint32_t *>(boot_info.kernel_virtual_end);

  // Initialize the entire bitmap as "fully reserved/used" for safety
  for (size_t i = 0; i < bitmap_size_words; ++i) {
    bitmap[i] = BITMAP_INIT;
  }

  // 3. Mark only the actual available RAM regions as "free" (0)
  for (size_t i = 0; i < boot_info.memory_map_count; ++i) {
    const auto &region = boot_info.memory_map[i];
    if (region.type == MemoryRegionType::AVAILABLE) {
      // Explicitly cast 64-bit bootloader fields down to size_t page frame indices
      const auto start_frame = static_cast<size_t>(region.base / page_size);
      const auto frame_count = static_cast<size_t>(region.length / page_size);

      for (size_t f = 0; f < frame_count; ++f) {
        clear_bit(start_frame + f);
      }
    }
  }

  // 4. Protect the kernel memory space and the bitmap itself so they aren't overwritten!
  const uintptr_t pmm_end = boot_info.kernel_physical_end + (bitmap_size_words * sizeof(uint32_t));
  reserve_region(boot_info.kernel_physical_start, pmm_end - boot_info.kernel_physical_start);

  arch::early_print("Physical Memory Manager online!\n");
}

UNDOS_KERNEL_API void reserve_region(uintptr_t base, size_t length) {
  const size_t start_frame = base / page_size;
  size_t frame_count = length / page_size;
  if (length % page_size != 0) frame_count++;

  for (size_t i = 0; i < frame_count; ++i) {
    set_bit(start_frame + i);
  }
}

UNDOS_KERNEL_API uintptr_t allocate_frame() {
  // Linear scan for the first free bit (0)
  for (size_t i = 0; i < bitmap_size_words; ++i) {
    if (bitmap[i] != BITMAP_INIT) {
      // If this entire 32-frame chunk isn't full
      for (size_t bit = 0; bit < 32; ++bit) {
        if (const size_t frame = i * 32 + bit; !test_bit(frame)) {
          set_bit(frame);
          return frame * page_size;// Return the physical address
        }
      }
    }
  }

  return 0;// Out of memory
}

UNDOS_KERNEL_API void free_frame(uintptr_t physical_address) {
  const size_t frame = physical_address / page_size;
  clear_bit(frame);
}
}// namespace kernel::pmm
