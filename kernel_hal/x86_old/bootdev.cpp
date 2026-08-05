#include <kernel/hal/x86/bios.hpp>
#include <kernel/hal/x86/bootdev.hpp>

#include <stddef.h>
#include <stdint.h>

namespace {

// Low-memory scratch for the Disk Address Packet + sector transfer buffer.
// Segment 0x1000 -> linear 0x00010000. Stays clear of the IVT/BDA at the
// bottom of memory and of the v86 stub/stack at segment 0x2000
// (linear 0x00020000-0x0002F000, see v86.cpp).
constexpr uint16_t kScratchSegment = 0x1000;
constexpr uint16_t kDapOffset = 0x0000;
constexpr uint16_t kBufferOffset = 0x0010;// immediately after the 16-byte DAP

constexpr uint32_t kSectorSize = 512;
// 64 sectors = 32 KiB; DAP(16) + buffer(32 KiB) ends at 0x18010, well below
// the v86 stub at 0x20000.
constexpr uint32_t kMaxSectorsPerCall = 64;

constexpr uint16_t kCarryFlag = 1u << 0;
constexpr uint8_t kBiosDiskInt = 0x13;
constexpr uint8_t kFirstHardDisk = 0x80;

inline uint32_t linear_addr(uint16_t segment, uint16_t offset) noexcept {
  return (static_cast<uint32_t>(segment) << 4) + offset;
}

inline volatile uint8_t *byte_ptr(uint16_t segment, uint16_t offset) noexcept {
  return reinterpret_cast<volatile uint8_t *>(linear_addr(segment, offset));
}

inline volatile uint16_t *word_ptr(uint16_t segment, uint16_t offset) noexcept {
  return reinterpret_cast<volatile uint16_t *>(linear_addr(segment, offset));
}

inline volatile uint64_t *qword_ptr(uint16_t segment, uint16_t offset) noexcept {
  return reinterpret_cast<volatile uint64_t *>(linear_addr(segment, offset));
}

inline bool carry_set(const HAL_x86_BiosRegs &regs) noexcept {
  return (regs.flags & kCarryFlag) != 0;
}

// Builds a 16-byte Disk Address Packet at SCRATCH:kDapOffset and points the
// transfer buffer at SCRATCH:kBufferOffset.
void write_dap(uint16_t sector_count, uint64_t start_lba) noexcept {
  volatile uint8_t *dap = byte_ptr(kScratchSegment, kDapOffset);
  dap[0] = 0x10;// size
  dap[1] = 0x00;// reserved
  *word_ptr(kScratchSegment, kDapOffset + 2) = sector_count;
  *word_ptr(kScratchSegment, kDapOffset + 4) = kBufferOffset;
  *word_ptr(kScratchSegment, kDapOffset + 6) = kScratchSegment;
  *qword_ptr(kScratchSegment, kDapOffset + 8) = start_lba;
}

bool read_sectors_chunk(uint32_t lba, uint16_t count, uint8_t *dest) noexcept {
  write_dap(count, lba);

  HAL_x86_BiosRegs regs{};
  regs.ax = 0x4200;// AH=0x42 extended read
  regs.dx = kFirstHardDisk;
  regs.si = kDapOffset;
  regs.ds = kScratchSegment;

  HAL_x86_ExecBiosInt(kBiosDiskInt, regs);
  if (carry_set(regs)) {
    return false;
  }

  const size_t bytes = static_cast<size_t>(count) * kSectorSize;
  volatile uint8_t *src = byte_ptr(kScratchSegment, kBufferOffset);
  for (size_t i = 0; i < bytes; ++i) {
    dest[i] = src[i];
  }
  return true;
}

}// namespace

UNDOS_HAL_API_DEF bool HAL_BOOTDEV_IsPresent() noexcept {
  HAL_x86_BiosRegs regs{};
  regs.ax = 0x4100;// AH=0x41 check extensions present
  regs.bx = 0x55AA;
  regs.dx = kFirstHardDisk;

  HAL_x86_ExecBiosInt(kBiosDiskInt, regs);

  // Success: CF clear and BX magic flipped to 0xAA55.
  if (carry_set(regs)) {
    return false;
  }
  return regs.bx == 0xAA55;
}

UNDOS_HAL_API_DEF bool HAL_BOOTDEV_ReadSectors(uint32_t lba, uint32_t count, kstd::span<uint8_t> buffer) noexcept {
  if (count == 0) {
    return true;
  }

  const uint64_t bytes_needed = static_cast<uint64_t>(count) * kSectorSize;
  if (buffer.size() < bytes_needed) {
    return false;
  }

  uint32_t remaining = count;
  uint32_t current_lba = lba;
  size_t out_off = 0;

  while (remaining > 0) {
    const uint32_t chunk = remaining > kMaxSectorsPerCall ? kMaxSectorsPerCall : remaining;
    if (!read_sectors_chunk(current_lba, static_cast<uint16_t>(chunk), buffer.data() + out_off)) {
      return false;
    }
    remaining -= chunk;
    current_lba += chunk;
    out_off += static_cast<size_t>(chunk) * kSectorSize;
  }

  return true;
}
