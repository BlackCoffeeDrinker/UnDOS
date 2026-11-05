#include <string_view.hpp>

#include <Kernel.hpp>
#include <kernel/boot/boot_info.hpp>
#include <kernel/spinlock.hpp>

#include "pmm.hpp"
#include "pmm_accounting.hpp"

using kernel::PhysicalAddress;
using kernel::VirtualAddress;

namespace {
constexpr size_t BITMAP_MAX_SIZE = 4096;
constexpr size_t MAX_FRAMES = BITMAP_MAX_SIZE * 32;
constexpr uint32_t PAGE_SIZE = 4096;
constexpr uint32_t BITMAP_INIT = 0xFFFFFFFFu;
constexpr size_t BITMAP_WORD_SIZE = 32;
constexpr size_t ISA_DMA_LIMIT = 0x1000000;
constexpr size_t OWNER_CAPACITY = 64;

constexpr size_t frame_to_word(size_t frame) noexcept { return frame / BITMAP_WORD_SIZE; }
constexpr size_t frame_to_bit(size_t frame) noexcept { return frame % BITMAP_WORD_SIZE; }

struct Interval {
  uint64_t start;
  uint64_t end;
};

uint32_t allocation_bitmap[BITMAP_MAX_SIZE];
uint32_t eligible_bitmap[BITMAP_MAX_SIZE];
uint32_t permanent_bitmap[BITMAP_MAX_SIZE];
hal::x86::FrameOwnerLedger<MAX_FRAMES, OWNER_CAPACITY> owner_ledger;
kernel::SpinLock pmm_lock;
size_t total_frames = 0;
size_t bitmap_size_words = 0;
uint64_t total_bytes = 0;
uint64_t free_bytes = 0;
uint64_t unmanaged_bytes = 0;
bool initialized = false;
bool ownership_valid = false;
bool shared_ownership_unsupported = false;

void set_bit(uint32_t *bitmap, size_t frame) noexcept {
  bitmap[frame_to_word(frame)] |= (1u << frame_to_bit(frame));
}
void clear_bit(uint32_t *bitmap, size_t frame) noexcept {
  bitmap[frame_to_word(frame)] &= ~(1u << frame_to_bit(frame));
}
bool test_bit(const uint32_t *bitmap, size_t frame) noexcept {
  return (bitmap[frame_to_word(frame)] & (1u << frame_to_bit(frame))) != 0;
}

void release_frame(size_t frame) noexcept {
  if (frame >= total_frames || !test_bit(eligible_bitmap, frame) ||
      !test_bit(allocation_bitmap, frame) || test_bit(permanent_bitmap, frame)) return;
  if (!owner_ledger.release(frame)) ownership_valid = false;
  clear_bit(allocation_bitmap, frame);
  free_bytes += PAGE_SIZE;
}

kstd::string_view map_type_to_string(kernel::MemoryRegionType type) noexcept {
  switch (type) {
    case kernel::MemoryRegionType::Available: return "Available";
    case kernel::MemoryRegionType::Reserved: return "Reserved";
    case kernel::MemoryRegionType::ACPI_Reclaim: return "ACPI Reclaim";
    case kernel::MemoryRegionType::None: return "Not used";
  }
  return "Unknown";
}

void reserve_range(uint64_t start, uint64_t end) noexcept {
  if (start >= end) return;
  const uint64_t clipped_end = end < total_frames ? end : total_frames;
  for (uint64_t frame = start; frame < clipped_end; ++frame) {
    const size_t index = static_cast<size_t>(frame);
    if (!test_bit(eligible_bitmap, index)) continue;
    if (!test_bit(allocation_bitmap, index)) free_bytes -= PAGE_SIZE;
    set_bit(allocation_bitmap, index);
    set_bit(permanent_bitmap, index);
  }
}
}// namespace

namespace hal::x86 {
void report_pmm(const kernel::BootInfoT &boot_info) noexcept {
  KE_Print("Parsing system memory layout with page size {}...", boot_info.page_size);

  for (size_t i = 0; i < boot_info.memory_regions.size(); ++i) {
    const auto &map = boot_info.memory_regions[i];
    if (map.type == kernel::MemoryRegionType::None) continue;
    KE_Print(" * Memory map region {} type: {}, base: 0x{:lx}, length: 0x{:lx}",
             i, map_type_to_string(map.type), static_cast<uintptr_t>(map.base), map.length);
  }
  KE_Print("OS-usable RAM: {} bytes, allocatable: {} bytes, unmanaged: {} bytes",
           total_bytes, free_bytes, unmanaged_bytes);
  if (unmanaged_bytes) KE_Print("PMM: usable RAM exceeds the represented frame capacity");
  for (const auto &region: boot_info.mapped_memory) {
    if (region.type != kernel::MappedMemoryRegionType::None)
      KE_Print("Reserving region at 0x{:x} for 0x{:x} bytes",
               static_cast<uintptr_t>(region.physical_base), region.length);
  }
}

void init_pmm(const kernel::BootInfoT &boot_info) noexcept {
  Interval intervals[32]{};
  size_t interval_count = 0;
  uint64_t max_frame = 0;
  for (const auto &region: boot_info.memory_regions) {
    if (region.type != kernel::MemoryRegionType::Available || !region.length) continue;
    const uint64_t base = static_cast<uintptr_t>(region.base);
    if (base > UINT64_MAX - region.length) continue;
    const uint64_t end_address = base + region.length;
    const uint64_t start = base / PAGE_SIZE + (base % PAGE_SIZE != 0);
    const uint64_t end = end_address / PAGE_SIZE;
    if (start >= end || interval_count == 32) continue;
    intervals[interval_count++] = {start, end};
    if (end > max_frame) max_frame = end;
  }

  for (size_t i = 1; i < interval_count; ++i) {
    const Interval value = intervals[i];
    size_t j = i;
    while (j && intervals[j - 1].start > value.start) {
      intervals[j] = intervals[j - 1];
      --j;
    }
    intervals[j] = value;
  }

  for (size_t i = 0; i < BITMAP_MAX_SIZE; ++i) {
    allocation_bitmap[i] = BITMAP_INIT;
    eligible_bitmap[i] = 0;
    permanent_bitmap[i] = 0;
  }
  owner_ledger.reset();

  const uint64_t represented_limit = max_frame < MAX_FRAMES ? max_frame : MAX_FRAMES;
  total_frames = static_cast<size_t>(represented_limit);
  bitmap_size_words = total_frames / BITMAP_WORD_SIZE +
                      (total_frames % BITMAP_WORD_SIZE != 0);
  total_bytes = free_bytes = unmanaged_bytes = 0;
  constexpr uint64_t LOW_MEMORY_FRAMES = 0x100000 / PAGE_SIZE;
  uint64_t merged_start = 0, merged_end = 0;
  bool have_interval = false;
  const auto account_interval = [](uint64_t start, uint64_t end) {
    if (start >= end) return;
    total_bytes += (end - start) * PAGE_SIZE;
    if (end > MAX_FRAMES) {
      const uint64_t unmanaged_start = start > MAX_FRAMES ? start : MAX_FRAMES;
      unmanaged_bytes += (end - unmanaged_start) * PAGE_SIZE;
    }
    const uint64_t represented_end = end < MAX_FRAMES ? end : MAX_FRAMES;
    for (uint64_t frame = start; frame < represented_end; ++frame) {
      const size_t index = static_cast<size_t>(frame);
      set_bit(eligible_bitmap, index);
      if (frame < LOW_MEMORY_FRAMES) {
        set_bit(permanent_bitmap, index);
      } else {
        clear_bit(allocation_bitmap, index);
        free_bytes += PAGE_SIZE;
      }
    }
  };
  for (size_t i = 0; i < interval_count; ++i) {
    if (!have_interval) {
      merged_start = intervals[i].start;
      merged_end = intervals[i].end;
      have_interval = true;
    } else if (intervals[i].start <= merged_end) {
      if (intervals[i].end > merged_end) merged_end = intervals[i].end;
    } else {
      account_interval(merged_start, merged_end);
      merged_start = intervals[i].start;
      merged_end = intervals[i].end;
    }
  }
  if (have_interval) account_interval(merged_start, merged_end);

  initialized = true;
  ownership_valid = true;
  shared_ownership_unsupported = false;

  // Boot-mapped memory is reserved, but only where firmware reported usable RAM.
  for (const auto &region: boot_info.mapped_memory) {
    if (region.type == kernel::MappedMemoryRegionType::None || !region.length) continue;
    const uint64_t base = static_cast<uintptr_t>(region.physical_base);
    if (base > UINT64_MAX - region.length) continue;
    const uint64_t end = base + region.length;
    reserve_range(base / PAGE_SIZE, end / PAGE_SIZE + (end % PAGE_SIZE != 0));
  }
}
}// namespace hal::x86

UNDOS_HAL_API_DEF void HAL_PMM_ReserveRegion(PhysicalAddress base, size_t length) noexcept {
  if (!length) return;
  const uintptr_t state = pmm_lock.lock_irqsave();
  const uint64_t start = static_cast<uintptr_t>(base) / PAGE_SIZE;
  const uint64_t address = static_cast<uintptr_t>(base);
  const uint64_t end_address = address > UINT64_MAX - length ? UINT64_MAX : address + length;
  reserve_range(start, end_address / PAGE_SIZE + (end_address % PAGE_SIZE != 0));
  pmm_lock.unlock_irqrestore(state);
}

UNDOS_HAL_API_DEF PhysicalAddress HAL_PMM_AllocateFrames(size_t count) noexcept {
  if (!count) return 0;
  const uintptr_t state = pmm_lock.lock_irqsave();
  if (!initialized || count > total_frames) {
    pmm_lock.unlock_irqrestore(state);
    return 0;
  }
  constexpr size_t dma_frames = ISA_DMA_LIMIT / PAGE_SIZE;
  size_t continuous_free = 0, start_frame = 0;
  for (size_t frame = dma_frames; frame < total_frames; ++frame) {
    if (hal::x86::frame_available(frame, eligible_bitmap, allocation_bitmap,
                                  permanent_bitmap)) {
      if (!continuous_free) start_frame = frame;
      if (++continuous_free == count) {
        for (size_t k = start_frame; k < start_frame + count; ++k) {
          set_bit(allocation_bitmap, k);
        }
        free_bytes -= count * PAGE_SIZE;
        pmm_lock.unlock_irqrestore(state);
        return start_frame * PAGE_SIZE;
      }
    } else {
      continuous_free = 0;
    }
  }
  continuous_free = 0;
  for (size_t frame = 0; frame < dma_frames && frame < total_frames; ++frame) {
    if (hal::x86::frame_available(frame, eligible_bitmap, allocation_bitmap,
                                  permanent_bitmap)) {
      if (!continuous_free) start_frame = frame;
      if (++continuous_free == count) {
        for (size_t k = start_frame; k < start_frame + count; ++k) {
          set_bit(allocation_bitmap, k);
        }
        free_bytes -= count * PAGE_SIZE;
        pmm_lock.unlock_irqrestore(state);
        return start_frame * PAGE_SIZE;
      }
    } else {
      continuous_free = 0;
    }
  }
  pmm_lock.unlock_irqrestore(state);
  return 0;
}

UNDOS_HAL_API_DEF PhysicalAddress HAL_PMM_AllocateFramesDMA(size_t count) noexcept {
  if (!count) return 0;
  const uintptr_t state = pmm_lock.lock_irqsave();
  if (!initialized || count > total_frames) {
    pmm_lock.unlock_irqrestore(state);
    return 0;
  }
  constexpr size_t dma_frames = ISA_DMA_LIMIT / PAGE_SIZE;
  size_t continuous_free = 0, start_frame = 0;
  for (size_t frame = 0; frame < dma_frames && frame < total_frames; ++frame) {
    if (!test_bit(allocation_bitmap, frame)) {
      if (!continuous_free) start_frame = frame;
      if (++continuous_free == count) {
        for (size_t k = start_frame; k < start_frame + count; ++k) {
          set_bit(allocation_bitmap, k);
        }
        free_bytes -= count * PAGE_SIZE;
        pmm_lock.unlock_irqrestore(state);
        return start_frame * PAGE_SIZE;
      }
    } else {
      continuous_free = 0;
    }
  }
  pmm_lock.unlock_irqrestore(state);
  return 0;
}

UNDOS_HAL_API_DEF void HAL_PMM_FreeFrames(PhysicalAddress base, size_t count) noexcept {
  if (!count || static_cast<uintptr_t>(base) % PAGE_SIZE) return;
  const uintptr_t state = pmm_lock.lock_irqsave();
  const uint64_t start = static_cast<uintptr_t>(base) / PAGE_SIZE;
  if (start >= total_frames || count > total_frames - start) {
    pmm_lock.unlock_irqrestore(state);
    return;
  }
  for (size_t i = 0; i < count; ++i)
    release_frame(static_cast<size_t>(start + i));
  pmm_lock.unlock_irqrestore(state);
}

UNDOS_HAL_API_DEF bool HAL_PMM_QueryUsage(kernel::PhysicalMemoryUsage *out) noexcept {
  if (!out) return false;
  const uintptr_t state = pmm_lock.lock_irqsave();
  out->totalBytes = total_bytes;
  out->freeBytes = free_bytes;
  out->usedBytes = total_bytes >= free_bytes ? total_bytes - free_bytes : 0;
  out->unmanagedBytes = unmanaged_bytes;
  out->initialized = initialized;
  out->ownershipValid = initialized && ownership_valid;
  out->sharedOwnershipUnsupported = shared_ownership_unsupported;
  const bool ready = initialized;
  pmm_lock.unlock_irqrestore(state);
  return ready;
}

UNDOS_HAL_API_DEF bool HAL_PMM_RegisterOwner(uint32_t pid) noexcept {
  if (!pid) return false;
  const uintptr_t state = pmm_lock.lock_irqsave();
  if (!initialized || !owner_ledger.register_owner(pid)) {
    pmm_lock.unlock_irqrestore(state);
    return false;
  }
  pmm_lock.unlock_irqrestore(state);
  return true;
}

UNDOS_HAL_API_DEF void HAL_PMM_UnregisterOwner(uint32_t pid) noexcept {
  const uintptr_t state = pmm_lock.lock_irqsave();
  if (!owner_ledger.unregister_owner(pid)) ownership_valid = false;
  pmm_lock.unlock_irqrestore(state);
}

UNDOS_HAL_API_DEF bool HAL_PMM_QueryOwnerFrames(uint32_t pid, uint64_t *frames) noexcept {
  if (!frames) return false;
  const uintptr_t state = pmm_lock.lock_irqsave();
  uint64_t count = 0;
  const bool valid = initialized && ownership_valid &&
      owner_ledger.query(pid, count);
  if (valid) *frames = count;
  pmm_lock.unlock_irqrestore(state);
  return valid;
}

UNDOS_HAL_API_DEF bool HAL_PMM_FreeOwnedFrames(
    uint32_t pid, PhysicalAddress base, size_t count) noexcept {
  if (!pid || !count) return false;
  const uintptr_t state = pmm_lock.lock_irqsave();
  const bool freed = owner_ledger.release_region(
      pid, static_cast<uintptr_t>(base), count, PAGE_SIZE, total_frames,
      eligible_bitmap, allocation_bitmap, permanent_bitmap,
      [](size_t frame) { release_frame(frame); });
  pmm_lock.unlock_irqrestore(state);
  return freed;
}

namespace hal::x86 {
bool assign_frame_owner(PhysicalAddress frame, uint32_t pid) noexcept {
  if (!pid) return true;
  if (static_cast<uintptr_t>(frame) % PAGE_SIZE) {
    const uintptr_t state = pmm_lock.lock_irqsave();
    ownership_valid = false;
    pmm_lock.unlock_irqrestore(state);
    return false;
  }
  const uint64_t index = static_cast<uintptr_t>(frame) / PAGE_SIZE;
  const uintptr_t state = pmm_lock.lock_irqsave();
  if (!initialized || index >= total_frames ||
      !test_bit(eligible_bitmap, static_cast<size_t>(index)) ||
      !test_bit(allocation_bitmap, static_cast<size_t>(index))) {
    ownership_valid = false;
    pmm_lock.unlock_irqrestore(state);
    return false;
  }
  const auto assignment = owner_ledger.assign(
      pid, static_cast<size_t>(index),
      test_bit(eligible_bitmap, static_cast<size_t>(index)),
      test_bit(allocation_bitmap, static_cast<size_t>(index)));
  if (assignment == hal::x86::FrameOwnerAssignment::Invalid) {
    ownership_valid = false;
    pmm_lock.unlock_irqrestore(state);
    return false;
  }
  if (assignment == hal::x86::FrameOwnerAssignment::Shared) {
    ownership_valid = false;
    shared_ownership_unsupported = true;
    shared_ownership_unsupported = true;
  }
  pmm_lock.unlock_irqrestore(state);
  return true;
}

bool free_owned_frames(uint32_t pid, PhysicalAddress base, size_t count) noexcept {
  return HAL_PMM_FreeOwnedFrames(pid, base, count);
}
}// namespace hal::x86
