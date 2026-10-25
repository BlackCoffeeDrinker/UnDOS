#pragma once

#include <stddef.h>
#include <stdint.h>

namespace progman {
constexpr unsigned MaxGroups = 6;
constexpr unsigned MaxItems = 32;
constexpr unsigned LabelLength = 32;
constexpr unsigned PathLength = 96;
constexpr unsigned ArgumentLength = 128;

inline bool copy(char *out, size_t capacity, const char *value) {
  size_t n = 0;
  while (value[n]) {
    if (n + 1 >= capacity || value[n] == '|' || value[n] == '\n' || value[n] == '\r') return false;
    ++n;
  }
  for (size_t i = 0; i <= n; ++i) out[i] = value[i];
  return true;
}

struct Group {
  uint32_t id{0};
  char title[LabelLength]{};
};

struct Item {
  uint32_t id{0};
  uint32_t group{0};
  char title[LabelLength]{};
  char path[PathLength]{};
  char arguments[ArgumentLength]{};
  uint32_t icon{0};
};

struct Model {
  Group groups[MaxGroups]{};
  Item items[MaxItems]{};
  bool modified{false};

  int group_index(uint32_t id) const {
    for (unsigned i = 0; i < MaxGroups; ++i) if (groups[i].id == id && id) return static_cast<int>(i);
    return -1;
  }

  int item_index(uint32_t id) const {
    for (unsigned i = 0; i < MaxItems; ++i) if (items[i].id == id && id) return static_cast<int>(i);
    return -1;
  }

  uint32_t next_id() const {
    uint32_t largest = 0;
    for (const auto &g : groups) if (g.id > largest) largest = g.id;
    for (const auto &item : items) if (item.id > largest) largest = item.id;
    return largest == UINT32_MAX ? 0 : largest + 1;
  }

  int add_group(const char *title) {
    Group candidate{};
    if (!title[0] || !copy(candidate.title, sizeof(candidate.title), title)) return -1;
    for (unsigned i = 0; i < MaxGroups; ++i) {
      if (groups[i].id) continue;
      candidate.id = next_id();
      if (!candidate.id) return -1;
      groups[i] = candidate;
      modified = true;
      return static_cast<int>(i);
    }
    return -1;
  }

  int add_item(uint32_t group, const char *title, const char *path, const char *args, uint32_t icon) {
    if (group_index(group) < 0 || !title[0] || path[0] != '/') return -1;
    Item candidate{};
    if (!copy(candidate.title, sizeof(candidate.title), title) ||
        !copy(candidate.path, sizeof(candidate.path), path) ||
        !copy(candidate.arguments, sizeof(candidate.arguments), args)) return -1;
    for (unsigned i = 0; i < MaxItems; ++i) {
      if (items[i].id) continue;
      candidate.id = next_id();
      if (!candidate.id) return -1;
      candidate.group = group;
      candidate.icon = icon;
      items[i] = candidate;
      modified = true;
      return static_cast<int>(i);
    }
    return -1;
  }

  bool remove_group(uint32_t id) {
    const int index = group_index(id);
    if (index < 0) return false;
    for (auto &item : items) if (item.group == id) item = Item{};
    groups[index] = Group{};
    modified = true;
    return true;
  }

  int count(uint32_t group) const {
    int n = 0;
    for (const auto &item : items) if (item.id && item.group == group) ++n;
    return n;
  }

  int item_at(uint32_t group, int ordinal) const {
    for (unsigned i = 0; i < MaxItems; ++i) {
      if (items[i].id && items[i].group == group && ordinal-- == 0) return static_cast<int>(i);
    }
    return -1;
  }
};

// Icon cells keep the classic 112-pixel minimum and widen to the widest
// measured label plus padding; overflow pages instead of truncating labels.
constexpr int MinimumCellWidth = 112;
constexpr int LabelPadding = 8;

struct Grid {
  int columns;
  int rows;
  int page_size;
  int cell_width;
  int cell_height;
  static Grid for_size(int width, int height, int labelHeight, int labelWidth = 0) {
    const int cellWidth = labelWidth > MinimumCellWidth - LabelPadding ? labelWidth + LabelPadding
                                                                        : MinimumCellWidth;
    int columns = width / cellWidth;
    const int cellHeight = 50 + labelHeight;
    int rows = height / cellHeight;
    if (columns < 1) columns = 1;
    if (rows < 1) rows = 1;
    return {.columns = columns, .rows = rows, .page_size = columns * rows,
            .cell_width = cellWidth, .cell_height = cellHeight};
  }
  int page_start(int selection) const { return selection / page_size * page_size; }
};

inline bool decimal(const char *s, uint32_t &out) {
  if (!*s) return false;
  uint32_t value = 0;
  while (*s) {
    if (*s < '0' || *s > '9' || value > (0xffffffffu - static_cast<unsigned>(*s - '0')) / 10) return false;
    value = value * 10 + static_cast<unsigned>(*s++ - '0');
  }
  out = value;
  return true;
}

inline bool equal(const char *a, const char *b) {
  while (*a && *a == *b) { ++a; ++b; }
  return *a == *b;
}

// Parse into a fresh model. Callers retain the live model until parsing succeeds.
inline const char *parse(Model &out, char *text) {
  out = Model{};
  bool version = false;
  while (*text) {
    char *line = text;
    while (*text && *text != '\n' && *text != '\r') ++text;
    if (*text) {
      const char end = *text;
      *text++ = 0;
      if (end == '\r' && *text == '\n') ++text;
    }
    if (!*line || *line == '#') continue;
    if (!version) {
      if (!equal(line, "version=1")) return "Expected version=1";
      version = true;
      continue;
    }
    char *parts[7]{};
    unsigned n = 1;
    parts[0] = line;
    for (char *p = line; *p; ++p) {
      if (*p != '|') continue;
      if (n == 7) return "Too many configuration fields";
      *p = 0;
      parts[n++] = p + 1;
    }
    uint32_t id = 0;
    if (equal(parts[0], "group") && n == 3) {
      if (!decimal(parts[1], id) || !id || id == 0xffffffffu ||
          out.group_index(id) >= 0 || out.item_index(id) >= 0) return "Invalid or duplicate group ID";
      const int i = out.add_group(parts[2]);
      if (i < 0) return "Invalid group title or too many groups";
      out.groups[i].id = id;
    } else if (equal(parts[0], "item") && n == 7) {
      uint32_t group = 0, icon = 0;
      if (!decimal(parts[1], id) || !id || id == 0xffffffffu ||
          !decimal(parts[2], group) || !decimal(parts[6], icon) ||
          out.item_index(id) >= 0 || out.group_index(id) >= 0) return "Invalid or duplicate item ID";
      const int i = out.add_item(group, parts[3], parts[4], parts[5], icon);
      if (i < 0) return "Invalid program, unknown group or too many items";
      out.items[i].id = id;
    } else return "Unknown configuration record";
  }
  if (!version) return "Empty configuration";
  out.modified = false;
  return nullptr;
}
} // namespace progman
