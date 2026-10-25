#pragma once

#include "realization.hpp"

namespace kernel::font {

constexpr size_t DefaultCacheBytes = 512 * 1024;
constexpr uint32_t DefaultCacheEntries = 2048;

struct CacheStats {
  uint32_t hits{0};
  uint32_t misses{0};
  uint32_t evictions{0};
  uint32_t entries{0};
  size_t bytes{0};
};

// Shared glyph-mask cache with a byte budget and an entry limit. Entries are
// reference counted, so a mask in use by a draw survives eviction: the cache
// only drops its own reference. Pinned masks therefore keep memory beyond the
// budget until their readers finish. Methods never run glyph programs or
// rasterize; callers compute outside the cache lock.
class GlyphCache {
public:
  explicit GlyphCache(size_t byteBudget = DefaultCacheBytes, uint32_t maxEntries = DefaultCacheEntries);

  // Returns an empty reference on a miss. A hit becomes the most recently used.
  Ref<GlyphMask> find(const GlyphKey &key);
  // Stores the mask and returns the canonical entry. When the key is already
  // present, the existing mask wins so that concurrent misses agree. A mask larger
  // than the byte budget is returned without being cached.
  Ref<GlyphMask> insert(const GlyphKey &key, Ref<GlyphMask> mask);
  // Drops every entry for a face identity, for example after its handle closes.
  void purge(uint32_t face);
  void clear();
  CacheStats stats() const;
  bool valid() const { return valid_; }

private:
  static constexpr int32_t None = -1;
  struct Entry {
    GlyphKey key{};
    Ref<GlyphMask> mask;
    int32_t prev{None}, next{None}, chain{None};
  };
  Entry &entry_at(int32_t index) { return entries_[static_cast<size_t>(index)]; }
  const Entry &entry_at(int32_t index) const { return entries_[static_cast<size_t>(index)]; }
  uint32_t bucket(const GlyphKey &key) const;
  int32_t locate(const GlyphKey &key) const;
  void unlink_lru(int32_t index);
  void push_front(int32_t index);
  void remove(int32_t index);
  void evict_until(size_t incoming);

  mutable Mutex mutex_;
  size_t budget_;
  bool valid_{false};
  kstd::vector<Entry> entries_;
  kstd::vector<int32_t> buckets_;
  int32_t free_{None};
  int32_t head_{None}, tail_{None};
  uint32_t hits_{0}, misses_{0}, evictions_{0}, count_{0};
  size_t bytes_{0};
};

} // namespace kernel::font
