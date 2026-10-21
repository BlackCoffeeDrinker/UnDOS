#pragma once

#include <stddef.h>
#include <stdint.h>

namespace filemanager {
constexpr size_t PathCapacity = 256;
constexpr int PageSize = 64;
constexpr int TreeCapacity = 256;

inline size_t length(const char *text) {
  size_t n = 0;
  while (text[n]) ++n;
  return n;
}

inline bool copy(char *out, size_t capacity, const char *text) {
  const size_t n = length(text);
  if (n >= capacity) return false;
  for (size_t i = 0; i <= n; ++i) out[i] = text[i];
  return true;
}

inline int compare(const char *a, const char *b) {
  while (*a && *b) {
    char x = *a++, y = *b++;
    if (x >= 'a' && x <= 'z') x -= 'a' - 'A';
    if (y >= 'a' && y <= 'z') y -= 'a' - 'A';
    if (x != y) return static_cast<unsigned char>(x) - static_cast<unsigned char>(y);
  }
  return static_cast<unsigned char>(*a) - static_cast<unsigned char>(*b);
}

inline bool canonical(const char *path, char (&out)[PathCapacity]) {
  if (path[0] != '/' || length(path) >= PathCapacity) return false;
  char result[PathCapacity] = "/";
  size_t used = 1;
  for (size_t i = 1; path[i];) {
    while (path[i] == '/') ++i;
    if (!path[i]) break;
    const size_t begin = i;
    while (path[i] && path[i] != '/') {
      if (static_cast<unsigned char>(path[i]) < 32 || path[i] == '\\') return false;
      ++i;
    }
    const size_t n = i - begin;
    if (n == 1 && path[begin] == '.') continue;
    if (n == 2 && path[begin] == '.' && path[begin + 1] == '.') {
      if (used > 1) {
        while (used > 1 && result[used - 1] != '/') --used;
        if (used > 1) --used;
      }
    } else {
      if (used > 1) result[used++] = '/';
      for (size_t j = 0; j < n; ++j) result[used++] = path[begin + j];
    }
    result[used] = 0;
  }
  return copy(out, sizeof(out), result);
}

inline bool join(const char *directory, const char *name, char (&out)[PathCapacity]) {
  if (!name[0]) return false;
  char combined[PathCapacity]{};
  if (name[0] == '/') return canonical(name, out);
  const size_t a = length(directory), b = length(name);
  const bool slash = a > 1;
  if (!a || a + slash + b >= sizeof(combined)) return false;
  copy(combined, sizeof(combined), directory);
  size_t pos = a;
  if (slash) combined[pos++] = '/';
  for (size_t i = 0; i <= b; ++i) combined[pos + i] = name[i];
  return canonical(combined, out);
}

inline void parent(char (&path)[PathCapacity]) {
  size_t n = length(path);
  while (n > 1 && path[n - 1] != '/') --n;
  if (n > 1) --n;
  path[n] = 0;
}

inline const char *basename(const char *path) {
  const char *name = path;
  for (const char *p = path; *p; ++p) if (*p == '/' && p[1]) name = p + 1;
  return name;
}

inline void decimal(uint64_t value, char (&out)[24]) {
  char reversed[24]{};
  unsigned n = 0;
  do { reversed[n++] = static_cast<char>('0' + value % 10); value /= 10; } while (value);
  for (unsigned i = 0; i < n; ++i) out[i] = reversed[n - i - 1];
  out[n] = 0;
}

struct Entry {
  char name[PathCapacity]{};
  uint64_t size{0};
  uint32_t attributes{0};
  bool directory() const { return attributes & 16; }
};

inline void sort(Entry *entries, int count, bool bySize, bool reverse) {
  const auto before = [=](const Entry &a, const Entry &b) {
    if (a.directory() != b.directory()) return a.directory();
    int order = bySize && a.size != b.size ? (a.size < b.size ? -1 : 1) : compare(a.name, b.name);
    return reverse ? order > 0 : order < 0;
  };
  for (int i = 1; i < count; ++i) {
    Entry value = entries[i];
    int j = i;
    while (j && before(value, entries[j - 1])) { entries[j] = entries[j - 1]; --j; }
    entries[j] = value;
  }
}
} // namespace filemanager
