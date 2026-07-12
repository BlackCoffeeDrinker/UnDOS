#include <catch2/catch_test_macros.hpp>

#include "../../drivers/volmgr/volmgr_partition.hpp"

#include <array.hpp>
#include <span.hpp>

using namespace volmgr;

namespace {

using Sector = kstd::array<uint8_t, 512>;

void PutLe32(kstd::span<uint8_t> p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
  p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
  p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

void PutLe64(kstd::span<uint8_t> p, uint64_t v) {
  PutLe32(p, static_cast<uint32_t>(v & 0xFFFFFFFF));
  PutLe32(p.subspan(4), static_cast<uint32_t>((v >> 32) & 0xFFFFFFFF));
}

void SetMbrEntry(kstd::span<uint8_t> sector0, size_t index, uint8_t type, uint32_t startLba, uint32_t sectors) {
  const kstd::span<uint8_t> e = sector0.subspan(446 + index * 16);
  e[4] = type;
  PutLe32(e.subspan(8), startLba);
  PutLe32(e.subspan(12), sectors);
}

void SetMbrSignature(kstd::span<uint8_t> sector0) {
  sector0[510] = 0x55;
  sector0[511] = 0xAA;
}

void SetGptHeader(kstd::span<uint8_t> lba1, uint64_t entriesLba, uint32_t numEntries, uint32_t entrySize) {
  const char sig[8] = {'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T'};
  for (size_t i = 0; i < 8; i++) lba1[i] = static_cast<uint8_t>(sig[i]);
  PutLe64(lba1.subspan(72), entriesLba);
  PutLe32(lba1.subspan(80), numEntries);
  PutLe32(lba1.subspan(84), entrySize);
}

void SetGptEntry(kstd::span<uint8_t> entries, size_t index, uint32_t entrySize, bool used, uint64_t firstLba, uint64_t lastLba) {
  const kstd::span<uint8_t> e = entries.subspan(index * entrySize, entrySize);
  if (used) e[0] = 0x01;// non-zero type GUID marks the entry as used
  PutLe64(e.subspan(32), firstLba);
  PutLe64(e.subspan(40), lastLba);
}

ByteView View(const Sector &s) { return ByteView(s.data(), s.size()); }

}// namespace

TEST_CASE("MBR detection and parsing", "[volmgr][mbr]") {
  Sector sector0{};

  SECTION("No signature yields None and no partitions") {
    kstd::array<PartitionInfo, 4> parts{};
    REQUIRE(DetectScheme(View(sector0), ByteView{}) == PartitionScheme::None);
    REQUIRE(ParseMbr(View(sector0), kstd::span<PartitionInfo>(parts.data(), parts.size())) == 0);
  }

  SECTION("Two valid partitions are parsed with correct offsets") {
    SetMbrSignature(sector0);
    SetMbrEntry(sector0, 0, 0x83, 2048, 20480);// 1 MiB in, 10 MiB long
    SetMbrEntry(sector0, 1, 0x0C, 24576, 4096);

    REQUIRE(DetectScheme(View(sector0), ByteView{}) == PartitionScheme::Mbr);

    kstd::array<PartitionInfo, 4> parts{};
    const size_t count = ParseMbr(View(sector0), kstd::span<PartitionInfo>(parts.data(), parts.size()));
    REQUIRE(count == 2);
    REQUIRE(parts[0].baseOffsetBytes == 2048ull * 512);
    REQUIRE(parts[0].lengthBytes == 20480ull * 512);
    REQUIRE(parts[1].baseOffsetBytes == 24576ull * 512);
    REQUIRE(parts[1].lengthBytes == 4096ull * 512);
  }

  SECTION("Empty and zero-length entries are skipped") {
    SetMbrSignature(sector0);
    SetMbrEntry(sector0, 0, 0x00, 100, 200);// empty type
    SetMbrEntry(sector0, 1, 0x83, 100, 0);  // zero length
    SetMbrEntry(sector0, 2, 0x83, 8192, 1000);

    kstd::array<PartitionInfo, 4> parts{};
    const size_t count = ParseMbr(View(sector0), kstd::span<PartitionInfo>(parts.data(), parts.size()));
    REQUIRE(count == 1);
    REQUIRE(parts[0].baseOffsetBytes == 8192ull * 512);
    REQUIRE(parts[0].lengthBytes == 1000ull * 512);
  }
}

TEST_CASE("GPT detection and parsing", "[volmgr][gpt]") {
  Sector sector0{};
  Sector lba1{};

  SECTION("Protective MBR + GPT header is detected as GPT") {
    SetMbrSignature(sector0);
    SetMbrEntry(sector0, 0, 0xEE, 1, 0xFFFFFFFF);// protective entry
    SetGptHeader(lba1, 2, 128, 128);
    REQUIRE(DetectScheme(View(sector0), View(lba1)) == PartitionScheme::Gpt);
  }

  SECTION("Protective MBR without readable header still reports GPT") {
    SetMbrSignature(sector0);
    SetMbrEntry(sector0, 0, 0xEE, 1, 0xFFFFFFFF);
    REQUIRE(DetectScheme(View(sector0), ByteView{}) == PartitionScheme::Gpt);
  }

  SECTION("GPT entries are parsed with correct ranges") {
    constexpr uint32_t entrySize = 128;
    SetGptHeader(lba1, 2, 4, entrySize);

    kstd::array<uint8_t, 4 * entrySize> entries{};
    const kstd::span<uint8_t> entriesView(entries.data(), entries.size());
    SetGptEntry(entriesView, 0, entrySize, true, 34, 2081);  // 2048 sectors
    SetGptEntry(entriesView, 1, entrySize, false, 0, 0);     // unused
    SetGptEntry(entriesView, 2, entrySize, true, 4096, 8191);// 4096 sectors

    kstd::array<PartitionInfo, 4> parts{};
    const size_t count = ParseGptEntries(View(lba1), ByteView(entries.data(), entries.size()), kstd::span<PartitionInfo>(parts.data(), parts.size()));
    REQUIRE(count == 2);
    REQUIRE(parts[0].baseOffsetBytes == 34ull * 512);
    REQUIRE(parts[0].lengthBytes == (2081ull - 34 + 1) * 512);
    REQUIRE(parts[1].baseOffsetBytes == 4096ull * 512);
    REQUIRE(parts[1].lengthBytes == (8191ull - 4096 + 1) * 512);
  }
}

TEST_CASE("Volume offset translation and range checking", "[volmgr][offset]") {
  const uint64_t base = 2048ull * 512;
  const uint64_t length = 4096ull * 512;

  SECTION("In-range access adds the base offset") {
    uint64_t abs = 0;
    REQUIRE(ComputeAbsoluteOffset(base, length, 512, 1024, abs));
    REQUIRE(abs == base + 512);
  }

  SECTION("Access exactly at the end is allowed") {
    uint64_t abs = 0;
    REQUIRE(ComputeAbsoluteOffset(base, length, length - 512, 512, abs));
    REQUIRE(abs == base + length - 512);
  }

  SECTION("Offset beyond the partition fails") {
    uint64_t abs = 12345;
    REQUIRE_FALSE(ComputeAbsoluteOffset(base, length, length + 1, 1, abs));
    REQUIRE(abs == 12345);// untouched
  }

  SECTION("Length overrunning the partition fails") {
    uint64_t abs = 0;
    REQUIRE_FALSE(ComputeAbsoluteOffset(base, length, length - 100, 200, abs));
  }
}
