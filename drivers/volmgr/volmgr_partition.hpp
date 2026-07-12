#pragma once

#include <stddef.h>
#include <stdint.h>

#include <span.hpp>

namespace volmgr {

constexpr uint32_t kSectorSize = 512;

// A discovered partition expressed in absolute byte offset + length on the disk.
struct PartitionInfo {
  uint64_t baseOffsetBytes;
  uint64_t lengthBytes;
};

enum class PartitionScheme {
  None,
  Mbr,
  Gpt,
};

using ByteView = kstd::span<const uint8_t>;

inline uint16_t ReadLe16(ByteView p) {
  return static_cast<uint16_t>(p[0]) | static_cast<uint16_t>(p[1] << 8);
}

inline uint32_t ReadLe32(ByteView p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

inline uint64_t ReadLe64(ByteView p) {
  return static_cast<uint64_t>(ReadLe32(p)) |
         (static_cast<uint64_t>(ReadLe32(p.subspan(4))) << 32);
}

// A classic MBR ends with the 0x55 0xAA boot signature at bytes 510/511.
inline bool HasMbrSignature(ByteView sector0) {
  return sector0.size() >= 512 && sector0[510] == 0x55 && sector0[511] == 0xAA;
}

// A protective MBR (present in front of a GPT disk) carries a single partition
// entry whose type byte is 0xEE.
inline bool IsGptProtectiveMbr(ByteView sector0) {
  for (size_t i = 0; i < 4; i++) {
    if (sector0[446 + i * 16 + 4] == 0xEE) return true;
  }
  return false;
}

// The GPT header (LBA 1) begins with the ASCII signature "EFI PART".
inline bool HasGptSignature(ByteView lba1) {
  static const char sig[8] = {'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T'};
  if (lba1.size() < 8) return false;
  for (size_t i = 0; i < 8; i++) {
    if (static_cast<char>(lba1[i]) != sig[i]) return false;
  }
  return true;
}

// Detects the partition scheme. `lba1` may be an empty span when the second
// sector was not read (in which case only MBR can be reported).
inline PartitionScheme DetectScheme(ByteView sector0, ByteView lba1) {
  if (!lba1.empty() && HasGptSignature(lba1)) return PartitionScheme::Gpt;
  if (HasMbrSignature(sector0)) {
    // A protective MBR without a readable GPT header still means GPT.
    if (IsGptProtectiveMbr(sector0)) return PartitionScheme::Gpt;
    return PartitionScheme::Mbr;
  }
  return PartitionScheme::None;
}

// Parses the 4 primary MBR partition entries (offset 446). Empty (type 0x00),
// GPT-protective (type 0xEE) and zero-length entries are skipped. Returns the
// number of entries written to `out` (bounded by `out.size()`).
inline size_t ParseMbr(ByteView sector0, kstd::span<PartitionInfo> out) {
  if (!HasMbrSignature(sector0)) return 0;
  size_t count = 0;
  for (size_t i = 0; i < 4 && count < out.size(); i++) {
    const ByteView e = sector0.subspan(446 + i * 16);
    const uint8_t type = e[4];
    if (type == 0x00 || type == 0xEE) continue;
    const uint32_t startLba = ReadLe32(e.subspan(8));
    const uint32_t sectors = ReadLe32(e.subspan(12));
    if (sectors == 0) continue;
    out[count].baseOffsetBytes = static_cast<uint64_t>(startLba) * kSectorSize;
    out[count].lengthBytes = static_cast<uint64_t>(sectors) * kSectorSize;
    count++;
  }
  return count;
}

// Parses GPT partition entries from a contiguous `entries` buffer. `header` is
// the GPT header (LBA 1). Unused entries (all-zero type GUID) and reversed
// ranges are skipped. Returns the number of entries written to `out`.
inline size_t ParseGptEntries(ByteView header, ByteView entries, kstd::span<PartitionInfo> out) {
  if (!HasGptSignature(header)) return 0;
  const uint32_t numEntries = ReadLe32(header.subspan(80));
  const uint32_t entrySize = ReadLe32(header.subspan(84));
  if (entrySize < 128) return 0;
  size_t count = 0;
  for (uint32_t i = 0; i < numEntries && count < out.size(); i++) {
    const size_t off = static_cast<size_t>(i) * entrySize;
    if (off + entrySize > entries.size()) break;
    const ByteView e = entries.subspan(off, entrySize);
    bool used = false;
    for (size_t b = 0; b < 16; b++) {
      if (e[b] != 0) {
        used = true;
        break;
      }
    }
    if (!used) continue;
    const uint64_t firstLba = ReadLe64(e.subspan(32));
    const uint64_t lastLba = ReadLe64(e.subspan(40));
    if (lastLba < firstLba) continue;
    out[count].baseOffsetBytes = firstLba * kSectorSize;
    out[count].lengthBytes = (lastLba - firstLba + 1) * kSectorSize;
    count++;
  }
  return count;
}

// Range-checks a volume-relative request and converts it to an absolute disk
// byte offset. Returns false (and leaves `absOffset` untouched) if the request
// would fall outside the partition.
inline bool ComputeAbsoluteOffset(uint64_t baseOffsetBytes, uint64_t lengthBytes, uint64_t reqOffset, uint64_t reqLength, uint64_t &absOffset) {
  if (reqOffset > lengthBytes) return false;
  if (reqLength > lengthBytes - reqOffset) return false;
  absOffset = baseOffsetBytes + reqOffset;
  return true;
}

} // namespace volmgr
