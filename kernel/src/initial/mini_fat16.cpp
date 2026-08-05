#include "mini_fat16.hpp"

#include <kernel/hal/x86/bootdev.hpp>

#include <volmgr_partition.hpp>

namespace kernel::initial::minifat {
namespace {

constexpr uint32_t kSectorSize = 512;
constexpr uint16_t kFatEofMin = 0xFFF8;
constexpr uint8_t kAttrLongName = 0x0F;
constexpr uint8_t kAttrDirectory = 0x10;
constexpr uint8_t kAttrVolumeId = 0x08;
constexpr uint8_t kEntryDeleted = 0xE5;
constexpr uint8_t kEntryEnd = 0x00;

struct [[gnu::packed]] FatBpb {
  uint8_t jmpBoot[3];
  char oemName[8];
  uint16_t bytesPerSector;
  uint8_t sectorsPerCluster;
  uint16_t reservedSectors;
  uint8_t numFATs;
  uint16_t rootEntryCount;
  uint16_t totalSectors16;
  uint8_t media;
  uint16_t fatSize16;
  uint16_t sectorsPerTrack;
  uint16_t numHeads;
  uint32_t hiddenSectors;
  uint32_t totalSectors32;
};

static_assert(sizeof(FatBpb) == 36);

struct [[gnu::packed]] FatDirEntry {
  char name[11];
  uint8_t attr;
  uint8_t ntReserved;
  uint8_t createTimeTenth;
  uint16_t createTime;
  uint16_t createDate;
  uint16_t lastAccessDate;
  uint16_t firstClusterHigh;
  uint16_t writeTime;
  uint16_t writeDate;
  uint16_t firstClusterLow;
  uint32_t fileSize;
};

static_assert(sizeof(FatDirEntry) == 32);

struct Volume {
  uint32_t partitionLba = 0;
  uint16_t bytesPerSector = kSectorSize;
  uint8_t sectorsPerCluster = 0;
  uint16_t reservedSectors = 0;
  uint8_t numFATs = 0;
  uint16_t rootEntryCount = 0;
  uint16_t fatSizeSectors = 0;
  uint32_t rootDirSector = 0; // partition-relative
  uint32_t rootDirSectors = 0;
  uint32_t dataStartSector = 0;// partition-relative
  uint32_t bytesPerCluster = 0;
};

inline uint16_t read_le16(const uint8_t *p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

bool read_abs_sectors(uint32_t lba, uint32_t count, kstd::span<uint8_t> buffer) {
  return HAL_BOOTDEV_ReadSectors(lba, count, buffer);
}

bool read_part_sectors(const Volume &vol, uint32_t rel_lba, uint32_t count, kstd::span<uint8_t> buffer) {
  return read_abs_sectors(vol.partitionLba + rel_lba, count, buffer);
}

bool to_short_name(kstd::string_view name, char out[11]) {
  for (size_t i = 0; i < 11; ++i) {
    out[i] = ' ';
  }

  // Skip empty components.
  if (name.empty() || name == "." || name == "..") {
    return false;
  }

  // Match tools/make-initial-disk.py's 8.3 truncation exactly:
  //   base, ext = os.path.splitext(name)
  //   ext = ext.lstrip('.').upper()[:3]
  //   base = base.upper()[:8]
  // so long names like "LocalMachine" become "LOCALMAC   " on disk and
  // must still resolve when the kernel asks for the full long name.
  size_t dot = kstd::string_view::npos;
  for (size_t i = 0; i < name.size(); ++i) {
    if (name[i] == '.') {
      // splitext keeps only the final extension separator.
      dot = i;
    }
  }

  const kstd::string_view base = (dot == kstd::string_view::npos) ? name : name.substr(0, dot);
  const kstd::string_view ext =
      (dot == kstd::string_view::npos) ? kstd::string_view{} : name.substr(dot + 1);

  if (base.empty()) {
    return false;
  }

  const size_t base_len = base.size() > 8 ? 8 : base.size();
  const size_t ext_len = ext.size() > 3 ? 3 : ext.size();

  for (size_t i = 0; i < base_len; ++i) {
    char c = base[i];
    if (c >= 'a' && c <= 'z') {
      c = static_cast<char>(c - 'a' + 'A');
    }
    out[i] = c;
  }
  for (size_t i = 0; i < ext_len; ++i) {
    char c = ext[i];
    if (c >= 'a' && c <= 'z') {
      c = static_cast<char>(c - 'a' + 'A');
    }
    out[8 + i] = c;
  }
  return true;
}

bool names_equal(const char entry[11], const char want[11]) {
  for (size_t i = 0; i < 11; ++i) {
    if (entry[i] != want[i]) {
      return false;
    }
  }
  return true;
}

bool open_volume(Volume &vol) {
  if (!HAL_BOOTDEV_IsPresent()) {
    return false;
  }

  uint8_t sector0[kSectorSize];
  if (!read_abs_sectors(0, 1, kstd::span<uint8_t>(sector0, kSectorSize))) {
    return false;
  }

  const auto scheme = volmgr::DetectScheme(
      kstd::span<const uint8_t>(sector0, kSectorSize),
      kstd::span<const uint8_t>());
  if (scheme != volmgr::PartitionScheme::Mbr) {
    return false;
  }

  volmgr::PartitionInfo parts[4]{};
  const size_t n = volmgr::ParseMbr(
      kstd::span<const uint8_t>(sector0, kSectorSize),
      kstd::span<volmgr::PartitionInfo>(parts, 4));
  if (n == 0) {
    return false;
  }

  // First primary partition is the FAT16 volume written by make-initial-disk.py.
  if ((parts[0].baseOffsetBytes % kSectorSize) != 0) {
    return false;
  }
  vol.partitionLba = static_cast<uint32_t>(parts[0].baseOffsetBytes / kSectorSize);

  uint8_t bpb_sector[kSectorSize];
  if (!read_part_sectors(vol, 0, 1, kstd::span<uint8_t>(bpb_sector, kSectorSize))) {
    return false;
  }
  if (bpb_sector[510] != 0x55 || bpb_sector[511] != 0xAA) {
    return false;
  }

  FatBpb bpb{};
  __builtin_memcpy(&bpb, bpb_sector, sizeof(bpb));

  if (bpb.bytesPerSector != kSectorSize || bpb.sectorsPerCluster == 0 ||
      bpb.numFATs == 0 || bpb.fatSize16 == 0 || bpb.rootEntryCount == 0) {
    return false;
  }

  vol.bytesPerSector = bpb.bytesPerSector;
  vol.sectorsPerCluster = bpb.sectorsPerCluster;
  vol.reservedSectors = bpb.reservedSectors;
  vol.numFATs = bpb.numFATs;
  vol.rootEntryCount = bpb.rootEntryCount;
  vol.fatSizeSectors = bpb.fatSize16;
  vol.bytesPerCluster = static_cast<uint32_t>(vol.sectorsPerCluster) * vol.bytesPerSector;

  // Root directory sits immediately after the FATs (FAT12/16 fixed root).
  vol.rootDirSector = static_cast<uint32_t>(vol.reservedSectors) +
                      static_cast<uint32_t>(vol.numFATs) * vol.fatSizeSectors;
  vol.rootDirSectors =
      (static_cast<uint32_t>(vol.rootEntryCount) * 32u + (vol.bytesPerSector - 1u)) /
      vol.bytesPerSector;
  vol.dataStartSector = vol.rootDirSector + vol.rootDirSectors;
  return true;
}

bool read_fat_entry(const Volume &vol, uint16_t cluster, uint16_t &next_out) {
  // Each FAT16 entry is 2 bytes. FAT begins at partition-relative reservedSectors.
  const uint32_t offset_bytes = static_cast<uint32_t>(cluster) * 2u;
  const uint32_t sector = vol.reservedSectors + (offset_bytes / vol.bytesPerSector);
  const uint32_t entry_off = offset_bytes % vol.bytesPerSector;

  uint8_t sector_buf[kSectorSize];
  if (!read_part_sectors(vol, sector, 1, kstd::span<uint8_t>(sector_buf, kSectorSize))) {
    return false;
  }
  next_out = read_le16(sector_buf + entry_off);
  return true;
}

bool cluster_to_lba(const Volume &vol, uint16_t cluster, uint32_t &rel_lba_out) {
  if (cluster < 2) {
    return false;
  }
  rel_lba_out = vol.dataStartSector +
                static_cast<uint32_t>(cluster - 2) * vol.sectorsPerCluster;
  return true;
}

bool read_cluster(const Volume &vol, uint16_t cluster, kstd::span<uint8_t> buffer) {
  if (buffer.size() < vol.bytesPerCluster) {
    return false;
  }
  uint32_t rel_lba = 0;
  if (!cluster_to_lba(vol, cluster, rel_lba)) {
    return false;
  }
  return read_part_sectors(vol, rel_lba, vol.sectorsPerCluster, buffer.first(vol.bytesPerCluster));
}

// Scans a contiguous directory image (`dir_bytes`) for an 8.3 name. On hit,
// writes the entry fields and returns true.
bool find_in_dir_bytes(kstd::span<const uint8_t> dir_bytes,
                       const char want[11],
                       bool want_dir,
                       FatDirEntry &entry_out) {
  const size_t count = dir_bytes.size() / sizeof(FatDirEntry);
  for (size_t i = 0; i < count; ++i) {
    FatDirEntry e{};
    __builtin_memcpy(&e, dir_bytes.data() + i * sizeof(FatDirEntry), sizeof(e));

    if (static_cast<uint8_t>(e.name[0]) == kEntryEnd) {
      break;
    }
    if (static_cast<uint8_t>(e.name[0]) == kEntryDeleted) {
      continue;
    }
    if (e.attr == kAttrLongName) {
      continue;
    }
    if ((e.attr & kAttrVolumeId) != 0 && (e.attr & kAttrDirectory) == 0) {
      continue;
    }

    const bool is_dir = (e.attr & kAttrDirectory) != 0;
    if (is_dir != want_dir) {
      continue;
    }
    if (!names_equal(e.name, want)) {
      continue;
    }

    entry_out = e;
    return true;
  }
  return false;
}

bool load_root_dir(const Volume &vol, kstd::vector<uint8_t> &out) {
  const uint32_t bytes = vol.rootDirSectors * vol.bytesPerSector;
  out.resize(bytes);
  return read_part_sectors(vol, vol.rootDirSector, vol.rootDirSectors,
                           kstd::span<uint8_t>(out.data(), out.size()));
}

bool load_cluster_chain_dir(const Volume &vol, uint16_t start_cluster, kstd::vector<uint8_t> &out) {
  out.clear();
  uint16_t cluster = start_cluster;
  // Hard cap to avoid infinite loops on a corrupt FAT.
  for (uint32_t guard = 0; guard < 4096; ++guard) {
    if (cluster < 2 || cluster >= kFatEofMin) {
      break;
    }
    const size_t old = out.size();
    out.resize(old + vol.bytesPerCluster);
    if (!read_cluster(vol, cluster, kstd::span<uint8_t>(out.data() + old, vol.bytesPerCluster))) {
      return false;
    }
    uint16_t next = 0;
    if (!read_fat_entry(vol, cluster, next)) {
      return false;
    }
    if (next >= kFatEofMin) {
      break;
    }
    cluster = next;
  }
  return !out.empty() || start_cluster == 0;
}

bool read_file_clusters(const Volume &vol, uint16_t start_cluster, uint32_t file_size,
                        kstd::vector<uint8_t> &out) {
  out.clear();
  if (file_size == 0 || start_cluster == 0) {
    return true;
  }

  out.reserve(file_size);
  uint16_t cluster = start_cluster;
  uint32_t remaining = file_size;

  kstd::vector<uint8_t> cluster_buf;
  cluster_buf.resize(vol.bytesPerCluster);

  for (uint32_t guard = 0; guard < 65536 && remaining > 0; ++guard) {
    if (cluster < 2 || cluster >= kFatEofMin) {
      return false;
    }
    if (!read_cluster(vol, cluster, kstd::span<uint8_t>(cluster_buf.data(), cluster_buf.size()))) {
      return false;
    }

    const uint32_t take = remaining < vol.bytesPerCluster ? remaining : vol.bytesPerCluster;
    const size_t old = out.size();
    out.resize(old + take);
    __builtin_memcpy(out.data() + old, cluster_buf.data(), take);
    remaining -= take;

    if (remaining == 0) {
      break;
    }

    uint16_t next = 0;
    if (!read_fat_entry(vol, cluster, next)) {
      return false;
    }
    if (next >= kFatEofMin) {
      return remaining == 0;
    }
    cluster = next;
  }

  return remaining == 0;
}

// Advances one path component. Leading/trailing slashes are ignored.
bool next_component(kstd::string_view &path, kstd::string_view &component) {
  while (!path.empty() && path[0] == '/') {
    path = path.substr(1);
  }
  if (path.empty()) {
    component = {};
    return false;
  }
  size_t slash = kstd::string_view::npos;
  for (size_t i = 0; i < path.size(); ++i) {
    if (path[i] == '/') {
      slash = i;
      break;
    }
  }
  if (slash == kstd::string_view::npos) {
    component = path;
    path = {};
  } else {
    component = path.substr(0, slash);
    path = path.substr(slash + 1);
  }
  return !component.empty();
}

}// namespace

bool ReadFile(kstd::string_view path, kstd::vector<uint8_t> &out) {
  out.clear();

  Volume vol{};
  if (!open_volume(vol)) {
    return false;
  }

  kstd::vector<uint8_t> dir_bytes;
  if (!load_root_dir(vol, dir_bytes)) {
    return false;
  }

  kstd::string_view remaining = path;
  kstd::string_view component{};

  // Single-pass: look ahead one component so intermediate names are dirs
  // and the final name is the file.
  if (!next_component(remaining, component)) {
    return false;
  }

  while (true) {
    kstd::string_view rest = remaining;
    kstd::string_view look{};
    const bool has_more = next_component(rest, look);

    char short_name[11];
    if (!to_short_name(component, short_name)) {
      return false;
    }

    FatDirEntry entry{};
    if (has_more) {
      // Intermediate directory.
      if (!find_in_dir_bytes(kstd::span<const uint8_t>(dir_bytes.data(), dir_bytes.size()),
                             short_name, true, entry)) {
        return false;
      }
      if (!load_cluster_chain_dir(vol, entry.firstClusterLow, dir_bytes)) {
        return false;
      }
      remaining = rest;
      component = look;
      continue;
    }

    // Final component: the file itself.
    if (!find_in_dir_bytes(kstd::span<const uint8_t>(dir_bytes.data(), dir_bytes.size()),
                           short_name, false, entry)) {
      return false;
    }
    return read_file_clusters(vol, entry.firstClusterLow, entry.fileSize, out);
  }
}

}// namespace kernel::initial::minifat
