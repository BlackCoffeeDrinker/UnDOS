#include <undosgui/gui.hpp>
#include <undoscrt/stdio.h>
#include <undoscrt/string.h>
#include "model.hpp"

namespace {
using namespace progman;

struct GroupView {
  uint32_t hwnd{0};
  int selection{0};
};

struct Shell {
  Model model;
  Model candidate;
  char configuration[12289]{};
  GroupView views[MaxGroups];
  uint32_t frame{0};
  uint32_t workspace{0};
  uint32_t menu{0};
  int active{-1};
  bool running{true};
  uint32_t reflowGeneration{0};
};
Shell shell;

bool group_grid(int width, int height, uint32_t group, Grid &grid) {
  gui::TextMetrics metrics{};
  if (!gui::system_font().metrics(metrics)) return false;
  uint32_t labelHeight = 0;
  uint32_t labelWidth = 0;
  const gui::TextLayout layout(gui::system_font());
  const int count = shell.model.count(group);
  for (int ordinal = 0; ordinal < count; ++ordinal) {
    const int itemIndex = shell.model.item_at(group, ordinal);
    if (itemIndex < 0) return false;
    gui::TextExtent extent{};
    if (!layout.measure(shell.model.items[itemIndex].title, extent)) return false;
    if (extent.height > labelHeight) labelHeight = extent.height;
    if (extent.width > labelWidth) labelWidth = extent.width;
  }
  if (!labelHeight) labelHeight = metrics.lineHeight;
  if (labelHeight > static_cast<uint32_t>(INT32_MAX - 50) ||
      labelWidth > static_cast<uint32_t>(INT32_MAX - LabelPadding)) return false;
  grid = Grid::for_size(width, height, static_cast<int>(labelHeight), static_cast<int>(labelWidth));
  return true;
}

void refresh_title() {
  gui::check(UD_SYS_SetWindowTextV1(shell.frame, "Program Manager"), "SetWindowText");
}

enum Command {
  NewGroup = 1, NewProgram, Properties, DeleteProgram, DeleteGroup, Run, Exit,
  ArrangeIcons, Cascade, Tile, About, GroupProperties, OpenGroupBase = 100
};

bool append_menu(uint32_t menu, MenuItemFlags flags, uint32_t item, const char *text) {
  return gui::check(UD_SYS_AppendMenuV1(menu, flags, item, text), "AppendMenu");
}

uint32_t create_menu(bool popup) {
  uint32_t menu = 0;
  if (!gui::check(UD_SYS_CreateMenuV1(popup, &menu), "CreateMenu")) return 0;
  return menu;
}

bool append_command(uint32_t menu, uint32_t command, const char *text) {
  return append_menu(menu, MenuItemFlags_String, command, text);
}

bool append_popup(uint32_t root, const char *text, uint32_t &popup) {
  popup = create_menu(true);
  if (!popup) return false;
  if (append_menu(root, MenuItemFlags_Popup, popup, text)) return true;
  gui::check(UD_SYS_DestroyMenuV1(popup), "DestroyMenu");
  popup = 0;
  return false;
}

bool rebuild_menu() {
  if (!shell.frame) return true;
  const uint32_t root = create_menu(false);
  if (!root) return false;
  uint32_t file = 0, options = 0, window = 0, help = 0;
  bool okay = append_popup(root, "File", file) &&
              append_popup(root, "Options", options) &&
              append_popup(root, "Window", window) &&
              append_popup(root, "Help", help);
  okay = okay && append_command(file, NewGroup, "New Group...");
  okay = okay && append_command(file, NewProgram, "New Program...");
  okay = okay && append_command(file, Properties, "Program Properties...");
  okay = okay && append_command(file, GroupProperties, "Group Properties...");
  okay = okay && append_command(file, DeleteProgram, "Delete Program...");
  okay = okay && append_command(file, DeleteGroup, "Delete Group...");
  okay = okay && append_command(file, Run, "Run...");
  okay = okay && append_command(file, Exit, "Exit");
  okay = okay && append_command(options, ArrangeIcons, "Arrange Icons");
  okay = okay && append_command(options, About, "Session settings only");
  okay = okay && append_command(window, Cascade, "Cascade");
  okay = okay && append_command(window, Tile, "Tile");
  for (unsigned i = 0; okay && i < MaxGroups; ++i) {
    if (shell.model.groups[i].id)
      okay = append_command(window, OpenGroupBase + i, shell.model.groups[i].title);
  }
  okay = okay && append_command(help, About, "About Program Manager...");
  if (!okay || !gui::check(UD_SYS_SetMenuV1(shell.frame, root), "SetMenu")) {
    gui::check(UD_SYS_DestroyMenuV1(root), "DestroyMenu");
    return false;
  }
  if (shell.menu) gui::check(UD_SYS_DestroyMenuV1(shell.menu), "DestroyMenu");
  shell.menu = root;
  return true;
}

void refresh_groups() {
  for (const auto &v : shell.views) if (v.hwnd) gui::invalidate(v.hwnd);
  refresh_title();
  rebuild_menu();
}

void focus(uint32_t hwnd) {
  if (hwnd) gui::check(UD_SYS_SetFocusV1(hwnd), "SetFocus");
}

bool launch(const char *path, const char *arguments) {
  crt_printf("progman: launching %s\n", path);
  HANDLE process = 0;
  const auto result = UD_SYS_CreateProcessV1(path, arguments, ProcessFlags_None, &process);
  if (!gui::check(result, "CreateProcess")) {
    gui::message("Cannot run program", "Executable or its loader failed. See serial log.");
    return false;
  }
  gui::check(UD_SYS_CloseV1(process), "Close process");
  return true;
}

bool ensure_group_window(unsigned index) {
  if (index >= MaxGroups || !shell.model.groups[index].id) return false;
  auto &view = shell.views[index];
  if (!view.hwnd) {
    view.hwnd = gui::window("ProgramGroup", shell.model.groups[index].title,
                            12 + static_cast<int>(index) * 28,
                            8 + static_cast<int>(index) * 28,
                            390, 240, shell.workspace, index);
  }
  return view.hwnd != 0;
}

int selected_item() {
  if (shell.active < 0) return -1;
  const auto i = static_cast<unsigned>(shell.active);
  return shell.model.item_at(shell.model.groups[i].id, shell.views[i].selection);
}

void open_group(unsigned index) {
  if (index >= MaxGroups || !shell.model.groups[index].id) return;
  auto &v = shell.views[index];
  const bool existed = v.hwnd != 0;
  if (!ensure_group_window(index)) return;
  if (existed) gui::show(v.hwnd, ShowCommand_Restore);
  if (v.hwnd) {
    shell.active = static_cast<int>(index);
    focus(v.hwnd);
  }
}

void launch_selection() {
  const int i = selected_item();
  if (i >= 0) launch(shell.model.items[i].path, shell.model.items[i].arguments);
}

void edit_group(bool create) {
  if (!create && shell.active < 0) return;
  char name[LabelLength]{};
  if (!create) copy(name, sizeof(name), shell.model.groups[shell.active].title);
  gui::Field field{.label = "Group name", .value = name, .capacity = sizeof(name)};
  if (!gui::dialog(create ? "New group" : "Group properties", "Changes last for this session only.",
                    &field, 1, true)) return;
  if (!name[0]) { gui::message("Invalid group", "A group must have a name."); return; }
  if (create) {
    const int index = shell.model.add_group(name);
    if (index < 0) gui::message("Cannot create group", "Group limit reached or invalid name.");
    else {
      crt_printf("progman: created group %u\n", shell.model.groups[index].id);
      open_group(static_cast<unsigned>(index));
    }
  } else {
    if (!copy(shell.model.groups[shell.active].title, LabelLength, name)) {
      gui::message("Invalid group", "The name contains a reserved character.");
      return;
    }
    shell.model.modified = true;
    gui::check(UD_SYS_SetWindowTextV1(shell.views[shell.active].hwnd, name), "SetWindowText");
  }
  refresh_groups();
}

void edit_program(bool create) {
  if (shell.active < 0) { gui::message("No group", "Open a group using the Window menu first."); return; }
  const int index = selected_item();
  if (!create && index < 0) return;
  Item item{};
  if (!create) item = shell.model.items[index];
  gui::Field fields[] = {{.label = "Description", .value = item.title, .capacity = sizeof(item.title)},
                         {.label = "Executable (absolute path)", .value = item.path, .capacity = sizeof(item.path)},
                         {.label = "Arguments", .value = item.arguments, .capacity = sizeof(item.arguments)}};
  if (!gui::dialog(create ? "New program" : "Program properties",
                   "Changes last for this session only.", fields, 3, true)) return;
  if (!item.title[0] || item.path[0] != '/' ||
      strchr(item.title, '|') || strchr(item.path, '|') || strchr(item.arguments, '|')) {
    gui::message("Invalid program", "Enter a name and absolute path; no | characters.");
    return;
  }
  if (create) {
    const auto group = shell.model.groups[shell.active].id;
    const int added = shell.model.add_item(group, item.title, item.path, item.arguments, 1);
    if (added < 0) gui::message("Cannot create program", "Program limit reached or invalid fields.");
    else {
      crt_printf("progman: created program %u\n", shell.model.items[added].id);
      shell.views[shell.active].selection = shell.model.count(group) - 1;
    }
  } else {
    shell.model.items[index] = item;
    shell.model.modified = true;
  }
  refresh_groups();
}

void remove_selection() {
  const int index = selected_item();
  if (index < 0) return;
  if (!gui::dialog("Delete program", "Remove the selected program from this session?", nullptr, 0,
                    true)) return;
  shell.model.items[index] = Item{};
  shell.model.modified = true;
  auto &v = shell.views[shell.active];
  if (v.selection) --v.selection;
  refresh_groups();
}

void remove_group() {
  if (shell.active < 0) return;
  if (!gui::dialog("Delete group", "Remove this group AND all its program entries?", nullptr, 0,
                    true)) return;
  const int index = shell.active;
  if (shell.views[index].hwnd) gui::destroy(shell.views[index].hwnd);
  shell.views[index] = GroupView{};
  shell.model.remove_group(shell.model.groups[index].id);
  shell.active = -1;
  refresh_groups();
}

bool run_dialog() {
  char path[PathLength]{}, arguments[ArgumentLength]{};
  gui::Field fields[] = {{.label = "Executable (absolute path)", .value = path, .capacity = sizeof(path)},
                         {.label = "Arguments", .value = arguments, .capacity = sizeof(arguments)}};
  const bool accepted = gui::dialog("Run", "Run a native UnDOS ELF program.", fields, 2, true);
  crt_printf("run dialog accepted=%u path='%s' args='%s'\n",
             accepted ? 1u : 0u, path, arguments);
  if (accepted) {
    if (path[0] != '/') gui::message("Invalid path", "An absolute executable path is required.");
    else return launch(path, arguments);
  }
  return false;
}

void arrange_windows(bool tile) {
  const Rect client = gui::client(shell.workspace);
  int count = 0;
  for (const auto &v : shell.views) if (v.hwnd) ++count;
  if (!count) return;
  int ordinal = 0;
  for (const auto &v : shell.views) {
    if (!v.hwnd) continue;
    gui::show(v.hwnd, ShowCommand_Restore);
    int width = client.right - 16;
    int height = client.bottom - 16;
    if (tile) {
      const int columns = count > 1 ? 2 : 1;
      const int rows = (count + columns - 1) / columns;
      width = (client.right - 12) / columns;
      height = (client.bottom - 12) / rows;
      gui::move(v.hwnd, 6 + (ordinal % columns) * width, 6 + (ordinal / columns) * height, width, height);
    } else {
      width -= 24 * (count - 1);
      height -= 24 * (count - 1);
      if (width < 150) width = 150;
      if (height < 100) height = 100;
      gui::move(v.hwnd, 8 + ordinal * 24, 8 + ordinal * 24, width, height);
    }
    ++ordinal;
  }
}

void execute(int command) {
  switch (command) {
    case NewGroup: edit_group(true); break;
    case NewProgram: edit_program(true); break;
    case Properties: edit_program(false); break;
    case GroupProperties: edit_group(false); break;
    case DeleteProgram: remove_selection(); break;
    case DeleteGroup: remove_group(); break;
    case Run: if (run_dialog()) return; break;
    case Exit:
      if (!shell.model.modified || gui::dialog("Exit Program Manager", "Discard unsaved session changes and exit?",
                                               nullptr, 0, true))
        shell.running = false;
      break;
    case ArrangeIcons: if (shell.active >= 0) gui::invalidate(shell.views[shell.active].hwnd); break;
    case Cascade: arrange_windows(false); break;
    case Tile: arrange_windows(true); break;
    case About:
      gui::message("About Program Manager", "UnDOS native shell. Changes are session-only.");
      break;
    default:
      if (command >= OpenGroupBase && command < OpenGroupBase + static_cast<int>(MaxGroups))
        open_group(static_cast<unsigned>(command - OpenGroupBase));
      break;
  }
  if (shell.active >= 0 && shell.views[shell.active].hwnd) focus(shell.views[shell.active].hwnd);
  else focus(shell.frame);
}

void keyboard(const Msg &msg, bool inGroup) {
  if (msg.wParam == 112) { execute(About); return; }
  if (!inGroup) {
    if (msg.wParam == 9 || msg.wParam == 13) {
      for (unsigned i = 0; i < MaxGroups; ++i)
        if (shell.model.groups[i].id) { open_group(i); break; }
    }
    return;
  }
  const unsigned index = gui::data(msg.hwnd);
  if (index >= MaxGroups) return;
  shell.active = static_cast<int>(index);
  auto &v = shell.views[index];
  const auto group = shell.model.groups[index].id;
  const auto area = gui::client(msg.hwnd);
  Grid grid{};
  if (!group_grid(area.right, area.bottom, group, grid)) return;
  const int count = shell.model.count(group);
  if (msg.wParam == 13) { launch_selection(); return; }
  if (msg.wParam == 46) { remove_selection(); return; }
  if (msg.wParam == 9) {
    for (unsigned step = 1; step <= MaxGroups; ++step) {
      const unsigned next = (index + step) % MaxGroups;
      if (shell.model.groups[next].id) { open_group(next); return; }
    }
  }
  int selected = v.selection;
  if (msg.wParam == 37) --selected;
  else if (msg.wParam == 39) ++selected;
  else if (msg.wParam == 38) selected -= grid.columns;
  else if (msg.wParam == 40) selected += grid.columns;
  else if (msg.wParam == 36) selected = 0;
  else if (msg.wParam == 35) selected = count - 1;
  else if (msg.wParam == 33) selected -= grid.page_size;
  else if (msg.wParam == 34) selected += grid.page_size;
  if (selected < 0) selected = 0;
  if (selected >= count) selected = count > 0 ? count - 1 : 0;
  v.selection = selected;
  gui::invalidate(v.hwnd);
}

void group_proc(const Msg &msg) {
  const unsigned index = gui::data(msg.hwnd);
  if (index >= MaxGroups) return;
  auto &v = shell.views[index];
  const auto group = shell.model.groups[index].id;
  if (msg.message == WindowMessage_Activate && msg.wParam) shell.active = static_cast<int>(index);
  else if (msg.message == WindowMessage_Close) {
    gui::show(msg.hwnd, ShowCommand_Hide);
    if (shell.active == static_cast<int>(index)) shell.active = -1;
  } else if (msg.message == WindowMessage_Size) gui::invalidate(msg.hwnd);
  else if (msg.message == WindowMessage_FontChanged) {
    shell.reflowGeneration = msg.wParam;
    gui::invalidate(msg.hwnd);
  } else if (msg.message == WindowMessage_KeyDown) keyboard(msg, true);
  else if (msg.message == WindowMessage_Paint) {
    gui::Paint p(msg.hwnd);
    const Rect area = gui::client(msg.hwnd);
    Grid grid{};
    if (!group_grid(area.right, area.bottom, group, grid)) return;
    const int count = shell.model.count(group);
    const int start = grid.page_start(v.selection);
    for (int ordinal = start; ordinal < count && ordinal < start + grid.page_size; ++ordinal) {
      const int i = shell.model.item_at(group, ordinal);
      const auto &item = shell.model.items[i];
      const int x = (ordinal - start) % grid.columns * grid.cell_width;
      const int y = (ordinal - start) / grid.columns * grid.cell_height;
      const bool selected = ordinal == v.selection;
      p.icon(x + (grid.cell_width - 32) / 2, y + 10, item.icon, selected);
      char label[LabelLength]{};
      gui::TextExtent extent{};
      const gui::TextLayout layout(gui::system_font());
      if (!layout.fit_prefix(item.title, label, sizeof(label),
                             static_cast<uint32_t>(grid.cell_width - LabelPadding), extent)) continue;
      const int labelWidth = static_cast<int>(extent.width);
      const int left = x + (grid.cell_width - labelWidth) / 2;
      p.fill(left - 2, y + 46, left + labelWidth + 2,
              y + 50 + static_cast<int>(extent.height), selected ? gui::Highlight : gui::Paper);
      p.draw_text(left, y + 48, label, gui::FontStyle::Regular,
                  selected ? gui::Paper : gui::Ink, selected ? gui::Highlight : gui::Paper);
    }
  } else if (msg.message == WindowMessage_LButtonDown || msg.message == WindowMessage_LButtonDblClk) {
    shell.active = static_cast<int>(index);
    const auto area = gui::client(msg.hwnd);
    Grid grid{};
    if (!group_grid(area.right, area.bottom, group, grid)) return;
    const int x = gui::mouse_x(msg), y = gui::mouse_y(msg);
    if (x < 0 || y < 0 || y >= grid.rows * grid.cell_height || x >= grid.columns * grid.cell_width) return;
    const int ordinal = grid.page_start(v.selection) + (y / grid.cell_height) * grid.columns +
                        x / grid.cell_width;
    if (ordinal >= shell.model.count(group)) return;
    v.selection = ordinal;
    gui::invalidate(msg.hwnd);
    if (msg.message == WindowMessage_LButtonDblClk) launch_selection();
  }
}

void frame_proc(const Msg &msg) {
  if (msg.message == WindowMessage_Paint) {
    gui::Paint paint(msg.hwnd);
  } else if (msg.message == WindowMessage_FontChanged) shell.reflowGeneration = msg.wParam;
  else if (msg.message == WindowMessage_Command) execute(static_cast<int>(msg.wParam));
  else if (msg.message == WindowMessage_Close) execute(Exit);
  else if (msg.message == WindowMessage_KeyDown) keyboard(msg, false);
  else if (msg.message == WindowMessage_Size) {
    if (shell.workspace) {
      const Rect area = gui::client(msg.hwnd);
      if (area.right > 0 && area.bottom > 0)
        gui::move(shell.workspace, 0, 0, area.right, area.bottom);
    }
    gui::invalidate(msg.hwnd);
  }
}

void workspace_proc(const Msg &msg) {
  if (msg.message == WindowMessage_Paint) { gui::Paint paint(msg.hwnd); }
  else if (msg.message == WindowMessage_KeyDown) keyboard(msg, false);
}

bool load_configuration() {
  HANDLE file = 0;
  if (!gui::check(UD_SYS_OpenV1("/System/PROGMAN.INI", FileOpenMode_Read, &file), "Open PROGMAN.INI")) return false;
  uint32_t total = 0;
  bool readable = true;
  while (total < sizeof(shell.configuration) - 1) {
    uint32_t n = sizeof(shell.configuration) - 1 - total;
    if (n > 4096) n = 4096;
    if (!gui::check(UD_SYS_ReadV1(file, reinterpret_cast<uint8_t *>(shell.configuration + total), &n),
                    "Read configuration")) { readable = false; break; }
    total += n;
    if (!n) break;
  }
  if (readable && total == sizeof(shell.configuration) - 1) {
    uint8_t extra = 0;
    uint32_t n = 1;
    readable = gui::check(UD_SYS_ReadV1(file, &extra, &n), "Read configuration limit") && n == 0;
  }
  gui::check(UD_SYS_CloseV1(file), "Close configuration");
  if (!readable) {
    crt_puts("progman: configuration is unreadable or too large\n");
    return false;
  }
  shell.configuration[total] = 0;
  const char *error = parse(shell.candidate, shell.configuration);
  if (error) { crt_printf("progman: %s\n", error); return false; }
  shell.model = shell.candidate;
  return true;
}
} // namespace

extern "C" int main(int, char **) {
  gui::TextMetrics font{};
  if (!gui::system_font().metrics(font)) return 1;
  if (!gui::register_class("ProgramManager", frame_proc, 8) ||
      !gui::register_class("ProgramWorkspace", workspace_proc, 8) ||
      !gui::register_class("ProgramGroup", group_proc, gui::Paper)) return 1;
  shell.frame = gui::window("ProgramManager", "Program Manager", 30, 30, 600, 400);
  if (!shell.frame) return 1;
  if (!load_configuration()) {
    gui::message("Configuration error", "Cannot load PROGMAN.INI. Recovery session is empty.");
    shell.model.add_group("Recovery");
  }
  if (!rebuild_menu()) return 1;
  const Rect area = gui::client(shell.frame);
  if (!gui::check(UD_SYS_CreateWindowV1("ProgramWorkspace", "",
                                      static_cast<WindowStyle>(WindowStyle_Visible | WindowStyle_Child),
                                      0, 0, area.right, area.bottom,
                                      shell.frame, 0, &shell.workspace), "Create workspace")) return 1;
  if (!gui::show(shell.frame, ShowCommand_Maximize)) return 1;
  int first = -1;
  for (unsigned i = 0; i < MaxGroups; ++i) {
    if (!shell.model.groups[i].id) continue;
    if (first < 0) first = static_cast<int>(i);
    if (!ensure_group_window(i)) return 1;
    if (static_cast<int>(i) != first && shell.views[i].hwnd)
      gui::show(shell.views[i].hwnd, ShowCommand_Minimize);
  }
  if (first >= 0) open_group(static_cast<unsigned>(first));
  Msg msg{};
  (void) UD_SYS_PrintV1("progman: session ready (F10 opens menus, Enter launches)");
  while (shell.running && gui::next(msg)) {
    gui::dispatch(msg);
    // An empty queue means every font notification and resulting repaint has
    // been handled, so the session shows the new font throughout.
    Msg pending{};
    if (shell.reflowGeneration && UD_SYS_PeekMessageV1(&pending, 0) == SyscallReturnCode_TIMEOUT) {
      (void) UD_SYS_PrintV1("progman: font reflow complete");
      shell.reflowGeneration = 0;
    }
  }
  gui::destroy(shell.frame);
  if (shell.menu) gui::check(UD_SYS_DestroyMenuV1(shell.menu), "DestroyMenu");
  return 0;
}
