
#include "../../../Kernel/Drivers/fs_fat/fat_long_name.hpp"
#include "static_string.hpp"
#include "string_view.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr char kShort[11] = {'L', 'O', 'N', 'G', 'F', 'I', '~', '1', 'T', 'X', 'T'};

// Builds the LFN entries for `name` in on-disk order (last fragment first).
std::vector<FAT_LfnEntry> MakeChain(const std::string &name, uint8_t checksum) {
  const size_t count = (name.size() + kLfnCharsPerEntry - 1) / kLfnCharsPerEntry;
  std::vector<FAT_LfnEntry> chain;
  for (size_t n = count; n >= 1; --n) {
    uint16_t chars[13];
    for (size_t i = 0; i < 13; i++) {
      const size_t pos = (n - 1) * 13 + i;
      chars[i] = pos < name.size() ? static_cast<uint16_t>(static_cast<uint8_t>(name[pos]))
                                   : (pos == name.size() ? 0x0000 : 0xFFFF);
    }
    FAT_LfnEntry e{};
    e.ord = static_cast<uint8_t>(n | (n == count ? kLfnLastEntry : 0));
    e.attr = 0x0F;
    e.checksum = checksum;
    for (size_t i = 0; i < 5; i++) e.name1[i] = chars[i];
    for (size_t i = 0; i < 6; i++) e.name2[i] = chars[5 + i];
    for (size_t i = 0; i < 2; i++) e.name3[i] = chars[11 + i];
    chain.push_back(e);
  }
  return chain;
}

bool Decode(const std::vector<FAT_LfnEntry> &chain, kstd::static_string<256> &out) {
  FAT_LfnAccumulator acc;
  for (const auto &e : chain) acc.Feed(e);
  return acc.Complete(kShort, out);
}

}// namespace

TEST_CASE("FAT_ShortNameChecksum matches reference vectors") {
  const char readme[11] = {'R', 'E', 'A', 'D', 'M', 'E', ' ', ' ', 'T', 'X', 'T'};
  REQUIRE(FAT_ShortNameChecksum(readme) == 115);
  REQUIRE(FAT_ShortNameChecksum(kShort) == 212);
}

TEST_CASE("FAT LFN single-entry chain with NUL and 0xFFFF padding") {
  kstd::static_string<256> out;
  REQUIRE(Decode(MakeChain("short.txt", 212), out));
  REQUIRE(out == "short.txt");
}

TEST_CASE("FAT LFN exact 13-char entry without terminator") {
  kstd::static_string<256> out;
  REQUIRE(Decode(MakeChain("abcdefghij.tx", 212), out));
  REQUIRE(out == "abcdefghij.tx");
}

TEST_CASE("FAT LFN multi-entry chain") {
  kstd::static_string<256> out;
  REQUIRE(Decode(MakeChain("A Rather Long File Name.text", 212), out));
  REQUIRE(out == "A Rather Long File Name.text");
}

TEST_CASE("FAT LFN 255-character name") {
  const std::string name(255, 'x');
  kstd::static_string<256> out;
  REQUIRE(Decode(MakeChain(name, 212), out));
  REQUIRE(out.size() == 255);
}

TEST_CASE("FAT LFN wrong checksum is rejected") {
  kstd::static_string<256> out;
  REQUIRE_FALSE(Decode(MakeChain("short.txt", 99), out));
}

TEST_CASE("FAT LFN missing sequence number is rejected") {
  auto chain = MakeChain("A Rather Long File Name.text", 212);
  chain.erase(chain.begin() + 1);
  kstd::static_string<256> out;
  REQUIRE_FALSE(Decode(chain, out));
}

TEST_CASE("FAT LFN orphan fragments are discarded after reset") {
  FAT_LfnAccumulator acc;
  for (const auto &e : MakeChain("orphan name here", 212)) acc.Feed(e);
  acc.Reset();// deleted entry encountered
  kstd::static_string<256> out;
  REQUIRE_FALSE(acc.Complete(kShort, out));
}

TEST_CASE("FAT LFN chain without short entry completion does not leak into next") {
  FAT_LfnAccumulator acc;
  auto chain = MakeChain("A Rather Long File Name.text", 212);
  acc.Feed(chain[0]);// stray start, then a fresh chain
  for (const auto &e : MakeChain("next.txt", 212)) acc.Feed(e);
  kstd::static_string<256> out;
  REQUIRE(acc.Complete(kShort, out));
  REQUIRE(out == "next.txt");
}

TEST_CASE("FAT LFN state persists across feeds split at a sector boundary") {
  FAT_LfnAccumulator acc;
  const auto chain = MakeChain("A Rather Long File Name.text", 212);
  acc.Feed(chain[0]);
  // ... sector read happens here; accumulator keeps state ...
  for (size_t i = 1; i < chain.size(); i++) acc.Feed(chain[i]);
  kstd::static_string<256> out;
  REQUIRE(acc.Complete(kShort, out));
  REQUIRE(out == "A Rather Long File Name.text");
  REQUIRE_FALSE(acc.Complete(kShort, out));// consumed
}

TEST_CASE("FAT LFN non-ASCII characters map to '?'") {
  auto chain = MakeChain("caf_.txt", 212);
  chain[0].name1[3] = 0x00E9;
  kstd::static_string<256> out;
  REQUIRE(Decode(chain, out));
  REQUIRE(out == "caf?.txt");
  REQUIRE_FALSE(FAT_LongNameEquals(kstd::string_view(out.data(), out.size()), "caf?.txt"));
}

TEST_CASE("FAT long names with unmappable characters are not listable") {
  REQUIRE(FAT_LongNameListable("A Rather Long File Name.text"));
  REQUIRE_FALSE(FAT_LongNameListable("caf?.txt"));
  REQUIRE_FALSE(FAT_LongNameListable(""));
}

TEST_CASE("FAT_LongNameEquals is ASCII case-insensitive") {
  REQUIRE(FAT_LongNameEquals("My Document.TXT", "my document.txt"));
  REQUIRE_FALSE(FAT_LongNameEquals("abc", "abd"));
  REQUIRE_FALSE(FAT_LongNameEquals("abc", "abcd"));
}
