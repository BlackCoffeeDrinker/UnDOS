#include <undosgui/gui.hpp>
#include <undoscrt/stdio.h>
#include <undoscrt/string.h>
#include "model.hpp"

namespace {
using namespace progman;
constexpr int MenuHeight = 22;
constexpr int MenuLeft[] = {0, 56, 136, 208, 264};
constexpr const char *MenuLabels[] = {"File", "Options", "Window", "Help"};

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
  int active{-1};
  bool running{true};
};
Shell shell;

void refresh_title() {
  gui::check(UD_SYS_SetWindowTextV1(shell.frame, "Program Manager"), "SetWindowText");
}

void refresh_groups() {
  for (auto &v : shell.views) if (v.hwnd) gui::invalidate(v.hwnd);
  refresh_title();
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

int selected_item() {
  if (shell.active < 0) return -1;
  const auto i = static_cast<unsigned>(shell.active);
  return shell.model.item_at(shell.model.groups[i].id, shell.views[i].selection);
}

void open_group(unsigned index) {
  if (index >= MaxGroups || !shell.model.groups[index].id) return;
  auto &v = shell.views[index];
  if (!v.hwnd) {
    v.hwnd = gui::window("ProgramGroup", shell.model.groups[index].title,
                         12 + static_cast<int>(index) * 28, 8 + static_cast<int>(index) * 28,
                         390, 240, shell.workspace, index);
  } else gui::show(v.hwnd, ShowCommand_Restore);
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
  gui::Field field{"Group name", name, sizeof(name)};
  if (!gui::dialog(create ? "New group" : "Group properties", "Changes last for this session only.", &field, 1)) return;
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
  gui::Field fields[] = {{"Description", item.title, sizeof(item.title)},
                         {"Executable (absolute path)", item.path, sizeof(item.path)},
                         {"Arguments", item.arguments, sizeof(item.arguments)}};
  if (!gui::dialog(create ? "New program" : "Program properties",
                   "Changes last for this session only.", fields, 3)) return;
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
  if (!gui::dialog("Delete program", "Remove the selected program from this session?", nullptr, 0)) return;
  shell.model.items[index] = Item{};
  shell.model.modified = true;
  auto &v = shell.views[shell.active];
  if (v.selection) --v.selection;
  refresh_groups();
}

void remove_group() {
  if (shell.active < 0) return;
  if (!gui::dialog("Delete group", "Remove this group AND all its program entries?", nullptr, 0)) return;
  const int index = shell.active;
  if (shell.views[index].hwnd) gui::destroy(shell.views[index].hwnd);
  shell.views[index] = GroupView{};
  shell.model.remove_group(shell.model.groups[index].id);
  shell.active = -1;
  refresh_groups();
}

bool run_dialog() {
  char path[PathLength]{}, arguments[ArgumentLength]{};
  gui::Field fields[] = {{"Executable (absolute path)", path, sizeof(path)},
                         {"Arguments", arguments, sizeof(arguments)}};
  if (gui::dialog("Run", "Run a native UnDOS ELF program.", fields, 2)) {
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
  for (auto &v : shell.views) {
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

enum Command {
  NewGroup = 1, NewProgram, Properties, DeleteProgram, DeleteGroup, Run, Exit,
  ArrangeIcons, Cascade, Tile, About, GroupProperties, OpenGroupBase = 100
};
struct MenuEntry { const char *label; int command; bool enabled; };
struct Menu {
  uint32_t hwnd{0};
  const MenuEntry *entries{nullptr};
  int count{0};
  int selected{0};
  int result{0};
  bool done{false};
};
Menu *activeMenu{nullptr};

void menu_proc(const Msg &msg) {
  if (!activeMenu || msg.hwnd != activeMenu->hwnd) return;
  auto &menu = *activeMenu;
  if (msg.message == WindowMessage_Paint) {
    gui::Paint p(msg.hwnd);
    for (int i = 0; i < menu.count; ++i) {
      const bool selected = i == menu.selected;
      const uint32_t bg = selected ? gui::Highlight : gui::Paper;
      p.fill(1, i * 22 + 1, 245, i * 22 + 23, bg);
      p.text(8, i * 22 + 4, menu.entries[i].label,
             !menu.entries[i].enabled ? 8 : selected ? gui::Paper : gui::Ink, bg);
    }
  } else if (msg.message == WindowMessage_Close) menu.done = true;
  else if (msg.message == WindowMessage_KeyDown) {
    if (msg.wParam == 27) menu.done = true;
    if (msg.wParam == 38 || msg.wParam == 40) {
      menu.selected = (menu.selected + (msg.wParam == 38 ? menu.count - 1 : 1)) % menu.count;
      gui::invalidate(menu.hwnd);
    }
    if (msg.wParam == 13 && menu.entries[menu.selected].enabled) {
      menu.result = menu.entries[menu.selected].command;
      menu.done = true;
    }
  } else if (msg.message == WindowMessage_LButtonDown) {
    const int index = gui::mouse_y(msg) / 22;
    if (index >= 0 && index < menu.count && menu.entries[index].enabled) {
      menu.result = menu.entries[index].command;
      menu.done = true;
    }
  } else if (msg.message == WindowMessage_Activate && !msg.wParam) menu.done = true;
}

int popup(int menuIndex, const MenuEntry *entries, int count) {
  Rect frame{};
  if (!gui::check(UD_SYS_GetWindowRectV1(shell.frame, &frame), "GetWindowRect")) return 0;
  const Rect area = gui::client(shell.frame);
  const int clientX = frame.left + (frame.right - frame.left - area.right) / 2;
  const int clientY = frame.bottom - area.bottom - (frame.right - frame.left - area.right) / 2;
  Menu menu{};
  menu.entries = entries;
  menu.count = count;
  if (!gui::check(UD_SYS_CreateWindowV1("ProgramMenu", "", static_cast<WindowStyle>(WindowStyle_Visible | WindowStyle_Border),
                                      clientX + MenuLeft[menuIndex], clientY + MenuHeight, 252, count * 22 + 6, 0, 0, &menu.hwnd),
                  "Create menu")) return 0;
  activeMenu = &menu;
  focus(menu.hwnd);
  Msg msg{};
  while (!menu.done && gui::next(msg)) {
    if (msg.hwnd == menu.hwnd) menu_proc(msg);
    else if (msg.message == WindowMessage_KeyDown || msg.message == WindowMessage_Char) {
      msg.hwnd = menu.hwnd;
      menu_proc(msg);
    }
    else if (msg.message == WindowMessage_Paint) gui::dispatch(msg);
  }
  gui::destroy(menu.hwnd);
  activeMenu = nullptr;
  return menu.result;
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
      if (!shell.model.modified || gui::dialog("Exit Program Manager", "Discard unsaved session changes and exit?", nullptr, 0))
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

void show_menu(int index) {
  const bool group = shell.active >= 0;
  const bool item = selected_item() >= 0;
  if (index == 0) {
    const MenuEntry entries[] = {{"New Group...", NewGroup, true}, {"New Program...", NewProgram, group},
      {"Program Properties...", Properties, item}, {"Group Properties...", GroupProperties, group},
      {"Delete Program...", DeleteProgram, item}, {"Delete Group...", DeleteGroup, group},
      {"Run...", Run, true}, {"Exit", Exit, true}};
    execute(popup(index, entries, 8));
  } else if (index == 1) {
    const MenuEntry entries[] = {{"Arrange Icons", ArrangeIcons, group},
      {"Session settings only", About, true}};
    execute(popup(index, entries, 2));
  } else if (index == 2) {
    MenuEntry entries[2 + MaxGroups] = {{"Cascade", Cascade, true}, {"Tile", Tile, true}};
    int n = 2;
    for (unsigned i = 0; i < MaxGroups; ++i)
      if (shell.model.groups[i].id)
        entries[n++] = {shell.model.groups[i].title, OpenGroupBase + static_cast<int>(i), true};
    execute(popup(index, entries, n));
  } else {
    const MenuEntry entries[] = {{"About Program Manager...", About, true}};
    execute(popup(3, entries, 1));
  }
}

void keyboard(const Msg &msg, bool inGroup) {
  // F10 and Alt+F/O/W/H open the menus from either frame or group.
  if (msg.wParam == 121) { show_menu(0); return; }
  if (msg.wParam == 112) { execute(About); return; }
  const uint32_t modifiers = msg.lParam >> 16;
  if (modifiers & 0x30) {
    if (msg.wParam == 'F') show_menu(0);
    else if (msg.wParam == 'O') show_menu(1);
    else if (msg.wParam == 'W') show_menu(2);
    else if (msg.wParam == 'H') show_menu(3);
    return;
  }
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
  const Grid grid = Grid::for_size(area.right, area.bottom);
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
  else if (msg.message == WindowMessage_KeyDown) keyboard(msg, true);
  else if (msg.message == WindowMessage_Paint) {
    gui::Paint p(msg.hwnd);
    const Rect area = gui::client(msg.hwnd);
    const Grid grid = Grid::for_size(area.right, area.bottom);
    const int count = shell.model.count(group);
    const int start = grid.page_start(v.selection);
    for (int ordinal = start; ordinal < count && ordinal < start + grid.page_size; ++ordinal) {
      const int i = shell.model.item_at(group, ordinal);
      const auto &item = shell.model.items[i];
      const int x = (ordinal - start) % grid.columns * 112;
      const int y = (ordinal - start) / grid.columns * 72;
      const bool selected = ordinal == v.selection;
      p.icon(x + 40, y + 10, item.icon, selected);
      char label[14]{};
      const size_t length = strlen(item.title);
      const size_t n = length > 13 ? 13 : length;
      memcpy(label, item.title, n);
      if (length > 13) { label[10] = '.'; label[11] = '.'; label[12] = '.'; }
      const int labelWidth = static_cast<int>(n) * gui::CellWidth;
      const int left = x + (112 - labelWidth) / 2;
      p.fill(left - 2, y + 46, left + labelWidth + 2, y + 64, selected ? gui::Highlight : gui::Paper);
      p.text(left, y + 47, label, selected ? gui::Paper : gui::Ink, selected ? gui::Highlight : gui::Paper);
    }
  } else if (msg.message == WindowMessage_LButtonDown || msg.message == WindowMessage_LButtonDblClk) {
    shell.active = static_cast<int>(index);
    const auto area = gui::client(msg.hwnd);
    const auto grid = Grid::for_size(area.right, area.bottom);
    const int x = gui::mouse_x(msg), y = gui::mouse_y(msg);
    if (x < 0 || y < 0 || y >= grid.rows * 72 || x >= grid.columns * 112) return;
    const int ordinal = grid.page_start(v.selection) + (y / 72) * grid.columns + x / 112;
    if (ordinal >= shell.model.count(group)) return;
    v.selection = ordinal;
    gui::invalidate(msg.hwnd);
    if (msg.message == WindowMessage_LButtonDblClk) launch_selection();
  }
}

void frame_proc(const Msg &msg) {
  if (msg.message == WindowMessage_Paint) {
    gui::Paint p(msg.hwnd);
    const Rect area = gui::client(msg.hwnd);
    p.fill(0, 0, area.right, MenuHeight, gui::Face);
    for (int i = 0; i < 4; ++i) {
      p.text(MenuLeft[i] + 8, 3, MenuLabels[i]);
      p.fill(MenuLeft[i] + 9, 17, MenuLeft[i] + 14, 18, gui::Ink);
    }
    p.fill(0, MenuHeight - 1, area.right, MenuHeight, gui::Ink);
  } else if (msg.message == WindowMessage_Close) execute(Exit);
  else if (msg.message == WindowMessage_KeyDown) keyboard(msg, false);
  else if (msg.message == WindowMessage_LButtonDown) {
    if (gui::mouse_y(msg) >= 0 && gui::mouse_y(msg) < MenuHeight)
      for (int i = 0; i < 4; ++i)
        if (gui::mouse_x(msg) >= MenuLeft[i] && gui::mouse_x(msg) < MenuLeft[i + 1]) { show_menu(i); break; }
  } else if (msg.message == WindowMessage_Size) {
    if (shell.workspace) {
      const Rect area = gui::client(msg.hwnd);
      if (area.right > 0 && area.bottom > MenuHeight)
        gui::move(shell.workspace, 0, MenuHeight, area.right, area.bottom - MenuHeight);
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
  uint32_t display = 0;
  DeviceCaps caps{};
  if (!gui::check(UD_SYS_CreateDCV1("", "", DCInitFlags_Primary, &display), "Open session display")) return 1;
  const bool queried = gui::check(UD_SYS_GetDeviceCapsV1(display, &caps), "Query session display");
  gui::check(UD_SYS_DeleteDCV1(display), "Close session display");
  if (!queried || caps.width < 640 || caps.height < 480) {
    crt_puts("progman: a display of at least 640x480 is required\n");
    return 1;
  }
  if (!gui::register_class("ProgramManager", frame_proc, 8) ||
      !gui::register_class("ProgramWorkspace", workspace_proc, 8) ||
      !gui::register_class("ProgramGroup", group_proc, gui::Paper) ||
      !gui::register_class("ProgramMenu", menu_proc)) return 1;
  shell.frame = gui::window("ProgramManager", "Program Manager", 4, 4,
                            static_cast<int>(caps.width) - 8, static_cast<int>(caps.height) - 12);
  if (!shell.frame) return 1;
  const Rect area = gui::client(shell.frame);
  if (!gui::check(UD_SYS_CreateWindowV1("ProgramWorkspace", "",
                                      static_cast<WindowStyle>(WindowStyle_Visible | WindowStyle_Child),
                                      0, MenuHeight, area.right, area.bottom - MenuHeight,
                                      shell.frame, 0, &shell.workspace), "Create workspace")) return 1;
  if (!load_configuration()) {
    gui::message("Configuration error", "Cannot load PROGMAN.INI. Recovery session is empty.");
    shell.model.add_group("Recovery");
  }
  int first = -1;
  for (unsigned i = 0; i < MaxGroups; ++i) {
    if (!shell.model.groups[i].id) continue;
    if (first < 0) first = static_cast<int>(i);
    open_group(i);
    if (static_cast<int>(i) != first && shell.views[i].hwnd)
      gui::show(shell.views[i].hwnd, ShowCommand_Minimize);
  }
  if (first >= 0) open_group(static_cast<unsigned>(first));
  refresh_title();
  Msg msg{};
  for (unsigned i = 0; i < 128 && UD_SYS_PeekMessageV1(&msg, 1) == SyscallReturnCode_OKAY; ++i)
    gui::dispatch(msg);
  (void) UD_SYS_PrintV1("progman: session ready (F10 menus, Enter launches)");
  while (shell.running && gui::next(msg)) gui::dispatch(msg);
  gui::destroy(shell.frame);
  return 0;
}
