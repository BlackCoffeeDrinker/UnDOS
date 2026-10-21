#include <undosgui/gui.hpp>
#include <undoscrt/string.h>
#include "model.hpp"

namespace {
using namespace filemanager;
constexpr int ToolbarTop = 24;
constexpr int ListTop = 104;
constexpr int RowHeight = 20;
constexpr int TreeWidth = 208;

enum Command { Open, Up, Location, Refresh, Copy, Move, Rename, Delete, NewFolder, Properties, Help, Exit };
constexpr const char *Toolbar[] = {"Open", "Up", "Path", "Refresh", "Copy", "Move", "Rename", "Delete", "New dir"};
struct TreeNode {
  char path[PathCapacity]{};
  int depth{0};
  bool expanded{false};
};
struct State {
  uint32_t hwnd{0};
  bool running{true};
  char path[PathCapacity] = "/";
  char status[256] = "Ready";
  Entry entries[PageSize]{};
  Entry candidate[PageSize]{};
  int count{0};
  uint32_t page{0};
  bool nextPage{false};
  int selection{-1};
  int scroll{0};
  bool sortSize{false};
  bool reverse{false};
  bool treeFocus{false};
  TreeNode tree[TreeCapacity]{};
  TreeNode children[TreeCapacity]{};
  int treeCount{1};
  int treeSelection{0};
  int treeScroll{0};
};
State app;
void notice(const char *title, const char *text);

void focus() {
  gui::check(UD_SYS_SetFocusV1(app.hwnd), "SetFocus");
  gui::invalidate(app.hwnd);
}

void message(const char *title, const char *text) {
  notice(title, text);
  focus();
}

bool checked(uint32_t result, const char *operation) {
  if (gui::check(result, operation)) return true;
  copy(app.status, sizeof(app.status), "Operation failed");
  message(operation, "Failed: missing path, existing target, busy/read-only volume or I/O error.\nShort names only. See the serial log for the status code.");
  return false;
}

bool read_entry(HANDLE handle, uint32_t index, Entry &entry, bool &found) {
  uint32_t capacity = sizeof(entry.name), present = 0;
  FileInformation info{};
  const auto result = UD_SYS_ReadDirectoryV1(handle, index, entry.name, &capacity, &info, &present);
  if (!checked(result, "Read directory")) return false;
  found = present != 0;
  if (found) {
    entry.size = info.size;
    entry.attributes = info.attributes;
  }
  return true;
}

bool load(const char *path, uint32_t page = 0) {
  char canonicalPath[PathCapacity]{};
  if (!canonical(path, canonicalPath)) { message("Invalid path", "Enter an absolute UnDOS path of at most 255 bytes."); return false; }
  HANDLE directory{};
  if (!checked(UD_SYS_OpenV1(canonicalPath, FileOpenMode_Read, &directory), "Open directory")) return false;
  FileInformation information{};
  bool okay = checked(UD_SYS_GetFileInformationV1(directory, &information), "Directory information");
  if (okay && !(information.attributes & 16)) {
    message("Not a directory", "Choose a folder, not a file.");
    okay = false;
  }
  // Do not replace the displayed directory until enumeration succeeds.
  int count = 0;
  bool more = false;
  if (okay) {
    for (int i = 0; i <= PageSize; ++i) {
      Entry entry{};
      bool found = false;
      if (!read_entry(directory, page * PageSize + static_cast<uint32_t>(i), entry, found)) { okay = false; break; }
      if (!found) break;
      if (i == PageSize) more = true;
      else app.candidate[count++] = entry;
    }
  }
  if (!checked(UD_SYS_CloseV1(directory), "Close directory")) okay = false;
  if (!okay) return false;
  copy(app.path, sizeof(app.path), canonicalPath);
  memcpy(app.entries, app.candidate, static_cast<size_t>(count) * sizeof(Entry));
  app.count = count;
  app.page = page;
  app.nextPage = more;
  app.selection = count ? 0 : -1;
  app.scroll = 0;
  sort(app.entries, app.count, app.sortSize, app.reverse);
  copy(app.status, sizeof(app.status), count ? "Ready" : "Empty directory");
  (void) UD_SYS_PrintV1("filemanager: directory ready");
  gui::check(UD_SYS_SetWindowTextV1(app.hwnd, "File Manager"), "SetWindowText");
  gui::invalidate(app.hwnd);
  return true;
}

int visible_rows() {
  const Rect area = gui::client(app.hwnd);
  const int rows = (area.bottom - ListTop - 54) / RowHeight;
  return rows > 0 ? rows : 1;
}

void ensure_visible() {
  int &scroll = app.treeFocus ? app.treeScroll : app.scroll;
  const int selected = app.treeFocus ? app.treeSelection : app.selection;
  const int count = app.treeFocus ? app.treeCount : app.count;
  if (selected < scroll) scroll = selected;
  if (selected >= scroll + visible_rows()) scroll = selected - visible_rows() + 1;
  if (scroll < 0) scroll = 0;
  const int maximum = count > visible_rows() ? count - visible_rows() : 0;
  if (scroll > maximum) scroll = maximum;
}

void reset_tree() {
  app.treeCount = 1;
  app.treeSelection = app.treeScroll = 0;
  app.tree[0] = {};
  copy(app.tree[0].path, PathCapacity, "/");
}

void expand_tree(int index) {
  if (index < 0 || index >= app.treeCount) return;
  auto &node = app.tree[index];
  if (node.expanded) {
    int end = index + 1;
    while (end < app.treeCount && app.tree[end].depth > node.depth) ++end;
    const int removed = end - index - 1;
    for (int i = end; i < app.treeCount; ++i) app.tree[i - removed] = app.tree[i];
    app.treeCount -= removed;
    node.expanded = false;
    app.treeSelection = index;
    return;
  }
  HANDLE directory{};
  if (!checked(UD_SYS_OpenV1(node.path, FileOpenMode_Read, &directory), "Expand folder")) return;
  int count = 0;
  bool okay = true;
  for (uint32_t i = 0;; ++i) {
    Entry entry{};
    bool found = false;
    if (!read_entry(directory, i, entry, found)) { okay = false; break; }
    if (!found) break;
    if (!entry.directory()) continue;
    if (count + app.treeCount >= TreeCapacity) {
      message("Tree capacity", "The tree holds 256 folders. Collapse another branch or use Path.");
      okay = false;
      break;
    }
    auto &child = app.children[count];
    child = {};
    if (!join(node.path, entry.name, child.path)) {
      message("Path too long", "A child path exceeds the 255-byte application limit.");
      okay = false;
      break;
    }
    child.depth = node.depth + 1;
    ++count;
  }
  if (!checked(UD_SYS_CloseV1(directory), "Close tree directory")) okay = false;
  if (!okay) return;
  for (int i = 1; i < count; ++i) {
    TreeNode value = app.children[i];
    int j = i;
    while (j && compare(value.path, app.children[j - 1].path) < 0) { app.children[j] = app.children[j - 1]; --j; }
    app.children[j] = value;
  }
  for (int i = app.treeCount - 1; i > index; --i) app.tree[i + count] = app.tree[i];
  for (int i = 0; i < count; ++i) app.tree[index + i + 1] = app.children[i];
  app.treeCount += count;
  node.expanded = true;
}

void navigate_tree() {
  if (load(app.tree[app.treeSelection].path)) app.treeFocus = true;
}

bool selected_path(char (&out)[PathCapacity]) {
  if (app.treeFocus || app.selection < 0 || app.selection >= app.count) {
    message("No selection", "Select a file or folder in the right pane.");
    return false;
  }
  if (join(app.path, app.entries[app.selection].name, out)) return true;
  message("Path too long", "The selected path exceeds 255 bytes.");
  return false;
}

void open_selection() {
  if (app.treeFocus) { navigate_tree(); return; }
  char path[PathCapacity]{};
  if (!selected_path(path)) return;
  const auto &entry = app.entries[app.selection];
  if (entry.directory()) { load(path); return; }
  const size_t n = length(entry.name);
  if (n < 4 || compare(entry.name + n - 4, ".elf")) {
    message("No file association", "Only native .ELF programs can be opened. Use Properties for other files.");
    return;
  }
  HANDLE process{};
  if (checked(UD_SYS_CreateProcessV1(path, "", ProcessFlags_None, &process), "Run program"))
    checked(UD_SYS_CloseV1(process), "Close process");
}

void operate(Command command) {
  char source[PathCapacity]{}, destination[PathCapacity]{}, input[PathCapacity]{};
  if (command == NewFolder) {
    gui::Field field{.label = "Folder name (8.3)", .value = input, .capacity = sizeof(input)};
    if (!gui::dialog("Create directory", "Create a folder in the current directory.", &field, 1)) { focus(); return; }
    if (!input[0] || strchr(input, '/') || !join(app.path, input, source)) {
      message("Invalid name", "Enter one short folder name."); return;
    }
  } else {
    if (!selected_path(source)) return;
    if (command == Delete) {
      if (!gui::dialog("Delete permanently", source, nullptr, 0)) { focus(); return; }
    } else {
      if (command == Rename) copy(input, sizeof(input), app.entries[app.selection].name);
      else copy(input, sizeof(input), source);
      gui::Field field{.label = command == Rename ? "New name (8.3)" : "Destination path", .value = input, .capacity = sizeof(input)};
      if (!gui::dialog(command == Copy ? "Copy file" : command == Move ? "Move file" : "Rename",
          "Existing destinations are never replaced. Same volume only.", &field, 1)) { focus(); return; }
      if ((command == Rename && strchr(input, '/')) || !join(app.path, input, destination)) {
        message("Invalid destination", "Enter a valid short name or an absolute destination path."); return;
      }
    }
  }
  const auto result = command == NewFolder ? UD_SYS_CreateDirectoryV1(source) :
      command == Delete ? UD_SYS_DeleteFileV1(source) :
      command == Copy ? UD_SYS_CopyFileV1(source, destination) :
      UD_SYS_MoveFileV1(source, destination);
  if (result == SyscallReturnCode_UNSUPPORTED) {
    gui::check(result, "File operation");
    message("Unsupported operation", "Folder copy and cross-parent folder moves are not supported.\nUse short names. Long-name entries and some volumes are read-only.");
  } else checked(result, "File operation");
  // A device failure may have changed disk state. Never retain stale tree nodes.
  reset_tree();
  load(app.path);
  focus();
}

void properties() {
  char path[PathCapacity]{};
  if (!selected_path(path)) return;
  const auto &entry = app.entries[app.selection];
  char size[24]{}, attributes[8]{};
  decimal(entry.size, size);
  unsigned n = 0;
  if (entry.attributes & 1) attributes[n++] = 'R';
  if (entry.attributes & 2) attributes[n++] = 'H';
  if (entry.attributes & 4) attributes[n++] = 'S';
  if (entry.attributes & 16) attributes[n++] = 'D';
  if (entry.attributes & 32) attributes[n++] = 'A';
  gui::Field fields[] = {{.label = "Path", .value = path, .capacity = sizeof(path)},
                        {.label = "Size in bytes", .value = size, .capacity = sizeof(size)},
                        {.label = "Attributes (R H S D A)", .value = attributes, .capacity = sizeof(attributes)}};
  gui::dialog("Properties", "Information only. Edits in this dialog are not saved.", fields, 3, false);
  focus();
}

void command(Command action) {
  if (action == Open) open_selection();
  else if (action == Up) { char path[PathCapacity]{}; copy(path, sizeof(path), app.path); parent(path); load(path); }
  else if (action == Location) {
    char path[PathCapacity]{};
    copy(path, sizeof(path), app.path);
    gui::Field field{.label = "Absolute path", .value = path, .capacity = sizeof(path)};
    if (gui::dialog("Go to directory", "UnDOS paths start with / (no DOS drive letters).", &field, 1)) load(path);
    focus();
  } else if (action == Refresh) { reset_tree(); load(app.path); }
  else if (action == Properties) properties();
  else if (action == Help) message("File Manager",
      "WINFILE-style file management for UnDOS.\n"
      "Enter: open/run   Backspace: parent   Tab: switch pane\n"
      "Arrows: select   Page Up/Down: page/scroll   F1: help\n"
      "F2: rename   F3: path   F4: properties   F5: refresh\n"
      "F6: copy   F7: move   F8: new folder   Delete: delete\n"
      "Tree: +/- or double-click expands/collapses a branch.\n"
      "8.3 names; empty-folder deletion; no recursive operations.\n"
      "Operations change the disk. There is no undo.");
  else if (action == Exit) app.running = false;
  else operate(action);
  gui::invalidate(app.hwnd);
}

void text(gui::Paint &paint, int x, int y, const char *value, int width,
    uint32_t color = gui::Ink, uint32_t background = gui::Paper) {
  if (width < gui::CellWidth) return;
  char clipped[256]{};
  size_t count = static_cast<size_t>(width / gui::CellWidth);
  if (count > sizeof(clipped) - 1) count = sizeof(clipped) - 1;
  const size_t n = length(value);
  if (count > n) count = n;
  memcpy(clipped, value, count);
  if (n > count && count >= 3) clipped[count - 3] = clipped[count - 2] = clipped[count - 1] = '.';
  paint.text(x, y, clipped, color, background);
}

void paint() {
  gui::Paint p(app.hwnd);
  if (!p) return;
  const Rect area = gui::client(app.hwnd);
  const int right = area.right, bottom = area.bottom;
  p.fill(0, 0, right, bottom, gui::Face);
  p.text(8, 4, "File   View   Help", gui::Ink);
  for (int i = 0; i < 9; ++i) p.button(4 + i * 72, ToolbarTop, 70, Toolbar[i]);
  p.fill(4, 54, right - 4, 78, gui::Paper);
  text(p, 10, 58, app.path, right - 24);
  p.text(8, 84, "[/] Directory tree", app.treeFocus ? gui::Highlight : gui::Ink);
  p.text(TreeWidth + 10, 84, "Name", !app.treeFocus ? gui::Highlight : gui::Ink);
  text(p, right - 186, 84, "Bytes", 104, gui::Ink, gui::Face);
  text(p, right - 76, 84, "Attr", 40, gui::Ink, gui::Face);
  p.fill(4, ListTop, TreeWidth - 2, bottom - 54, gui::Paper);
  p.fill(TreeWidth + 2, ListTop, right - 4, bottom - 54, gui::Paper);
  for (int row = 0; row < visible_rows(); ++row) {
    const int y = ListTop + row * RowHeight;
    const int nodeIndex = app.treeScroll + row;
    if (nodeIndex < app.treeCount) {
      const auto &node = app.tree[nodeIndex];
      const bool selected = app.treeFocus && nodeIndex == app.treeSelection;
      const auto bg = selected ? gui::Highlight : gui::Paper;
      p.fill(5, y, TreeWidth - 3, y + RowHeight, bg);
      const int indent = node.depth < 8 ? node.depth * 14 : 112;
      p.text(8 + indent, y + 2, node.expanded ? "-" : "+", selected ? gui::Paper : gui::Ink, bg);
      text(p, 22 + indent, y + 2, nodeIndex ? basename(node.path) : "/", TreeWidth - 44 - indent,
           selected ? gui::Paper : gui::Ink, bg);
    }
    const int index = app.scroll + row;
    if (index >= app.count) continue;
    const auto &entry = app.entries[index];
    const bool selected = !app.treeFocus && index == app.selection;
    const auto bg = selected ? gui::Highlight : gui::Paper;
    const auto fg = selected ? gui::Paper : gui::Ink;
    p.fill(TreeWidth + 3, y, right - 21, y + RowHeight, bg);
    p.text(TreeWidth + 8, y + 2, entry.directory() ? "+" : " ", fg, bg);
    text(p, TreeWidth + 24, y + 2, entry.name, right - TreeWidth - 220, fg, bg);
    char number[24]{};
    if (entry.directory()) copy(number, sizeof(number), "<DIR>");
    else decimal(entry.size, number);
    text(p, right - 186, y + 2, number, 104, fg, bg);
    char attributes[] = "----";
    if (entry.attributes & 1) attributes[0] = 'R';
    if (entry.attributes & 2) attributes[1] = 'H';
    if (entry.attributes & 4) attributes[2] = 'S';
    if (entry.attributes & 32) attributes[3] = 'A';
    text(p, right - 76, y + 2, attributes, 40, fg, bg);
  }
  const int bars[] = {TreeWidth - 18, right - 20};
  const int counts[] = {app.treeCount, app.count};
  const int offsets[] = {app.treeScroll, app.scroll};
  for (int i = 0; i < 2; ++i) {
    p.fill(bars[i], ListTop, bars[i] + 16, bottom - 54, gui::Face);
    p.text(bars[i] + 4, ListTop + 2, "^");
    p.text(bars[i] + 4, bottom - 72, "v");
    const int track = bottom - 54 - ListTop - 48;
    if (track > 0 && counts[i] > visible_rows()) {
      const int thumb = ListTop + 20 + offsets[i] * track / (counts[i] - visible_rows());
      p.fill(bars[i] + 2, thumb, bars[i] + 14, thumb + 8, 8);
    }
  }
  p.button(4, bottom - 50, 64, "Prev");
  p.button(72, bottom - 50, 64, "Next");
  p.button(144, bottom - 50, 64, "Props");
  char page[24]{};
  decimal(static_cast<uint64_t>(app.page) + 1, page);
  p.text(222, bottom - 46, "Page");
  p.text(266, bottom - 46, page);
  p.text(330, bottom - 46, app.nextPage ? "(more entries)" : "(last page)");
  text(p, 8, bottom - 20, app.status, right - 16, gui::Ink, gui::Face);
}

void page(bool next) {
  if (next && app.nextPage) load(app.path, app.page + 1);
  else if (!next && app.page) load(app.path, app.page - 1);
}

struct Menu {
  uint32_t hwnd{0};
  const char *const *labels{nullptr};
  int count{0};
  int selection{0};
  int result{-1};
  bool done{false};
};
Menu *activeMenu{nullptr};

struct Notice {
  uint32_t hwnd{0};
  const char *text{nullptr};
  bool done{false};
};
Notice *activeNotice{nullptr};

int notice_lines(const char *value, gui::Paint *paint = nullptr) {
  int row = 0;
  while (*value) {
    char line[69]{};
    int n = 0;
    while (*value && *value != '\n' && n < 68) line[n++] = *value++;
    if (*value == '\n') ++value;
    if (paint) paint->text(12, 12 + row * 18, line);
    ++row;
  }
  return row;
}

void notice_procedure(const Msg &msg) {
  if (!activeNotice || msg.hwnd != activeNotice->hwnd) return;
  auto &state = *activeNotice;
  if (msg.message == WindowMessage_Paint) {
    gui::Paint p(state.hwnd);
    const int rows = notice_lines(state.text, &p);
    p.button(244, rows * 18 + 24, 80, "OK", true);
  } else if (msg.message == WindowMessage_Close ||
             (msg.message == WindowMessage_KeyDown && (msg.wParam == 13 || msg.wParam == 27))) state.done = true;
  else if (msg.message == WindowMessage_LButtonDown) {
    const int y = notice_lines(state.text) * 18 + 24;
    if (gui::inside(gui::mouse_x(msg), gui::mouse_y(msg), 244, y, 324, y + 24)) state.done = true;
  }
}

void notice(const char *title, const char *value) {
  Notice state;
  state.text = value;
  state.hwnd = gui::window("FileManagerNotice", title, 60, 65, 576, notice_lines(value) * 18 + 92);
  if (!state.hwnd) return;
  activeNotice = &state;
  gui::check(UD_SYS_SetFocusV1(state.hwnd), "Focus notice");
  Msg msg{};
  while (!state.done && gui::next(msg)) {
    if (msg.hwnd == state.hwnd || msg.message == WindowMessage_Paint) gui::dispatch(msg);
    else if (msg.message == WindowMessage_KeyDown) { msg.hwnd = state.hwnd; gui::dispatch(msg); }
  }
  gui::destroy(state.hwnd);
  activeNotice = nullptr;
}

void menu_procedure(const Msg &msg) {
  if (!activeMenu || msg.hwnd != activeMenu->hwnd) return;
  auto &menu = *activeMenu;
  if (msg.message == WindowMessage_Paint) {
    gui::Paint p(menu.hwnd);
    for (int i = 0; i < menu.count; ++i) {
      const bool selected = i == menu.selection;
      const auto bg = selected ? gui::Highlight : gui::Paper;
      p.fill(1, i * 22 + 1, 249, (i + 1) * 22 + 1, bg);
      p.text(8, i * 22 + 4, menu.labels[i], selected ? gui::Paper : gui::Ink, bg);
    }
  } else if (msg.message == WindowMessage_Close) menu.done = true;
  else if (msg.message == WindowMessage_KeyDown) {
    if (msg.wParam == 27) menu.done = true;
    else if (msg.wParam == 38 || msg.wParam == 40) {
      menu.selection = (menu.selection + (msg.wParam == 38 ? menu.count - 1 : 1)) % menu.count;
      gui::invalidate(menu.hwnd);
    } else if (msg.wParam == 13) { menu.result = menu.selection; menu.done = true; }
  } else if (msg.message == WindowMessage_LButtonDown) {
    const int index = (gui::mouse_y(msg) - 1) / 22;
    if (index >= 0 && index < menu.count) { menu.result = index; menu.done = true; }
  } else if (msg.message == WindowMessage_MouseMove) {
    const int index = (gui::mouse_y(msg) - 1) / 22;
    if (index >= 0 && index < menu.count && index != menu.selection) {
      menu.selection = index;
      gui::invalidate(menu.hwnd);
    }
  } else if (msg.message == WindowMessage_Activate && !msg.wParam) menu.done = true;
}

int menu(int left, const char *const *labels, int count) {
  Rect frame{};
  if (!gui::check(UD_SYS_GetWindowRectV1(app.hwnd, &frame), "Get menu anchor")) return -1;
  const Rect area = gui::client(app.hwnd);
  const int border = (frame.right - frame.left - area.right) / 2;
  const int clientX = frame.left + border;
  const int clientY = frame.bottom - area.bottom - border;
  Menu popup;
  popup.labels = labels;
  popup.count = count;
  activeMenu = &popup;
  if (!gui::check(UD_SYS_CreateWindowV1("FileManagerMenu", "",
      static_cast<WindowStyle>(WindowStyle_Visible | WindowStyle_Border),
      clientX + left, clientY + 22, 252, count * 22 + 6, 0, 0, &popup.hwnd), "Create menu")) {
    activeMenu = nullptr;
    return -1;
  }
  if (popup.hwnd) {
    gui::check(UD_SYS_SetFocusV1(popup.hwnd), "Focus menu");
    Msg msg{};
    while (!popup.done && gui::next(msg)) {
      if (msg.hwnd == popup.hwnd || msg.message == WindowMessage_Paint) gui::dispatch(msg);
      else if (msg.message == WindowMessage_KeyDown || msg.message == WindowMessage_Char) {
        msg.hwnd = popup.hwnd;
        menu_procedure(msg);
      }
    }

    gui::destroy(popup.hwnd);
  }
  activeMenu = nullptr;
  focus();
  return popup.result;
}

void show_menu(bool view) {
  if (!view) {
    constexpr const char *labels[] = {"Open / Run      Enter", "Copy               F6", "Move               F7",
        "Rename             F2", "Delete            Del", "Create directory   F8", "Properties         F4", "Exit"};
    constexpr Command choices[] = {Open, Copy, Move, Rename, Delete, NewFolder, Properties, Exit};
    const int selected = menu(0, labels, 8);
    if (selected >= 0) command(choices[selected]);
  } else {
    constexpr const char *labels[] = {"Sort page by name", "Sort page by size", "Reverse order", "Go to path         F3", "Refresh            F5"};
    const int selected = menu(56, labels, 5);
    if (selected == 3) command(Location);
    else if (selected == 4) command(Refresh);
    else if (selected >= 0) {
      if (selected == 2) app.reverse = !app.reverse;
      else app.sortSize = selected == 1;
      sort(app.entries, app.count, app.sortSize, app.reverse);
      app.selection = app.count ? 0 : -1;
      copy(app.status, sizeof(app.status), app.sortSize ? "Current page sorted by size; directories first" : "Current page sorted by name; directories first");
    }
  }
}

void procedure(const Msg &msg) {
  if (msg.message == WindowMessage_Close) app.running = false;
  else if (msg.message == WindowMessage_Paint) paint();
  else if (msg.message == WindowMessage_Size) { ensure_visible(); gui::invalidate(app.hwnd); }
  else if (msg.message == WindowMessage_KeyDown) {
    const auto key = msg.wParam;
    if (key == 9) app.treeFocus = !app.treeFocus;
    else if (key == 13) command(Open);
    else if (key == 8) command(Up);
    else if (key == 46) command(Delete);
    else if (key == 121) show_menu(false);
    else if (key >= 112 && key <= 119) {
      constexpr Command keys[] = {Help, Rename, Location, Properties, Refresh, Copy, Move, NewFolder};
      command(keys[key - 112]);
    } else if (key == 38 || key == 40 || key == 36 || key == 35) {
      int &selected = app.treeFocus ? app.treeSelection : app.selection;
      const int count = app.treeFocus ? app.treeCount : app.count;
      if (count) {
        if (key == 36) selected = 0;
        else if (key == 35) selected = count - 1;
        else if (key == 38 && selected > 0) --selected;
        else if (key == 40 && selected + 1 < count) ++selected;
      }
    } else if (key == 33 || key == 34) {
      if (!app.treeFocus) page(key == 34);
      else {
        app.treeSelection += key == 34 ? visible_rows() : -visible_rows();
        if (app.treeSelection < 0) app.treeSelection = 0;
        if (app.treeSelection >= app.treeCount) app.treeSelection = app.treeCount - 1;
      }
    } else if (app.treeFocus && (key == 37 || key == 39 || key == 107 || key == 109)) {
      const bool expand = key == 39 || key == 107;
      if (app.tree[app.treeSelection].expanded != expand) expand_tree(app.treeSelection);
    }
    ensure_visible();
    gui::invalidate(app.hwnd);
  } else if (msg.message == WindowMessage_LButtonDown || msg.message == WindowMessage_LButtonDblClk) {
    const int x = gui::mouse_x(msg), y = gui::mouse_y(msg);
    const Rect area = gui::client(app.hwnd);
    if (y < 22) {
      if (x < 56) {
        show_menu(false);
      } else if (x < 112) {
        show_menu(true);
      } else if (x < 170) command(Help);
    } else if (y >= ToolbarTop && y < ToolbarTop + 24 && x >= 4 && x < 4 + 9 * 72) {
      command(static_cast<Command>((x - 4) / 72));
    } else if (y >= 80 && y < ListTop && x > TreeWidth) {
      app.reverse = !app.reverse;
      app.sortSize = x >= area.right - 170;
      sort(app.entries, app.count, app.sortSize, app.reverse);
      app.selection = app.count ? 0 : -1;
    } else if (y >= ListTop && y < area.bottom - 54 && x >= 4 && x < area.right - 4) {
      const int row = (y - ListTop) / RowHeight;
      const bool treeBar = x >= TreeWidth - 18 && x < TreeWidth - 2;
      const bool fileBar = x >= area.right - 20;
      if (treeBar || fileBar) {
        app.treeFocus = treeBar;
        int &selected = treeBar ? app.treeSelection : app.selection;
        const int count = treeBar ? app.treeCount : app.count;
        const bool down = y >= area.bottom - 74 || (y >= ListTop + 20 && y > ListTop + visible_rows() * RowHeight / 2);
        const int step = y < ListTop + 20 || y >= area.bottom - 74 ? 1 : visible_rows();
        selected += down ? step : -step;
        if (selected >= count) selected = count - 1;
        if (selected < 0 && count) selected = 0;
      } else if (x < TreeWidth) {
        const int index = app.treeScroll + row;
        if (index < app.treeCount) {
          app.treeFocus = true;
          app.treeSelection = index;
          if (msg.message == WindowMessage_LButtonDblClk) { expand_tree(index); navigate_tree(); }
          else if (x < 22 + app.tree[index].depth * 14) expand_tree(index);
          else navigate_tree();
        }
      } else {
        const int index = app.scroll + row;
        if (index < app.count) {
          app.treeFocus = false;
          app.selection = index;
          if (msg.message == WindowMessage_LButtonDblClk) open_selection();
        }
      }
    } else if (y >= area.bottom - 50 && y < area.bottom - 26) {
      if (x >= 4 && x < 68) page(false);
      else if (x >= 72 && x < 136) page(true);
      else if (x >= 144 && x < 208) command(Properties);
    }
    ensure_visible();
    gui::invalidate(app.hwnd);
  }
}
} // namespace

extern "C" int main(int argc, char **argv) {
  if (!gui::register_class("FileManager", procedure) || !gui::register_class("FileManagerMenu", menu_procedure) ||
      !gui::register_class("FileManagerNotice", notice_procedure)) return 1;
  app.hwnd = gui::window("FileManager", "File Manager", 30, 30, 680, 430);
  if (!app.hwnd) return 1;
  reset_tree();
  if (!load(argc > 1 && argv ? argv[1] : "/")) copy(app.status, sizeof(app.status), "Cannot load directory. Use Path or Refresh to retry.");
  else expand_tree(0);
  focus();
  Msg msg{};
  while (app.running && gui::next(msg)) gui::dispatch(msg);
  return gui::destroy(app.hwnd) ? 0 : 1;
}
