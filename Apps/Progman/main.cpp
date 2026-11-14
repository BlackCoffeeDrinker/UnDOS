#include <userspace.h>
#include <windows.h>
#include <undosgui/extensions.h>
#include <undoscrt/stdio.h>
#include <undoscrt/string.h>
#include <undos/syscalls.h>
#include "model.hpp"

namespace {
using namespace progman;

struct GroupView {
  HWND hwnd{nullptr};
  int selection{0};
};

struct Shell {
  Model model;
  Model candidate;
  char configuration[12289]{};
  GroupView views[MaxGroups];
  HWND frame{nullptr};
  HWND workspace{nullptr};
  HMENU menu{nullptr};
  UNDOS_UI_CONTEXT ui{};
  int active{-1};
  bool running{true};
  uint32_t reflowGeneration{0};
};
Shell shell;

bool group_grid(int width, int height, uint32_t group, Grid &grid) {
  UNDOS_TEXT_METRICS metrics{};
  if (!UndosGetFontMetrics(shell.ui.font, UNDOS_TEXT_REGULAR,
                           UNDOS_TEXT_BASIC, &metrics)) return false;
  uint32_t labelHeight = 0;
  uint32_t labelWidth = 0;
  const int count = shell.model.count(group);
  for (int ordinal = 0; ordinal < count; ++ordinal) {
    const int itemIndex = shell.model.item_at(group, ordinal);
    if (itemIndex < 0) return false;
    UNDOS_TEXT_EXTENT extent{};
    if (!UndosMeasureText(shell.ui.font, UNDOS_TEXT_REGULAR,
            UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC,
            shell.model.items[itemIndex].title, &extent)) return false;
    if (extent.lineHeight > labelHeight) labelHeight = extent.lineHeight;
    if (extent.advanceWidth > labelWidth) labelWidth = extent.advanceWidth;
  }
  if (!labelHeight) labelHeight = metrics.lineHeight;
  if (labelHeight > static_cast<uint32_t>(INT32_MAX - 50) ||
      labelWidth > static_cast<uint32_t>(INT32_MAX - LabelPadding)) return false;
  grid = Grid::for_size(width, height, static_cast<int>(labelHeight), static_cast<int>(labelWidth));
  return true;
}

void refresh_title() {
  if (!SetWindowText(shell.frame, "Program Manager"))
    crt_printf("progman: SetWindowText failed %u/%u\n", GetLastError(),
               UndosGetLastStatus());
}

enum Command {
  NewGroup = 1, NewProgram, Properties, DeleteProgram, DeleteGroup, Run, Exit,
  ArrangeIcons, Cascade, Tile, About, GroupProperties, OpenGroupBase = 100
};

bool syscall_ok(uint32_t result, const char *operation) {
  if (result == SyscallReturnCode_OKAY) return true;
  crt_printf("progman: %s failed status %u\n", operation,
             static_cast<unsigned>(result));
  (void)UD_SYS_PrintV1("progman: operation failed");
  return false;
}

bool sdk_ok(BOOL result, const char *operation) {
  if (result) return true;
  crt_printf("progman: %s failed error %u status %u\n", operation,
             GetLastError(), UndosGetLastStatus());
  (void)UD_SYS_PrintV1("progman: UI operation failed");
  return false;
}

bool append_menu(HMENU menu, UINT flags, UINT_PTR item, const char *text) {
  return sdk_ok(AppendMenu(menu, flags, item, text), "AppendMenu");
}

HMENU create_menu(bool popup) {
  HMENU menu = popup ? CreatePopupMenu() : CreateMenu();
  if (!menu) sdk_ok(FALSE, "CreateMenu");
  return menu;
}

bool append_command(HMENU menu, uint32_t command, const char *text) {
  return append_menu(menu, MF_STRING, command, text);
}

bool append_popup(HMENU root, const char *text, HMENU &popup) {
  popup = create_menu(true);
  if (!popup) return false;
  if (append_menu(root, MF_POPUP, reinterpret_cast<UINT_PTR>(popup), text)) return true;
  (void)DestroyMenu(popup);
  popup = 0;
  return false;
}

bool rebuild_menu() {
  if (!shell.frame) return true;
  const HMENU root = create_menu(false);
  if (!root) return false;
  HMENU file = nullptr, options = nullptr, window = nullptr, help = nullptr;
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
  if (!okay || !sdk_ok(SetMenu(shell.frame, root), "SetMenu")) {
    (void)DestroyMenu(root);
    return false;
  }
  if (shell.menu) (void)DestroyMenu(shell.menu);
  shell.menu = root;
  return true;
}

void refresh_groups() {
  for (const auto &v : shell.views)
    if (v.hwnd) (void)sdk_ok(InvalidateRect(v.hwnd, nullptr, TRUE), "InvalidateRect");
  refresh_title();
  rebuild_menu();
}

void focus(HWND hwnd) {
  if (hwnd) (void)sdk_ok(UndosSetFocus(hwnd), "SetFocus");
}

void recover_windows() {
  for (UINT attempt = 0; attempt < 64; ++attempt) {
    UNDOS_WINDOW_CREATION_RECOVERY creation{};
    if (UndosGetWindowCreationRecovery(&creation)) {
      if (!DestroyWindow(creation.window)) {
        crt_printf("progman: creation cleanup failed setup error/status %u/%u "
                   "cleanup error/status %u/%u\n",
                   creation.setupError, creation.setupStatus,
                   creation.cleanupError, creation.cleanupStatus);
        return;
      }
      continue;
    }
    UNDOS_UI_CLEANUP_RECOVERY cleanup{};
    if (UndosGetUiCleanupRecovery(&cleanup)) {
      if (!DestroyWindow(cleanup.window)) {
        crt_printf("progman: UI cleanup failed operation error/status %u/%u "
                   "cleanup error/status %u/%u\n",
                   cleanup.operationError, cleanup.operationStatus,
                   cleanup.cleanupError, cleanup.cleanupStatus);
        return;
      }
      continue;
    }
    break;
  }
}

void check_dispatch_result(uint32_t before) {
  if (shell.ui.resultSerial != before && shell.ui.result < 0) {
    crt_printf("progman: default UI operation failed operation error/status %u/%u cleanup error/status %u/%u\n",
               shell.ui.operationError, shell.ui.operationStatus,
               shell.ui.cleanupError, shell.ui.cleanupStatus);
    (void)UD_SYS_PrintV1("progman: default UI operation failed");
  }
  recover_windows();
}

bool dialog(const char *title, const char *message, UNDOS_DIALOG_FIELD *fields,
            UINT count, BOOL confirm) {
  const int result = UndosRunDialog(&shell.ui, shell.frame, title, message,
                                    fields, count, confirm);
  if (result < 0)
    crt_printf("progman: dialog failed %u/%u\n", shell.ui.operationError,
               shell.ui.operationStatus);
  recover_windows();
  return result > 0;
}

void message(const char *title, const char *text) {
  (void)dialog(title, text, nullptr, 0, FALSE);
}

bool launch(const char *path, const char *arguments) {
  crt_printf("progman: launching %s\n", path);
  HANDLE process = 0;
  const auto result = UD_SYS_CreateProcessV1(path, arguments, ProcessFlags_None, UINT32_MAX, UINT32_MAX, UINT32_MAX, &process);
  if (!syscall_ok(result, "CreateProcess")) {
    message("Cannot run program", "Executable or its loader failed. See serial log.");
    return false;
  }
  (void)syscall_ok(UD_SYS_CloseV1(process), "Close process");
  return true;
}

bool ensure_group_window(unsigned index) {
  if (index >= MaxGroups || !shell.model.groups[index].id) return false;
  auto &view = shell.views[index];
  if (!view.hwnd) {
    view.hwnd = CreateWindow("ProgramGroup", shell.model.groups[index].title,
        WS_CHILD | WS_CAPTION | WS_BORDER | WS_VISIBLE,
        12 + static_cast<int>(index) * 28,
        8 + static_cast<int>(index) * 28, 390, 240,
        shell.workspace, nullptr, nullptr,
        reinterpret_cast<void *>(static_cast<uintptr_t>(index)));
    if (!view.hwnd) {
      sdk_ok(FALSE, "CreateWindow group");
      recover_windows();
      return false;
    }
    if (!UndosAttachUiContext(view.hwnd, &shell.ui)) {
      sdk_ok(FALSE, "attach group window");
      (void)DestroyWindow(view.hwnd);
      view.hwnd = nullptr;
      recover_windows();
      return false;
    }
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
  if (existed) (void)sdk_ok(UndosShowWindow(v.hwnd, SW_RESTORE), "ShowWindow");
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
  UNDOS_DIALOG_FIELD field{.label = "Group name", .value = name, .capacity = sizeof(name)};
  if (!dialog(create ? "New group" : "Group properties", "Changes last for this session only.",
                    &field, 1, true)) return;
  if (!name[0]) { message("Invalid group", "A group must have a name."); return; }
  if (create) {
    const int index = shell.model.add_group(name);
    if (index < 0) message("Cannot create group", "Group limit reached or invalid name.");
    else {
      crt_printf("progman: created group %u\n", shell.model.groups[index].id);
      open_group(static_cast<unsigned>(index));
    }
  } else {
    if (!copy(shell.model.groups[shell.active].title, LabelLength, name)) {
      message("Invalid group", "The name contains a reserved character.");
      return;
    }
    shell.model.modified = true;
    (void)sdk_ok(SetWindowText(shell.views[shell.active].hwnd, name), "SetWindowText");
  }
  refresh_groups();
}

void edit_program(bool create) {
  if (shell.active < 0) { message("No group", "Open a group using the Window menu first."); return; }
  const int index = selected_item();
  if (!create && index < 0) return;
  Item item{};
  if (!create) item = shell.model.items[index];
  UNDOS_DIALOG_FIELD fields[] = {{.label = "Description", .value = item.title, .capacity = sizeof(item.title)},
                         {.label = "Executable (absolute path)", .value = item.path, .capacity = sizeof(item.path)},
                         {.label = "Arguments", .value = item.arguments, .capacity = sizeof(item.arguments)}};
  if (!dialog(create ? "New program" : "Program properties",
                   "Changes last for this session only.", fields, 3, true)) return;
  if (!item.title[0] || item.path[0] != '/' ||
      strchr(item.title, '|') || strchr(item.path, '|') || strchr(item.arguments, '|')) {
    message("Invalid program", "Enter a name and absolute path; no | characters.");
    return;
  }
  if (create) {
    const auto group = shell.model.groups[shell.active].id;
    const int added = shell.model.add_item(group, item.title, item.path, item.arguments, 1);
    if (added < 0) message("Cannot create program", "Program limit reached or invalid fields.");
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
  if (!dialog("Delete program", "Remove the selected program from this session?", nullptr, 0,
                    true)) return;
  shell.model.items[index] = Item{};
  shell.model.modified = true;
  auto &v = shell.views[shell.active];
  if (v.selection) --v.selection;
  refresh_groups();
}

void remove_group() {
  if (shell.active < 0) return;
  if (!dialog("Delete group", "Remove this group AND all its program entries?", nullptr, 0,
                    true)) return;
  const int index = shell.active;
  if (shell.views[index].hwnd) {
    (void)DestroyWindow(shell.views[index].hwnd);
    shell.views[index].hwnd = nullptr;
  }
  shell.views[index] = GroupView{};
  shell.model.remove_group(shell.model.groups[index].id);
  shell.active = -1;
  refresh_groups();
}

bool run_dialog() {
  char path[PathLength]{}, arguments[ArgumentLength]{};
  UNDOS_DIALOG_FIELD fields[] = {{.label = "Executable (absolute path)", .value = path, .capacity = sizeof(path)},
                         {.label = "Arguments", .value = arguments, .capacity = sizeof(arguments)}};
  const bool accepted = dialog("Run", "Run a native UnDOS ELF program.", fields, 2, true);
  crt_printf("run dialog accepted=%u path='%s' args='%s'\n",
             accepted ? 1u : 0u, path, arguments);
  if (accepted) {
    if (path[0] != '/') message("Invalid path", "An absolute executable path is required.");
    else return launch(path, arguments);
  }
  return false;
}

void arrange_windows(bool tile) {
  RECT client{};
  if (!GetClientRect(shell.workspace, &client)) {
    sdk_ok(FALSE, "GetClientRect workspace");
    return;
  }
  int count = 0;
  for (const auto &v : shell.views) if (v.hwnd) ++count;
  if (!count) return;
  int ordinal = 0;
  for (const auto &v : shell.views) {
    if (!v.hwnd) continue;
    (void)UndosShowWindow(v.hwnd, SW_RESTORE);
    int width = client.right - 16;
    int height = client.bottom - 16;
    if (tile) {
      const int columns = count > 1 ? 2 : 1;
      const int rows = (count + columns - 1) / columns;
      width = (client.right - 12) / columns;
      height = (client.bottom - 12) / rows;
      (void)MoveWindow(v.hwnd, 6 + (ordinal % columns) * width,
          6 + (ordinal / columns) * height, width, height, TRUE);
    } else {
      width -= 24 * (count - 1);
      height -= 24 * (count - 1);
      if (width < 150) width = 150;
      if (height < 100) height = 100;
      (void)MoveWindow(v.hwnd, 8 + ordinal * 24, 8 + ordinal * 24,
                       width, height, TRUE);
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
      if (!shell.model.modified || dialog("Exit Program Manager", "Discard unsaved session changes and exit?",
                                               nullptr, 0, true))
        shell.running = false;
      break;
    case ArrangeIcons: if (shell.active >= 0) (void)InvalidateRect(shell.views[shell.active].hwnd, nullptr, TRUE); break;
    case Cascade: arrange_windows(false); break;
    case Tile: arrange_windows(true); break;
    case About:
      message("About Program Manager", "UnDOS native shell. Changes are session-only.");
      break;
    default:
      if (command >= OpenGroupBase && command < OpenGroupBase + static_cast<int>(MaxGroups))
        open_group(static_cast<unsigned>(command - OpenGroupBase));
      break;
  }
  if (shell.active >= 0 && shell.views[shell.active].hwnd) focus(shell.views[shell.active].hwnd);
  else focus(shell.frame);
}

bool refresh_font(WPARAM generation) {
  if ((!shell.ui.metricsValid || generation != shell.ui.metrics.generation) &&
      !UndosRefreshUi(&shell.ui)) {
    crt_printf("progman: font refresh failed %u/%u\n",
               shell.ui.operationError, shell.ui.operationStatus);
    return false;
  }
  shell.reflowGeneration = static_cast<uint32_t>(generation);
  return true;
}

bool set_colors(HDC dc, UINT foreground, UINT background,
                BOOL opaque = TRUE) {
  COLORREF fg = UndosPaletteColor(foreground);
  COLORREF bg = UndosPaletteColor(background);
  return fg != CLR_INVALID && bg != CLR_INVALID &&
      SetTextColor(dc, fg) != CLR_INVALID && SetBkColor(dc, bg) != CLR_INVALID &&
      SetBkMode(dc, opaque ? OPAQUE : TRANSPARENT) != 0;
}

bool fill(HDC dc, int left, int top, int right, int bottom, UINT color) {
  return set_colors(dc, color, color) &&
      UndosFillRectangle(dc, left, top, right, bottom);
}

bool draw(HDC dc, int x, int y, const char *text, UINT foreground = 0,
          UINT background = 7) {
  return set_colors(dc, foreground, background) &&
      UndosDrawText(dc, x, y, UNDOS_TEXT_REGULAR, UNDOS_TEXT_PROPORTIONAL,
                    UNDOS_TEXT_BASIC, text);
}

bool begin_paint(HWND window, PAINTSTRUCT &paint) {
  paint = {};
  paint.hdc = BeginPaint(window, &paint);
  if (!paint.hdc) return sdk_ok(FALSE, "BeginPaint");
  UNDOS_FONT previous = 0;
  BOOL hadPrevious = FALSE;
  if (!UndosSelectFont(paint.hdc, shell.ui.font, &previous, &hadPrevious)) {
    sdk_ok(FALSE, "UndosSelectFont");
    (void)EndPaint(window, &paint);
    return false;
  }
  if (hadPrevious && !UndosCloseFont(previous)) {
    sdk_ok(FALSE, "UndosCloseFont");
    (void)EndPaint(window, &paint);
    return false;
  }
  return true;
}

void end_paint(HWND window, PAINTSTRUCT &paint) {
  if (!EndPaint(window, &paint)) (void)sdk_ok(FALSE, "EndPaint");
}

void keyboard(const MSG &msg, bool inGroup) {
  if (msg.wParam == 112) { execute(About); return; }
  if (!inGroup) {
    if (msg.wParam == 9 || msg.wParam == 13) {
      for (unsigned i = 0; i < MaxGroups; ++i)
        if (shell.model.groups[i].id) { open_group(i); break; }
    }
    return;
  }
  const unsigned index = static_cast<unsigned>(
      GetWindowLongPtr(msg.hwnd, GWLP_USERDATA));
  if (index >= MaxGroups) return;
  shell.active = static_cast<int>(index);
  auto &v = shell.views[index];
  const auto group = shell.model.groups[index].id;
  RECT area{};
  if (!GetClientRect(msg.hwnd, &area)) return;
  Grid grid{};
  if (!group_grid(area.right, area.bottom, group, grid)) return;
  const int count = shell.model.count(group);
  if (msg.wParam == VK_RETURN) { launch_selection(); return; }
  if (msg.wParam == 46) { remove_selection(); return; }
  if (msg.wParam == VK_TAB) {
    for (unsigned step = 1; step <= MaxGroups; ++step) {
      const unsigned next = (index + step) % MaxGroups;
      if (shell.model.groups[next].id) { open_group(next); return; }
    }
  }
  int selected = v.selection;
  if (msg.wParam == VK_LEFT) --selected;
  else if (msg.wParam == VK_RIGHT) ++selected;
  else if (msg.wParam == VK_UP) selected -= grid.columns;
  else if (msg.wParam == VK_DOWN) selected += grid.columns;
  else if (msg.wParam == VK_HOME) selected = 0;
  else if (msg.wParam == VK_END) selected = count - 1;
  else if (msg.wParam == VK_PRIOR) selected -= grid.page_size;
  else if (msg.wParam == VK_NEXT) selected += grid.page_size;
  if (selected < 0) selected = 0;
  if (selected >= count) selected = count > 0 ? count - 1 : 0;
  v.selection = selected;
  (void)InvalidateRect(v.hwnd, nullptr, TRUE);
}

LRESULT CALLBACK group_proc(HWND window, UINT messageId, WPARAM wParam,
                            LPARAM lParam) {
  const unsigned index = static_cast<unsigned>(
      GetWindowLongPtr(window, GWLP_USERDATA));
  if (index >= MaxGroups) return DefWindowProc(window, messageId, wParam, lParam);
  auto &v = shell.views[index];
  const auto group = shell.model.groups[index].id;
  if (messageId == WM_ACTIVATE && wParam) shell.active = static_cast<int>(index);
  else if (messageId == WM_CLOSE) {
    (void)UndosShowWindow(window, SW_HIDE);
    if (shell.active == static_cast<int>(index)) shell.active = -1;
    return 0;
  } else if (messageId == WM_SIZE || messageId == WM_DISPLAYCHANGE) {
    if (messageId == WM_DISPLAYCHANGE) {
      UINT width = 0, height = 0;
      if (!UndosGetDisplaySize(&shell.ui, &width, &height))
        crt_printf("progman: display refresh failed %u/%u\n", GetLastError(),
                   UndosGetLastStatus());
    }
    (void)InvalidateRect(window, nullptr, TRUE);
  }
  else if (messageId == WM_FONTCHANGED) {
    (void)refresh_font(wParam);
    (void)InvalidateRect(window, nullptr, TRUE);
  } else if (messageId == WM_KEYDOWN) {
    const MSG msg{.hwnd = window, .message = messageId,
                  .wParam = wParam, .lParam = lParam};
    keyboard(msg, true);
  } else if (messageId == WM_PAINT) {
    PAINTSTRUCT paint{};
    if (!begin_paint(window, paint)) return 0;
    RECT area{};
    if (!GetClientRect(window, &area)) {
      sdk_ok(FALSE, "GetClientRect");
      end_paint(window, paint);
      return 0;
    }
    Grid grid{};
    if (!group_grid(area.right, area.bottom, group, grid)) {
      end_paint(window, paint);
      return 0;
    }
    const int count = shell.model.count(group);
    const int start = grid.page_start(v.selection);
    for (int ordinal = start; ordinal < count && ordinal < start + grid.page_size; ++ordinal) {
      const int i = shell.model.item_at(group, ordinal);
      const auto &item = shell.model.items[i];
      const int x = (ordinal - start) % grid.columns * grid.cell_width;
      const int y = (ordinal - start) / grid.columns * grid.cell_height;
      const bool selected = ordinal == v.selection;
      (void)UndosDrawClassicIcon(paint.hdc,
          x + (grid.cell_width - 32) / 2, y + 10, item.icon, selected);
      char label[LabelLength]{};
      UNDOS_TEXT_EXTENT extent{};
      if (!UndosFitTextPrefix(shell.ui.font, UNDOS_TEXT_REGULAR,
              UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC, item.title, label,
              sizeof(label), static_cast<UINT>(grid.cell_width - LabelPadding),
              &extent)) continue;
      const int labelWidth = static_cast<int>(extent.advanceWidth);
      const int left = x + (grid.cell_width - labelWidth) / 2;
      (void)fill(paint.hdc, left - 2, y + 46, left + labelWidth + 2,
          y + 50 + static_cast<int>(extent.lineHeight),
          selected ? 1u : 15u);
      (void)draw(paint.hdc, left, y + 48, label,
                 selected ? 15u : 0u, selected ? 1u : 15u);
    }
    end_paint(window, paint);
    return 0;
  } else if (messageId == WM_LBUTTONDOWN || messageId == WM_LBUTTONDBLCLK) {
    shell.active = static_cast<int>(index);
    RECT area{};
    if (!GetClientRect(window, &area)) return 0;
    Grid grid{};
    if (!group_grid(area.right, area.bottom, group, grid)) return 0;
    const int x = static_cast<int16_t>(lParam & 0xFFFF);
    const int y = static_cast<int16_t>(lParam >> 16);
    if (x < 0 || y < 0 || y >= grid.rows * grid.cell_height || x >= grid.columns * grid.cell_width) return 0;
    const int ordinal = grid.page_start(v.selection) + (y / grid.cell_height) * grid.columns +
                        x / grid.cell_width;
    if (ordinal >= shell.model.count(group)) return 0;
    v.selection = ordinal;
    (void)InvalidateRect(window, nullptr, TRUE);
    if (messageId == WM_LBUTTONDBLCLK) launch_selection();
    return 0;
  }
  return DefWindowProc(window, messageId, wParam, lParam);
}

LRESULT CALLBACK frame_proc(HWND window, UINT messageId, WPARAM wParam,
                            LPARAM lParam) {
  if (messageId == WM_PAINT) {
    PAINTSTRUCT paint{};
    if (begin_paint(window, paint)) end_paint(window, paint);
    return 0;
  } else if (messageId == WM_FONTCHANGED) {
    (void)refresh_font(wParam);
    return 0;
  } else if (messageId == WM_COMMAND) {
    execute(static_cast<int>(wParam));
    return 0;
  } else if (messageId == WM_CLOSE) {
    execute(Exit);
    return 0;
  } else if (messageId == WM_KEYDOWN) {
    const MSG msg{.hwnd = window, .message = messageId,
                  .wParam = wParam, .lParam = lParam};
    keyboard(msg, false);
    return 0;
  } else if (messageId == WM_SIZE) {
    if (shell.workspace) {
      RECT area{};
      if (!GetClientRect(window, &area)) return 0;
      if (area.right > 0 && area.bottom > 0)
        (void)MoveWindow(shell.workspace, 0, 0, area.right, area.bottom, TRUE);
    }
    (void)InvalidateRect(window, nullptr, TRUE);
  } else if (messageId == WM_DISPLAYCHANGE) {
    UINT width = 0, height = 0;
    if (!UndosGetDisplaySize(&shell.ui, &width, &height))
      crt_printf("progman: display refresh failed %u/%u\n", GetLastError(),
                 UndosGetLastStatus());
    (void)InvalidateRect(window, nullptr, TRUE);
    return 0;
  }
  return DefWindowProc(window, messageId, wParam, lParam);
}

LRESULT CALLBACK workspace_proc(HWND window, UINT messageId, WPARAM wParam,
                                LPARAM lParam) {
  if (messageId == WM_PAINT) {
    PAINTSTRUCT paint{};
    if (begin_paint(window, paint)) end_paint(window, paint);
    return 0;
  }
  if (messageId == WM_KEYDOWN) {
    const MSG msg{.hwnd = window, .message = messageId,
                  .wParam = wParam, .lParam = lParam};
    keyboard(msg, false);
    return 0;
  }
  return DefWindowProc(window, messageId, wParam, lParam);
}

bool load_configuration() {
  HANDLE file = 0;
  if (!syscall_ok(UD_SYS_OpenV1("/System/PROGMAN.INI", FileOpenMode_Read, &file),
                  "Open PROGMAN.INI")) return false;
  uint32_t total = 0;
  bool readable = true;
  while (total < sizeof(shell.configuration) - 1) {
    uint32_t n = sizeof(shell.configuration) - 1 - total;
    if (n > 4096) n = 4096;
    if (!syscall_ok(UD_SYS_ReadV1(file,
            reinterpret_cast<uint8_t *>(shell.configuration + total), &n),
            "Read configuration")) { readable = false; break; }
    total += n;
    if (!n) break;
  }
  if (readable && total == sizeof(shell.configuration) - 1) {
    uint8_t extra = 0;
    uint32_t n = 1;
    readable = syscall_ok(UD_SYS_ReadV1(file, &extra, &n),
                          "Read configuration limit") && n == 0;
  }
  (void)syscall_ok(UD_SYS_CloseV1(file), "Close configuration");
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

void cleanup_shell() {
  for (auto &view : shell.views) {
    if (view.hwnd && !DestroyWindow(view.hwnd))
      (void)sdk_ok(FALSE, "DestroyWindow group");
    view.hwnd = nullptr;
  }
  if (shell.workspace && !DestroyWindow(shell.workspace))
    (void)sdk_ok(FALSE, "DestroyWindow workspace");
  shell.workspace = nullptr;
  if (shell.frame && !DestroyWindow(shell.frame))
    (void)sdk_ok(FALSE, "DestroyWindow frame");
  shell.frame = nullptr;
  recover_windows();
  if (shell.menu && !DestroyMenu(shell.menu))
    (void)sdk_ok(FALSE, "DestroyMenu");
  shell.menu = nullptr;
  if (shell.ui._sdkState && !UndosCloseUi(&shell.ui))
    crt_printf("progman: UI close failed %u/%u\n",
               shell.ui.operationError, shell.ui.operationStatus);
}
} // namespace

extern "C" int main(int, char **) {
  if (!UndosInitUi(&shell.ui)) {
    crt_printf("progman: UI initialization failed %u/%u\n",
               shell.ui.operationError, shell.ui.operationStatus);
    if (shell.ui._sdkState) (void)UndosCloseUi(&shell.ui);
    return 1;
  }
  UINT displayWidth = 0, displayHeight = 0;
  if (!UndosGetDisplaySize(&shell.ui, &displayWidth, &displayHeight) ||
      !displayWidth || !displayHeight) {
    sdk_ok(FALSE, "display size");
    (void)UndosCloseUi(&shell.ui);
    return 1;
  }
  WNDCLASS frameClass{};
  frameClass.lpfnWndProc = frame_proc;
  frameClass.hbrBackground = UNDOS_PALETTE_BRUSH(8);
  frameClass.lpszClassName = "ProgramManager";
  WNDCLASS workspaceClass{};
  workspaceClass.lpfnWndProc = workspace_proc;
  workspaceClass.hbrBackground = UNDOS_PALETTE_BRUSH(8);
  workspaceClass.lpszClassName = "ProgramWorkspace";
  WNDCLASS groupClass{};
  groupClass.lpfnWndProc = group_proc;
  groupClass.hbrBackground = UNDOS_PALETTE_BRUSH(15);
  groupClass.lpszClassName = "ProgramGroup";
  if (!RegisterClass(&frameClass) || !RegisterClass(&workspaceClass) ||
      !RegisterClass(&groupClass)) {
    sdk_ok(FALSE, "RegisterClass");
    (void)UndosCloseUi(&shell.ui);
    return 1;
  }
  const int width = static_cast<int>(displayWidth < 600 ? displayWidth : 600);
  const int height = static_cast<int>(displayHeight < 400 ? displayHeight : 400);
  shell.frame = CreateWindow("ProgramManager", "Program Manager",
      WS_VISIBLE | WS_OVERLAPPEDWINDOW, 30, 30, width, height,
      nullptr, nullptr, nullptr, nullptr);
  if (!shell.frame || !UndosAttachUiContext(shell.frame, &shell.ui)) {
    sdk_ok(FALSE, "CreateWindow/attach Program Manager");
    cleanup_shell();
    return 1;
  }
  if (!load_configuration()) {
    message("Configuration error", "Cannot load PROGMAN.INI. Recovery session is empty.");
    shell.model.add_group("Recovery");
  }
  if (!rebuild_menu()) {
    cleanup_shell();
    return 1;
  }
  RECT area{};
  if (!GetClientRect(shell.frame, &area)) {
    sdk_ok(FALSE, "GetClientRect frame");
    cleanup_shell();
    return 1;
  }
  shell.workspace = CreateWindow("ProgramWorkspace", "",
      WS_CHILD | WS_VISIBLE, 0, 0, area.right, area.bottom,
      shell.frame, nullptr, nullptr, nullptr);
  if (!shell.workspace || !UndosAttachUiContext(shell.workspace, &shell.ui)) {
    sdk_ok(FALSE, "CreateWindow/attach workspace");
    cleanup_shell();
    return 1;
  }
  if (!UndosShowWindow(shell.frame, SW_MAXIMIZE)) {
    sdk_ok(FALSE, "ShowWindow");
    cleanup_shell();
    return 1;
  }
  int first = -1;
  for (unsigned i = 0; i < MaxGroups; ++i) {
    if (!shell.model.groups[i].id) continue;
    if (first < 0) first = static_cast<int>(i);
    if (!ensure_group_window(i)) {
      cleanup_shell();
      return 1;
    }
    if (static_cast<int>(i) != first && shell.views[i].hwnd)
      (void)UndosShowWindow(shell.views[i].hwnd, SW_MINIMIZE);
  }
  if (first >= 0) open_group(static_cast<unsigned>(first));
  MSG msg{};
  (void) UD_SYS_PrintV1("progman: session ready (F10 opens menus, Enter launches)");
  while (shell.running) {
    const int received = GetMessage(&msg, nullptr, 0, 0);
    if (received < 0) {
      sdk_ok(FALSE, "GetMessage");
      break;
    }
    if (!received) break;
    const uint32_t serial = shell.ui.resultSerial;
    (void)DispatchMessage(&msg);
    check_dispatch_result(serial);
    // An empty queue means every font notification and resulting repaint has
    // been handled, so the session shows the new font throughout.
    if (shell.reflowGeneration) {
      MSG pending{};
      const DWORD previousError = GetLastError();
      SetLastError(ERROR_TIMEOUT);
      const BOOL hasPending =
          PeekMessage(&pending, nullptr, 0, 0, PM_NOREMOVE);
      const DWORD peekError = GetLastError();
      SetLastError(previousError);
      if (!hasPending && peekError == ERROR_TIMEOUT) {
        (void) UD_SYS_PrintV1("progman: font reflow complete");
        shell.reflowGeneration = 0;
      }
    }
  }
  cleanup_shell();
  return 0;
}
