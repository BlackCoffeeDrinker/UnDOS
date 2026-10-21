
#pragma once

#include <kernel/kerror.hpp>
#include <kernel/kobject/KConfigurationRoot.hpp>

#include <array.hpp>
#include <list.hpp>
#include <span.hpp>
#include <static_string.hpp>
#include <string_view.hpp>

/*
+-----------------------------------------------------------------------+
| HEADER (32 Bytes, Aligned)                                            |
|  - Magic: "CFG1" (4 Bytes)                                            |
|  - Version: uint16_t (2 Bytes)                                        |
|  - Index Entry Count: uint16_t (2 Bytes)                              |
|  - Name Entry Count: uint16_t (2 Bytes)                               |
|  - Reserved / Alignment Padding (6 Bytes)                             |
|  - Offset to First Index Block: uint32_t (4 Bytes, Absolute)          |
|  - Offset to First Name Block: uint32_t (4 Bytes, Absolute)           |
|  - Offset to Data Region: uint32_t (4 Bytes, Absolute)                |
|  - Reserved Field: uint32_t (4 Bytes)                                 |
+-----------------------------------------------------------------------+
| INDEX BLOCK (4096 Bytes Page-Aligned)                                 |
|  - Next Block Offset: uint32_t (4 Bytes, Absolute, 0 = End)           |
|  - Reserved: uint32_t (4 Bytes)                                       |
|  - Entries: Array of 170 x IndexEntry (24 Bytes Each = 4080 Bytes)    |
|    - Path Hash: uint64_t (8 Bytes)                                    |
|    - Name entry index: uint16_t (2 Bytes)                             |
|    - Value Type: uint8_t (1 Byte)                                     |
|    - Reserved/Padding: uint8_t (1 Byte)                               |
|    - Data Offset: uint32_t (4 Bytes, Absolute)                        |
|    - Data Length: uint32_t (4 Bytes)                                  |
+-----------------------------------------------------------------------+
| NAME BLOCK (Page-Aligned)                                             |
|  - Next Block Offset: uint32_t (4 Bytes, Absolute)                    |
|  - Entries: Array of 56 NameEntry (72 bytes each = 4032)              |
|    - Path Hash: uint64_t (8 Bytes)                                    |
|    - Name: char[64]                                                   |
+-----------------------------------------------------------------------+
| DATA REGION                                                           |
|  - Contiguous raw binary blobs and primitive configuration payloads   |
+-----------------------------------------------------------------------+
| INDEX BLOCK (4096 Bytes Page-Aligned)                                 |
|  ...                                                                  |
+-----------------------------------------------------------------------+
| NAME BLOCK (Page-Aligned)                                             |
|  ...                                                                  |
+-----------------------------------------------------------------------+
|  ...                                                                  |
+-----------------------------------------------------------------------+
*/

// Raw, byte-level on-disk I/O for the configuration manager's hive file
// format: the structures above and the primitives to read/write them
// (ReadRawAt/WriteRawAt and everything built on them). configuration_manager.cpp
// only ever talks to a hive's backing file through this module - it never
// touches file offsets or on-disk structures directly - so the exact same
// (well-tested) code path used by the kernel is available to tests/tooling
// that need to build a valid hive file (e.g. an empty one) without
// duplicating the on-disk format elsewhere.
namespace kernel::configuration_manager_io {

constexpr kstd::array<uint8_t, 4> MAGIC = {'C', 'F', 'G', '1'};

// Sentinel `nameEntryIndex` meaning "this entry has no associated NAME
// BLOCK entry" - either because a name slot couldn't be allocated for it
// (its paired NAME BLOCK was full) or, for entries predating name support,
// because none was ever persisted.
constexpr uint16_t NO_NAME_ENTRY = 0xFFFF;

struct alignas(8) ConfigurationFileHeader {
  kstd::array<uint8_t, 4> magic{MAGIC};
  uint16_t version{1};
  uint16_t indexEntryCount{0};
  uint16_t nameEntryCount{0};
  uint8_t reserved0[6]{};
  uint32_t offsetToFirstIndexBlock{0};
  uint32_t offsetToFirstNameBlock{0};
  uint32_t offsetToDataRegion{0};
  uint32_t reserved1{0};
};
static_assert(sizeof(ConfigurationFileHeader) == 32, "ConfigurationFileHeader must match the on-disk header size");

// On-disk layout of a single INDEX BLOCK entry (32 bytes, aligned 8).
// Only the index is ever kept in memory (one Root entry per key); the
// DATA REGION payloads are read lazily, on demand, straight from the file.
// `parentHash` persists the entry's parent's key hash (0 for a top-level
// entry) so that hierarchy survives a reload from disk: keyHash alone is
// derived from the short name only (see StaticIdentifier) and can collide
// across different parents (e.g. two drivers each having a "Start" child).
struct alignas(8) ConfigurationIndexEntry {
  uint64_t keyHash{0};
  uint64_t parentHash{0};
  uint16_t nameEntryIndex{NO_NAME_ENTRY};
  uint8_t valueType{0};
  uint8_t reserved0{0};
  uint32_t offset{0};
  uint32_t length{0};
  uint32_t reserved1{0};
};
static_assert(sizeof(ConfigurationIndexEntry) == 32, "ConfigurationIndexEntry must match the on-disk index entry size");

constexpr size_t ENTRIES_PER_INDEX_BLOCK = 127;

// A whole INDEX BLOCK: a fixed-size, page-aligned (4096 byte) chunk holding
// up to 127 entries plus the absolute offset of the next INDEX BLOCK in the
// chain (0 meaning "end of chain").
struct alignas(8) ConfigurationIndexBlock {
  uint32_t nextBlockOffset{0};// absolute file offset, 0 = end of chain
  uint32_t reserved{0};
  ConfigurationIndexEntry entries[ENTRIES_PER_INDEX_BLOCK]{};
  uint8_t padding[24]{};
};
static_assert(sizeof(ConfigurationIndexBlock) == 4096, "ConfigurationIndexBlock must match the on-disk index block size");

constexpr size_t NAME_LENGTH = 64;

// On-disk layout of a single NAME BLOCK entry (72 bytes, aligned 8).
struct alignas(8) ConfigurationNameEntry {
  uint64_t keyHash{0};
  char name[NAME_LENGTH]{};
};
static_assert(sizeof(ConfigurationNameEntry) == 72, "ConfigurationNameEntry must match the on-disk name entry size");

constexpr size_t ENTRIES_PER_NAME_BLOCK = 56;

// A whole NAME BLOCK: a fixed-size, page-aligned (4096 byte) chunk holding
// up to 56 entries plus the absolute offset of the next NAME BLOCK in the
// chain. Each INDEX BLOCK is immediately followed by its paired NAME BLOCK.
struct alignas(8) ConfigurationNameBlock {
  uint32_t nextBlockOffset{0};// absolute file offset, 0 = end of chain
  uint8_t reserved[4]{};
  ConfigurationNameEntry entries[ENTRIES_PER_NAME_BLOCK]{};
  uint8_t padding[56]{};
};
static_assert(sizeof(ConfigurationNameBlock) == 4096, "ConfigurationNameBlock must match the on-disk name block size");

// Byte size of an INDEX BLOCK + its paired NAME BLOCK: the unit that is
// appended, as a whole, every time the chain needs to grow. A block pair's
// DATA REGION starts right after this.
constexpr uint64_t BLOCK_PAIR_BYTES = sizeof(ConfigurationIndexBlock) + sizeof(ConfigurationNameBlock);

kstd::error_code ReadRawAt(const kernel::KObjectPtr<kernel::KIOObject> &file, uint64_t offset, kstd::span<uint8_t> outValue);
kstd::error_code WriteRawAt(const kernel::KObjectPtr<kernel::KIOObject> &file, uint64_t offset, kstd::span<const uint8_t> data);

// The NAME BLOCK immediately follows its paired INDEX BLOCK on disk (see the
// on-disk layout diagram at the top of this file).
uint64_t PairedNameBlockOffset(uint32_t indexBlockOffset);

// Reads back the short name stored at `nameEntryIndex` within the NAME BLOCK
// paired with `indexBlockOffset`, sanity-checking that its stored keyHash
// still matches. Leaves `outName` untouched (empty) on any failure - a
// missing/mismatched name is not fatal, the entry is simply unnamed.
void ReadNameEntry(const kernel::KObjectPtr<kernel::KIOObject> &file, uint32_t indexBlockOffset, uint16_t nameEntryIndex, uint64_t keyHash, kstd::static_string<32> &outName);

// Where the next never-before-used byte of the block pair's DATA REGION is:
// one past the highest (offset + length) among the entries belonging to
// that INDEX BLOCK, or right at the start of the block pair's data region
// if it has no entries yet. Each block pair's DATA REGION sits between its
// own fixed-size INDEX BLOCK + NAME BLOCK and the next block pair (or the
// end of the file, for the last pair in the chain), so growth must stay
// scoped to entries sharing the same `indexBlockOffset`.
uint64_t ComputeDataRegionEnd(const kernel::KConfigurationRoot &root, uint32_t indexBlockOffset);

kstd::error_code PersistIndexEntry(const kernel::KObjectPtr<kernel::KIOObject> &file, const kernel::KConfigurationRoot::Root &root);

kstd::error_code PersistNameEntry(const kernel::KObjectPtr<kernel::KIOObject> &file, uint32_t indexBlockOffset, uint16_t nameEntryIndex, uint64_t keyHash, const kstd::string_view &name);

// Finds the first unused (keyHash == 0) slot in the NAME BLOCK paired with
// `indexBlockOffset`, persists `name` into it, and returns its slot index.
// Unlike FindOrAllocateFreeSlot() for the INDEX BLOCK chain, this never
// allocates a brand-new NAME BLOCK: a paired NAME BLOCK only has room for 56
// names even though its INDEX BLOCK has room for 170 entries, so it is
// possible (if unlikely in practice) for this to simply run out of room. In
// that case the caller's entry is left unnamed rather than failing outright.
kstd::error_code FindOrAllocateNameSlot(const kernel::KObjectPtr<kernel::KIOObject> &file, uint32_t indexBlockOffset, uint64_t keyHash, const kstd::string_view &name, uint16_t &outNameEntryIndex);

// Finds the value entry for `keyHash`/`parentHash`, validates its stored
// type/size against what the caller expects, and reads its raw payload
// bytes. Both keyHash and parentHash are needed to identify the entry:
// keyHash alone can collide across different parents (see
// KConfigurationKey::parentHash).
kstd::error_code ReadKeyValue(const kernel::KConfigurationRoot &root, uint64_t keyHash, uint64_t parentHash, kernel::ConfigurationValueType expectedType, kstd::span<uint8_t> outValue);

// Finds the first unused (keyHash == 0) INDEX BLOCK slot across the hive's
// chained block pairs, extending the chain with a brand-new (empty)
// INDEX BLOCK/NAME BLOCK pair at the end of the file if every existing
// INDEX BLOCK is already full. Slots within a block are only ever appended
// in order (deletion of entries - and therefore reuse of interior holes -
// isn't implemented yet), so the first free slot in a block is simply the
// one right after however many entries already live there.
kstd::error_code FindOrAllocateFreeSlot(kernel::KConfigurationRoot &root, uint32_t &outIndexBlockOffset, uint32_t &outIndexEntryOffset);

// Finds the existing value entry for `keyHash`/`parentHash`, writes (or
// relocates, if the payload size changed) its stored bytes, and persists
// both the value data and its (possibly-updated) index entry to disk. Does
// not create new entries: the target key must already have an entry in the
// index. Both keyHash and parentHash are needed to identify the entry:
// keyHash alone can collide across different parents (see
// KConfigurationKey::parentHash).
kstd::error_code WriteKeyValue(kernel::KConfigurationRoot &root, uint64_t keyHash, uint64_t parentHash, kernel::ConfigurationValueType type, kstd::span<const uint8_t> data);

// Writes a fresh HEADER plus a single, fully-empty INDEX BLOCK/NAME BLOCK
// pair to `file`, producing a minimal-but-valid, empty configuration hive
// (no entries). configuration_manager.cpp's EnsureRootIsOpened() never
// creates or formats a file itself - the file must already exist and be
// valid - so this is how tooling (or tests) build one ahead of time.
kstd::error_code FormatEmptyConfigurationFile(const kernel::KObjectPtr<kernel::KIOObject> &file);

// Reads back `file`'s HEADER and walks its chained INDEX BLOCKs, populating
// `outRoots`/`outNumRoots` with every entry found (mirroring the on-disk
// layout diagram at the top of this file). Returns an error (InvalidFormat for a bad header), leaving
// `outRoots`/`outNumRoots` untouched, if the header is missing/invalid or a
// block in the chain can't be read.
kstd::error_code LoadConfigurationIndex(const kernel::KObjectPtr<kernel::KIOObject> &file, kstd::list<kernel::KConfigurationRoot::Root> &outRoots, size_t &outNumRoots);

}// namespace kernel::configuration_manager_io
