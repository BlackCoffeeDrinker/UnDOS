#pragma once

#include <static_string.hpp>
#include <string_view.hpp>

#include <stdint.h>

// VFAT long-file-name (LFN) directory entry, 32 bytes. Read-only decoding support.
struct [[gnu::packed]] FAT_LfnEntry {
  uint8_t ord;
  uint16_t name1[5];
  uint8_t attr;
  uint8_t type;
  uint8_t checksum;
  uint16_t name2[6];
  uint16_t cluster;
  uint16_t name3[2];
};
static_assert(sizeof(FAT_LfnEntry) == 32);

inline constexpr uint8_t kLfnLastEntry = 0x40;
inline constexpr size_t kLfnCharsPerEntry = 13;
inline constexpr size_t kLfnMaxName = 255;
inline constexpr size_t kLfnMaxEntries = (kLfnMaxName + kLfnCharsPerEntry - 1) / kLfnCharsPerEntry;// 20

inline uint8_t FAT_ShortNameChecksum(const char (&name)[11]) {
  uint8_t sum = 0;
  for (size_t i = 0; i < 11; i++) {
    sum = static_cast<uint8_t>(((sum & 1) << 7) + (sum >> 1) + static_cast<uint8_t>(name[i]));
  }
  return sum;
}

// Collects LFN fragments in on-disk order (highest sequence first) until the
// owning short entry arrives. State persists across sector/cluster reads.
class FAT_LfnAccumulator {
public:
  void Reset() {
    _active = false;
    _expected = 0;
    _count = 0;
  }

  void Feed(const FAT_LfnEntry &e) {
    const uint8_t seq = e.ord & 0x1f;
    if (e.ord & kLfnLastEntry) {
      Reset();
      if (seq == 0 || seq > kLfnMaxEntries || (e.ord & 0x80)) return;
      _active = true;
      _count = seq;
      _checksum = e.checksum;
    } else if (!_active || seq == 0 || seq != _expected - 1 || e.checksum != _checksum) {
      Reset();
      return;
    }
    _expected = seq;
    uint16_t *dst = &_chars[(seq - 1) * kLfnCharsPerEntry];
    for (size_t i = 0; i < 5; i++) *dst++ = e.name1[i];
    for (size_t i = 0; i < 6; i++) *dst++ = e.name2[i];
    for (size_t i = 0; i < 2; i++) *dst++ = e.name3[i];
  }

  // Validates the chain against the short entry and writes the ASCII long name.
  // Always resets the accumulator. Returns false if no valid chain is pending.
  bool Complete(const char (&shortName)[11], kstd::static_string<256> &out) {
    const bool ok = _active && _expected == 1 && _checksum == FAT_ShortNameChecksum(shortName);
    const size_t total = _count * kLfnCharsPerEntry;
    Reset();
    if (!ok) return false;
    out.clear();
    for (size_t i = 0; i < total; i++) {
      const uint16_t c = _chars[i];
      if (c == 0x0000) break;
      if (c == 0xFFFF) break;
      if (out.size() >= kLfnMaxName) return false;
      out.append(c < 0x80 ? static_cast<char>(c) : '?');
    }
    return !out.empty();
  }

private:
  uint16_t _chars[kLfnMaxEntries * kLfnCharsPerEntry]{};
  uint8_t _count{0};
  uint8_t _expected{0};
  uint8_t _checksum{0};
  bool _active{false};
};

// A decoded long name can be listed only if it can also be opened by that
// name: '?' marks unmappable UCS-2, which FAT_LongNameEquals never matches.
inline bool FAT_LongNameListable(kstd::string_view name) {
  if (name.empty()) return false;
  for (size_t i = 0; i < name.size(); i++) {
    if (name[i] == '?') return false;
  }
  return true;
}

// ASCII case-insensitive comparison; '?' (unmappable UCS-2) never matches.
inline bool FAT_LongNameEquals(kstd::string_view a, kstd::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); i++) {
    char x = a[i];
    char y = b[i];
    if (x == '?' || y == '?') return false;
    if (x >= 'a' && x <= 'z') x = static_cast<char>(x - 'a' + 'A');
    if (y >= 'a' && y <= 'z') y = static_cast<char>(y - 'a' + 'A');
    if (x != y) return false;
  }
  return true;
}
