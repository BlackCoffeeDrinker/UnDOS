#include "glyph_cache.hpp"

namespace kernel::font {
namespace {
constexpr uint32_t HashMultiplier = 0x9e3779b1u;
constexpr uint32_t PolicyShift = 24;

uint32_t next_power_of_two(uint32_t value) {
  uint32_t result = 1;
  while (result < value) result <<= 1;
  return result;
}
} // namespace

GlyphCache::GlyphCache(size_t byteBudget, uint32_t maxEntries) : budget_(byteBudget) {
  if (!maxEntries || maxEntries > (1u << 20)) return;
  entries_.resize(maxEntries);
  buckets_.resize(next_power_of_two(maxEntries * 2));
  if (entries_.size() != maxEntries || buckets_.size() < maxEntries * 2) return;
  for (auto &bucket : buckets_) bucket = None;
  for (uint32_t i = 0; i < maxEntries; ++i) entries_[i].chain = i + 1 < maxEntries ? static_cast<int32_t>(i + 1) : None;
  free_ = 0;
  valid_ = true;
}

uint32_t GlyphCache::bucket(const GlyphKey &key) const {
  uint32_t hash = key.face * HashMultiplier;
  hash = (hash ^ key.glyph) * HashMultiplier;
  hash = (hash ^ (key.ppem | (static_cast<uint32_t>(key.policy) << PolicyShift))) * HashMultiplier;
  return (hash >> 7) & to_u32(buckets_.size() - 1);
}

int32_t GlyphCache::locate(const GlyphKey &key) const {
  if (!valid_) return None;
  for (int32_t at = buckets_[bucket(key)]; at != None; at = entry_at(at).chain)
    if (entry_at(at).key == key) return at;
  return None;
}

void GlyphCache::unlink_lru(int32_t index) {
  Entry &entry = entry_at(index);
  if (entry.prev != None) entry_at(entry.prev).next = entry.next;
  else head_ = entry.next;
  if (entry.next != None) entry_at(entry.next).prev = entry.prev;
  else tail_ = entry.prev;
  entry.prev = entry.next = None;
}

void GlyphCache::push_front(int32_t index) {
  Entry &entry = entry_at(index);
  entry.prev = None;
  entry.next = head_;
  if (head_ != None) entry_at(head_).prev = index;
  head_ = index;
  if (tail_ == None) tail_ = index;
}

void GlyphCache::remove(int32_t index) {
  Entry &entry = entry_at(index);
  int32_t *link = &buckets_[bucket(entry.key)];
  while (*link != index) link = &entry_at(*link).chain;
  *link = entry.chain;
  unlink_lru(index);
  bytes_ -= entry.mask->byteSize();
  entry.mask.reset();
  entry.chain = free_;
  free_ = index;
  --count_;
}

void GlyphCache::evict_until(size_t incoming) {
  while (tail_ != None && (bytes_ + incoming > budget_ || free_ == None)) {
    remove(tail_);
    ++evictions_;
  }
}

Ref<GlyphMask> GlyphCache::find(const GlyphKey &key) {
  kstd::unique_lock lock(mutex_);
  const int32_t at = locate(key);
  if (at == None) {
    ++misses_;
    return {};
  }
  ++hits_;
  unlink_lru(at);
  push_front(at);
  return entry_at(at).mask;
}

Ref<GlyphMask> GlyphCache::insert(const GlyphKey &key, Ref<GlyphMask> mask) {
  if (!mask) return mask;
  const size_t size = mask->byteSize();
  kstd::unique_lock lock(mutex_);
  if (!valid_ || size > budget_) return mask;
  if (const int32_t at = locate(key); at != None) {
    unlink_lru(at);
    push_front(at);
    return entry_at(at).mask;
  }
  evict_until(size);
  if (free_ == None) return mask;
  const int32_t index = free_;
  Entry &entry = entry_at(index);
  free_ = entry.chain;
  entry.key = key;
  entry.mask = mask;
  const uint32_t slot = bucket(key);
  entry.chain = buckets_[slot];
  buckets_[slot] = index;
  push_front(index);
  bytes_ += size;
  ++count_;
  return mask;
}

void GlyphCache::purge(uint32_t face) {
  kstd::unique_lock lock(mutex_);
  for (int32_t at = head_; at != None;) {
    const int32_t next = entry_at(at).next;
    if (entry_at(at).key.face == face) remove(at);
    at = next;
  }
}

void GlyphCache::clear() {
  kstd::unique_lock lock(mutex_);
  while (tail_ != None) remove(tail_);
}

CacheStats GlyphCache::stats() const {
  kstd::unique_lock lock(mutex_);
  return {.hits = hits_, .misses = misses_, .evictions = evictions_, .entries = count_, .bytes = bytes_};
}

} // namespace kernel::font
