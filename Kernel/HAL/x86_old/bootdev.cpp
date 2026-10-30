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

struct DiskGeometry {
  uint16_t cylinders = 0;
  uint16_t heads = 0;
  uint16_t sectors_per_track = 0;
};

struct DiskAccess {
  bool edd = false;
  DiskGeometry geometry{};
};

bool probe_edd() noexcept {
  HAL_x86_BiosRegs regs{};
  regs.ax = 0x4100;// AH=0x41 check extensions present
  regs.bx = 0x55AA;
  regs.dx = kFirstHardDisk;

  HAL_x86_ExecBiosInt(kBiosDiskInt, regs);
  return !carry_set(regs) && regs.bx == 0xAA55 && (regs.cx & 0x0001) != 0;
}

bool probe_geometry(DiskGeometry &geometry) noexcept {
  HAL_x86_BiosRegs regs{};
  regs.ax = 0x0800;// AH=0x08 get drive parameters
  regs.dx = kFirstHardDisk;

  HAL_x86_ExecBiosInt(kBiosDiskInt, regs);
  if (carry_set(regs)) {
    return false;
  }

  const uint8_t cl = static_cast<uint8_t>(regs.cx & 0x00FF);
  const uint8_t ch = static_cast<uint8_t>(regs.cx >> 8);
  const uint16_t max_cylinder = static_cast<uint16_t>(
      ch | ((cl & 0xC0) << 2));
  const uint16_t sectors_per_track = cl & 0x3F;
  if (sectors_per_track == 0) {
    return false;
  }

  geometry.cylinders = max_cylinder + 1;
  geometry.heads = static_cast<uint16_t>((regs.dx >> 8) + 1);
  geometry.sectors_per_track = sectors_per_track;
  return true;
}

bool select_disk_access(DiskAccess &access) noexcept {
  if (probe_edd()) {
    access.edd = true;
    return true;
  }

  access.edd = false;
  return probe_geometry(access.geometry);
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

void copy_scratch_chunk(uint16_t count, uint8_t *dest) noexcept {
  const size_t bytes = static_cast<size_t>(count) * kSectorSize;
  const volatile uint8_t *src = byte_ptr(kScratchSegment, kBufferOffset);
  for (size_t i = 0; i < bytes; ++i) {
    dest[i] = src[i];
  }
}

bool read_edd_chunk(uint64_t lba, uint16_t count, uint8_t *dest) noexcept {
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

  copy_scratch_chunk(count, dest);
  return true;
}

bool read_chs_chunk(uint64_t lba, uint16_t count, const DiskGeometry &geometry,
                    uint8_t *dest) noexcept {
  const uint64_t sectors_per_cylinder =
      static_cast<uint64_t>(geometry.sectors_per_track) * geometry.heads;
  const uint64_t cylinder = lba / sectors_per_cylinder;
  const uint64_t head = (lba / geometry.sectors_per_track) % geometry.heads;
  const uint16_t sector = static_cast<uint16_t>(
      (lba % geometry.sectors_per_track) + 1);
  if (cylinder >= geometry.cylinders || count == 0 ||
      count > geometry.sectors_per_track - sector + 1) {
    return false;
  }

  const uint16_t cylinder_low = static_cast<uint16_t>(cylinder & 0x00FF);
  const uint16_t cylinder_high = static_cast<uint16_t>((cylinder >> 2) & 0x00C0);
  HAL_x86_BiosRegs regs{};
  regs.ax = static_cast<uint16_t>(0x0200 | count);// AH=0x02 read sectors
  regs.bx = kBufferOffset;
  regs.cx = static_cast<uint16_t>((cylinder_low << 8) | cylinder_high | sector);
  regs.dx = static_cast<uint16_t>((head << 8) | kFirstHardDisk);
  regs.es = kScratchSegment;

  HAL_x86_ExecBiosInt(kBiosDiskInt, regs);
  if (carry_set(regs)) {
    return false;
  }

  copy_scratch_chunk(count, dest);
  return true;
}

}// namespace

UNDOS_HAL_API_DEF bool HAL_BOOTDEV_IsPresent() noexcept {
  DiskAccess access{};
  return select_disk_access(access);
}

UNDOS_HAL_API_DEF bool HAL_BOOTDEV_ReadSectors(uint32_t lba, uint32_t count, kstd::span<uint8_t> buffer) noexcept {
  if (count == 0) {
    return true;
  }

  const uint64_t bytes_needed = static_cast<uint64_t>(count) * kSectorSize;
  if (buffer.size() < bytes_needed) {
    return false;
  }

  const uint64_t end_lba = static_cast<uint64_t>(lba) + count - 1;
  if (end_lba > UINT32_MAX) {
    return false;
  }

  DiskAccess access{};
  if (!select_disk_access(access)) {
    return false;
  }

  if (!access.edd) {
    const uint64_t capacity =
        static_cast<uint64_t>(access.geometry.cylinders) *
        access.geometry.heads * access.geometry.sectors_per_track;
    if (end_lba >= capacity) {
      return false;
    }
  }

  uint32_t remaining = count;
  uint64_t current_lba = lba;
  size_t out_off = 0;

  while (remaining > 0) {
    const uint32_t max_chunk =
        remaining > kMaxSectorsPerCall ? kMaxSectorsPerCall : remaining;
    const uint16_t chunk = static_cast<uint16_t>(max_chunk);
    uint8_t *dest = buffer.data() + out_off;
    if (access.edd) {
      if (!read_edd_chunk(current_lba, chunk, dest)) {
        return false;
      }
    } else {
      const uint32_t sector_in_track =
          static_cast<uint32_t>(current_lba % access.geometry.sectors_per_track);
      const uint32_t track_remaining =
          access.geometry.sectors_per_track - sector_in_track;
      const uint16_t chs_chunk = static_cast<uint16_t>(
          max_chunk < track_remaining ? max_chunk : track_remaining);
      if (!read_chs_chunk(current_lba, chs_chunk, access.geometry, dest)) {
        return false;
      }
      remaining -= chs_chunk;
      current_lba += chs_chunk;
      out_off += static_cast<size_t>(chs_chunk) * kSectorSize;
      continue;
    }

    remaining -= chunk;
    current_lba += chunk;
    out_off += static_cast<size_t>(chunk) * kSectorSize;
  }

  return true;
}
