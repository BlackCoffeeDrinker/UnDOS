#include <catch2/catch_test_macros.hpp>

#include <kernel/hal/x86/bootdev.hpp>

#include "initial/mini_fat16.hpp"

#include <array>
#include <cstring>

namespace {
constexpr uint32_t kSectorSize = 512;
constexpr uint32_t kPartitionStart = 16;
std::array<uint8_t, 64 * kSectorSize> g_disk{};
uint32_t g_partitionStart = kPartitionStart;
uint32_t g_partitionSectors = 4;
size_t g_readCalls = 0;
bool g_outsideRead = false;

void put16(uint8_t *p, uint16_t value) {
  p[0] = static_cast<uint8_t>(value);
  p[1] = static_cast<uint8_t>(value >> 8);
}

void put32(uint8_t *p, uint32_t value) {
  p[0] = static_cast<uint8_t>(value);
  p[1] = static_cast<uint8_t>(value >> 8);
  p[2] = static_cast<uint8_t>(value >> 16);
  p[3] = static_cast<uint8_t>(value >> 24);
}

void set_root_entry(size_t index, const std::array<uint8_t, 11> &name) {
  auto *entry = g_disk.data() + 3 * kSectorSize + index * 32;
  std::memcpy(entry, name.data(), name.size());
  entry[11] = 0x20;
  put16(entry + 26, 2);
  put32(entry + 28, 4);
}

std::array<uint8_t, 11> short_name(const char *base, const char *extension) {
  std::array<uint8_t, 11> name{};
  name.fill(' ');
  for (size_t i = 0; i < 8 && base[i]; ++i) name[i] = static_cast<uint8_t>(base[i]);
  for (size_t i = 0; i < 3 && extension[i]; ++i) name[8 + i] = static_cast<uint8_t>(extension[i]);
  return name;
}

void prepare_image(uint32_t partitionSectors = 4, uint8_t sectorsPerCluster = 1,
                   uint16_t fileCluster = 2, uint32_t partitionStart = kPartitionStart) {
  g_disk.fill(0);
  g_partitionStart = partitionStart;
  g_partitionSectors = partitionSectors;
  g_readCalls = 0;
  g_outsideRead = false;

  auto *mbr = g_disk.data();
  mbr[510] = 0x55;
  mbr[511] = 0xAA;
  mbr[446 + 4] = 0x06;
  put32(mbr + 446 + 8, partitionStart);
  put32(mbr + 446 + 12, partitionSectors);

  auto *bpb = g_disk.data() + kSectorSize;
  put16(bpb + 11, kSectorSize);
  bpb[13] = sectorsPerCluster;
  put16(bpb + 14, 1);
  bpb[16] = 1;
  put16(bpb + 17, 16);
  put16(bpb + 19, static_cast<uint16_t>(partitionSectors));
  bpb[21] = 0xF8;
  put16(bpb + 22, 1);
  bpb[510] = 0x55;
  bpb[511] = 0xAA;

  auto *root = g_disk.data() + 3 * kSectorSize;
  constexpr char diskName[11] = {'M','I','X','E','D',' ',' ',' ','T','X','T'};
  for (size_t i = 0; i < sizeof(diskName); ++i) root[i] = static_cast<uint8_t>(diskName[i]);
  root[11] = 0x20;
  put16(root + 26, fileCluster);
  put32(root + 28, 4);

  auto *data = g_disk.data() + 4 * kSectorSize;
  data[0] = 'd'; data[1] = 'a'; data[2] = 't'; data[3] = 'a';
}
}

UNDOS_HAL_API_DEF bool HAL_BOOTDEV_IsPresent() noexcept { return true; }

UNDOS_HAL_API_DEF bool HAL_BOOTDEV_ReadSectors(uint32_t lba, uint32_t count,
                                               kstd::span<uint8_t> buffer) noexcept {
  ++g_readCalls;
  if (lba >= g_partitionStart &&
      (static_cast<uint64_t>(lba) + count > static_cast<uint64_t>(g_partitionStart) + g_partitionSectors)) {
    g_outsideRead = true;
  }
  if (buffer.size() != static_cast<size_t>(count) * kSectorSize) return false;
  if (lba == 0) {
    if (count != 1) return false;
    __builtin_memcpy(buffer.data(), g_disk.data(), buffer.size());
    return true;
  }
  if (lba < g_partitionStart) return false;
  const uint64_t relative = static_cast<uint64_t>(lba) - g_partitionStart;
  if (relative + count > g_partitionSectors || relative + count + 1 > g_disk.size() / kSectorSize) return false;
  __builtin_memcpy(buffer.data(), g_disk.data() + (relative + 1) * kSectorSize, buffer.size());
  return true;
}

TEST_CASE("Mini FAT16 accepts reads from the final partition sector and uppercases both name parts", "[fat][mini-fat16]") {
  prepare_image();
  kstd::vector<uint8_t> contents;
  REQUIRE_FALSE(kernel::initial::minifat::ReadFile("mIxEd.tXt", contents));
  REQUIRE(contents.size() == 4);
  REQUIRE(contents[0] == 'd');
  REQUIRE(contents[1] == 'a');
  REQUIRE(contents[2] == 't');
  REQUIRE(contents[3] == 'a');
  REQUIRE_FALSE(g_outsideRead);
  REQUIRE(g_readCalls == 4);
}

TEST_CASE("Mini FAT16 preserves final-dot, 8.3 truncation, padding, and non-ASCII name bytes", "[fat][mini-fat16]") {
  prepare_image();
  const uint8_t highByteName[11] = {0xe1, ' ', ' ', ' ', ' ', ' ', ' ', ' ', 'T', ' ', ' '};
  set_root_entry(0, short_name("ALPHA.BE", "TXT"));
  set_root_entry(1, short_name("LONGFILE", "EXT"));
  set_root_entry(2, short_name("A", "B"));
  std::array<uint8_t, 11> nonAscii{};
  std::memcpy(nonAscii.data(), highByteName, sizeof(highByteName));
  set_root_entry(3, nonAscii);

  const auto expectFile = [](kstd::string_view name) {
    kstd::vector<uint8_t> contents;
    REQUIRE_FALSE(kernel::initial::minifat::ReadFile(name, contents));
    REQUIRE(contents.size() == 4);
    REQUIRE(contents[0] == 'd');
    REQUIRE(contents[1] == 'a');
    REQUIRE(contents[2] == 't');
    REQUIRE(contents[3] == 'a');
  };
  expectFile("alpha.beta.txt");
  expectFile("LongFilename.extension");
  expectFile("a.b");
  const char nonAsciiPath[] = {static_cast<char>(0xe1), '.', 't', '\0'};
  expectFile(kstd::string_view(nonAsciiPath, 3));
}

TEST_CASE("Mini FAT16 rejects an out-of-range or partition-crossing cluster read before HAL I/O", "[fat][mini-fat16]") {
  SECTION("first sector beyond the partition") {
    prepare_image(4, 1, 3);
    kstd::vector<uint8_t> contents;
    REQUIRE(kernel::initial::minifat::ReadFile("mixed.txt", contents) == kernel::KernelError::OutOfRange);
    REQUIRE_FALSE(g_outsideRead);
    REQUIRE(g_readCalls == 3);
  }

  SECTION("cluster read crossing the partition end") {
    prepare_image(4, 2, 2);
    kstd::vector<uint8_t> contents;
    REQUIRE(kernel::initial::minifat::ReadFile("mixed.txt", contents) == kernel::KernelError::OutOfRange);
    REQUIRE_FALSE(g_outsideRead);
    REQUIRE(g_readCalls == 3);
  }
}

TEST_CASE("Mini FAT16 rejects absolute LBA overflow without issuing the overflowing read", "[fat][mini-fat16]") {
  prepare_image(4, 1, 2, UINT32_MAX - 1);
  kstd::vector<uint8_t> contents;
  REQUIRE(kernel::initial::minifat::ReadFile("mixed.txt", contents) == kernel::KernelError::ArithmeticOverflow);
  REQUIRE_FALSE(g_outsideRead);
  REQUIRE(g_readCalls == 2);
}
