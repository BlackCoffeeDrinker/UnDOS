#pragma once

#include <stddef.h>
#include <stdint.h>
#include <undoscrt/heap.h>

// Called when the CRT heap cannot satisfy a request; must not return.
[[noreturn]] void fontloader_allocation_failure();

namespace undos::font::loader {

// Minimal growable array over the CRT heap, for trivially copyable elements.
template <typename T>
class GrowableArray {
  static_assert(__is_trivially_copyable(T), "GrowableArray requires trivially copyable elements");

public:
  GrowableArray() = default;
  GrowableArray(const GrowableArray &) = delete;
  GrowableArray &operator=(const GrowableArray &) = delete;
  GrowableArray(GrowableArray &&other) noexcept
      : items(other.items), count(other.count), reserved(other.reserved) {
    other.items = nullptr;
    other.count = other.reserved = 0;
  }
  GrowableArray &operator=(GrowableArray &&other) noexcept {
    if (this != &other) {
      crt_free(items);
      items = other.items; count = other.count; reserved = other.reserved;
      other.items = nullptr;
      other.count = other.reserved = 0;
    }
    return *this;
  }
  ~GrowableArray() { crt_free(items); }

  size_t size() const { return count; }
  size_t capacity() const { return reserved; }
  bool empty() const { return count == 0; }
  T *data() { return items; }
  const T *data() const { return items; }
  T &operator[](size_t index) { return items[index]; }
  const T &operator[](size_t index) const { return items[index]; }
  T *begin() { return items; }
  T *end() { return items + count; }
  const T *begin() const { return items; }
  const T *end() const { return items + count; }

  void reserve(size_t wanted) {
    if (wanted <= reserved) return;
    if (wanted > SIZE_MAX / sizeof(T)) fontloader_allocation_failure();
    void *grown = crt_realloc(items, wanted * sizeof(T));
    if (!grown) fontloader_allocation_failure();
    items = static_cast<T *>(grown);
    reserved = wanted;
  }
  // New elements are value-initialized, like a std::vector.
  void resize(size_t wanted) {
    reserve(wanted);
    for (size_t i = count; i < wanted; ++i) items[i] = T{};
    count = wanted;
  }
  void push_back(const T &value) {
    if (count == reserved) reserve(reserved ? reserved * 2 : 16);
    items[count++] = value;
  }
  void clear() { count = 0; }

private:
  T *items{nullptr};
  size_t count{0};
  size_t reserved{0};
};

} // namespace undos::font::loader
