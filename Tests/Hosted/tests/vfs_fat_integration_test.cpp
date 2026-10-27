#include <catch2/catch_test_macros.hpp>

#include <kernel/configuration_manager.hpp>
#include <kernel/io.hpp>
#include <kernel/vfs.hpp>

#include "ob_test_support.hpp"
#include "pnp_test_hooks.hpp"
#include "stdkrn.hpp"

#include "fake_disk.hpp"
#include "fake_disk_file_backend.hpp"

#include <array.hpp>
#include <static_string.hpp>
#include <strfmt.hpp>

#include <cstdio>
#include <dlfcn.h>
#include <stdlib.h>
#include <unistd.h>
#include <algorithm>
#include <vector>

namespace {

using DriverEntryFn = void (*)(kernel::KObjectPtr<kernel::KDriverObject> &, const kernel::KConfigurationKeyPtr &);
constexpr uint32_t kSectorSize = 512;

struct FailingWrites final : diskfake::IBackingStore {
  explicit FailingWrites(diskfake::IBackingStore &store) : backing(store) {}
  diskfake::IBackingStore &backing;
  kstd::expected<size_t, kstd::error_code> Size() const override { return backing.Size(); }
  kstd::error_code Resize(size_t size) override { return backing.Resize(size); }
  kstd::error_code ReadAt(uint64_t offset, kstd::span<uint8_t> buffer) override { return backing.ReadAt(offset, buffer); }
  kstd::error_code WriteAt(uint64_t, kstd::span<const uint8_t>) override { return kernel::KernelError::IoError; }
};

struct CountingBacking final : diskfake::IBackingStore {
  explicit CountingBacking(diskfake::IBackingStore &store) : backing(store) {}

  diskfake::IBackingStore &backing;
  uint64_t fatStart{0};
  uint64_t fatEnd{0};
  std::vector<uint64_t> directoryOffsets;
  std::vector<uint64_t> fileOffsets;
  uint64_t failReadOffset{UINT64_MAX};
  uint64_t failWriteOffset{UINT64_MAX};
  size_t partialWriteBytes{0};
  size_t readCalls{0};
  size_t requestedBytes{0};
  size_t fatReadCalls{0};
  size_t fatReadSectors{0};
  size_t directoryReadCalls{0};
  size_t fileReadCalls{0};

  kstd::expected<size_t, kstd::error_code> Size() const override { return backing.Size(); }
  kstd::error_code Resize(size_t size) override { return backing.Resize(size); }

  kstd::error_code ReadAt(uint64_t offset, kstd::span<uint8_t> buffer) override {
    ++readCalls;
    requestedBytes += buffer.size();
    if (offset >= fatStart && offset < fatEnd) {
      ++fatReadCalls;
      fatReadSectors += (buffer.size() + kSectorSize - 1) / kSectorSize;
    }
    if (std::find(directoryOffsets.begin(), directoryOffsets.end(), offset) != directoryOffsets.end())
      ++directoryReadCalls;
    if (std::find(fileOffsets.begin(), fileOffsets.end(), offset) != fileOffsets.end())
      ++fileReadCalls;
    if (offset == failReadOffset) return kernel::KernelError::DeviceError;
    return backing.ReadAt(offset, buffer);
  }

  kstd::error_code WriteAt(uint64_t offset, kstd::span<const uint8_t> buffer) override {
    if (offset == failWriteOffset) {
      if (partialWriteBytes) {
        const auto prefix = partialWriteBytes < buffer.size() ? partialWriteBytes : buffer.size();
        if (const auto status = backing.WriteAt(offset, buffer.first(prefix)); status) return status;
      }
      return kernel::KernelError::IoError;
    }
    return backing.WriteAt(offset, buffer);
  }

  void ResetCounts() {
    readCalls = requestedBytes = fatReadCalls = fatReadSectors = 0;
    directoryReadCalls = fileReadCalls = 0;
  }
};

struct ShortReadControl {
  bool enabled{false};
  uint64_t offset{0};
  size_t requestSize{0};
  size_t reportedBytes{0};
};

ShortReadControl g_shortRead;
struct ReadErrorControl {
  bool enabled{false};
  uint64_t offset{0};
};
ReadErrorControl g_readError;

kstd::error_code ShortableDiskRead(const kernel::KDevicePtr<kernel::KDevice> &device,
    uint64_t offset, kstd::span<uint8_t> buffer, size_t &transferred) {
  if (g_readError.enabled && offset == g_readError.offset) {
    transferred = 0;
    return kernel::KernelError::DeviceError;
  }
  const auto status = diskfake::Read(device, offset, buffer, transferred);
  if (!status && g_shortRead.enabled && offset == g_shortRead.offset &&
      buffer.size() == g_shortRead.requestSize && g_shortRead.reportedBytes < transferred)
    transferred = g_shortRead.reportedBytes;
  return status;
}

kstd::static_string<128> MakeTempImagePath() {
  char path[] = "./undos-vfs-fat-XXXXXX";
  const int descriptor = mkstemp(path);
  REQUIRE(descriptor >= 0);
  REQUIRE(close(descriptor) == 0);
  return kstd::static_string<128>(path);
}

void PutLe32(kstd::span<uint8_t> p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
  p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
  p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

void PutLe16(kstd::span<uint8_t> p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

constexpr uint32_t kPartitionStartLba = 1;
constexpr uint32_t kPartitionSectors = 140000;
constexpr uint32_t kFat12Sectors = 39;// 1 (BPB) + 1 (FAT) + 1 (root dir) + 36 (data)
constexpr uint32_t kDiskSectors = kPartitionStartLba + kFat12Sectors;

// Builds the initial disk prefix: the MBR exposes a large partition window for
// parameterized FAT fixtures, while this seed contains a minimal FAT12 BPB and
// its first sectors for the existing integration sections.
kstd::array<uint8_t, kDiskSectors * kSectorSize> BuildFat12DiskImage() {
  kstd::array<uint8_t, kDiskSectors * kSectorSize> disk{};

  // --- MBR (LBA 0) ---
  const kstd::span<uint8_t> mbr(disk.data(), kSectorSize);
  const kstd::span<uint8_t> entry0 = mbr.subspan(446);
  entry0[4] = 0x01;// FAT12 partition type
  PutLe32(entry0.subspan(8), kPartitionStartLba);
  PutLe32(entry0.subspan(12), kPartitionSectors);
  mbr[510] = 0x55;
  mbr[511] = 0xAA;

  // --- FAT12 BPB (partition-relative LBA 0, i.e. disk LBA 1) ---
  const kstd::span<uint8_t> bpb(disk.data() + kPartitionStartLba * kSectorSize, kSectorSize);
  bpb[0] = 0xEB;// jmpBoot
  bpb[1] = 0x3C;
  bpb[2] = 0x90;
  const char oem[8] = {'U', 'N', 'D', 'O', 'S', ' ', ' ', ' '};
  for (size_t i = 0; i < 8; i++) bpb[3 + i] = static_cast<uint8_t>(oem[i]);
  PutLe16(bpb.subspan(11), kSectorSize);// bytesPerSector
  bpb[13] = 1;                          // sectorsPerCluster
  PutLe16(bpb.subspan(14), 1);          // reservedSectors
  bpb[16] = 1;                          // numFATs
  PutLe16(bpb.subspan(17), 16);         // rootEntryCount (-> 1 root dir sector)
  PutLe16(bpb.subspan(19), static_cast<uint16_t>(kFat12Sectors));// totalSectors16
  bpb[21] = 0xF8;                                                    // media
  PutLe16(bpb.subspan(22), 1);                                       // fatSize16
  bpb[38] = 0x29;
  PutLe32(bpb.subspan(39), 0x12345678);
  bpb[510] = 0x55;
  bpb[511] = 0xaa;

  return disk;
}

enum class TestFatType { Fat12, Fat16, Fat32 };

struct FatImageLayout {
  TestFatType type{TestFatType::Fat12};
  uint32_t sectorsPerCluster{1};
  uint32_t reservedSectors{1};
  uint32_t fatSectors{1};
  uint32_t rootEntries{256};
  uint32_t rootSectors{16};
  uint32_t dataStartSector{0};
  uint32_t totalClusters{0};
  uint32_t volumeSectors{0};
  std::vector<uint32_t> fileClusters;
  std::vector<uint32_t> rootClusters;
  std::vector<uint32_t> directoryClusters;
  std::vector<uint8_t> expectedFile;
  std::vector<uint64_t> directoryOffsets;
  std::vector<uint64_t> rootDirectoryOffsets;
  std::vector<uint64_t> fileOffsets;
};

struct TestDirectoryItem {
  std::array<uint8_t, 11> name{};
  uint8_t attributes{0x20};
  uint32_t size{0};
  uint32_t cluster{0};
};

std::array<uint8_t, 11> RawShortName(const char *name) {
  std::array<uint8_t, 11> result{};
  for (size_t i = 0; i < result.size() && name[i]; ++i) result[i] = static_cast<uint8_t>(name[i]);
  return result;
}

std::array<uint8_t, 11> NumberedShortName(char prefix, uint32_t index) {
  char name[12]{};
  std::snprintf(name, sizeof(name), "%c%07uTXT", prefix, index);
  return RawShortName(name);
}

TestDirectoryItem MakeItem(const char *name, uint8_t attributes = 0x20, uint32_t size = 0, uint32_t cluster = 0) {
  return {.name = RawShortName(name), .attributes = attributes, .size = size, .cluster = cluster};
}

TestDirectoryItem MakeNumberedItem(char prefix, uint32_t index) {
  TestDirectoryItem item{};
  item.name = NumberedShortName(prefix, index);
  item.size = index + 1;
  return item;
}

void StoreDirectoryItem(std::vector<uint8_t> &bytes, size_t slot, const TestDirectoryItem &item) {
  auto *entry = bytes.data() + slot * 32;
  for (size_t i = 0; i < item.name.size(); ++i) entry[i] = item.name[i];
  entry[11] = item.attributes;
  PutLe16(kstd::span<uint8_t>(entry + 26, 2), static_cast<uint16_t>(item.cluster));
  PutLe16(kstd::span<uint8_t>(entry + 20, 2), static_cast<uint16_t>(item.cluster >> 16));
  PutLe32(kstd::span<uint8_t>(entry + 28, 4), item.size);
}

std::vector<uint32_t> MakeClusterChain(uint32_t totalClusters, size_t count, uint32_t seed,
    std::vector<uint32_t> &used, bool includeFat12Straddle = false) {
  std::vector<uint32_t> result;
  result.reserve(count);
  for (size_t index = 0; index < count; ++index) {
    uint32_t candidate = includeFat12Straddle && index == 5 && totalClusters > 341
        ? 341
        : 2 + (seed + static_cast<uint32_t>(index) * 37) % totalClusters;
    for (uint32_t attempts = 0; attempts < totalClusters; ++attempts) {
      if (candidate >= 2 && candidate - 2 < totalClusters &&
          std::find(used.begin(), used.end(), candidate) == used.end()) break;
      candidate = candidate + 1 >= totalClusters + 2 ? 2 : candidate + 1;
    }
    REQUIRE(candidate >= 2);
    REQUIRE(candidate - 2 < totalClusters);
    REQUIRE(std::find(used.begin(), used.end(), candidate) == used.end());
    used.push_back(candidate);
    result.push_back(candidate);
  }
  return result;
}

void SetFatValue(std::vector<uint8_t> &fat, TestFatType type, uint32_t cluster, uint32_t value) {
  const uint32_t offset = type == TestFatType::Fat12 ? cluster + cluster / 2 :
      cluster * (type == TestFatType::Fat16 ? 2u : 4u);
  if (type == TestFatType::Fat12) {
    uint16_t raw = static_cast<uint16_t>(fat[offset] | (static_cast<uint16_t>(fat[offset + 1]) << 8));
    if (cluster & 1) raw = static_cast<uint16_t>((raw & 0x000f) | ((value & 0x0fff) << 4));
    else raw = static_cast<uint16_t>((raw & 0xf000) | (value & 0x0fff));
    fat[offset] = static_cast<uint8_t>(raw);
    fat[offset + 1] = static_cast<uint8_t>(raw >> 8);
  } else if (type == TestFatType::Fat16) {
    fat[offset] = static_cast<uint8_t>(value);
    fat[offset + 1] = static_cast<uint8_t>(value >> 8);
  } else {
    value |= 0xa0000000;
    for (size_t i = 0; i < 4; ++i) fat[offset + i] = static_cast<uint8_t>(value >> (8 * i));
  }
}

uint32_t FatEndMarker(TestFatType type) {
  switch (type) {
    case TestFatType::Fat12: return 0x0fff;
    case TestFatType::Fat16: return 0xffff;
    case TestFatType::Fat32: return 0x0fffffff;
  }
  return 0;
}

void LinkFatChain(std::vector<uint8_t> &fat, TestFatType type, const std::vector<uint32_t> &chain) {
  for (size_t i = 0; i < chain.size(); ++i)
    SetFatValue(fat, type, chain[i], i + 1 == chain.size() ? FatEndMarker(type) : chain[i + 1]);
}

std::vector<TestDirectoryItem> MakeDirectoryItems(size_t visibleCount, bool includeSkips = true) {
  std::vector<TestDirectoryItem> items;
  items.reserve(visibleCount + (includeSkips ? 5 : 0));
  for (uint32_t i = 0; i < visibleCount; ++i) {
    if (includeSkips && i == 14) items.push_back(MakeItem("LFNENTRY   ", 0x0f));
    items.push_back(MakeNumberedItem('F', i));
    if (includeSkips && (i == 7 || i == 28)) {
      auto deleted = MakeItem("DELETED TXT");
      deleted.name[0] = 0xe5;
      items.push_back(deleted);
    }
    if (includeSkips && i == 29) {
      items.push_back(MakeItem("VOLUME     ", 0x08));
      items.push_back(MakeItem(".          ", 0x10));
    }
  }
  return items;
}

void WriteClusterBytes(diskfake::IBackingStore &backing, const FatImageLayout &layout,
    uint32_t cluster, kstd::span<const uint8_t> bytes) {
  const uint64_t partitionBase = static_cast<uint64_t>(kPartitionStartLba) * kSectorSize;
  const uint64_t sector = layout.dataStartSector + static_cast<uint64_t>(cluster - 2) * layout.sectorsPerCluster;
  REQUIRE_FALSE(backing.WriteAt(partitionBase + sector * kSectorSize, bytes));
}

FatImageLayout SeedFatImage(diskfake::IBackingStore &backing, TestFatType type,
    uint32_t sectorsPerCluster, bool fullFixedRoot = false, bool includeSkips = true,
    bool truncateFileChain = false, bool zeroFileStart = false,
    bool invalidDirectoryStart = false, bool cycleDirectory = false, bool fullDirectory = false) {
  FatImageLayout layout{};
  layout.type = type;
  layout.sectorsPerCluster = sectorsPerCluster;
  layout.totalClusters = type == TestFatType::Fat12 ? 600 :
      type == TestFatType::Fat16 ? 4085 : 65525;
  layout.reservedSectors = type == TestFatType::Fat32 ? 32 : 1;
  layout.rootEntries = type == TestFatType::Fat32 ? 0 : 256;
  layout.rootSectors = (layout.rootEntries * 32 + kSectorSize - 1) / kSectorSize;
  const uint64_t fatBytes = type == TestFatType::Fat12
      ? (static_cast<uint64_t>(layout.totalClusters + 2) * 3 + 1) / 2
      : static_cast<uint64_t>(layout.totalClusters + 2) * (type == TestFatType::Fat16 ? 2 : 4);
  layout.fatSectors = static_cast<uint32_t>((fatBytes + kSectorSize - 1) / kSectorSize);
  layout.dataStartSector = layout.reservedSectors + 2 * layout.fatSectors + layout.rootSectors;
  layout.volumeSectors = layout.dataStartSector + layout.totalClusters * sectorsPerCluster;
  REQUIRE(layout.volumeSectors < kPartitionSectors);

  const uint64_t partitionBase = static_cast<uint64_t>(kPartitionStartLba) * kSectorSize;
  std::vector<uint8_t> boot(kSectorSize);
  boot[0] = 0xeb;
  PutLe16(kstd::span<uint8_t>(boot.data() + 11, 2), kSectorSize);
  boot[13] = static_cast<uint8_t>(sectorsPerCluster);
  PutLe16(kstd::span<uint8_t>(boot.data() + 14, 2), static_cast<uint16_t>(layout.reservedSectors));
  boot[16] = 2;
  PutLe16(kstd::span<uint8_t>(boot.data() + 17, 2), static_cast<uint16_t>(layout.rootEntries));
  if (layout.volumeSectors <= UINT16_MAX)
    PutLe16(kstd::span<uint8_t>(boot.data() + 19, 2), static_cast<uint16_t>(layout.volumeSectors));
  else
    PutLe32(kstd::span<uint8_t>(boot.data() + 32, 4), layout.volumeSectors);
  boot[21] = 0xf8;
  if (type == TestFatType::Fat32) {
    PutLe32(kstd::span<uint8_t>(boot.data() + 36, 4), layout.fatSectors);
    PutLe32(kstd::span<uint8_t>(boot.data() + 44, 4), 2);
    boot[66] = 0x29;
    PutLe32(kstd::span<uint8_t>(boot.data() + 67, 4), 0x12345678);
  } else {
    PutLe16(kstd::span<uint8_t>(boot.data() + 22, 2), static_cast<uint16_t>(layout.fatSectors));
    boot[38] = 0x29;
    PutLe32(kstd::span<uint8_t>(boot.data() + 39, 4), 0x12345678);
  }
  boot[510] = 0x55;
  boot[511] = 0xaa;
  REQUIRE_FALSE(backing.WriteAt(partitionBase, kstd::span<const uint8_t>(boot.data(), boot.size())));

  std::vector<uint8_t> fat(static_cast<size_t>(layout.fatSectors) * kSectorSize);
  if (type == TestFatType::Fat12) {
    fat[0] = 0xf8; fat[1] = fat[2] = 0xff;
  } else if (type == TestFatType::Fat16) {
    fat[0] = 0xf8; fat[1] = fat[2] = fat[3] = 0xff;
  } else {
    SetFatValue(fat, type, 0, 0x0ffffff8);
    SetFatValue(fat, type, 1, 0x0fffffff);
  }

  std::vector<uint32_t> used;
  std::vector<uint32_t> directoryClusters;
  if (type == TestFatType::Fat32) {
    layout.rootClusters.push_back(2);
    used.push_back(2);
    const auto rootTail = MakeClusterChain(layout.totalClusters, 8, 3, used);
    layout.rootClusters.insert(layout.rootClusters.end(), rootTail.begin(), rootTail.end());
    LinkFatChain(fat, type, layout.rootClusters);
  }
  directoryClusters = MakeClusterChain(layout.totalClusters,
      cycleDirectory ? layout.totalClusters : 9, 1, used);
  layout.directoryClusters = directoryClusters;
  LinkFatChain(fat, type, directoryClusters);
  if (cycleDirectory) SetFatValue(fat, type, directoryClusters.back(), directoryClusters.front());
  if (fullFixedRoot || cycleDirectory) {
    layout.fileClusters.clear();
  } else {
    layout.fileClusters = MakeClusterChain(layout.totalClusters, 40, 11, used,
        type == TestFatType::Fat12);
    LinkFatChain(fat, type, layout.fileClusters);
    if (truncateFileChain) SetFatValue(fat, type, layout.fileClusters.front(), FatEndMarker(type));
  }
  for (uint32_t copy = 0; copy < 2; ++copy) {
    const uint64_t offset = partitionBase +
        static_cast<uint64_t>(layout.reservedSectors + copy * layout.fatSectors) * kSectorSize;
    REQUIRE_FALSE(backing.WriteAt(offset, kstd::span<const uint8_t>(fat.data(), fat.size())));
  }

  std::vector<TestDirectoryItem> rootItems;
  if (fullFixedRoot) {
    for (uint32_t i = 0; i < layout.rootEntries; ++i) rootItems.push_back(MakeNumberedItem('R', i));
  } else if (cycleDirectory) {
    rootItems.push_back(MakeItem("DIR        ", 0x10, 0, invalidDirectoryStart
        ? layout.totalClusters + 2 : directoryClusters.front()));
  } else {
    TestDirectoryItem data = MakeItem("DATA    BIN", 0x20,
        40 * sectorsPerCluster * kSectorSize - 13,
        zeroFileStart ? 0 : layout.fileClusters.front());
    TestDirectoryItem directory = MakeItem("DIR        ", 0x10, 0,
        invalidDirectoryStart ? layout.totalClusters + 2 : directoryClusters.front());
    rootItems.push_back(data);
    rootItems.push_back(directory);
    const auto children = MakeDirectoryItems(128, includeSkips);
    rootItems.insert(rootItems.end(), children.begin(), children.end());
  }

  if (type != TestFatType::Fat32) {
    std::vector<uint8_t> root(static_cast<size_t>(layout.rootSectors) * kSectorSize);
    for (size_t i = 0; i < rootItems.size(); ++i) StoreDirectoryItem(root, i, rootItems[i]);
    const uint64_t rootSector = layout.reservedSectors + 2 * layout.fatSectors;
    REQUIRE_FALSE(backing.WriteAt(partitionBase + rootSector * kSectorSize,
        kstd::span<const uint8_t>(root.data(), root.size())));
    for (uint32_t sector = 0; sector < layout.rootSectors; ++sector)
      layout.rootDirectoryOffsets.push_back(partitionBase + (rootSector + sector) * kSectorSize);
  }

  const size_t clusterBytes = static_cast<size_t>(sectorsPerCluster) * kSectorSize;
  if (!fullFixedRoot) {
    std::vector<uint8_t> directoryBytes(layout.directoryClusters.size() * clusterBytes);
    std::vector<TestDirectoryItem> childItems;
    if (cycleDirectory) {
      childItems.reserve(static_cast<size_t>(layout.totalClusters) * sectorsPerCluster * (kSectorSize / 32));
      const size_t entries = static_cast<size_t>(layout.totalClusters) * sectorsPerCluster * (kSectorSize / 32);
      for (uint32_t i = 0; i < entries; ++i) childItems.push_back(MakeNumberedItem('C', i));
    } else if (fullDirectory) {
      childItems = MakeDirectoryItems(
          static_cast<size_t>(layout.directoryClusters.size()) * sectorsPerCluster * (kSectorSize / 32), false);
    } else {
      childItems = MakeDirectoryItems(128, includeSkips);
    }
    for (size_t i = 0; i < childItems.size(); ++i) StoreDirectoryItem(directoryBytes, i, childItems[i]);
    for (size_t i = 0; i < layout.directoryClusters.size(); ++i) {
      const auto clusterSpan = kstd::span<const uint8_t>(directoryBytes.data() + i * clusterBytes, clusterBytes);
      WriteClusterBytes(backing, layout, layout.directoryClusters[i], clusterSpan);
      for (uint32_t sector = 0; sector < sectorsPerCluster; ++sector) {
        const uint64_t lba = layout.dataStartSector +
            static_cast<uint64_t>(layout.directoryClusters[i] - 2) * sectorsPerCluster + sector;
        layout.directoryOffsets.push_back(partitionBase + lba * kSectorSize);
      }
    }
    if (type == TestFatType::Fat32) {
      std::vector<uint8_t> rootBytes(layout.rootClusters.size() * clusterBytes);
      for (size_t i = 0; i < rootItems.size(); ++i) StoreDirectoryItem(rootBytes, i, rootItems[i]);
      for (size_t i = 0; i < layout.rootClusters.size(); ++i) {
        const auto clusterSpan = kstd::span<const uint8_t>(rootBytes.data() + i * clusterBytes, clusterBytes);
        WriteClusterBytes(backing, layout, layout.rootClusters[i], clusterSpan);
        for (uint32_t sector = 0; sector < sectorsPerCluster; ++sector) {
          const uint64_t lba = layout.dataStartSector +
              static_cast<uint64_t>(layout.rootClusters[i] - 2) * sectorsPerCluster + sector;
          layout.rootDirectoryOffsets.push_back(partitionBase + lba * kSectorSize);
        }
      }
    }

    layout.expectedFile.resize(static_cast<size_t>(40 * clusterBytes - 13));
    std::vector<uint8_t> fileCluster(clusterBytes);
    for (size_t logical = 0; logical < layout.fileClusters.size(); ++logical) {
      for (size_t byte = 0; byte < clusterBytes; ++byte) {
        const uint8_t value = static_cast<uint8_t>((logical * 29 + byte * 13 + 7) & 0xff);
        fileCluster[byte] = value;
        if (logical * clusterBytes + byte < layout.expectedFile.size())
          layout.expectedFile[logical * clusterBytes + byte] = value;
      }
      WriteClusterBytes(backing, layout, layout.fileClusters[logical],
          kstd::span<const uint8_t>(fileCluster.data(), fileCluster.size()));
      const uint64_t lba = layout.dataStartSector +
          static_cast<uint64_t>(layout.fileClusters[logical] - 2) * sectorsPerCluster;
      layout.fileOffsets.push_back(partitionBase + lba * kSectorSize);
    }
  }

  return layout;
}

// Test-local volatile hive standing in for driver_loader.cpp's "Boot" hive,
// so LoadDriver() below can hand each driver a per-driver configuration key
// the same way KE_DRIVER_Load() does for a real boot driver.
kernel::KConfigurationKeyPtr GetTestBootRoot() {
  if (auto existing = KE_CM_OpenRoot("IntegrationTestBoot").value_or(nullptr)) {
    return existing;
  }
  return KE_CM_OpenRoot("IntegrationTestBoot").value_or(nullptr);
}

// Loads a driver module and calls its DriverEntry with a freshly created
// KDriverObject, mirroring what driver_loader.cpp would do for a real ELF
// driver image.
kernel::KObjectPtr<kernel::KDriverObject> LoadDriver(const char *path, const char *driverName) {
  void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  REQUIRE(handle != nullptr);

  auto *entry = reinterpret_cast<DriverEntryFn>(dlsym(handle, "DriverEntry"));
  REQUIRE(entry != nullptr);

  auto driver = kernel::CreateKObject<kernel::KDriverObject>(driverName).value_or(nullptr);

  kernel::KConfigurationKeyPtr driverConfig = nullptr;
  if (const auto bootRoot = GetTestBootRoot()) {
    driverConfig = KE_CM_CreateKey(bootRoot, driverName, kernel::KConfigurationKeyAccess::ReadWrite).value_or(nullptr);
  }

  entry(driver, driverConfig);
  return driver;
}

}// namespace

TEST_CASE("End-to-end disk_fake -> volmgr -> fs_fat -> VFS mount", "[vfs][fat][integration]") {
  EnsureObjectManagerInitialized();
  g_shortRead = {};

  const auto imagePath = MakeTempImagePath();
  const kstd::string_view imagePathView(imagePath);
  diskfake::FileBackingStore backing(imagePathView);
  REQUIRE(backing.IsOpen());

  const auto diskImage = BuildFat12DiskImage();
  REQUIRE(diskfake::Seed(diskfake::FakeDiskContext{}, kstd::span<const uint8_t>(diskImage)) == kernel::KernelError::InvalidState);// no backing set, sanity-check Seed's guard
  REQUIRE_FALSE(backing.Resize(static_cast<size_t>(kPartitionStartLba + kPartitionSectors) * kSectorSize));
  REQUIRE_FALSE(backing.WriteAt(0, kstd::span<const uint8_t>(diskImage)));

  UNSCOPED_INFO("Disk image left on disk for inspection at: " << imagePath.data());

  // --- disk_fake: a Disk PDO backed by the image file we just wrote ---
  auto diskDriver = kernel::CreateKObject<kernel::KDriverObject>("IntegrationDiskDriver").value_or(nullptr);
  diskDriver->Read = ShortableDiskRead;
  diskDriver->Write = diskfake::Write;

  static unsigned fixtureNumber = 0;
  kstd::static_string<64> diskName;
  kstd::format(diskName, "IntegrationDisk{}", ++fixtureNumber);
  auto diskDevice = KE_IO_CreateDeviceWithContext<diskfake::FakeDiskContext>(diskDriver, diskName, kernel::device_type::Disk).value_or(nullptr);
  REQUIRE(diskDevice != nullptr);
  diskDevice->deviceExtension.as<diskfake::FakeDiskContext>()->backing = &backing;

  // --- volmgr: load as a standalone module and drive AddDevice/StartDevice directly ---
  auto volmgrDriver = LoadDriver(VOLMGR_DRIVER_PATH, "IntegrationVolMgrDriver");
  REQUIRE(volmgrDriver->AddDevice.valid());
  REQUIRE(volmgrDriver->StartDevice.valid());

  REQUIRE_FALSE(volmgrDriver->AddDevice(volmgrDriver, diskDevice));
  g_lastReportedPdo = nullptr;
  volmgrDriver->StartDevice(diskDevice);

  const auto volumePdo = g_lastReportedPdo;
  REQUIRE(volumePdo != nullptr);
  REQUIRE(volumePdo->deviceType == kernel::device_type::Volume);

  // --- fs_fat: load as a standalone module; its DriverEntry registers "FAT" with the VFS ---
  LoadDriver(FS_FAT_DRIVER_PATH, "IntegrationFatDriver");

  // KE_VFS_Mount only permits mounting at a non-root path when a parent mount
  // already exists, so mount this standalone volume at the root "/".
  REQUIRE_FALSE(KE_VFS_Mount(volumePdo, "FAT", "/", ""));

  SECTION("Directory browsing and persistent short-name file management") {
    using kernel::KernelError;
    using kernel::VFSFileOperation;
    kstd::array<uint8_t, 512> root{};
    const char sourceName[] = "SOURCE  TXT";
    for (size_t i = 0; i < 11; ++i) root[i] = static_cast<uint8_t>(sourceName[i]);
    root[11] = 32;
    PutLe16(kstd::span(root).subspan(26), 2);
    PutLe32(kstd::span(root).subspan(28), 700);
    REQUIRE_FALSE(backing.WriteAt(3 * kSectorSize, root));
    kstd::array<uint8_t, 512> fat{};
    fat[0] = 0xf8; fat[1] = fat[2] = 0xff;
    fat[3] = 3; fat[4] = 0xf0; fat[5] = 0xff; // cluster 2 -> 3 -> EOC
    REQUIRE_FALSE(backing.WriteAt(2 * kSectorSize, fat));
    kstd::array<uint8_t, 1024> data{};
    for (size_t i = 0; i < 700; ++i) data[i] = static_cast<uint8_t>(i * 17);
    REQUIRE_FALSE(backing.WriteAt(4 * kSectorSize, data));

    auto directory = KE_VFS_OpenFile("/", kernel::KIOObject::OpenMode::Read);
    REQUIRE(directory);
    kernel::KVFSDirectoryEntry entry{};
    auto found = KE_VFS_ReadDirectory(*directory, 0, entry);
    REQUIRE(found);
    REQUIRE(*found);
    REQUIRE(kstd::string_view(entry.name) == "SOURCE.TXT");
    REQUIRE(entry.size == 700);
    REQUIRE(entry.attributes == 32);
    REQUIRE_FALSE(*KE_VFS_ReadDirectory(*directory, 1, entry));
    REQUIRE_FALSE(*KE_VFS_ReadDirectory(*directory, UINT32_MAX, entry));
    REQUIRE(KE_VFS_OperateFile(VFSFileOperation::Copy, "/SOURCE.TXT", "/COPY.TXT") == KernelError::ResourceConflict);
    REQUIRE(KE_VFS_Unmount("/") == KernelError::ResourceConflict);
    REQUIRE_FALSE(KE_VFS_CloseFile(*directory));
    REQUIRE(KE_VFS_ReadDirectory(*directory, 0, entry).error() == KernelError::InvalidState);
    REQUIRE(KE_VFS_ReadDirectory(nullptr, 0, entry).error() == KernelError::InvalidHandle);
    REQUIRE(KE_VFS_OperateFile(VFSFileOperation::Delete, "/", "") == KernelError::InvalidArgument);
    REQUIRE(KE_VFS_OperateFile(VFSFileOperation::Delete, "/../SOURCE.TXT", "") == KernelError::InvalidArgument);
    REQUIRE(KE_VFS_OperateFile(VFSFileOperation::Delete, "/SOURCE.TXT", "/other") == KernelError::InvalidArgument);
    REQUIRE(KE_VFS_OperateFile(static_cast<VFSFileOperation>(99), "/SOURCE.TXT", "") == KernelError::InvalidArgument);
    REQUIRE(KE_VFS_OperateFile(VFSFileOperation::Copy, "/SOURCE.TXT", "/SOURCE.TXT") == KernelError::AlreadyExists);
    REQUIRE(KE_VFS_OperateFile(VFSFileOperation::Copy, "/SOURCE.TXT", "/bad*name") == KernelError::InvalidArgument);
    REQUIRE(KE_VFS_OperateFile(VFSFileOperation::Copy, "/SOURCE.TXT", "/TOOLONGNAME.TXT") == KernelError::NameTooLong);
    REQUIRE_FALSE(KE_VFS_OperateFile(VFSFileOperation::Copy, "/SOURCE.TXT", "/COPY.TXT"));
    auto copied = KE_VFS_OpenFile("/COPY.TXT", kernel::KIOObject::OpenMode::Read);
    REQUIRE(copied);
    REQUIRE((*copied)->vfsNode->size == 700);
    kstd::array<uint8_t, 1024> read{};
    REQUIRE(*KE_VFS_ReadFile(*copied, read) == 700);
    for (size_t i = 0; i < 700; ++i) REQUIRE(read[i] == data[i]);
    REQUIRE_FALSE(KE_VFS_CloseFile(*copied));
    auto unrelated = KE_VFS_OpenFile("/./source.txt", kernel::KIOObject::OpenMode::ReadWrite);
    REQUIRE(unrelated);
    REQUIRE(KE_VFS_OperateFile(VFSFileOperation::Delete, "/SOURCE.TXT", "") == KernelError::ResourceConflict);
    REQUIRE_FALSE(KE_VFS_OperateFile(VFSFileOperation::CreateDirectory, "/FOLDER", ""));
    REQUIRE_FALSE(KE_VFS_CloseFile(*unrelated));
    REQUIRE(KE_VFS_OperateFile(VFSFileOperation::Copy, "/FOLDER", "/OTHER") == KernelError::Unsupported);
    REQUIRE_FALSE(KE_VFS_OperateFile(VFSFileOperation::Move, "/COPY.TXT", "/FOLDER/MOVED.TXT"));
    REQUIRE_FALSE(KE_VFS_OpenFile("/COPY.TXT", kernel::KIOObject::OpenMode::Read));
    REQUIRE(KE_VFS_OperateFile(VFSFileOperation::Delete, "/FOLDER", "") == KernelError::AccessDenied);
    REQUIRE_FALSE(KE_VFS_OperateFile(VFSFileOperation::Move, "/FOLDER/MOVED.TXT", "/FOLDER/NEW.TXT"));
    REQUIRE_FALSE(KE_VFS_OperateFile(VFSFileOperation::Delete, "/FOLDER/NEW.TXT", ""));
    REQUIRE_FALSE(KE_VFS_OperateFile(VFSFileOperation::Move, "/FOLDER", "/RENAMED"));
    REQUIRE_FALSE(KE_VFS_OperateFile(VFSFileOperation::Delete, "/RENAMED", ""));
    // Remounting proves that enumeration and content came from persisted sectors.
    REQUIRE_FALSE(KE_VFS_Unmount("/"));
    REQUIRE_FALSE(KE_VFS_Mount(volumePdo, "FAT", "/", ""));
    directory = KE_VFS_OpenFile("/", kernel::KIOObject::OpenMode::Read);
    REQUIRE(directory);
    REQUIRE(*KE_VFS_ReadDirectory(*directory, 0, entry));
    REQUIRE(kstd::string_view(entry.name) == "SOURCE.TXT");
    REQUIRE_FALSE(*KE_VFS_ReadDirectory(*directory, 1, entry));
    REQUIRE_FALSE(KE_VFS_CloseFile(*directory));
    REQUIRE_FALSE(backing.ReadAt(2 * kSectorSize, read));
    REQUIRE(read[3] == 3); // source chain is unchanged
    REQUIRE(read[4] == 0xf0);
    REQUIRE(read[5] == 0xff);
    for (size_t i = 6; i < 54; ++i) REQUIRE(read[i] == 0); // allocated clusters reclaimed
  }

  SECTION("FAT chain checkpoints preserve streamed data and linear backing reads") {
    using kernel::KernelError;
    CountingBacking counted(backing);
    auto *diskContext = diskDevice->deviceExtension.as<diskfake::FakeDiskContext>();
    const auto prepare = [&](TestFatType type, uint32_t sectorsPerCluster,
                             bool fullRoot = false, bool includeSkips = true,
                             bool truncateFileChain = false, bool zeroFileStart = false,
                             bool invalidDirectoryStart = false, bool cycleDirectory = false,
                             bool fullDirectory = false) {
      if (KE_OB_FindDirectChildOfType<kernel::KVolumeMountObject>(KE_OB_GetVFSDirectory(), "/"))
        REQUIRE_FALSE(KE_VFS_Unmount("/"));
      diskContext->backing = &backing;
      counted.failReadOffset = counted.failWriteOffset = UINT64_MAX;
      counted.partialWriteBytes = 0;
      auto layout = SeedFatImage(backing, type, sectorsPerCluster, fullRoot, includeSkips,
          truncateFileChain, zeroFileStart, invalidDirectoryStart, cycleDirectory, fullDirectory);
      const uint64_t base = static_cast<uint64_t>(kPartitionStartLba) * kSectorSize;
      counted.fatStart = base + static_cast<uint64_t>(layout.reservedSectors) * kSectorSize;
      counted.fatEnd = counted.fatStart + static_cast<uint64_t>(layout.fatSectors) * kSectorSize;
      counted.directoryOffsets = layout.directoryOffsets;
      counted.fileOffsets = layout.fileOffsets;
      g_shortRead = {};
      diskContext->backing = &counted;
      REQUIRE_FALSE(KE_VFS_Mount(volumePdo, "FAT", "/", ""));
      counted.ResetCounts();
      return layout;
    };

    const auto emptyReadLayout = prepare(TestFatType::Fat16, 1, false, false);
    auto emptyReadFile = KE_VFS_OpenFile("/DATA.BIN", kernel::KIOObject::OpenMode::Read);
    REQUIRE(emptyReadFile);
    constexpr uint64_t emptyReadOffset = 37;
    REQUIRE(KE_VFS_SeekFile(*emptyReadFile, emptyReadOffset).value() == emptyReadOffset);
    counted.ResetCounts();
    REQUIRE(KE_VFS_ReadFile(*emptyReadFile, kstd::span<uint8_t>{}).value() == 0);
    REQUIRE((*emptyReadFile)->offset == emptyReadOffset);
    REQUIRE(KE_VFS_TellFile(*emptyReadFile).value() == emptyReadOffset);
    REQUIRE(counted.readCalls == 0);
    REQUIRE(counted.fatReadCalls == 0);
    REQUIRE_FALSE(KE_VFS_CloseFile(*emptyReadFile));

    const TestFatType streamTypes[] = {TestFatType::Fat12, TestFatType::Fat16, TestFatType::Fat32};
    const uint32_t clusterSectors[] = {2, 1, 1};
    for (size_t typeIndex = 0; typeIndex < 3; ++typeIndex) {
      const auto layout = prepare(streamTypes[typeIndex], clusterSectors[typeIndex]);
      auto file = KE_VFS_OpenFile("/DATA.BIN", kernel::KIOObject::OpenMode::Read);
      REQUIRE(file);
      const size_t clusterBytes = static_cast<size_t>(layout.sectorsPerCluster) * kSectorSize;
      std::vector<uint8_t> actual(layout.expectedFile.size());
      const size_t chunks[] = {1, 97, 511, clusterBytes + 23, 173};
      size_t position = 0;
      size_t chunkIndex = 0;
      counted.ResetCounts();
      while (position < actual.size()) {
        const size_t request = std::min(chunks[chunkIndex++ % 5], actual.size() - position);
        const auto read = KE_VFS_ReadFile(*file, kstd::span<uint8_t>(actual.data() + position, request));
        REQUIRE(read);
        REQUIRE(*read == request);
        position += request;
        REQUIRE(KE_VFS_TellFile(*file).value() == position);
      }
      REQUIRE(actual == layout.expectedFile);
      REQUIRE(counted.fatReadCalls == layout.fileClusters.size() - 1);
      REQUIRE(counted.fatReadSectors == 2 * (layout.fileClusters.size() - 1));
      const auto readsAtEnd = counted.fatReadCalls;
      REQUIRE(KE_VFS_ReadFile(*file, kstd::span<uint8_t>{}).value() == 0);
      REQUIRE(KE_VFS_ReadFile(*file, kstd::span<uint8_t>(actual.data(), 1)).value() == 0);
      REQUIRE(counted.fatReadCalls == readsAtEnd);
      REQUIRE_FALSE(KE_VFS_CloseFile(*file));
    }

    const auto layout = prepare(TestFatType::Fat12, 2);
    const size_t clusterBytes = static_cast<size_t>(layout.sectorsPerCluster) * kSectorSize;
    auto first = KE_VFS_OpenFile("/DATA.BIN", kernel::KIOObject::OpenMode::Read);
    auto second = KE_VFS_OpenFile("/DATA.BIN", kernel::KIOObject::OpenMode::Read);
    REQUIRE(first);
    REQUIRE(second);
    uint8_t byte = 0;
    REQUIRE(*KE_VFS_ReadFile(*first, kstd::span<uint8_t>(&byte, 1)) == 1);
    REQUIRE(byte == layout.expectedFile[0]);
    REQUIRE(*KE_VFS_ReadFile(*second, kstd::span<uint8_t>(&byte, 1)) == 1);
    REQUIRE(byte == layout.expectedFile[0]);
    REQUIRE((*first)->offset == 1);
    REQUIRE((*second)->offset == 1);

    const uint64_t forwardOffset = clusterBytes * 11 + 19;
    counted.ResetCounts();
    REQUIRE(KE_VFS_SeekFile(*first, forwardOffset).value() == forwardOffset);
    uint8_t sample[4]{};
    REQUIRE(*KE_VFS_ReadFile(*first, sample) == sizeof(sample));
    for (size_t i = 0; i < sizeof(sample); ++i) REQUIRE(sample[i] == layout.expectedFile[forwardOffset + i]);
    REQUIRE((*second)->offset == 1);

    counted.ResetCounts();
    REQUIRE(KE_VFS_SeekFile(*first, clusterBytes * 10).value() == clusterBytes * 10);
    REQUIRE(*KE_VFS_ReadFile(*first, kstd::span<uint8_t>(&byte, 1)) == 1);
    REQUIRE(counted.fatReadCalls == 10);
    REQUIRE(KE_VFS_SeekFile(*second, clusterBytes * 5).value() == clusterBytes * 5);
    REQUIRE(*KE_VFS_ReadFile(*second, kstd::span<uint8_t>(&byte, 1)) == 1);
    REQUIRE(counted.fatReadCalls == 15);
    counted.ResetCounts();
    REQUIRE(KE_VFS_SeekFile(*first, clusterBytes * 11).value() == clusterBytes * 11);
    REQUIRE(*KE_VFS_ReadFile(*first, kstd::span<uint8_t>(&byte, 1)) == 1);
    REQUIRE(counted.fatReadCalls == 1);

    const uint64_t backwardOffset = clusterBytes * 3 + 9;
    REQUIRE(KE_VFS_SeekFile(*first, backwardOffset).value() == backwardOffset);
    REQUIRE(*KE_VFS_ReadFile(*first, sample) == sizeof(sample));
    for (size_t i = 0; i < sizeof(sample); ++i) REQUIRE(sample[i] == layout.expectedFile[backwardOffset + i]);
    REQUIRE(KE_VFS_SeekFile(*first, 0).value() == 0);
    auto shared = *first;
    REQUIRE(*KE_VFS_ReadFile(shared, kstd::span<uint8_t>(&byte, 1)) == 1);
    REQUIRE((*first)->offset == 1);
    REQUIRE(KE_VFS_SeekFile(*first, layout.expectedFile.size()).value() == layout.expectedFile.size());
    REQUIRE(KE_VFS_ReadFile(*first, kstd::span<uint8_t>(&byte, 1)).value() == 0);
    const uint64_t pastEnd = layout.expectedFile.size() + 37;
    REQUIRE(KE_VFS_SeekFile(*first, pastEnd).value() == pastEnd);
    REQUIRE(KE_VFS_ReadFile(*first, kstd::span<uint8_t>(&byte, 1)).value() == 0);
    REQUIRE((*first)->offset == pastEnd);
    shared = nullptr;
    REQUIRE_FALSE(KE_VFS_CloseFile(*first));
    REQUIRE_FALSE(KE_VFS_CloseFile(*second));

    const auto retainedCheckpointLayout = prepare(TestFatType::Fat16, 1, false, false);
    auto retainedCheckpointFile = KE_VFS_OpenFile("/DATA.BIN", kernel::KIOObject::OpenMode::Read);
    REQUIRE(retainedCheckpointFile);
    const uint64_t retainedClusterSize = kSectorSize;
    const uint64_t checkpointOffset = retainedClusterSize * 10;
    REQUIRE(KE_VFS_SeekFile(*retainedCheckpointFile, checkpointOffset).value() == checkpointOffset);
    REQUIRE(*KE_VFS_ReadFile(*retainedCheckpointFile, kstd::span<uint8_t>(&byte, 1)) == 1);
    REQUIRE(byte == retainedCheckpointLayout.expectedFile[checkpointOffset]);
    const uint32_t firstChainEntryOffset = retainedCheckpointLayout.fileClusters.front() * 2;
    counted.failReadOffset = counted.fatStart +
        (firstChainEntryOffset / kSectorSize) * kSectorSize;
    const uint64_t backwardFailureOffset = retainedClusterSize * 5;
    REQUIRE(KE_VFS_SeekFile(*retainedCheckpointFile, backwardFailureOffset).value() == backwardFailureOffset);
    counted.ResetCounts();
    auto failedBackwardRead = KE_VFS_ReadFile(*retainedCheckpointFile, kstd::span<uint8_t>(&byte, 1));
    REQUIRE_FALSE(failedBackwardRead);
    REQUIRE(failedBackwardRead.error() == KernelError::DeviceError);
    REQUIRE((*retainedCheckpointFile)->offset == backwardFailureOffset);
    REQUIRE(counted.fatReadCalls == 1);
    counted.failReadOffset = UINT64_MAX;
    const uint64_t forwardContinuationOffset = retainedClusterSize * 11;
    REQUIRE(KE_VFS_SeekFile(*retainedCheckpointFile, forwardContinuationOffset).value() ==
        forwardContinuationOffset);
    counted.ResetCounts();
    REQUIRE(*KE_VFS_ReadFile(*retainedCheckpointFile, kstd::span<uint8_t>(&byte, 1)) == 1);
    REQUIRE(byte == retainedCheckpointLayout.expectedFile[forwardContinuationOffset]);
    REQUIRE((*retainedCheckpointFile)->offset == forwardContinuationOffset + 1);
    REQUIRE(counted.fatReadCalls == 1);
    REQUIRE_FALSE(KE_VFS_CloseFile(*retainedCheckpointFile));

    const auto faultLayout = prepare(TestFatType::Fat16, 1, false, false);
    auto file = KE_VFS_OpenFile("/DATA.BIN", kernel::KIOObject::OpenMode::Read);
    REQUIRE(file);
    const size_t faultClusterBytes = faultLayout.sectorsPerCluster * kSectorSize;
    const uint32_t firstCluster = faultLayout.fileClusters[0];
    const uint32_t firstEntryOffset = firstCluster * 2;
    counted.failReadOffset = counted.fatStart + (firstEntryOffset / kSectorSize) * kSectorSize;
    counted.ResetCounts();
    std::vector<uint8_t> cluster(faultClusterBytes);
    REQUIRE(*KE_VFS_ReadFile(*file, kstd::span<uint8_t>(cluster.data(), cluster.size())) == cluster.size());
    REQUIRE(counted.fatReadCalls == 0);
    REQUIRE((*file)->offset == faultClusterBytes);
    auto failedLink = KE_VFS_ReadFile(*file, kstd::span<uint8_t>(&byte, 1));
    REQUIRE_FALSE(failedLink);
    REQUIRE(failedLink.error() == KernelError::DeviceError);
    REQUIRE((*file)->offset == faultClusterBytes);
    REQUIRE(counted.fatReadCalls == 1);
    counted.failReadOffset = UINT64_MAX;
    REQUIRE(*KE_VFS_ReadFile(*file, kstd::span<uint8_t>(&byte, 1)) == 1);
    REQUIRE(byte == faultLayout.expectedFile[faultClusterBytes]);
    REQUIRE_FALSE(KE_VFS_CloseFile(*file));

    const auto dirLayout = prepare(TestFatType::Fat16, 1, false, false);
    auto directory = KE_VFS_OpenFile("/DIR", kernel::KIOObject::OpenMode::Read);
    REQUIRE(directory);
    counted.failReadOffset = dirLayout.directoryOffsets.front();
    counted.ResetCounts();
    kernel::KVFSDirectoryEntry entry{};
    auto failedDirectorySector = KE_VFS_ReadDirectory(*directory, 0, entry);
    REQUIRE_FALSE(failedDirectorySector);
    REQUIRE(failedDirectorySector.error() == KernelError::DeviceError);
    REQUIRE(counted.directoryReadCalls == 1);
    counted.failReadOffset = UINT64_MAX;
    REQUIRE(*KE_VFS_ReadDirectory(*directory, 0, entry));
    REQUIRE(kstd::string_view(entry.name) == "F0000000.TXT");
    REQUIRE(counted.directoryReadCalls == 2);
    REQUIRE_FALSE(KE_VFS_CloseFile(*directory));

    directory = KE_VFS_OpenFile("/DIR", kernel::KIOObject::OpenMode::Read);
    REQUIRE(directory);
    const uint32_t dirEntryOffset = dirLayout.directoryClusters[0] * 2;
    counted.failReadOffset = counted.fatStart + (dirEntryOffset / kSectorSize) * kSectorSize;
    counted.ResetCounts();
    REQUIRE(*KE_VFS_ReadDirectory(*directory, 15, entry));
    REQUIRE(kstd::string_view(entry.name) == "F0000015.TXT");
    REQUIRE(counted.fatReadCalls == 0);
    const auto failedNext = KE_VFS_ReadDirectory(*directory, 16, entry);
    REQUIRE_FALSE(failedNext);
    REQUIRE(failedNext.error() == KernelError::DeviceError);
    REQUIRE(counted.fatReadCalls == 1);
    counted.failReadOffset = UINT64_MAX;
    REQUIRE(*KE_VFS_ReadDirectory(*directory, 16, entry));
    REQUIRE(kstd::string_view(entry.name) == "F0000016.TXT");
    REQUIRE_FALSE(KE_VFS_CloseFile(*directory));

    for (const auto type : {TestFatType::Fat12, TestFatType::Fat16, TestFatType::Fat32}) {
      const auto largeDir = prepare(type, 1);
      auto target = type == TestFatType::Fat32
          ? KE_VFS_OpenFile("/", kernel::KIOObject::OpenMode::Read)
          : KE_VFS_OpenFile("/DIR", kernel::KIOObject::OpenMode::Read);
      REQUIRE(target);
      counted.directoryOffsets = type == TestFatType::Fat32
          ? largeDir.rootDirectoryOffsets : largeDir.directoryOffsets;
      counted.ResetCounts();
      const uint32_t visible = type == TestFatType::Fat32 ? 130 : 128;
      for (uint32_t index = 0; index < visible; ++index) {
        REQUIRE(*KE_VFS_ReadDirectory(*target, index, entry));
        char expected[16]{};
        if (type == TestFatType::Fat32 && index == 0) {
          std::snprintf(expected, sizeof(expected), "DATA.BIN");
          REQUIRE(entry.size == largeDir.expectedFile.size());
          REQUIRE(entry.attributes == 0x20);
        } else if (type == TestFatType::Fat32 && index == 1) {
          std::snprintf(expected, sizeof(expected), "DIR");
          REQUIRE(entry.size == 0);
          REQUIRE(entry.attributes == 0x10);
        } else {
          const uint32_t childIndex = type == TestFatType::Fat32 ? index - 2 : index;
          std::snprintf(expected, sizeof(expected), "F%07u.TXT", childIndex);
          REQUIRE(entry.size == childIndex + 1);
          REQUIRE(entry.attributes == 0x20);
        }
        REQUIRE(kstd::string_view(entry.name) == expected);
      }
      REQUIRE_FALSE(*KE_VFS_ReadDirectory(*target, visible, entry));
      REQUIRE(counted.fatReadCalls == largeDir.directoryClusters.size() - 1);
      const auto sectorCount = type == TestFatType::Fat32
          ? largeDir.rootDirectoryOffsets.size() : largeDir.directoryOffsets.size();
      REQUIRE(counted.directoryReadCalls <= visible + sectorCount + 1);
      const auto readsAtEof = counted.readCalls;
      REQUIRE_FALSE(*KE_VFS_ReadDirectory(*target, visible, entry));
      REQUIRE_FALSE(*KE_VFS_ReadDirectory(*target, UINT32_MAX, entry));
      REQUIRE(counted.readCalls == readsAtEof);
      REQUIRE_FALSE(KE_VFS_CloseFile(*target));
    }

    const auto fixedRoot = prepare(TestFatType::Fat12, 1);
    auto root = KE_VFS_OpenFile("/", kernel::KIOObject::OpenMode::Read);
    REQUIRE(root);
    counted.directoryOffsets = fixedRoot.rootDirectoryOffsets;
    counted.ResetCounts();
    for (uint32_t index = 0; index < 130; ++index) {
      REQUIRE(*KE_VFS_ReadDirectory(*root, index, entry));
      if (index == 0) {
        REQUIRE(kstd::string_view(entry.name) == "DATA.BIN");
        REQUIRE(entry.size == fixedRoot.expectedFile.size());
        REQUIRE(entry.attributes == 0x20);
      } else if (index == 1) {
        REQUIRE(kstd::string_view(entry.name) == "DIR");
        REQUIRE(entry.size == 0);
        REQUIRE(entry.attributes == 0x10);
      } else {
        char expected[16]{};
        std::snprintf(expected, sizeof(expected), "F%07u.TXT", index - 2);
        REQUIRE(kstd::string_view(entry.name) == expected);
        REQUIRE(entry.size == index - 1);
        REQUIRE(entry.attributes == 0x20);
      }
    }
    REQUIRE_FALSE(*KE_VFS_ReadDirectory(*root, 130, entry));
    REQUIRE(counted.fatReadCalls == 0);
    REQUIRE(counted.directoryReadCalls <= 130 + fixedRoot.rootSectors + 1);
    const auto fixedEofReads = counted.readCalls;
    REQUIRE_FALSE(*KE_VFS_ReadDirectory(*root, UINT32_MAX, entry));
    REQUIRE(counted.readCalls == fixedEofReads);
    REQUIRE_FALSE(KE_VFS_CloseFile(*root));

    const auto fullRoot = prepare(TestFatType::Fat16, 1, true);
    root = KE_VFS_OpenFile("/", kernel::KIOObject::OpenMode::Read);
    REQUIRE(root);
    counted.directoryOffsets = fullRoot.rootDirectoryOffsets;
    counted.ResetCounts();
    for (uint32_t index = 0; index < fullRoot.rootEntries; ++index) {
      REQUIRE(*KE_VFS_ReadDirectory(*root, index, entry));
      char expected[16]{};
      std::snprintf(expected, sizeof(expected), "R%07u.TXT", index);
      REQUIRE(kstd::string_view(entry.name) == expected);
    }
    REQUIRE_FALSE(*KE_VFS_ReadDirectory(*root, fullRoot.rootEntries, entry));
    REQUIRE(counted.fatReadCalls == 0);
    REQUIRE(counted.directoryReadCalls <= fullRoot.rootEntries + fullRoot.rootSectors + 1);
    REQUIRE_FALSE(KE_VFS_CloseFile(*root));

    const auto replayLayout = prepare(TestFatType::Fat16, 1);
    auto replay = KE_VFS_OpenFile("/DIR", kernel::KIOObject::OpenMode::Read);
    REQUIRE(replay);
    counted.directoryOffsets = replayLayout.directoryOffsets;
    counted.ResetCounts();
    const uint32_t replayIndices[] = {0, 1, 1, 7, 2, 3};
    for (const uint32_t index : replayIndices) {
      REQUIRE(*KE_VFS_ReadDirectory(*replay, index, entry));
      char expected[16]{};
      std::snprintf(expected, sizeof(expected), "F%07u.TXT", index);
      REQUIRE(kstd::string_view(entry.name) == expected);
    }
    REQUIRE_FALSE(*KE_VFS_ReadDirectory(*replay, UINT32_MAX, entry));
    const auto readsAfterUnknownEof = counted.readCalls;
    REQUIRE_FALSE(*KE_VFS_ReadDirectory(*replay, UINT32_MAX, entry));
    REQUIRE(counted.readCalls == readsAfterUnknownEof);
    REQUIRE(*KE_VFS_ReadDirectory(*replay, 0, entry));
    REQUIRE(kstd::string_view(entry.name) == "F0000000.TXT");
    counted.ResetCounts();
    kstd::error_code lfnMutation;
    {
      const auto mount = (*replay)->mountPoint;
      kstd::unique_lock lock(mount->fileMutex);
      lfnMutation = mount->fileSystem->OperateFile(mount, kernel::VFSFileOperation::Move,
          *(*replay)->vfsNode, "F0000014.TXT", (*replay)->vfsNode.get(), "RENAMED.TXT");
    }
    REQUIRE(lfnMutation == KernelError::Unsupported);
    counted.ResetCounts();
    REQUIRE(*KE_VFS_ReadDirectory(*replay, 1, entry));
    REQUIRE(kstd::string_view(entry.name) == "F0000001.TXT");
    REQUIRE(counted.fatReadCalls == 0);
    REQUIRE_FALSE(KE_VFS_CloseFile(*replay));
    REQUIRE(KE_VFS_OperateFile(kernel::VFSFileOperation::Move,
        "/DIR/F0000014.TXT", "/DIR/RENAMED.TXT") == KernelError::Unsupported);

    counted.ResetCounts();
    const auto missing = KE_VFS_OpenFile("/DIR", kernel::KIOObject::OpenMode::Read);
    REQUIRE(missing);
    const auto mount = (*missing)->mountPoint;
    kstd::error_code missingResult;
    {
      kstd::unique_lock lock(mount->fileMutex);
      missingResult = mount->fileSystem->OperateFile(mount, kernel::VFSFileOperation::Delete,
          *(*missing)->vfsNode, "MISSING.TXT", nullptr, {});
    }
    REQUIRE(missingResult == KernelError::NotFound);
    REQUIRE(counted.fatReadCalls == replayLayout.directoryClusters.size() - 1);
    REQUIRE(KE_VFS_OperateFile(kernel::VFSFileOperation::CreateDirectory, "/BLOCKED", "") ==
        KernelError::ResourceConflict);
    REQUIRE_FALSE(KE_VFS_CloseFile(*missing));

    const auto multiSectorDirectoryLayout = prepare(TestFatType::Fat16, 2);
    auto multiSectorDirectory = KE_VFS_OpenFile("/DIR", kernel::KIOObject::OpenMode::Read);
    REQUIRE(multiSectorDirectory);
    counted.directoryOffsets = multiSectorDirectoryLayout.directoryOffsets;
    counted.ResetCounts();
    kstd::error_code multiSectorMissing;
    {
      const auto mountPoint = (*multiSectorDirectory)->mountPoint;
      kstd::unique_lock lock(mountPoint->fileMutex);
      multiSectorMissing = mountPoint->fileSystem->OperateFile(mountPoint, kernel::VFSFileOperation::Delete,
          *(*multiSectorDirectory)->vfsNode, "MISSING.TXT", nullptr, {});
    }
    REQUIRE(multiSectorMissing == KernelError::NotFound);
    REQUIRE(counted.directoryReadCalls == 9);
    REQUIRE(counted.fatReadCalls == 4);
    REQUIRE_FALSE(KE_VFS_CloseFile(*multiSectorDirectory));

    const auto fullDirectoryLayout = prepare(TestFatType::Fat16, 1, false, false, false, false, false, false, true);
    auto growingDirectory = KE_VFS_OpenFile("/DIR", kernel::KIOObject::OpenMode::Read);
    REQUIRE(growingDirectory);
    counted.directoryOffsets = fullDirectoryLayout.directoryOffsets;
    REQUIRE(*KE_VFS_ReadDirectory(*growingDirectory, 143, entry));
    REQUIRE(kstd::string_view(entry.name) == "F0000143.TXT");
    REQUIRE_FALSE(*KE_VFS_ReadDirectory(*growingDirectory, UINT32_MAX, entry));
    REQUIRE(KE_VFS_OperateFile(kernel::VFSFileOperation::CreateDirectory, "/BLOCKED", "") ==
        KernelError::ResourceConflict);
    {
      const auto mountPoint = (*growingDirectory)->mountPoint;
      kstd::unique_lock lock(mountPoint->fileMutex);
      REQUIRE_FALSE(mountPoint->fileSystem->OperateFile(mountPoint, kernel::VFSFileOperation::CreateDirectory,
          *(*growingDirectory)->vfsNode, "APPEND", nullptr, {}));
    }
    counted.ResetCounts();
    REQUIRE(*KE_VFS_ReadDirectory(*growingDirectory, 144, entry));
    REQUIRE(kstd::string_view(entry.name) == "APPEND");
    REQUIRE(entry.attributes == 0x10);
    REQUIRE(counted.fatReadCalls == 9);
    REQUIRE_FALSE(KE_VFS_CloseFile(*growingDirectory));

    const auto renameMoveLayout = prepare(TestFatType::Fat16, 1);
    auto renameDirectory = KE_VFS_OpenFile("/DIR", kernel::KIOObject::OpenMode::Read);
    auto moveRoot = KE_VFS_OpenFile("/", kernel::KIOObject::OpenMode::Read);
    REQUIRE(renameDirectory);
    REQUIRE(moveRoot);
    counted.directoryOffsets = renameMoveLayout.directoryOffsets;
    REQUIRE_FALSE(*KE_VFS_ReadDirectory(*renameDirectory, UINT32_MAX, entry));
    REQUIRE_FALSE(*KE_VFS_ReadDirectory(*moveRoot, UINT32_MAX, entry));
    {
      const auto mountPoint = (*renameDirectory)->mountPoint;
      kstd::unique_lock lock(mountPoint->fileMutex);
      REQUIRE_FALSE(mountPoint->fileSystem->OperateFile(mountPoint, kernel::VFSFileOperation::Move,
          *(*renameDirectory)->vfsNode, "F0000000.TXT", (*renameDirectory)->vfsNode.get(), "RENAMED.TXT"));
    }
    REQUIRE(*KE_VFS_ReadDirectory(*renameDirectory, 0, entry));
    REQUIRE(kstd::string_view(entry.name) == "RENAMED.TXT");
    {
      const auto mountPoint = (*renameDirectory)->mountPoint;
      kstd::unique_lock lock(mountPoint->fileMutex);
      REQUIRE_FALSE(mountPoint->fileSystem->OperateFile(mountPoint, kernel::VFSFileOperation::Move,
          *(*renameDirectory)->vfsNode, "F0000001.TXT", (*moveRoot)->vfsNode.get(), "MOVED.TXT"));
    }
    REQUIRE(*KE_VFS_ReadDirectory(*renameDirectory, 1, entry));
    REQUIRE(kstd::string_view(entry.name) == "F0000002.TXT");
      auto movedFile = KE_VFS_OpenFile("/MOVED.TXT", kernel::KIOObject::OpenMode::Read);
      REQUIRE(movedFile);
      REQUIRE_FALSE(KE_VFS_CloseFile(*movedFile));
      REQUIRE(*KE_VFS_ReadDirectory(*moveRoot, 10, entry));
      REQUIRE(kstd::string_view(entry.name) == "MOVED.TXT");
      REQUIRE(*KE_VFS_ReadDirectory(*moveRoot, 11, entry));
      REQUIRE(kstd::string_view(entry.name) == "F0000008.TXT");
    REQUIRE_FALSE(KE_VFS_CloseFile(*renameDirectory));
    REQUIRE_FALSE(KE_VFS_CloseFile(*moveRoot));

    const auto reusableLayout = prepare(TestFatType::Fat16, 1);
    auto reusableDirectory = KE_VFS_OpenFile("/DIR", kernel::KIOObject::OpenMode::Read);
    REQUIRE(reusableDirectory);
    counted.directoryOffsets = reusableLayout.directoryOffsets;
    REQUIRE_FALSE(*KE_VFS_ReadDirectory(*reusableDirectory, UINT32_MAX, entry));
    REQUIRE(KE_VFS_OperateFile(kernel::VFSFileOperation::Delete, "/DIR/F0000000.TXT", "") ==
        KernelError::ResourceConflict);
    {
      const auto mountPoint = (*reusableDirectory)->mountPoint;
      kstd::unique_lock lock(mountPoint->fileMutex);
      REQUIRE_FALSE(mountPoint->fileSystem->OperateFile(mountPoint, kernel::VFSFileOperation::Delete,
          *(*reusableDirectory)->vfsNode, "F0000000.TXT", nullptr, {}));
      REQUIRE_FALSE(mountPoint->fileSystem->OperateFile(mountPoint, kernel::VFSFileOperation::CreateDirectory,
          *(*reusableDirectory)->vfsNode, "REUSED", nullptr, {}));
    }
    REQUIRE(*KE_VFS_ReadDirectory(*reusableDirectory, 0, entry));
    REQUIRE(kstd::string_view(entry.name) == "REUSED");
    REQUIRE(entry.attributes == 0x10);
    REQUIRE(*KE_VFS_ReadDirectory(*reusableDirectory, 1, entry));
    REQUIRE(kstd::string_view(entry.name) == "F0000001.TXT");
    REQUIRE_FALSE(KE_VFS_CloseFile(*reusableDirectory));

    const auto fileMutationLayout = prepare(TestFatType::Fat16, 1);
    auto liveFile = KE_VFS_OpenFile("/DATA.BIN", kernel::KIOObject::OpenMode::Read);
    REQUIRE(liveFile);
    const uint64_t fileClusterSize = kSectorSize;
    REQUIRE(KE_VFS_SeekFile(*liveFile, fileClusterSize * 8).value() == fileClusterSize * 8);
    REQUIRE(*KE_VFS_ReadFile(*liveFile, kstd::span<uint8_t>(&byte, 1)) == 1);
    counted.ResetCounts();
    REQUIRE_FALSE(KE_VFS_OperateFile(kernel::VFSFileOperation::CreateDirectory, "/NEW", ""));
    counted.ResetCounts();
    REQUIRE(KE_VFS_SeekFile(*liveFile, fileClusterSize * 10).value() == fileClusterSize * 10);
    REQUIRE(*KE_VFS_ReadFile(*liveFile, kstd::span<uint8_t>(&byte, 1)) == 1);
    REQUIRE(byte == fileMutationLayout.expectedFile[fileClusterSize * 10]);
    REQUIRE(counted.fatReadCalls == 10);
    REQUIRE_FALSE(KE_VFS_CloseFile(*liveFile));

    const auto dirMutationLayout = prepare(TestFatType::Fat16, 1);
    auto mutableDirectory = KE_VFS_OpenFile("/DIR", kernel::KIOObject::OpenMode::Read);
    REQUIRE(mutableDirectory);
    counted.directoryOffsets = dirMutationLayout.directoryOffsets;
    REQUIRE(*KE_VFS_ReadDirectory(*mutableDirectory, 64, entry));
    REQUIRE(kstd::string_view(entry.name) == "F0000064.TXT");
    REQUIRE_FALSE(*KE_VFS_ReadDirectory(*mutableDirectory, UINT32_MAX, entry));
    REQUIRE(KE_VFS_OperateFile(kernel::VFSFileOperation::CreateDirectory, "/PUBLIC", "") ==
        KernelError::ResourceConflict);
    {
      const auto mountPoint = (*mutableDirectory)->mountPoint;
      kstd::unique_lock lock(mountPoint->fileMutex);
      REQUIRE_FALSE(mountPoint->fileSystem->OperateFile(mountPoint, kernel::VFSFileOperation::Delete,
          *(*mutableDirectory)->vfsNode, "F0000000.TXT", nullptr, {}));
    }
    counted.ResetCounts();
    REQUIRE(*KE_VFS_ReadDirectory(*mutableDirectory, 64, entry));
    REQUIRE(kstd::string_view(entry.name) == "F0000065.TXT");
    REQUIRE(counted.fatReadCalls > 0);
    counted.ResetCounts();
    REQUIRE_FALSE(*KE_VFS_ReadDirectory(*mutableDirectory, UINT32_MAX, entry));
    REQUIRE(counted.fatReadCalls == dirMutationLayout.directoryClusters.size() - 5);
    REQUIRE(counted.directoryReadCalls <= dirMutationLayout.directoryClusters.size());
    REQUIRE(*KE_VFS_ReadDirectory(*mutableDirectory, 0, entry));
    REQUIRE(kstd::string_view(entry.name) == "F0000001.TXT");
    REQUIRE_FALSE(KE_VFS_CloseFile(*mutableDirectory));

    const auto failedWriteLayout = prepare(TestFatType::Fat16, 1);
    auto failedDirectory = KE_VFS_OpenFile("/DIR", kernel::KIOObject::OpenMode::Read);
    REQUIRE(failedDirectory);
    REQUIRE(*KE_VFS_ReadDirectory(*failedDirectory, 64, entry));
    REQUIRE_FALSE(*KE_VFS_ReadDirectory(*failedDirectory, UINT32_MAX, entry));
    const uint64_t firstDirectorySector = counted.directoryOffsets.front();
    counted.failWriteOffset = firstDirectorySector;
    {
      const auto mountPoint = (*failedDirectory)->mountPoint;
      kstd::unique_lock lock(mountPoint->fileMutex);
      REQUIRE(mountPoint->fileSystem->OperateFile(mountPoint, kernel::VFSFileOperation::Move,
          *(*failedDirectory)->vfsNode, "F0000000.TXT", (*failedDirectory)->vfsNode.get(),
          "Z0000000.TXT") == KernelError::IoError);
    }
    counted.failWriteOffset = UINT64_MAX;
    counted.ResetCounts();
    REQUIRE_FALSE(*KE_VFS_ReadDirectory(*failedDirectory, UINT32_MAX, entry));
    REQUIRE(counted.directoryReadCalls > 0);
    counted.ResetCounts();
    REQUIRE(*KE_VFS_ReadDirectory(*failedDirectory, 0, entry));
    REQUIRE(kstd::string_view(entry.name) == "F0000000.TXT");
    REQUIRE(counted.directoryReadCalls > 0);
    REQUIRE_FALSE(KE_VFS_CloseFile(*failedDirectory));

    const auto partialWriteLayout = prepare(TestFatType::Fat16, 1);
    auto partialDirectory = KE_VFS_OpenFile("/DIR", kernel::KIOObject::OpenMode::Read);
    REQUIRE(partialDirectory);
    REQUIRE(*KE_VFS_ReadDirectory(*partialDirectory, 64, entry));
    REQUIRE_FALSE(*KE_VFS_ReadDirectory(*partialDirectory, UINT32_MAX, entry));
    counted.failWriteOffset = counted.directoryOffsets.front();
    counted.partialWriteBytes = 1;
    {
      const auto mountPoint = (*partialDirectory)->mountPoint;
      kstd::unique_lock lock(mountPoint->fileMutex);
      REQUIRE(mountPoint->fileSystem->OperateFile(mountPoint, kernel::VFSFileOperation::Move,
          *(*partialDirectory)->vfsNode, "F0000000.TXT", (*partialDirectory)->vfsNode.get(),
          "Z0000000.TXT") == KernelError::IoError);
    }
    counted.failWriteOffset = UINT64_MAX;
    counted.partialWriteBytes = 0;
    counted.ResetCounts();
    REQUIRE_FALSE(*KE_VFS_ReadDirectory(*partialDirectory, UINT32_MAX, entry));
    REQUIRE(counted.directoryReadCalls > 0);
    counted.ResetCounts();
    REQUIRE(*KE_VFS_ReadDirectory(*partialDirectory, 0, entry));
    REQUIRE(kstd::string_view(entry.name) == "Z0000000.TXT");
    REQUIRE(counted.directoryReadCalls > 0);
    REQUIRE_FALSE(KE_VFS_CloseFile(*partialDirectory));

    const auto mirrorLayout = prepare(TestFatType::Fat16, 1);
    root = KE_VFS_OpenFile("/", kernel::KIOObject::OpenMode::Read);
    REQUIRE(root);
    counted.directoryOffsets = mirrorLayout.rootDirectoryOffsets;
    REQUIRE(*KE_VFS_ReadDirectory(*root, 64, entry));
    REQUIRE_FALSE(*KE_VFS_ReadDirectory(*root, UINT32_MAX, entry));
    const uint64_t mirrorFatStart = counted.fatEnd;
    counted.failWriteOffset = mirrorFatStart + 2 * 2;
    {
      const auto mountPoint = (*root)->mountPoint;
      kstd::unique_lock lock(mountPoint->fileMutex);
      REQUIRE(mountPoint->fileSystem->OperateFile(mountPoint, kernel::VFSFileOperation::CreateDirectory,
          *(*root)->vfsNode, "NEW", nullptr, {}) == KernelError::IoError);
    }
    counted.failWriteOffset = UINT64_MAX;
    uint8_t primaryEntry[2]{}, mirrorEntry[2]{};
    REQUIRE_FALSE(backing.ReadAt(counted.fatStart + 4, primaryEntry));
    REQUIRE_FALSE(backing.ReadAt(mirrorFatStart + 4, mirrorEntry));
    REQUIRE((primaryEntry[0] != mirrorEntry[0] || primaryEntry[1] != mirrorEntry[1]));
    counted.ResetCounts();
    REQUIRE_FALSE(*KE_VFS_ReadDirectory(*root, UINT32_MAX, entry));
    REQUIRE(counted.directoryReadCalls > 0);
    counted.ResetCounts();
    REQUIRE(*KE_VFS_ReadDirectory(*root, 0, entry));
    REQUIRE(kstd::string_view(entry.name) == "DATA.BIN");
    REQUIRE(counted.directoryReadCalls > 0);
    REQUIRE_FALSE(KE_VFS_CloseFile(*root));

    const auto readFaultLayout = prepare(TestFatType::Fat16, 1);
    file = KE_VFS_OpenFile("/DATA.BIN", kernel::KIOObject::OpenMode::Read);
    REQUIRE(file);
    const uint64_t faultOffset = kSectorSize * 3;
    REQUIRE(KE_VFS_SeekFile(*file, faultOffset).value() == faultOffset);
    const uint32_t linkByteOffset = readFaultLayout.fileClusters[0] * 2;
    counted.failReadOffset = counted.fatStart + (linkByteOffset / kSectorSize) * kSectorSize;
    counted.ResetCounts();
    auto failedFatRead = KE_VFS_ReadFile(*file, kstd::span<uint8_t>(&byte, 1));
    REQUIRE_FALSE(failedFatRead);
    REQUIRE(failedFatRead.error() == KernelError::DeviceError);
    REQUIRE((*file)->offset == faultOffset);
    counted.failReadOffset = UINT64_MAX;
    REQUIRE(*KE_VFS_ReadFile(*file, kstd::span<uint8_t>(&byte, 1)) == 1);
    REQUIRE(byte == readFaultLayout.expectedFile[faultOffset]);
    REQUIRE_FALSE(KE_VFS_CloseFile(*file));

    const size_t dataClusterSize = readFaultLayout.sectorsPerCluster * kSectorSize;
    file = KE_VFS_OpenFile("/DATA.BIN", kernel::KIOObject::OpenMode::Read);
    REQUIRE(file);
    counted.failReadOffset = readFaultLayout.fileOffsets.front();
    counted.ResetCounts();
    std::vector<uint8_t> initialData(dataClusterSize);
    auto failedInitialData = KE_VFS_ReadFile(*file,
        kstd::span<uint8_t>(initialData.data(), initialData.size()));
    REQUIRE_FALSE(failedInitialData);
    REQUIRE(failedInitialData.error() == KernelError::DeviceError);
    REQUIRE((*file)->offset == 0);
    counted.failReadOffset = UINT64_MAX;
    REQUIRE(*KE_VFS_ReadFile(*file, kstd::span<uint8_t>(initialData.data(), initialData.size())) == dataClusterSize);
    REQUIRE((*file)->offset == dataClusterSize);
    for (size_t i = 0; i < initialData.size(); ++i) REQUIRE(initialData[i] == readFaultLayout.expectedFile[i]);
    REQUIRE_FALSE(KE_VFS_CloseFile(*file));

    auto zeroShortFile = KE_VFS_OpenFile("/DATA.BIN", kernel::KIOObject::OpenMode::Read);
    REQUIRE(zeroShortFile);
    g_shortRead = {.enabled = true, .offset = readFaultLayout.fileOffsets.front(),
        .requestSize = dataClusterSize, .reportedBytes = 0};
    std::vector<uint8_t> zeroShortData(dataClusterSize);
    auto zeroShortRead = KE_VFS_ReadFile(*zeroShortFile,
        kstd::span<uint8_t>(zeroShortData.data(), zeroShortData.size()));
    REQUIRE_FALSE(zeroShortRead);
    REQUIRE(zeroShortRead.error() == KernelError::UnexpectedEndOfFile);
    REQUIRE((*zeroShortFile)->offset == 0);
    g_shortRead = {};
    REQUIRE(*KE_VFS_ReadFile(*zeroShortFile,
        kstd::span<uint8_t>(zeroShortData.data(), zeroShortData.size())) == dataClusterSize);
    REQUIRE((*zeroShortFile)->offset == dataClusterSize);
    for (size_t i = 0; i < zeroShortData.size(); ++i) REQUIRE(zeroShortData[i] == readFaultLayout.expectedFile[i]);
    REQUIRE_FALSE(KE_VFS_CloseFile(*zeroShortFile));

    file = KE_VFS_OpenFile("/DATA.BIN", kernel::KIOObject::OpenMode::Read);
    REQUIRE(file);
    counted.failReadOffset = readFaultLayout.fileOffsets[1];
    counted.ResetCounts();
    std::vector<uint8_t> prefixBuffer(dataClusterSize + 23);
    const auto prefix = KE_VFS_ReadFile(*file,
        kstd::span<uint8_t>(prefixBuffer.data(), prefixBuffer.size()));
    REQUIRE(prefix);
    REQUIRE(*prefix == dataClusterSize);
    REQUIRE((*file)->offset == dataClusterSize);
    auto failedDataRetry = KE_VFS_ReadFile(*file, kstd::span<uint8_t>(&byte, 1));
    REQUIRE_FALSE(failedDataRetry);
    REQUIRE(failedDataRetry.error() == KernelError::DeviceError);
    REQUIRE((*file)->offset == dataClusterSize);
    counted.failReadOffset = UINT64_MAX;
    REQUIRE(*KE_VFS_ReadFile(*file, kstd::span<uint8_t>(&byte, 1)) == 1);
    REQUIRE(byte == readFaultLayout.expectedFile[dataClusterSize]);
    REQUIRE_FALSE(KE_VFS_CloseFile(*file));

    auto shortFile = KE_VFS_OpenFile("/DATA.BIN", kernel::KIOObject::OpenMode::Read);
    REQUIRE(shortFile);
    g_shortRead = {.enabled = true, .offset = readFaultLayout.fileOffsets[0],
        .requestSize = dataClusterSize, .reportedBytes = 100};
    std::vector<uint8_t> shortBuffer(dataClusterSize);
    const auto shortFileRead = KE_VFS_ReadFile(*shortFile,
        kstd::span<uint8_t>(shortBuffer.data(), shortBuffer.size()));
    REQUIRE(shortFileRead);
    REQUIRE(*shortFileRead == 100);
    REQUIRE((*shortFile)->offset == 100);
    for (size_t i = 0; i < 100; ++i) REQUIRE(shortBuffer[i] == readFaultLayout.expectedFile[i]);
    g_shortRead = {};
    const auto shortFileRetry = KE_VFS_ReadFile(*shortFile,
        kstd::span<uint8_t>(shortBuffer.data() + 100, shortBuffer.size() - 100));
    REQUIRE(shortFileRetry);
    REQUIRE(*shortFileRetry == shortBuffer.size() - 100);
    REQUIRE((*shortFile)->offset == shortBuffer.size());
    for (size_t i = 0; i < shortBuffer.size(); ++i) REQUIRE(shortBuffer[i] == readFaultLayout.expectedFile[i]);
    REQUIRE_FALSE(KE_VFS_CloseFile(*shortFile));

    auto shortDirectory = KE_VFS_OpenFile("/DIR", kernel::KIOObject::OpenMode::Read);
    REQUIRE(shortDirectory);
    g_shortRead = {.enabled = true, .offset = readFaultLayout.directoryOffsets.front(),
        .requestSize = kSectorSize, .reportedBytes = kSectorSize - 1};
    auto shortSector = KE_VFS_ReadDirectory(*shortDirectory, 0, entry);
    REQUIRE_FALSE(shortSector);
    REQUIRE(shortSector.error() == KernelError::UnexpectedEndOfFile);
    g_shortRead = {};
    REQUIRE(*KE_VFS_ReadDirectory(*shortDirectory, 0, entry));
    REQUIRE_FALSE(KE_VFS_CloseFile(*shortDirectory));

    auto shortFatFile = KE_VFS_OpenFile("/DATA.BIN", kernel::KIOObject::OpenMode::Read);
    REQUIRE(shortFatFile);
    REQUIRE(KE_VFS_SeekFile(*shortFatFile, dataClusterSize).value() == dataClusterSize);
    const uint32_t entryOffset = readFaultLayout.fileClusters[0] * 2;
    const size_t requiredFatBytes = entryOffset % kSectorSize + 2;
    g_shortRead = {.enabled = true,
        .offset = counted.fatStart + (entryOffset / kSectorSize) * kSectorSize,
        .requestSize = 2 * kSectorSize, .reportedBytes = requiredFatBytes};
    REQUIRE(*KE_VFS_ReadFile(*shortFatFile, kstd::span<uint8_t>(&byte, 1)) == 1);
    REQUIRE(byte == readFaultLayout.expectedFile[dataClusterSize]);
    g_shortRead = {};
    REQUIRE(KE_VFS_SeekFile(*shortFatFile, dataClusterSize * 2).value() == dataClusterSize * 2);
    const uint32_t nextEntryOffset = readFaultLayout.fileClusters[1] * 2;
    const size_t nextRequiredFatBytes = nextEntryOffset % kSectorSize + 2;
    g_shortRead = {.enabled = true,
        .offset = counted.fatStart + (nextEntryOffset / kSectorSize) * kSectorSize,
        .requestSize = 2 * kSectorSize, .reportedBytes = nextRequiredFatBytes - 1};
    auto shortFatFailure = KE_VFS_ReadFile(*shortFatFile, kstd::span<uint8_t>(&byte, 1));
    REQUIRE_FALSE(shortFatFailure);
    REQUIRE(shortFatFailure.error() == KernelError::UnexpectedEndOfFile);
    REQUIRE((*shortFatFile)->offset == dataClusterSize * 2);
    g_shortRead = {};
    REQUIRE(*KE_VFS_ReadFile(*shortFatFile, kstd::span<uint8_t>(&byte, 1)) == 1);
    REQUIRE(byte == readFaultLayout.expectedFile[dataClusterSize * 2]);
    REQUIRE_FALSE(KE_VFS_CloseFile(*shortFatFile));

    const auto earlyEocLayout = prepare(TestFatType::Fat16, 1, false, true, true);
    auto earlyEocFile = KE_VFS_OpenFile("/DATA.BIN", kernel::KIOObject::OpenMode::Read);
    REQUIRE(earlyEocFile);
    REQUIRE(KE_VFS_SeekFile(*earlyEocFile, kSectorSize).value() == kSectorSize);
    auto earlyEoc = KE_VFS_ReadFile(*earlyEocFile, kstd::span<uint8_t>(&byte, 1));
    REQUIRE_FALSE(earlyEoc);
    REQUIRE(earlyEoc.error() == KernelError::UnexpectedEndOfFile);
    REQUIRE((*earlyEocFile)->offset == kSectorSize);
    REQUIRE_FALSE(KE_VFS_CloseFile(*earlyEocFile));

    prepare(TestFatType::Fat16, 1, false, true, false, true);
    auto zeroStart = KE_VFS_OpenFile("/DATA.BIN", kernel::KIOObject::OpenMode::Read);
    REQUIRE(zeroStart);
    auto zeroStartRead = KE_VFS_ReadFile(*zeroStart, kstd::span<uint8_t>(&byte, 1));
    REQUIRE_FALSE(zeroStartRead);
    REQUIRE(zeroStartRead.error() == KernelError::InvalidFormat);
    REQUIRE((*zeroStart)->offset == 0);
    REQUIRE_FALSE(KE_VFS_CloseFile(*zeroStart));

    const auto invalidDirLayout = prepare(TestFatType::Fat16, 1, false, true, false, false, true);
    auto invalidDirectory = KE_VFS_OpenFile("/DIR", kernel::KIOObject::OpenMode::Read);
    REQUIRE(invalidDirectory);
    auto invalidSector = KE_VFS_ReadDirectory(*invalidDirectory, 0, entry);
    REQUIRE_FALSE(invalidSector);
    REQUIRE(invalidSector.error() == KernelError::InvalidFormat);
    REQUIRE_FALSE(KE_VFS_CloseFile(*invalidDirectory));

    const auto cycleLayout = prepare(TestFatType::Fat12, 1, false, true, false, false, false, true);
    auto cycleDirectory = KE_VFS_OpenFile("/DIR", kernel::KIOObject::OpenMode::Read);
    REQUIRE(cycleDirectory);
    counted.directoryOffsets = cycleLayout.directoryOffsets;
    counted.ResetCounts();
    auto cycleRead = KE_VFS_ReadDirectory(*cycleDirectory, UINT32_MAX, entry);
    REQUIRE_FALSE(cycleRead);
    REQUIRE(cycleRead.error() == KernelError::InvalidFormat);
    REQUIRE(counted.fatReadCalls == cycleLayout.totalClusters - 1);
    REQUIRE(counted.directoryReadCalls == cycleLayout.totalClusters);
    counted.ResetCounts();
    cycleRead = KE_VFS_ReadDirectory(*cycleDirectory, UINT32_MAX, entry);
    REQUIRE_FALSE(cycleRead);
    REQUIRE(cycleRead.error() == KernelError::InvalidFormat);
    REQUIRE(counted.fatReadCalls == cycleLayout.totalClusters - 1);
    REQUIRE_FALSE(KE_VFS_CloseFile(*cycleDirectory));
  }

  SECTION("FAT32 allocation mirrors and FSInfo invalidation") {
    using kernel::VFSFileOperation;
    REQUIRE_FALSE(KE_VFS_Unmount("/"));
    constexpr uint32_t reserved = 32, fatSectors = 512, clusters = 65525;
    constexpr uint32_t dataSector = reserved + 2 * fatSectors;
    constexpr uint32_t sectors = dataSector + clusters;
    REQUIRE_FALSE(backing.Resize(static_cast<size_t>(sectors) * kSectorSize));
    kstd::array<uint8_t, 512> bpb{};
    PutLe16(kstd::span(bpb).subspan(11), 512);
    bpb[13] = 1;
    PutLe16(kstd::span(bpb).subspan(14), reserved);
    bpb[16] = 2;
    PutLe32(kstd::span(bpb).subspan(32), sectors);
    PutLe32(kstd::span(bpb).subspan(36), fatSectors);
    PutLe32(kstd::span(bpb).subspan(44), 2);
    PutLe16(kstd::span(bpb).subspan(48), 1);
    PutLe16(kstd::span(bpb).subspan(50), 6);
    bpb[510] = 0x55;
    bpb[511] = 0xaa;
    REQUIRE_FALSE(backing.WriteAt(0, bpb));
    kstd::array<uint8_t, 512> fat{};
    PutLe32(kstd::span(fat), 0x0ffffff8);
    PutLe32(kstd::span(fat).subspan(4), 0xffffffff);
    PutLe32(kstd::span(fat).subspan(8), 0x0fffffff);
    REQUIRE_FALSE(backing.WriteAt(reserved * kSectorSize, fat));
    REQUIRE_FALSE(backing.WriteAt((reserved + fatSectors) * kSectorSize, fat));
    kstd::array<uint8_t, 512> info{};
    PutLe32(kstd::span(info), 0x41615252);
    PutLe32(kstd::span(info).subspan(484), 0x61417272);
    PutLe32(kstd::span(info).subspan(488), clusters - 1);
    PutLe32(kstd::span(info).subspan(492), 3);
    REQUIRE_FALSE(backing.WriteAt(kSectorSize, info));
    REQUIRE_FALSE(backing.WriteAt(7 * kSectorSize, info));
    auto raw = KE_IO_CreateDeviceWithContext<diskfake::FakeDiskContext>(diskDriver, "RawFat32", kernel::device_type::Disk);
    REQUIRE(raw);
    (*raw)->deviceExtension.as<diskfake::FakeDiskContext>()->backing = &backing;
    REQUIRE_FALSE(KE_VFS_Mount(*raw, "FAT", "/", ""));
    REQUIRE_FALSE(KE_VFS_OperateFile(VFSFileOperation::CreateDirectory, "/TESTDIR", ""));
    auto directory = KE_VFS_OpenFile("/", kernel::KIOObject::OpenMode::Read);
    REQUIRE(directory);
    kernel::KVFSDirectoryEntry entry{};
    REQUIRE(*KE_VFS_ReadDirectory(*directory, 0, entry));
    REQUIRE(kstd::string_view(entry.name) == "TESTDIR");
    REQUIRE(entry.attributes & 16);
    REQUIRE_FALSE(KE_VFS_CloseFile(*directory));
    kstd::array<uint8_t, 512> mirror{};
    REQUIRE_FALSE(backing.ReadAt(reserved * kSectorSize, fat));
    REQUIRE_FALSE(backing.ReadAt((reserved + fatSectors) * kSectorSize, mirror));
    for (size_t i = 0; i < fat.size(); ++i) REQUIRE(fat[i] == mirror[i]);
    REQUIRE(fat[12] == 0xff); // cluster 3 allocated
    REQUIRE_FALSE(backing.ReadAt(kSectorSize, info));
    for (size_t i = 488; i < 496; ++i) REQUIRE(info[i] == 0xff);
    REQUIRE_FALSE(backing.ReadAt(7 * kSectorSize, info));
    for (size_t i = 488; i < 496; ++i) REQUIRE(info[i] == 0xff);
    REQUIRE_FALSE(KE_VFS_OperateFile(VFSFileOperation::Delete, "/TESTDIR", ""));
    REQUIRE_FALSE(backing.ReadAt(reserved * kSectorSize, fat));
    for (size_t i = 12; i < 16; ++i) REQUIRE(fat[i] == 0);
  }

  SECTION("Directory growth, empty files and mutation boundaries") {
    using kernel::KernelError;
    using kernel::VFSFileOperation;
    kstd::array<uint8_t, 512> root{};
    const char seed[] = "EMPTY   TXT";
    for (size_t i = 0; i < 11; ++i) root[i] = static_cast<uint8_t>(seed[i]);
    root[11] = 32;
    REQUIRE_FALSE(backing.WriteAt(3 * kSectorSize, root));
    REQUIRE_FALSE(KE_VFS_OperateFile(VFSFileOperation::CreateDirectory, "/DIR", ""));
    for (int i = 0; i < 20; ++i) {
      char name[] = "/DIR/F00.TXT";
      name[6] = static_cast<char>('0' + i / 10);
      name[7] = static_cast<char>('0' + i % 10);
      REQUIRE_FALSE(KE_VFS_OperateFile(VFSFileOperation::Copy, "/EMPTY.TXT", name));
    }
    auto directory = KE_VFS_OpenFile("/DIR", kernel::KIOObject::OpenMode::Read);
    REQUIRE(directory);
    kernel::KVFSDirectoryEntry entry{};
    for (uint32_t i = 0; i < 20; ++i) {
      REQUIRE(*KE_VFS_ReadDirectory(*directory, i, entry));
      REQUIRE(entry.size == 0);
    }
    REQUIRE_FALSE(*KE_VFS_ReadDirectory(*directory, 20, entry));
    REQUIRE_FALSE(KE_VFS_CloseFile(*directory));
    REQUIRE(KE_VFS_OperateFile(VFSFileOperation::Move, "/DIR", "/DIR/NESTED") == KernelError::Unsupported);
    for (int i = 0; i < 20; ++i) {
      char name[] = "/DIR/F00.TXT";
      name[6] = static_cast<char>('0' + i / 10);
      name[7] = static_cast<char>('0' + i % 10);
      REQUIRE_FALSE(KE_VFS_OperateFile(VFSFileOperation::Delete, name, ""));
    }
    REQUIRE_FALSE(KE_VFS_OperateFile(VFSFileOperation::Delete, "/DIR", ""));
    // The fixed FAT12 root cannot grow: source plus 15 copies fills its 16 slots.
    for (int i = 0; i < 15; ++i) {
      char name[] = "/R00.TXT";
      name[2] = static_cast<char>('0' + i / 10);
      name[3] = static_cast<char>('0' + i % 10);
      REQUIRE_FALSE(KE_VFS_OperateFile(VFSFileOperation::Copy, "/EMPTY.TXT", name));
    }
    REQUIRE(KE_VFS_OperateFile(VFSFileOperation::Copy, "/EMPTY.TXT", "/FULL.TXT") == KernelError::ResourceLimitExceeded);
    REQUIRE_FALSE(KE_VFS_OperateFile(VFSFileOperation::Delete, "/R00.TXT", ""));
    REQUIRE_FALSE(KE_VFS_OperateFile(VFSFileOperation::Copy, "/EMPTY.TXT", "/REUSED.TXT"));
    REQUIRE_FALSE(backing.ReadAt(3 * kSectorSize, root));
    root[11] |= 1;
    REQUIRE_FALSE(backing.WriteAt(3 * kSectorSize, root));
    REQUIRE(KE_VFS_OperateFile(VFSFileOperation::Delete, "/EMPTY.TXT", "") == KernelError::AccessDenied);
    // An LFN fragment before the short entry must not be left dangling by rename.
    root[11] &= static_cast<uint8_t>(~1u);
    for (size_t i = 0; i < 32; ++i) root[32 + i] = root[i];
    root[0] = 0x41;
    root[11] = 0x0f;
    REQUIRE_FALSE(backing.WriteAt(3 * kSectorSize, root));
    REQUIRE(KE_VFS_OperateFile(VFSFileOperation::Move, "/EMPTY.TXT", "/NEW.TXT") == KernelError::Unsupported);
  }

  SECTION("Device write failures are explicit and disable later mutations") {
    FailingWrites failures(backing);
    auto *context = diskDevice->deviceExtension.as<diskfake::FakeDiskContext>();
    context->backing = &failures;
    const auto result = KE_VFS_OperateFile(kernel::VFSFileOperation::CreateDirectory, "/FAIL", "");
    context->backing = &backing;
    REQUIRE(result == kernel::KernelError::IoError);
    REQUIRE(KE_VFS_OperateFile(kernel::VFSFileOperation::CreateDirectory, "/RETRY", "") == kernel::KernelError::Unsupported);
    auto directory = KE_VFS_OpenFile("/", kernel::KIOObject::OpenMode::Read);
    REQUIRE(directory);
    kernel::KVFSDirectoryEntry entry{};
    REQUIRE_FALSE(*KE_VFS_ReadDirectory(*directory, 0, entry));
    REQUIRE_FALSE(KE_VFS_CloseFile(*directory));
  }

  // Clean up the mount so later tests mounting at "/" aren't blocked by this
  // one (the singleton VFS directory persists for the whole process).
  if (auto mount = KE_OB_FindDirectChildOfType<kernel::KVolumeMountObject>(KE_OB_GetVFSDirectory(), "/")) {
    KE_OB_RemoveObject(KE_OB_GetVFSDirectory(), mount);
  }

  std::remove(imagePath.data());
}

TEST_CASE("FAT volume-ID probe validates supported VBRs and preserves I/O errors", "[fat-volume-id]") {
  EnsureObjectManagerInitialized();
  g_shortRead = {};
  g_readError = {};

  const auto imagePath = MakeTempImagePath();
  diskfake::FileBackingStore backing(imagePath);
  REQUIRE(backing.IsOpen());
  REQUIRE_FALSE(backing.Resize(static_cast<size_t>(kPartitionStartLba + kPartitionSectors) * kSectorSize));
  const auto seed = BuildFat12DiskImage();
  REQUIRE_FALSE(backing.WriteAt(0, kstd::span<const uint8_t>(seed)));

  auto diskDriver = kernel::CreateKObject<kernel::KDriverObject>("FatVolumeIdDiskDriver").value_or(nullptr);
  REQUIRE(diskDriver != nullptr);
  diskDriver->Read = ShortableDiskRead;
  diskDriver->Write = diskfake::Write;
  static unsigned fixtureNumber = 0;
  kstd::static_string<64> diskName;
  kstd::format(diskName, "FatVolumeIdDisk{}", ++fixtureNumber);
  auto diskDevice = KE_IO_CreateDeviceWithContext<diskfake::FakeDiskContext>(
      diskDriver, diskName, kernel::device_type::Disk).value_or(nullptr);
  REQUIRE(diskDevice != nullptr);
  diskDevice->deviceExtension.as<diskfake::FakeDiskContext>()->backing = &backing;

  auto volumeManager = LoadDriver(VOLMGR_DRIVER_PATH, "FatVolumeIdVolMgrDriver");
  REQUIRE(volumeManager->AddDevice.valid());
  REQUIRE(volumeManager->StartDevice.valid());
  REQUIRE_FALSE(volumeManager->AddDevice(volumeManager, diskDevice));
  g_lastReportedPdo = nullptr;
  volumeManager->StartDevice(diskDevice);
  const auto volume = g_lastReportedPdo;
  REQUIRE(volume != nullptr);
  REQUIRE(volume->deviceType == kernel::device_type::Volume);

  auto filesystem = KE_OB_FindDirectChildOfType<kernel::KFilesystemObject>(
      KE_OB_GetFilesystemDirectory(), "FAT");
  if (!filesystem || !filesystem->ProbeVolumeId) {
    LoadDriver(FS_FAT_DRIVER_PATH, "FatVolumeIdFilesystemDriver");
    filesystem = KE_OB_FindDirectChildOfType<kernel::KFilesystemObject>(
        KE_OB_GetFilesystemDirectory(), "FAT");
  }
  REQUIRE(filesystem != nullptr);
  REQUIRE(filesystem->ProbeVolumeId);

  const uint64_t partitionBase = static_cast<uint64_t>(kPartitionStartLba) * kSectorSize;
  for (const auto type: {TestFatType::Fat12, TestFatType::Fat16, TestFatType::Fat32}) {
    SeedFatImage(backing, type, 1);
    uint32_t serial = 0;
    const auto result = filesystem->ProbeVolumeId(volume, serial);
    REQUIRE(result);
    REQUIRE(*result);
    REQUIRE(serial == 0x12345678);
  }

  SECTION("Non-candidates include invalid signatures, missing serials, and ExFAT") {
    SeedFatImage(backing, TestFatType::Fat16, 1);
    kstd::array<uint8_t, kSectorSize> boot{};
    REQUIRE_FALSE(backing.ReadAt(partitionBase, kstd::span<uint8_t>(boot)));
    boot[38] = 0;
    REQUIRE_FALSE(backing.WriteAt(partitionBase, kstd::span<const uint8_t>(boot)));
    uint32_t serial = 0;
    auto result = filesystem->ProbeVolumeId(volume, serial);
    REQUIRE(result);
    REQUIRE_FALSE(*result);

    SeedFatImage(backing, TestFatType::Fat12, 1);
    REQUIRE_FALSE(backing.ReadAt(partitionBase, kstd::span<uint8_t>(boot)));
    boot[510] = 0;
    REQUIRE_FALSE(backing.WriteAt(partitionBase, kstd::span<const uint8_t>(boot)));
    result = filesystem->ProbeVolumeId(volume, serial);
    REQUIRE(result);
    REQUIRE_FALSE(*result);

    for (auto &byte: boot) byte = 0;
    for (size_t i = 0; i < 8; ++i) boot[3 + i] = static_cast<uint8_t>("EXFAT   "[i]);
    boot[108] = 9;
    boot[109] = 0;
    boot[510] = 0x55;
    boot[511] = 0xaa;
    REQUIRE_FALSE(backing.WriteAt(partitionBase, kstd::span<const uint8_t>(boot)));
    result = filesystem->ProbeVolumeId(volume, serial);
    REQUIRE(result);
    REQUIRE_FALSE(*result);
  }

  SECTION("Invalid BPB layouts are non-candidates") {
    auto rejectsBpbValue = [&](TestFatType type, size_t offset, size_t width, uint32_t value) {
      SeedFatImage(backing, type, 1);
      kstd::array<uint8_t, kSectorSize> boot{};
      REQUIRE_FALSE(backing.ReadAt(partitionBase, kstd::span<uint8_t>(boot)));
      if (width == 1) {
        boot[offset] = static_cast<uint8_t>(value);
      } else if (width == 2) {
        PutLe16(kstd::span<uint8_t>(boot).subspan(offset), static_cast<uint16_t>(value));
      } else {
        PutLe32(kstd::span<uint8_t>(boot).subspan(offset), value);
      }
      REQUIRE_FALSE(backing.WriteAt(partitionBase, kstd::span<const uint8_t>(boot)));
      uint32_t serial = 0;
      const auto result = filesystem->ProbeVolumeId(volume, serial);
      INFO("BPB offset " << offset << ", width " << width << ", value " << value);
      REQUIRE(result);
      REQUIRE_FALSE(*result);
    };

    rejectsBpbValue(TestFatType::Fat12, 11, 2, 256); // sector size below minimum
    rejectsBpbValue(TestFatType::Fat12, 11, 2, 1000); // sector size is not a power of two
    rejectsBpbValue(TestFatType::Fat12, 11, 2, 8192); // sector size above maximum
    rejectsBpbValue(TestFatType::Fat12, 13, 1, 0); // sectors per cluster is zero
    rejectsBpbValue(TestFatType::Fat12, 13, 1, 3); // sectors per cluster is not a power of two
    rejectsBpbValue(TestFatType::Fat12, 13, 1, 192); // sectors per cluster exceeds limit
    rejectsBpbValue(TestFatType::Fat12, 14, 2, 0); // reserved sector count is zero
    rejectsBpbValue(TestFatType::Fat12, 14, 2, UINT16_MAX); // reserved and FAT sectors exceed volume
    rejectsBpbValue(TestFatType::Fat12, 16, 1, 0); // FAT count is zero
    rejectsBpbValue(TestFatType::Fat12, 22, 2, 0); // FAT size is zero
    rejectsBpbValue(TestFatType::Fat12, 19, 2, 0); // total sector count is zero
    rejectsBpbValue(TestFatType::Fat12, 17, 2, UINT16_MAX); // root metadata exceeds volume
    rejectsBpbValue(TestFatType::Fat16, 22, 2, 1); // FAT cannot hold classified clusters
    rejectsBpbValue(TestFatType::Fat32, 36, 4, 0); // FAT32 size is zero
    rejectsBpbValue(TestFatType::Fat32, 44, 4, 1); // FAT32 root cluster is invalid
  }

  SECTION("Short reads and device errors remain probe errors") {
    SeedFatImage(backing, TestFatType::Fat12, 1);
    const uint64_t absoluteBootOffset = partitionBase;
    g_shortRead = {.enabled = true, .offset = absoluteBootOffset,
        .requestSize = kSectorSize, .reportedBytes = kSectorSize - 1};
    uint32_t serial = 0;
    const auto shortRead = filesystem->ProbeVolumeId(volume, serial);
    g_shortRead = {};
    REQUIRE_FALSE(shortRead);
    REQUIRE(shortRead.error() == kernel::KernelError::UnexpectedEndOfFile);

    g_readError = {.enabled = true, .offset = absoluteBootOffset};
    const auto deviceError = filesystem->ProbeVolumeId(volume, serial);
    g_readError = {};
    REQUIRE_FALSE(deviceError);
    REQUIRE(deviceError.error() == kernel::KernelError::DeviceError);
  }

  std::remove(imagePath.data());
}
