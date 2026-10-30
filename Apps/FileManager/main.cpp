#include <undosgui/gui.hpp>
#include <undoscrt/string.h>
#include "model.hpp"

namespace {
using namespace filemanager;
constexpr int ToolbarTop = 4;
constexpr int CompactToolbarWidth = 4 + 9 * 72;
constexpr int TreeWidth = 208;

enum Command {
  Open, Up, Location, Refresh, Copy, Move, Rename, Delete, NewFolder,
  Properties, Help, Exit, SortName, SortSize, Reverse
};
constexpr const char *Toolbar[] = {"Open", "Up", "Path", "Refresh", "Copy", "Move", "Rename", "Delete", "New dir"};
struct TreeNode {
  char path[PathCapacity]{};
  int depth{0};
  bool expanded{false};
};
struct State {
  uint32_t hwnd{0};
  uint32_t menu{0};
  bool running{true};
  char path[PathCapacity] = "/";
  char status[256] = "Ready";
  char readyMarker[PathCapacity + 40]{};
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

struct Layout {
  Rect area{};
  bool compactToolbar{false};
  int toolbarRows{1};
  int lineHeight{0};
  int rowHeight{0};
  int buttonHeight{0};
  int toolbarPitch{0};
  int pathTop{34};
  int pathBottom{58};
  int pathTextY{38};
  int headerY{64};
  int listTop{84};
  int listBottom{0};
  int footerTop{0};
  int footerTextY{0};
};

Layout layout() {
  Layout result{};
  result.area = gui::client(app.hwnd);
  result.compactToolbar = result.area.right < CompactToolbarWidth;
  gui::TextMetrics metrics{};
  if (gui::system_font().metrics(metrics)) result.lineHeight = static_cast<int>(metrics.lineHeight);
  result.rowHeight = result.lineHeight + 4;
  for (const char *label : Toolbar) {
    const int height = gui::button_height(label);
    if (height > result.buttonHeight) result.buttonHeight = height;
  }
  const char *const footerLabels[] = {"Prev", "Next", "Props"};
  for (const char *label : footerLabels) {
    const int height = gui::button_height(label);
    if (height > result.buttonHeight) result.buttonHeight = height;
  }
  result.toolbarPitch = result.buttonHeight + 4;
  if (result.compactToolbar) {
    result.toolbarRows = 2;
  }
  result.pathTop = ToolbarTop + result.toolbarRows * result.toolbarPitch;
  result.pathBottom = result.pathTop + result.lineHeight + 8;
  result.pathTextY = result.pathTop + 4;
  result.headerY = result.pathBottom + 4;
  result.listTop = result.headerY + result.lineHeight + 4;
  result.footerTop = result.area.bottom - (result.buttonHeight + 2 * result.lineHeight + 14);
  result.footerTextY = result.footerTop + result.buttonHeight + 4;
  result.listBottom = result.footerTop - 4;
  return result;
}

int visible_rows(const Layout &current) {
  if (current.rowHeight <= 0 || current.listBottom <= current.listTop) return 0;
  const int rows = (current.listBottom - current.listTop) / current.rowHeight;
  return rows > 0 ? rows : 1;
}

bool append_menu(uint32_t menu, MenuItemFlags flags, uint32_t item, const char *text) {
  return gui::check(UD_SYS_AppendMenuV1(menu, flags, item, text), "AppendMenu");
}

uint32_t create_menu(bool popup) {
  uint32_t menu = 0;
  if (!gui::check(UD_SYS_CreateMenuV1(popup, &menu), "CreateMenu")) return 0;
  return menu;
}

bool append_command(uint32_t menu, Command commandId, const char *text) {
  return append_menu(menu, MenuItemFlags_String, static_cast<uint32_t>(commandId), text);
}

bool append_popup(uint32_t root, const char *text, uint32_t &popup) {
  popup = create_menu(true);
  if (!popup) return false;
  if (append_menu(root, MenuItemFlags_Popup, popup, text)) return true;
  gui::check(UD_SYS_DestroyMenuV1(popup), "DestroyMenu");
  popup = 0;
  return false;
}

bool create_app_menu() {
  const uint32_t root = create_menu(false);
  if (!root) return false;
  uint32_t file = 0, view = 0;
  bool okay = append_popup(root, "File", file) &&
              append_popup(root, "View", view);
  okay = okay && append_command(file, Open, "Open / Run");
  okay = okay && append_command(file, Up, "Parent directory");
  okay = okay && append_command(file, Location, "Go to path...");
  okay = okay && append_command(file, Refresh, "Refresh");
  okay = okay && append_command(file, Copy, "Copy...");
  okay = okay && append_command(file, Move, "Move...");
  okay = okay && append_command(file, Rename, "Rename...");
  okay = okay && append_command(file, Delete, "Delete...");
  okay = okay && append_command(file, NewFolder, "Create directory...");
  okay = okay && append_command(file, Properties, "Properties");
  okay = okay && append_command(file, Exit, "Exit");
  okay = okay && append_command(view, SortName, "Sort page by name");
  okay = okay && append_command(view, SortSize, "Sort page by size");
  okay = okay && append_command(view, Reverse, "Reverse order");
  okay = okay && append_command(root, Help, "Help");
  if (!okay || !gui::check(UD_SYS_SetMenuV1(app.hwnd, root), "SetMenu")) {
    gui::check(UD_SYS_DestroyMenuV1(root), "DestroyMenu");
    return false;
  }
  app.menu = root;
  return true;
}

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
  copy(app.readyMarker, sizeof(app.readyMarker), "filemanager: directory ready path=");
  const size_t prefix = strlen(app.readyMarker);
  copy(app.readyMarker + prefix, sizeof(app.readyMarker) - prefix, app.path);
  gui::check(UD_SYS_SetWindowTextV1(app.hwnd, "File Manager"), "SetWindowText");
  gui::invalidate(app.hwnd);
  return true;
}

void ensure_visible() {
  const auto current = layout();
  const int rows = visible_rows(current);
  if (rows <= 0) return;
  int &scroll = app.treeFocus ? app.treeScroll : app.scroll;
  const int selected = app.treeFocus ? app.treeSelection : app.selection;
  const int count = app.treeFocus ? app.treeCount : app.count;
  if (selected < scroll) scroll = selected;
  if (selected >= scroll + rows) scroll = selected - rows + 1;
  if (scroll < 0) scroll = 0;
  const int maximum = count > rows ? count - rows : 0;
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
    if (!gui::dialog("Create directory", "Create a folder in the current directory.", &field, 1,
                      true)) { focus(); return; }
    if (!input[0] || strchr(input, '/') || !join(app.path, input, source)) {
      message("Invalid name", "Enter one short folder name."); return;
    }
  } else {
    if (!selected_path(source)) return;
    if (command == Delete) {
      if (!gui::dialog("Delete permanently", source, nullptr, 0, true)) { focus(); return; }
    } else {
      if (command == Rename) copy(input, sizeof(input), app.entries[app.selection].name);
      else copy(input, sizeof(input), source);
      gui::Field field{.label = command == Rename ? "New name (8.3)" : "Destination path", .value = input, .capacity = sizeof(input)};
      if (!gui::dialog(command == Copy ? "Copy file" : command == Move ? "Move file" : "Rename",
          "Existing destinations are never replaced. Same volume only.", &field, 1,
          true)) { focus(); return; }
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
  gui::dialog("Properties", "Information only. Edits in this dialog are not saved.", fields, 3,
               false);
  focus();
}

void command(Command action) {
  if (action == Open) open_selection();
  else if (action == Up) { char path[PathCapacity]{}; copy(path, sizeof(path), app.path); parent(path); load(path); }
  else if (action == Location) {
    char path[PathCapacity]{};
    copy(path, sizeof(path), app.path);
    gui::Field field{.label = "Absolute path", .value = path, .capacity = sizeof(path)};
    if (gui::dialog("Go to directory", "UnDOS paths start with / (no DOS drive letters).", &field, 1,
                     true)) load(path);
    focus();
  } else if (action == Refresh) { reset_tree(); load(app.path); }
  else if (action == SortName || action == SortSize || action == Reverse) {
    if (action == SortName) app.sortSize = false;
    else if (action == SortSize) app.sortSize = true;
    else app.reverse = !app.reverse;
    sort(app.entries, app.count, app.sortSize, app.reverse);
    app.selection = app.count ? 0 : -1;
    copy(app.status, sizeof(app.status), app.sortSize
        ? "Current page sorted by size; directories first"
        : "Current page sorted by name; directories first");
  }
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
  if (width <= 0) return;
  char clipped[256]{};
  gui::TextExtent extent{};
  const gui::TextLayout layout(gui::system_font());
  if (layout.fit_prefix(value, clipped, sizeof(clipped), static_cast<uint32_t>(width), extent))
    paint.draw_text(x, y, clipped, gui::FontStyle::Regular, color, background);
}

void paint() {
  gui::Paint p(app.hwnd);
  if (!p) return;
  const auto current = layout();
  if (current.lineHeight <= 0 || current.rowHeight <= 0) return;
  const Rect area = current.area;
  const int right = area.right, bottom = area.bottom;
  const int rowsVisible = visible_rows(current);
  p.fill(0, 0, right, bottom, gui::Face);
  for (int i = 0; i < 9; ++i) {
    const int buttonRow = current.compactToolbar ? i / 5 : 0;
    const int buttonColumn = current.compactToolbar ? i % 5 : i;
    p.button(4 + buttonColumn * 72, ToolbarTop + buttonRow * current.toolbarPitch, 70, Toolbar[i]);
  }
  p.fill(4, current.pathTop, right - 4, current.pathBottom, gui::Paper);
  text(p, 10, current.pathTextY, app.path, right - 24);
  p.draw_text(8, current.headerY, "[/] Directory tree", gui::FontStyle::Regular, app.treeFocus ? gui::Highlight : gui::Ink);
  p.draw_text(TreeWidth + 10, current.headerY, "Name", gui::FontStyle::Regular, !app.treeFocus ? gui::Highlight : gui::Ink);
  text(p, right - 186, current.headerY, "Bytes", 104, gui::Ink, gui::Face);
  text(p, right - 76, current.headerY, "Attr", 40, gui::Ink, gui::Face);
  p.fill(4, current.listTop, TreeWidth - 2, current.listBottom, gui::Paper);
  p.fill(TreeWidth + 2, current.listTop, right - 4, current.listBottom, gui::Paper);
  for (int row = 0; row < rowsVisible; ++row) {
    const int y = current.listTop + row * current.rowHeight;
    const int textY = y + (current.rowHeight - current.lineHeight) / 2;
    const int nodeIndex = app.treeScroll + row;
    if (nodeIndex < app.treeCount) {
      const auto &node = app.tree[nodeIndex];
      const bool selected = app.treeFocus && nodeIndex == app.treeSelection;
      const auto bg = selected ? gui::Highlight : gui::Paper;
      p.fill(5, y, TreeWidth - 3, y + current.rowHeight, bg);
      const int indent = node.depth < 8 ? node.depth * 14 : 112;
      p.draw_text(8 + indent, textY, node.expanded ? "-" : "+", gui::FontStyle::Regular, selected ? gui::Paper : gui::Ink, bg);
      text(p, 22 + indent, textY, nodeIndex ? basename(node.path) : "/", TreeWidth - 44 - indent,
           selected ? gui::Paper : gui::Ink, bg);
    }
    const int index = app.scroll + row;
    if (index >= app.count) continue;
    const auto &entry = app.entries[index];
    const bool selected = !app.treeFocus && index == app.selection;
    const auto bg = selected ? gui::Highlight : gui::Paper;
    const auto fg = selected ? gui::Paper : gui::Ink;
    p.fill(TreeWidth + 3, y, right - 21, y + current.rowHeight, bg);
    p.draw_text(TreeWidth + 8, textY, entry.directory() ? "+" : " ", gui::FontStyle::Regular, fg, bg);
    text(p, TreeWidth + 24, textY, entry.name, right - TreeWidth - 220, fg, bg);
    char number[24]{};
    if (entry.directory()) copy(number, sizeof(number), "<DIR>");
    else decimal(entry.size, number);
    text(p, right - 186, textY, number, 104, fg, bg);
    char attributes[] = "----";
    if (entry.attributes & 1) attributes[0] = 'R';
    if (entry.attributes & 2) attributes[1] = 'H';
    if (entry.attributes & 4) attributes[2] = 'S';
    if (entry.attributes & 32) attributes[3] = 'A';
    text(p, right - 76, textY, attributes, 40, fg, bg);
  }
  const int bars[] = {TreeWidth - 18, right - 20};
  const int counts[] = {app.treeCount, app.count};
  const int offsets[] = {app.treeScroll, app.scroll};
  for (int i = 0; i < 2; ++i) {
    p.fill(bars[i], current.listTop, bars[i] + 16, current.listBottom, gui::Face);
    p.draw_text(bars[i] + 4, current.listTop + 2, "^");
    p.draw_text(bars[i] + 4, current.listBottom - current.lineHeight - 2, "v");
    const int arrowHeight = current.lineHeight + 4;
    const int track = current.listBottom - current.listTop - 2 * arrowHeight;
    if (track > 0 && counts[i] > rowsVisible) {
      const int thumb = current.listTop + arrowHeight + offsets[i] * track / (counts[i] - rowsVisible);
      p.fill(bars[i] + 2, thumb, bars[i] + 14, thumb + current.rowHeight, 8);
    }
  }
  p.button(4, current.footerTop, 64, "Prev");
  p.button(72, current.footerTop, 64, "Next");
  p.button(144, current.footerTop, 64, "Props");
  char page[24]{};
  decimal(static_cast<uint64_t>(app.page) + 1, page);
  p.draw_text(222, current.footerTop + (current.buttonHeight - current.lineHeight) / 2, "Page");
  p.draw_text(266, current.footerTop + (current.buttonHeight - current.lineHeight) / 2, page);
  p.draw_text(330, current.footerTop + (current.buttonHeight - current.lineHeight) / 2,
              app.nextPage ? "(more entries)" : "(last page)");
  text(p, 8, current.footerTextY, app.status, right - 16, gui::Ink, gui::Face);
}

void page(bool next) {
  if (next && app.nextPage) load(app.path, app.page + 1);
  else if (!next && app.page) load(app.path, app.page - 1);
}

void notice(const char *title, const char *value) {
  gui::message(title, value);
}

void procedure(const Msg &msg) {
  if (msg.message == WindowMessage_Close) app.running = false;
  else if (msg.message == WindowMessage_FontChanged) { ensure_visible(); gui::invalidate(app.hwnd); }
  else if (msg.message == WindowMessage_Command && msg.wParam <= Reverse)
    command(static_cast<Command>(msg.wParam));
  else if (msg.message == WindowMessage_Paint) paint();
  else if (msg.message == WindowMessage_Size) { ensure_visible(); gui::invalidate(app.hwnd); }
  else if (msg.message == WindowMessage_KeyDown) {
    const auto current = layout();
    const int rowsVisible = visible_rows(current);
    if (current.rowHeight <= 0 || current.lineHeight <= 0) return;
    const auto key = msg.wParam;
    if (key == 9) app.treeFocus = !app.treeFocus;
    else if (key == 13) command(Open);
    else if (key == 8) command(Up);
    else if (key == 46) command(Delete);
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
        app.treeSelection += key == 34 ? rowsVisible : -rowsVisible;
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
    const auto current = layout();
    const Rect area = current.area;
    const int rowsVisible = visible_rows(current);
    if (current.rowHeight <= 0 || current.lineHeight <= 0) return;
    if (y >= ToolbarTop && y < ToolbarTop + current.toolbarRows * current.toolbarPitch && x >= 4) {
      const int buttonRow = (y - ToolbarTop) / current.toolbarPitch;
      const int buttonColumn = (x - 4) / 72;
      const int buttonsInRow = current.compactToolbar && buttonRow == 1 ? 4 :
                               current.compactToolbar ? 5 : 9;
      if (buttonColumn < buttonsInRow && x < 4 + buttonColumn * 72 + 70 &&
          y < ToolbarTop + buttonRow * current.toolbarPitch + current.buttonHeight) {
        const int index = current.compactToolbar ? buttonRow * 5 + buttonColumn : buttonColumn;
        command(static_cast<Command>(index));
      }
    } else if (y >= current.headerY - 4 && y < current.listTop && x > TreeWidth) {
      app.reverse = !app.reverse;
      app.sortSize = x >= area.right - 170;
      sort(app.entries, app.count, app.sortSize, app.reverse);
      app.selection = app.count ? 0 : -1;
    } else if (y >= current.listTop && y < current.listBottom && x >= 4 && x < area.right - 4) {
      const int row = (y - current.listTop) / current.rowHeight;
      const bool treeBar = x >= TreeWidth - 18 && x < TreeWidth - 2;
      const bool fileBar = x >= area.right - 20;
      if (treeBar || fileBar) {
        app.treeFocus = treeBar;
        int &selected = treeBar ? app.treeSelection : app.selection;
        const int count = treeBar ? app.treeCount : app.count;
        const int arrowHeight = current.lineHeight + 4;
        const bool down = y >= current.listBottom - arrowHeight ||
                          (y >= current.listTop + arrowHeight &&
                           y > current.listTop + rowsVisible * current.rowHeight / 2);
        const int step = y < current.listTop + arrowHeight ||
                                 y >= current.listBottom - arrowHeight
                             ? 1 : rowsVisible;
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
    } else if (y >= current.footerTop && y < current.footerTop + current.buttonHeight) {
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
  gui::TextMetrics font{};
  if (!gui::system_font().metrics(font) || !font.lineHeight) return 1;
  const int windowWidth = 680;
  const int initialRows = 12;
  int buttonHeight = 0;
  for (const char *label : Toolbar) {
    const int height = gui::button_height(label);
    if (height > buttonHeight) buttonHeight = height;
  }
  const char *const footerLabels[] = {"Prev", "Next", "Props"};
  for (const char *label : footerLabels) {
    const int height = gui::button_height(label);
    if (height > buttonHeight) buttonHeight = height;
  }
  const int64_t wantedHeight = 4 + 2 * (buttonHeight + 4) +
                               static_cast<int64_t>(initialRows + 4) * (font.lineHeight + 4) + 48;
  const int initialHeight = static_cast<int>(wantedHeight);
  if (!gui::register_class("FileManager", procedure)) return 1;
  app.hwnd = gui::window("FileManager", "File Manager", 30, 30, windowWidth, initialHeight);
  if (!app.hwnd) return 1;
  if (!create_app_menu()) return 1;
  reset_tree();
  if (!load(argc > 1 && argv ? argv[1] : "/")) copy(app.status, sizeof(app.status), "Cannot load directory. Use Path or Refresh to retry.");
  else expand_tree(0);
  focus();
  Msg msg{};
  while (app.running && gui::next(msg)) {
    gui::dispatch(msg);
    // Report a loaded directory once its repaint has drained from the queue.
    Msg pending{};
    if (app.readyMarker[0] && UD_SYS_PeekMessageV1(&pending, 0) == SyscallReturnCode_TIMEOUT) {
      (void) UD_SYS_PrintV1(app.readyMarker);
      app.readyMarker[0] = 0;
    }
  }
  const bool destroyed = gui::destroy(app.hwnd);
  const bool menuDestroyed = gui::check(UD_SYS_DestroyMenuV1(app.menu), "DestroyMenu");
  return destroyed && menuDestroyed ? 0 : 1;
}
