
#include "../../drivers/fs_fat/fat_short_name.hpp"
#include "static_string.hpp"
#include "string_view.hpp"

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("FAT_ToShortName basic conversion") {
  kstd::static_string<12> shortName;
  shortName.append("           ");// 11 spaces, will be overwritten below

  REQUIRE(FAT_ToShortName("readme.txt", kstd::string_view(shortName.data(), shortName.size())));
  REQUIRE(shortName == "README  TXT");
}

TEST_CASE("FAT_ToShortName uppercases mixed case names") {
  kstd::static_string<12> shortName;
  shortName.append("           ");

  REQUIRE(FAT_ToShortName("Kernel.Bin", kstd::string_view(shortName.data(), shortName.size())));
  REQUIRE(shortName == "KERNEL  BIN");
}

TEST_CASE("FAT_ToShortName pads short base and extension with spaces") {
  kstd::static_string<12> shortName;
  shortName.append("           ");

  REQUIRE(FAT_ToShortName("a.c", kstd::string_view(shortName.data(), shortName.size())));
  REQUIRE(shortName == "A       C  ");
}

TEST_CASE("FAT_ToShortName without extension") {
  kstd::static_string<12> shortName;
  shortName.append("           ");

  REQUIRE(FAT_ToShortName("noext", kstd::string_view(shortName.data(), shortName.size())));
  REQUIRE(shortName == "NOEXT      ");
}

TEST_CASE("FAT_ToShortName with maximum-length base and extension") {
  kstd::static_string<12> shortName;
  shortName.append("           ");

  REQUIRE(FAT_ToShortName("longname.ext", kstd::string_view(shortName.data(), shortName.size())));
  REQUIRE(shortName == "LONGNAMEEXT");
}

TEST_CASE("FAT_ToShortName rejects base name longer than 8 characters") {
  kstd::static_string<12> shortName;
  shortName.append("           ");

  REQUIRE_FALSE(FAT_ToShortName("toolongname.txt", kstd::string_view(shortName.data(), shortName.size())));
}

TEST_CASE("FAT_ToShortName rejects extension longer than 3 characters") {
  kstd::static_string<12> shortName;
  shortName.append("           ");

  REQUIRE_FALSE(FAT_ToShortName("file.longext", kstd::string_view(shortName.data(), shortName.size())));
}

TEST_CASE("FAT_ToShortName treats a trailing dot as an empty extension") {
  kstd::static_string<12> shortName;
  shortName.append("           ");

  REQUIRE(FAT_ToShortName("name.", kstd::string_view(shortName.data(), shortName.size())));
  REQUIRE(shortName == "NAME       ");
}

TEST_CASE("FAT_ToShortName uses the last dot to split base and extension") {
  kstd::static_string<12> shortName;
  shortName.append("           ");

  REQUIRE(FAT_ToShortName("a.b.c", kstd::string_view(shortName.data(), shortName.size())));
  REQUIRE(shortName == "A.B     C  ");
}

// Reproduces a scenario found while looking up files in a FAT directory: a raw on-disk directory
// entry name is a fixed 11-byte, space-padded field with NO NUL terminator. Whatever follows it
// in the struct (e.g. the attribute byte) is *not* guaranteed to be zero. `kstd::string_view` now
// has a dedicated `char[N]` constructor/comparison overloads that use the array's actual bound
// (11) instead of scanning for a NUL terminator, so both an implicit conversion of the raw
// `char[11]` field and an explicit fixed-length view correctly compare only the 11 name bytes,
// without reading past the field.
TEST_CASE("Raw 11-byte FAT directory entry name compares correctly without an explicit length") {
  // Simulates `FAT_DirEntry::name` immediately followed by a non-zero attribute byte, just
  // like the real on-disk struct: no NUL terminator anywhere within the first several bytes.
  struct RawEntry {
    char name[11];
    uint8_t attr;
  };

  RawEntry entry{};
  kstd::static_string<12> shortName;
  shortName.append("           ");
  REQUIRE(FAT_ToShortName("SYSTEM", kstd::string_view(shortName.data(), shortName.size())));

  for (size_t i = 0; i < 11; i++) entry.name[i] = shortName[i];
  entry.attr = 0x20;// non-zero, non-space byte right after the name field

  // The array-bound constructor/comparison correctly uses the 11-byte bound of `entry.name`,
  // so this now compares equal even without an explicit length.
  REQUIRE(kstd::string_view(entry.name) == shortName);
  REQUIRE(entry.name == shortName);

  // An explicit fixed-length view still works too, and matches.
  REQUIRE(kstd::string_view(entry.name, sizeof(entry.name)) == shortName);
}
