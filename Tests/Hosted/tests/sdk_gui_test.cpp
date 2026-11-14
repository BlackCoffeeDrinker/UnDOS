#include <catch2/catch_test_macros.hpp>
#include <windows.h>
#include <undosgui/extensions.h>

#include "private/backend.h"
#include "private/font_mapping.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <map>
#include <set>

namespace {
extern "C" void undosgui_test_reset(void);
extern "C" void undosgui_ui_test_reset(void);
extern "C" void undosgui_test_reset_menu(void);
extern "C" void undosgui_test_track_dc(uint32_t handle, uint32_t window);

enum class Operation {
  None,
  RegisterClass,
  CreateWindow,
  DestroyWindow,
  ShowWindow,
  MoveWindow,
  SetMenu,
  SetLong,
  SetText,
  AppendMenu,
  GetMessage,
  PeekMessage,
  BeginPaint,
  EndPaint,
  SetColors,
  AcquireFont,
  CloseFont,
  SelectFont,
  GetFontMetrics,
  MeasureText,
  DrawText,
  SetFocus,
  CreateDisplayDc,
  GetDisplaySize,
  DeleteDisplayDc
};

struct NativeWindow {
  uint32_t style{};
  uint32_t userData{};
  uint32_t parent{};
};

Operation failOperation{};
uint32_t failStatus{UI_STATUS_INVALID_ARGUMENT};
std::string failCreateClass;
uint32_t nextWindow{10};
uint32_t nextDc{0x10001};
uint32_t nextMenu{20};
uint32_t lastRegisteredBackground{};
uintptr_t lastRegisteredProcedure{};
std::string lastClassName, lastTitle, lastSetTitle, lastMenuText;
uint32_t lastCreateStyle{}, lastCreateParent{}, lastCreateUserData{};
uint32_t lastShowCommand{}, lastPeekRemove{}, lastMenuFlags{}, lastMenuItem{};
std::array<int32_t, 4> lastMove{};
uint32_t moveCalls{};
uint32_t failMoveCall{}, failMoveStatus{UI_STATUS_RETRY};
UiRect backendWindowRect{0, 0, 33, 44};
UiRect backendClientRect{1, 2, 31, 42};
uint32_t lastForeground{}, lastBackground{}, lastOpaque{};
uint32_t lastInvalidatedErase{};
UiRect lastInvalidatedRect{};
bool lastInvalidatedWhole{};
uint32_t lastPostedWindow{}, lastPostedMessage{}, lastPostedWParam{}, lastPostedLParam{};
uint32_t postCalls{};
uint32_t focusCalls{}, lastFocused{};
uint32_t failFocusCall{}, failFocusStatus{UI_STATUS_RETRY};
uint32_t acquireFontCalls{};
uint32_t displayCreateCalls{}, displayQueryCalls{}, displayDeleteCalls{};
uint32_t displayToken{}, displayWidth{640}, displayHeight{480};
bool changeDisplayOnMessage{};
uint32_t changedDisplayWidth{}, changedDisplayHeight{};
bool variableTextMetrics{};
uint32_t lastEndWindow{}, lastEndDc{}, endCalls{}, beginCalls{};
uint32_t lastClosedFont{};
uint32_t registerCalls{}, createCalls{}, destroyCalls{}, showCalls{}, setMenuCalls{};
uint32_t setTextCalls{};
uint32_t closeFontCalls{}, selectFontCalls{}, drawCalls{}, fontMeasureCalls{};
uint32_t selectedToken{};
bool selectedTokenValid{};
uint32_t returnedPreviousFont{};
uint32_t forcedDestroyStatus{};
uint32_t cleanupEndStatus{};
uint32_t cleanupFontStatus{};
uint32_t failCloseFontCall{};
uint32_t failCloseFontStatus{UI_STATUS_OUT_OF_MEMORY};
std::array<uint32_t, 4> longRequest{};
std::vector<UiMessage> queue;
std::vector<UiMessage> scriptedMenuMessages;
std::vector<UiMessage> scriptedDialogMessages;
std::vector<std::string> drawnStrings;
std::vector<std::array<int32_t, 6>> gdiOps;
std::unordered_map<uint32_t, std::vector<uint8_t>> fakeBitmaps;
std::unordered_map<uint32_t, uint32_t> memoryDcBitmaps;
std::vector<std::array<int32_t, 8>> blits;
uint32_t nextBitmap{0x30001};
uint32_t failDeleteStatus{};
std::vector<uint32_t> menuHandles;
std::unordered_map<uint32_t, std::vector<std::pair<uint32_t, uint32_t>>> fakeMenus;
std::vector<uint32_t> destroyRequests;
std::vector<std::array<int32_t, 4>> moveRequests;
std::unordered_map<uint32_t, NativeWindow> nativeWindows;
bool recursiveDestroyResult{};
bool operationAfterDestroyResult{};
uint32_t destroyCallbackCalls{};
HWND parentToDestroyFromChild{};
bool nestedParentDestroyResult{};
uint32_t ownerCharCalls{};
uint32_t ownerCommandCalls{}, ownerMouseCalls{}, unrelatedMessageCalls{};
uint32_t ownerSystemCommandCalls{}, ownerCloseCalls{};
UNDOS_UI_CONTEXT *nestedDialogContext{};
DWORD nestedDialogError{}, nestedDialogStatus{};

uint32_t status(Operation operation) {
  if (failOperation != operation) return UI_STATUS_OK;
  failOperation = Operation::None;
  return failStatus;
}

uint32_t fakeTicks = 0;
int waitMessageCalls = 0;
uint32_t lastWaitTimeout = 0;
uint32_t fakeCapture = 0;
struct FakeFile { uint64_t size; uint32_t attributes; uint32_t writeTime = 0; };
std::map<std::string, FakeFile> fakeFiles;
struct FakeMount { std::string path, label, fileSystem; uint32_t serial, flags; };
std::vector<FakeMount> fakeMounts;
std::map<uint32_t, std::string> fakeOpen;
uint32_t fakeNextHandle = 1;
std::vector<std::array<std::string, 3>> fakeFileOps;
std::vector<std::pair<std::string, std::string>> fakeProcesses;
void resetBackend() {
  undosgui_test_reset();
  undosgui_ui_test_reset();
  undosgui_test_reset_menu();
  fakeMenus.clear();
  failOperation = Operation::None;
  failStatus = UI_STATUS_INVALID_ARGUMENT;
  failCreateClass.clear();
  fakeTicks = 0;
  waitMessageCalls = 0;
  lastWaitTimeout = 0;
  fakeCapture = 0;
  fakeFiles = {{"/", {0, 0x10}}};
  fakeMounts = {{"/", "UNDOS", "FAT16", 0x1234ABCDu, UI_VOLUME_CASE_PRESERVED}};
  fakeOpen.clear();
  fakeFileOps.clear();
  fakeProcesses.clear();
  nextWindow = 10;
  nextDc = 0x10001;
  nextMenu = 20;
  lastRegisteredBackground = lastCreateStyle = lastCreateParent = 0;
  lastCreateUserData = lastShowCommand = lastPeekRemove = 0;
  lastMenuFlags = lastMenuItem = lastForeground = lastBackground = 0;
  lastMove = {};
  moveCalls = 0;
  failMoveCall = 0;
  failMoveStatus = UI_STATUS_RETRY;
  backendWindowRect = {0, 0, 33, 44};
  backendClientRect = {1, 2, 31, 42};
  lastOpaque = lastEndWindow = lastEndDc = endCalls = beginCalls = 0;
  lastClosedFont = UINT32_MAX;
  registerCalls = createCalls = destroyCalls = showCalls = setMenuCalls = 0;
  setTextCalls = 0;
  lastInvalidatedErase = 0;
  lastInvalidatedRect = {};
  lastInvalidatedWhole = false;
  lastPostedWindow = lastPostedMessage = lastPostedWParam = lastPostedLParam = 0;
  postCalls = 0;
  focusCalls = lastFocused = 0;
  failFocusCall = 0;
  failFocusStatus = UI_STATUS_RETRY;
  acquireFontCalls = 0;
  displayCreateCalls = displayQueryCalls = displayDeleteCalls = 0;
  displayToken = 0;
  displayWidth = 640;
  displayHeight = 480;
  changeDisplayOnMessage = false;
  changedDisplayWidth = changedDisplayHeight = 0;
  variableTextMetrics = false;
  closeFontCalls = selectFontCalls = drawCalls = fontMeasureCalls = 0;
  selectedToken = 0;
  selectedTokenValid = false;
  returnedPreviousFont = 0;
  forcedDestroyStatus = UI_STATUS_OK;
  cleanupEndStatus = UI_STATUS_OK;
  cleanupFontStatus = UI_STATUS_OK;
  failCloseFontCall = 0;
  failCloseFontStatus = UI_STATUS_OUT_OF_MEMORY;
  failDeleteStatus = UI_STATUS_OK;
  longRequest = {};
  lastClassName.clear();
  lastTitle.clear();
  lastSetTitle.clear();
  lastMenuText.clear();
  queue.clear();
  scriptedMenuMessages.clear();
  scriptedDialogMessages.clear();
  drawnStrings.clear();
  gdiOps.clear();
  fakeBitmaps.clear();
  memoryDcBitmaps.clear();
  blits.clear();
  menuHandles.clear();
  destroyRequests.clear();
  moveRequests.clear();
  nativeWindows.clear();
  recursiveDestroyResult = true;
  operationAfterDestroyResult = true;
  destroyCallbackCalls = 0;
  ownerCharCalls = 0;
  ownerCommandCalls = ownerMouseCalls = unrelatedMessageCalls = 0;
  ownerSystemCommandCalls = ownerCloseCalls = 0;
  nestedDialogContext = NULL;
  nestedDialogError = nestedDialogStatus = 0;
  parentToDestroyFromChild = nullptr;
  nestedParentDestroyResult = false;
}

LRESULT CALLBACK test_proc(HWND window, UINT message, WPARAM, LPARAM) {
  if (message == WM_DESTROY) {
    ++destroyCallbackCalls;
    recursiveDestroyResult = DestroyWindow(window);
  }
  return 73;
}

LRESULT CALLBACK destroy_parent_from_child_proc(HWND window, UINT message,
                                                 WPARAM, LPARAM) {
  if (message == WM_DESTROY) {
    ++destroyCallbackCalls;
    if (window != parentToDestroyFromChild)
      nestedParentDestroyResult = DestroyWindow(parentToDestroyFromChild);
    else
      recursiveDestroyResult = DestroyWindow(window);
  }
  return 79;
}

LRESULT CALLBACK replacement_proc(HWND, UINT, WPARAM, LPARAM) {
  return 101;
}

LRESULT CALLBACK self_destroy_proc(HWND window, UINT message, WPARAM, LPARAM) {
  if (message == WM_USER) {
    (void)DestroyWindow(window);
    operationAfterDestroyResult = ShowWindow(window, SW_SHOW);
  }
  return 102;
}

LRESULT CALLBACK destroy_on_user_proc(HWND window, UINT message, WPARAM,
                                      LPARAM) {
  if (message == WM_USER) (void)DestroyWindow(window);
  return 103;
}

LRESULT CALLBACK count_owner_char_proc(HWND, UINT message, WPARAM, LPARAM) {
  if (message == WM_CHAR) ++ownerCharCalls;
  return 104;
}

LRESULT CALLBACK dialog_gate_owner_proc(HWND, UINT message, WPARAM, LPARAM) {
  if (message == WM_COMMAND) ++ownerCommandCalls;
  if (message == WM_LBUTTONDOWN) ++ownerMouseCalls;
  return 105;
}

LRESULT CALLBACK unrelated_message_proc(HWND, UINT message, WPARAM, LPARAM) {
  if (message == WM_USER) ++unrelatedMessageCalls;
  return 106;
}

LRESULT CALLBACK system_command_owner_proc(HWND window, UINT message,
                                            WPARAM wParam, LPARAM lParam) {
  if (message == WM_SYSCOMMAND) ++ownerSystemCommandCalls;
  if (message == WM_CLOSE) ++ownerCloseCalls;
  return DefWindowProc(window, message, wParam, lParam);
}

LRESULT CALLBACK nested_dialog_owner_proc(HWND window, UINT message, WPARAM,
                                          LPARAM) {
  if (message == WM_USER) {
    char value[4] = "old";
    UNDOS_DIALOG_FIELD field{"Nested", value, sizeof(value)};
    (void)UndosRunDialog(nestedDialogContext, window, "Nested", "Rejected",
                         &field, 1, TRUE);
    nestedDialogError = GetLastError();
    nestedDialogStatus = UndosGetLastStatus();
  }
  return 107;
}

WNDCLASS test_class(const char *name = "sdk-test") {
  WNDCLASS value{};
  value.lpfnWndProc = test_proc;
  value.lpszClassName = name;
  value.hbrBackground = UNDOS_PALETTE_BRUSH(7);
  return value;
}

void erase_native_window_tree(uint32_t window) {
  std::vector<uint32_t> children;
  for (const auto &[child, record] : nativeWindows)
    if (record.parent == window) children.push_back(child);
  for (uint32_t child : children) erase_native_window_tree(child);
  nativeWindows.erase(window);
}

HWND make_window(const char *name = "sdk-test", DWORD style = 0,
                 HWND parent = nullptr) {
  WNDCLASS klass = test_class(name);
  REQUIRE(RegisterClass(&klass) != 0);
  HWND window = CreateWindow(name, "sdk window", style, 0, 0, 200, 100,
                             parent, nullptr, nullptr, nullptr);
  REQUIRE(window != nullptr);
  return window;
}

HWND make_ui_owner(UNDOS_UI_CONTEXT *context, const char *name,
                   WNDPROC procedure = test_proc) {
  WNDCLASS klass = test_class(name);
  klass.lpfnWndProc = procedure;
  REQUIRE(RegisterClass(&klass) != 0);
  HWND window = CreateWindow(name, "UI owner", 0, 0, 0, 240, 160,
                             nullptr, nullptr, nullptr, nullptr);
  REQUIRE(window);
  REQUIRE(UndosAttachUiContext(window, context));
  return window;
}

LPARAM pack_point(int x, int y) {
  return static_cast<LPARAM>(
      (static_cast<uint32_t>(y) << 16) | (static_cast<uint32_t>(x) & 0xFFFFu));
}

} // namespace

extern "C" void sdk_gui_reset_backend_for_migration() {
  resetBackend();
}

extern "C" void sdk_gui_queue_for_migration(uint32_t window, uint32_t message,
                                             uint32_t wParam, uint32_t lParam) {
  queue.push_back({window, message, wParam, lParam, 0});
}

extern "C" uint32_t sdk_gui_focus_calls() { return focusCalls; }
extern "C" uint32_t sdk_gui_last_focused() { return lastFocused; }
extern "C" uint32_t sdk_gui_display_create_calls() {
  return displayCreateCalls;
}
extern "C" uint32_t sdk_gui_display_query_calls() {
  return displayQueryCalls;
}
extern "C" uint32_t sdk_gui_display_delete_calls() {
  return displayDeleteCalls;
}

extern "C" uint32_t sdk_gui_draw_calls_for_migration() {
  return drawCalls;
}

extern "C" uint32_t sdk_gui_move_calls_for_migration() {
  return moveCalls;
}

extern "C" uint32_t sdk_gui_last_create_parent_for_migration() {
  return lastCreateParent;
}

extern "C" uint32_t sdk_gui_pending_messages_for_migration() {
  return static_cast<uint32_t>(queue.size());
}

extern "C" bool sdk_gui_saw_draw_text_for_migration(const char *text) {
  for (const auto &drawn : drawnStrings)
    if (drawn == text) return true;
  return false;
}

extern "C" void sdk_gui_set_client_size_for_migration(uint32_t width,
                                                        uint32_t height) {
  backendClientRect = {0, 0, static_cast<int32_t>(width),
                       static_cast<int32_t>(height)};
}

extern "C" void sdk_gui_set_display_size_for_migration(uint32_t width,
                                                         uint32_t height) {
  displayWidth = width;
  displayHeight = height;
}

extern "C" void sdk_gui_script_menu_message(uint32_t message, uint32_t wParam,
                                             uint32_t lParam) {
  scriptedMenuMessages.push_back({0, message, wParam, lParam, 0});
}

extern "C" void sdk_gui_script_dialog_message(uint32_t message,
                                               uint32_t wParam,
                                               uint32_t lParam) {
  scriptedDialogMessages.push_back({0, message, wParam, lParam, 0});
}

extern "C" void sdk_gui_change_display_on_message(uint32_t width,
                                                    uint32_t height) {
  changeDisplayOnMessage = true;
  changedDisplayWidth = width;
  changedDisplayHeight = height;
}

extern "C" void sdk_gui_fail_next_register_for_migration() {
  failOperation = Operation::RegisterClass;
  failStatus = UI_STATUS_NOT_FOUND;
}

extern "C" void sdk_gui_fail_create_class_for_migration(const char *name) {
  failCreateClass = name ? name : "";
}

extern "C" void sdk_gui_fail_next_move_for_migration(uint32_t statusValue) {
  failMoveCall = moveCalls + 1;
  failMoveStatus = statusValue;
}

extern "C" void sdk_gui_fail_menu_cleanup_for_migration() {
  returnedPreviousFont = 77;
  sdk_gui_script_menu_message(WM_PAINT, 0, 0);
  sdk_gui_script_menu_message(WM_KEYDOWN, VK_ESCAPE, 0);
  cleanupEndStatus = UI_STATUS_RETRY;
  failCloseFontCall = closeFontCalls + 2;
  failCloseFontStatus = UI_STATUS_OUT_OF_MEMORY;
}

extern "C" void sdk_gui_fail_creation_rollback_for_migration() {
  failOperation = Operation::ShowWindow;
  failStatus = UI_STATUS_RETRY;
  forcedDestroyStatus = UI_STATUS_OUT_OF_MEMORY;
}

extern "C" void sdk_gui_fail_ui_rollback_for_migration() {
  failOperation = Operation::SetFocus;
  failStatus = UI_STATUS_INVALID_ADDRESS;
  forcedDestroyStatus = UI_STATUS_OUT_OF_MEMORY;
}

extern "C" void sdk_gui_fail_next_destroy_for_migration(uint32_t statusValue) {
  forcedDestroyStatus = statusValue;
}

extern "C" uint32_t sdk_gui_destroy_calls_for_migration() {
  return destroyCalls;
}

extern "C" {
void *crt_malloc(size_t size) { return std::malloc(size); }
void crt_free(void *memory) { std::free(memory); }
void *crt_realloc(void *memory, size_t size) {
  return std::realloc(memory, size);
}

uint32_t ui_backend_register_class(const char *name, uintptr_t procedure,
                                  uint32_t background, uint32_t *atom) {
  ++registerCalls;
  uint32_t result = status(Operation::RegisterClass);
  if (result != UI_STATUS_OK) return result;
  lastClassName = name;
  lastRegisteredProcedure = procedure;
  lastRegisteredBackground = background;
  *atom = 501;
  return UI_STATUS_OK;
}

uint32_t ui_backend_create_window(const char *className, const char *title,
                                  uint32_t style, int32_t, int32_t, int32_t,
                                  int32_t, uint32_t parent, uint32_t userData,
                                  uint32_t *window) {
  ++createCalls;
  if (!failCreateClass.empty() && failCreateClass == className) {
    failCreateClass.clear();
    return UI_STATUS_OUT_OF_MEMORY;
  }
  uint32_t result = status(Operation::CreateWindow);
  if (result != UI_STATUS_OK) return result;
  lastClassName = className;
  lastTitle = title;
  lastCreateStyle = style;
  lastCreateParent = parent;
  lastCreateUserData = userData;
  *window = nextWindow++;
  nativeWindows[*window] = {style, userData, parent};
  auto appendScript = [&](const std::vector<UiMessage> &script) {
    for (UiMessage message : script) {
      message.hwnd = *window;
      queue.push_back(message);
    }
  };
  if (className == std::string("UndosUiMenu")) {
    appendScript(scriptedMenuMessages);
    scriptedMenuMessages.clear();
  }
  if (className == std::string("UndosUiDialog")) {
    appendScript(scriptedDialogMessages);
    scriptedDialogMessages.clear();
  }
  return UI_STATUS_OK;
}

uint32_t ui_backend_destroy_window(uint32_t window) {
  ++destroyCalls;
  destroyRequests.push_back(window);
  if (forcedDestroyStatus != UI_STATUS_OK) {
    uint32_t result = forcedDestroyStatus;
    forcedDestroyStatus = UI_STATUS_OK;
    return result;
  }
  uint32_t result = status(Operation::DestroyWindow);
  if (result == UI_STATUS_OK) erase_native_window_tree(window);
  return result;
}

uint32_t ui_backend_show_window(uint32_t window, uint32_t command) {
  ++showCalls;
  uint32_t result = status(Operation::ShowWindow);
  if (result == UI_STATUS_OK) {
    lastShowCommand = command;
    if (command == UI_SHOW_HIDE) nativeWindows[window].style &= ~UI_WINDOW_VISIBLE;
    else nativeWindows[window].style |= UI_WINDOW_VISIBLE;
  }
  return result;
}

uint32_t ui_backend_move_window(uint32_t, int32_t x, int32_t y, int32_t width,
                                int32_t height) {
  ++moveCalls;
  lastMove = {x, y, width, height};
  moveRequests.push_back(lastMove);
  if (moveCalls == failMoveCall) {
    failMoveCall = 0;
    return failMoveStatus;
  }
  return status(Operation::MoveWindow);
}

uint32_t ui_backend_invalidate(uint32_t, const UiRect *rect, uint32_t erase) {
  lastInvalidatedWhole = rect == nullptr;
  if (rect) lastInvalidatedRect = *rect;
  lastInvalidatedErase = erase;
  return UI_STATUS_OK;
}

uint32_t ui_backend_get_rect(uint32_t, uint32_t client, UiRect *rect) {
  *rect = client ? backendClientRect : backendWindowRect;
  return UI_STATUS_OK;
}

uint32_t ui_backend_get_window_long(uint32_t window, uint32_t index, uint32_t *value) {
  longRequest[0] = index;
  auto found = nativeWindows.find(window);
  if (found == nativeWindows.end()) return UI_STATUS_INVALID_ARGUMENT;
  *value = index == UI_WINDOW_STYLE ? found->second.style : found->second.userData;
  return UI_STATUS_OK;
}

uint32_t ui_backend_set_window_long(uint32_t window, uint32_t index, uint32_t value,
                                    uint32_t *previous) {
  uint32_t result = status(Operation::SetLong);
  if (result != UI_STATUS_OK) return result;
  longRequest = {index, value, 0, 0};
  auto found = nativeWindows.find(window);
  if (found == nativeWindows.end()) return UI_STATUS_INVALID_ARGUMENT;
  if (index == UI_WINDOW_STYLE) {
    *previous = found->second.style;
    found->second.style = value;
  } else {
    *previous = found->second.userData;
    found->second.userData = value;
  }
  return UI_STATUS_OK;
}

uint32_t ui_backend_set_window_text(uint32_t, const char *text) {
  ++setTextCalls;
  uint32_t result = status(Operation::SetText);
  if (result == UI_STATUS_OK) lastSetTitle = text;
  return result;
}

uint32_t ui_backend_create_menu(uint32_t, uint32_t *menu) {
  *menu = nextMenu++;
  menuHandles.push_back(*menu);
  return UI_STATUS_OK;
}

uint32_t ui_backend_destroy_menu(uint32_t) { return UI_STATUS_OK; }

uint32_t ui_backend_append_menu(uint32_t menu, uint32_t flags, uint32_t item,
                                const char *text) {
  uint32_t result = status(Operation::AppendMenu);
  if (result == UI_STATUS_OK) {
    fakeMenus[menu].push_back({flags, item});
    lastMenuFlags = flags;
    lastMenuItem = item;
    lastMenuText = text;
  }
  return result;
}

uint32_t ui_backend_set_menu(uint32_t, uint32_t) {
  ++setMenuCalls;
  return status(Operation::SetMenu);
}
static std::pair<uint32_t, uint32_t> *fake_menu_command(uint32_t menu,
                                                       uint32_t command) {
  auto found = fakeMenus.find(menu);
  if (found == fakeMenus.end()) return nullptr;
  for (auto &entry : found->second) {
    if (entry.first & UI_MENU_SEPARATOR) continue;
    if (entry.first & UI_MENU_POPUP) {
      if (auto *nested = fake_menu_command(entry.second, command)) return nested;
    } else if (entry.second == command) {
      return &entry;
    }
  }
  return nullptr;
}

uint32_t ui_backend_modify_menu_state(uint32_t menu, uint32_t item,
                                      uint32_t byPosition, uint32_t mask,
                                      uint32_t flags, uint32_t *previous,
                                      uint32_t *itemId) {
  auto found = fakeMenus.find(menu);
  if (found == fakeMenus.end() &&
      std::find(menuHandles.begin(), menuHandles.end(), menu) == menuHandles.end())
    return UI_STATUS_INVALID_ARGUMENT;
  std::pair<uint32_t, uint32_t> *entry = nullptr;
  if (byPosition) {
    if (found == fakeMenus.end() || item >= found->second.size())
      return UI_STATUS_NOT_FOUND;
    entry = &found->second[item];
  } else {
    entry = fake_menu_command(menu, item);
    if (!entry) return UI_STATUS_NOT_FOUND;
  }
  *previous = entry->first;
  *itemId = entry->second;
  if (mask && (entry->first & UI_MENU_SEPARATOR)) return UI_STATUS_INVALID_ARGUMENT;
  entry->first = (entry->first & ~mask) | flags;
  return UI_STATUS_OK;
}

uint32_t ui_backend_remove_menu_item(uint32_t menu, uint32_t item,
                                     uint32_t byPosition, uint32_t *itemId) {
  auto found = fakeMenus.find(menu);
  if (found == fakeMenus.end()) return UI_STATUS_NOT_FOUND;
  auto &entries = found->second;
  for (size_t i = 0; i < entries.size(); ++i) {
    const bool match = byPosition ? i == item
                                  : !(entries[i].first & UI_MENU_SEPARATOR) &&
                                        entries[i].second == item;
    if (!match) continue;
    *itemId = entries[i].second;
    entries.erase(entries.begin() + static_cast<long>(i));
    return UI_STATUS_OK;
  }
  return UI_STATUS_NOT_FOUND;
}
// Like the compositor, post WM_CAPTURECHANGED to the window losing capture.
static void fake_capture(uint32_t window) {
  if (fakeCapture && fakeCapture != window)
    queue.push_back({fakeCapture, WM_CAPTURECHANGED, 0, window, 0});
  fakeCapture = window;
}
uint32_t ui_backend_set_capture(uint32_t window) {
  fake_capture(window);
  return UI_STATUS_OK;
}
uint32_t ui_backend_release_capture(void) {
  fake_capture(0);
  return UI_STATUS_OK;
}
uint32_t ui_backend_tick_ms(void) { return fakeTicks; }
void ui_backend_sleep(uint32_t ms) { fakeTicks += ms ? ms : 1; }

struct FakeEvent {
  bool manual = false;
  bool signalled = false;
};
std::map<uint32_t, FakeEvent> fakeEvents;
std::map<uint32_t, uint32_t> fakeThreadExit;// handle -> exit code (absent: running)
std::set<uint32_t> fakeThreads;
uint32_t fakeNextKernelHandle = 0x500;
uint32_t lastThreadEntry = 0, lastThreadParameter = 0, lastThreadStack = 0;
uint32_t fakeThreadId = 7;
uint32_t windowDcCalls = 0, windowDcReleases = 0, lastReleasedDc = 0;
uint32_t windowDcStatus = UI_STATUS_OK;

uint32_t ui_backend_create_thread(uint32_t entry, uint32_t parameter, uint32_t stackSize,
                                  uint32_t *handle, uint32_t *threadId) {
  if (stackSize > 0x100000u) return UI_STATUS_OUT_OF_MEMORY;
  lastThreadEntry = entry;
  lastThreadParameter = parameter;
  lastThreadStack = stackSize;
  *handle = fakeNextKernelHandle++;
  *threadId = 40 + *handle;
  fakeThreads.insert(*handle);
  return UI_STATUS_OK;
}
void ui_backend_exit_thread(uint32_t) { std::abort(); }
uint32_t ui_backend_thread_exit_code(uint32_t handle, uint32_t *code) {
  if (!fakeThreads.count(handle)) return UI_STATUS_INVALID_ARGUMENT;
  const auto exited = fakeThreadExit.find(handle);
  if (exited == fakeThreadExit.end()) return UI_STATUS_RETRY;
  *code = exited->second;
  return UI_STATUS_OK;
}
uint32_t ui_backend_current_thread_id(void) { return fakeThreadId; }
uint32_t ui_backend_wait_object(uint32_t handle, uint32_t) {
  if (fakeThreads.count(handle))
    return fakeThreadExit.count(handle) ? UI_STATUS_OK : UI_STATUS_TIMEOUT;
  const auto event = fakeEvents.find(handle);
  if (event == fakeEvents.end()) return UI_STATUS_INVALID_ARGUMENT;
  if (!event->second.signalled) return UI_STATUS_TIMEOUT;
  if (!event->second.manual) event->second.signalled = false;
  return UI_STATUS_OK;
}
uint32_t ui_backend_create_event(uint32_t manualReset, uint32_t initialState, uint32_t *handle) {
  *handle = fakeNextKernelHandle++;
  fakeEvents[*handle] = {manualReset != 0, initialState != 0};
  return UI_STATUS_OK;
}
uint32_t ui_backend_set_event(uint32_t handle) {
  const auto event = fakeEvents.find(handle);
  if (event == fakeEvents.end()) return UI_STATUS_INVALID_ARGUMENT;
  event->second.signalled = true;
  return UI_STATUS_OK;
}
uint32_t ui_backend_reset_event(uint32_t handle) {
  const auto event = fakeEvents.find(handle);
  if (event == fakeEvents.end()) return UI_STATUS_INVALID_ARGUMENT;
  event->second.signalled = false;
  return UI_STATUS_OK;
}
uint32_t ui_backend_close_handle(uint32_t handle) {
  if (fakeEvents.erase(handle) || fakeThreads.erase(handle)) return UI_STATUS_OK;
  return UI_STATUS_INVALID_ARGUMENT;
}
uint32_t ui_backend_get_dc(uint32_t, uint32_t *dc) {
  if (windowDcStatus != UI_STATUS_OK) return windowDcStatus;
  ++windowDcCalls;
  *dc = nextDc++;
  return UI_STATUS_OK;
}
uint32_t ui_backend_release_dc(uint32_t, uint32_t dc) {
  ++windowDcReleases;
  lastReleasedDc = dc;
  return UI_STATUS_OK;
}

uint32_t ui_backend_file_open(const char *path, uint32_t *handle) {
  if (!fakeFiles.count(path)) return UI_STATUS_NOT_FOUND;
  *handle = fakeNextHandle++;
  fakeOpen[*handle] = path;
  return UI_STATUS_OK;
}
uint32_t ui_backend_file_close(uint32_t handle) {
  return fakeOpen.erase(handle) ? UI_STATUS_OK : UI_STATUS_INVALID_ARGUMENT;
}
static std::vector<std::string> fake_children(const std::string &dir) {
  std::vector<std::string> names;
  const std::string prefix = dir == "/" ? "/" : dir + "/";
  for (const auto &[path, file] : fakeFiles)
    if (path != "/" && path.rfind(prefix, 0) == 0 &&
        path.find('/', prefix.size()) == std::string::npos)
      names.push_back(path.substr(prefix.size()));
  return names;
}
uint32_t ui_backend_file_read_directory(uint32_t handle, uint32_t index,
                                        char *name, uint32_t capacity,
                                        UiFileInfo *info, uint32_t *found) {
  auto open = fakeOpen.find(handle);
  if (open == fakeOpen.end()) return UI_STATUS_INVALID_ARGUMENT;
  const auto names = fake_children(open->second);
  *found = index < names.size();
  if (!*found) return UI_STATUS_OK;
  if (names[index].size() + 1 > capacity) return UI_STATUS_INVALID_SIZE;
  std::memcpy(name, names[index].c_str(), names[index].size() + 1);
  const auto prefix = open->second == "/" ? std::string("/") : open->second + "/";
  const auto &file = fakeFiles[prefix + names[index]];
  *info = {file.size, file.attributes, file.writeTime, file.writeTime & 0xFFFF0000u,
           file.writeTime};
  return UI_STATUS_OK;
}
uint32_t ui_backend_file_information(uint32_t handle, UiFileInfo *info) {
  auto open = fakeOpen.find(handle);
  if (open == fakeOpen.end()) return UI_STATUS_INVALID_ARGUMENT;
  const auto &file = fakeFiles[open->second];
  *info = {file.size, file.attributes, file.writeTime, file.writeTime & 0xFFFF0000u,
           file.writeTime};
  return UI_STATUS_OK;
}
uint32_t ui_backend_file_set_attributes(const char *path, uint32_t attributes) {
  fakeFileOps.push_back({"attrib", path, std::to_string(attributes)});
  auto file = fakeFiles.find(path);
  if (file == fakeFiles.end()) return UI_STATUS_NOT_FOUND;
  if (attributes & ~0x27u || std::string(path) == "/") return UI_STATUS_INVALID_ARGUMENT;
  file->second.attributes = (file->second.attributes & 0x10u) | attributes;
  return UI_STATUS_OK;
}
uint32_t ui_backend_query_mount(uint32_t index, char *name, uint32_t capacity,
                                uint32_t *found) {
  *found = index < fakeMounts.size();
  if (!*found) return UI_STATUS_OK;
  const auto &path = fakeMounts[index].path;
  if (path.size() + 1 > capacity) return UI_STATUS_INVALID_SIZE;
  std::memcpy(name, path.c_str(), path.size() + 1);
  return UI_STATUS_OK;
}
uint32_t ui_backend_volume_information(const char *mountPoint, char *label,
                                       uint32_t labelCapacity, char *fileSystem,
                                       uint32_t fileSystemCapacity,
                                       uint32_t *serial, uint32_t *maxComponent,
                                       uint32_t *flags) {
  for (const auto &mount : fakeMounts) {
    if (mount.path != mountPoint) continue;
    if (label) {
      if (mount.label.size() + 1 > labelCapacity) return UI_STATUS_INVALID_SIZE;
      std::memcpy(label, mount.label.c_str(), mount.label.size() + 1);
    }
    if (fileSystem) {
      if (mount.fileSystem.size() + 1 > fileSystemCapacity) return UI_STATUS_INVALID_SIZE;
      std::memcpy(fileSystem, mount.fileSystem.c_str(), mount.fileSystem.size() + 1);
    }
    *serial = mount.serial;
    *maxComponent = 255;
    *flags = mount.flags;
    return UI_STATUS_OK;
  }
  return UI_STATUS_NOT_FOUND;
}
uint32_t ui_backend_file_operate(uint32_t operation, const char *source,
                                 const char *destination) {
  fakeFileOps.push_back({std::to_string(operation), source,
                         destination ? destination : ""});
  switch (operation) {
    case UI_FILE_COPY: fakeFiles[destination] = fakeFiles[source]; break;
    case UI_FILE_MOVE:
      fakeFiles[destination] = fakeFiles[source];
      fakeFiles.erase(source);
      break;
    case UI_FILE_DELETE: fakeFiles.erase(source); break;
    case UI_FILE_MKDIR: fakeFiles[source] = {0, 0x10}; break;
  }
  return UI_STATUS_OK;
}
uint32_t ui_backend_create_process(const char *path, const char *arguments) {
  if (!fakeFiles.count(path)) return UI_STATUS_NOT_FOUND;
  fakeProcesses.push_back({path, arguments});
  return UI_STATUS_OK;
}

uint32_t ui_backend_get_menu(uint32_t, uint32_t *menu) {
  *menu = menuHandles.empty() ? 0 : menuHandles.back();
  return UI_STATUS_OK;
}

uint32_t ui_backend_get_message(UiMessage *message) {
  uint32_t result = status(Operation::GetMessage);
  if (result != UI_STATUS_OK) return result;
  if (queue.empty()) return UI_STATUS_TIMEOUT;
  *message = queue.front();
  queue.erase(queue.begin());
  if (message->message == WM_DISPLAYCHANGE && changeDisplayOnMessage) {
    displayWidth = changedDisplayWidth;
    displayHeight = changedDisplayHeight;
    changeDisplayOnMessage = false;
  }
  return UI_STATUS_OK;
}

uint32_t ui_backend_peek_message(UiMessage *message, uint32_t remove) {
  uint32_t result = status(Operation::PeekMessage);
  if (result != UI_STATUS_OK) return result;
  lastPeekRemove = remove;
  if (queue.empty()) return UI_STATUS_TIMEOUT;
  *message = queue.front();
  if (remove) queue.erase(queue.begin());
  return UI_STATUS_OK;
}

uint32_t ui_backend_wait_message(uint32_t timeoutMs) {
  ++waitMessageCalls;
  lastWaitTimeout = timeoutMs;
  if (!queue.empty()) return UI_STATUS_OK;
  // An empty fake queue never fills while waiting: let the timeout elapse.
  if (timeoutMs == UINT32_MAX) return UI_STATUS_TIMEOUT;
  fakeTicks += timeoutMs ? timeoutMs : 1;
  return UI_STATUS_TIMEOUT;
}

uint32_t ui_backend_post_message(uint32_t window, uint32_t message,
                                 uint32_t wParam, uint32_t lParam) {
  ++postCalls;
  lastPostedWindow = window;
  lastPostedMessage = message;
  lastPostedWParam = wParam;
  lastPostedLParam = lParam;
  queue.push_back({window, message, wParam, lParam, 0});
  return UI_STATUS_OK;
}

uint32_t ui_backend_set_focus(uint32_t window) {
  ++focusCalls;
  if (focusCalls == failFocusCall) {
    failFocusCall = 0;
    return failFocusStatus;
  }
  const uint32_t result = status(Operation::SetFocus);
  if (result == UI_STATUS_OK) lastFocused = window;
  return result;
}

uint32_t ui_backend_create_display_dc(uint32_t *dc) {
  ++displayCreateCalls;
  const uint32_t result = status(Operation::CreateDisplayDc);
  if (result == UI_STATUS_OK) *dc = displayToken;
  return result;
}

uint32_t ui_backend_get_display_size(uint32_t, uint32_t *width,
                                     uint32_t *height) {
  ++displayQueryCalls;
  const uint32_t result = status(Operation::GetDisplaySize);
  if (result == UI_STATUS_OK) {
    *width = displayWidth;
    *height = displayHeight;
  }
  return result;
}

uint32_t ui_backend_delete_dc(uint32_t) {
  ++displayDeleteCalls;
  if (failDeleteStatus) {
    const uint32_t result = failDeleteStatus;
    failDeleteStatus = UI_STATUS_OK;
    return result;
  }
  return status(Operation::DeleteDisplayDc);
}

uint32_t ui_backend_begin_paint(uint32_t, uint32_t *dc, UiPaintInfo *paint) {
  uint32_t result = status(Operation::BeginPaint);
  if (result != UI_STATUS_OK) return result;
  ++beginCalls;
  *dc = nextDc++;
  paint->rcPaint = {4, 5, 64, 75};
  paint->erased = 1;
  return UI_STATUS_OK;
}

uint32_t ui_backend_end_paint(uint32_t window, uint32_t dc) {
  ++endCalls;
  lastEndWindow = window;
  lastEndDc = dc;
  uint32_t result = cleanupEndStatus;
  cleanupEndStatus = UI_STATUS_OK;
  if (result == UI_STATUS_OK) result = status(Operation::EndPaint);
  return result;
}

uint32_t ui_backend_set_draw_colors(uint32_t, uint32_t foreground,
                                    uint32_t background, uint32_t opaque) {
  uint32_t result = status(Operation::SetColors);
  if (result == UI_STATUS_OK) {
    lastForeground = foreground;
    lastBackground = background;
    lastOpaque = opaque;
  }
  return result;
}

uint32_t ui_backend_fill_rectangle(uint32_t, int32_t left, int32_t top,
                                   int32_t right, int32_t bottom) {
  gdiOps.push_back({1, left, top, right, bottom,
                    static_cast<int32_t>(lastForeground)});
  return UI_STATUS_OK;
}

uint32_t ui_backend_move_to(uint32_t, int32_t x, int32_t y) {
  gdiOps.push_back({2, x, y, 0, 0, static_cast<int32_t>(lastForeground)});
  return UI_STATUS_OK;
}
uint32_t ui_backend_line_to(uint32_t, int32_t x, int32_t y) {
  gdiOps.push_back({3, x, y, 0, 0, static_cast<int32_t>(lastForeground)});
  return UI_STATUS_OK;
}

uint32_t ui_backend_create_memory_dc(uint32_t *dc) {
  *dc = nextDc++;
  memoryDcBitmaps[*dc] = 0;
  return UI_STATUS_OK;
}

uint32_t ui_backend_create_bitmap(uint32_t width, uint32_t height,
                                  const uint8_t *pixels, uint32_t length,
                                  uint32_t *bitmap) {
  std::vector<uint8_t> bits(static_cast<size_t>(width) * height * 4u);
  if (pixels) std::memcpy(bits.data(), pixels, std::min<size_t>(length, bits.size()));
  *bitmap = nextBitmap++;
  fakeBitmaps[*bitmap] = std::move(bits);
  return UI_STATUS_OK;
}

uint32_t ui_backend_set_bitmap_bits(uint32_t bitmap, const uint8_t *pixels,
                                    uint32_t length) {
  auto found = fakeBitmaps.find(bitmap);
  if (found == fakeBitmaps.end()) return UI_STATUS_INVALID_ARGUMENT;
  std::memcpy(found->second.data(), pixels,
              std::min<size_t>(length, found->second.size()));
  return UI_STATUS_OK;
}

uint32_t ui_backend_get_bitmap_bits(uint32_t bitmap, uint8_t *pixels,
                                    uint32_t length) {
  auto found = fakeBitmaps.find(bitmap);
  if (found == fakeBitmaps.end()) return UI_STATUS_INVALID_ARGUMENT;
  std::memcpy(pixels, found->second.data(),
              std::min<size_t>(length, found->second.size()));
  return UI_STATUS_OK;
}

uint32_t ui_backend_select_bitmap(uint32_t dc, uint32_t bitmap) {
  if (!memoryDcBitmaps.count(dc) || !fakeBitmaps.count(bitmap))
    return UI_STATUS_INVALID_ARGUMENT;
  memoryDcBitmaps[dc] = bitmap;
  return UI_STATUS_OK;
}

uint32_t ui_backend_delete_bitmap(uint32_t bitmap) {
  return fakeBitmaps.erase(bitmap) ? UI_STATUS_OK : UI_STATUS_INVALID_ARGUMENT;
}

uint32_t ui_backend_bit_blt(uint32_t dst, int32_t x, int32_t y, uint32_t width,
                            uint32_t height, uint32_t src, int32_t srcX,
                            int32_t srcY) {
  blits.push_back({static_cast<int32_t>(dst), x, y, static_cast<int32_t>(width),
                   static_cast<int32_t>(height), static_cast<int32_t>(src), srcX,
                   srcY});
  return UI_STATUS_OK;
}

uint32_t ui_backend_acquire_font(uint32_t, uint32_t, uint32_t *font,
                                 uint32_t *generation) {
  ++acquireFontCalls;
  uint32_t result = status(Operation::AcquireFont);
  if (result != UI_STATUS_OK) return result;
  *font = 0;
  *generation = 9;
  return UI_STATUS_OK;
}

uint32_t ui_backend_close_font(uint32_t font) {
  ++closeFontCalls;
  lastClosedFont = font;
  if (closeFontCalls == failCloseFontCall) {
    failCloseFontCall = 0;
    return failCloseFontStatus;
  }
  uint32_t result = cleanupFontStatus;
  cleanupFontStatus = UI_STATUS_OK;
  return result == UI_STATUS_OK ? status(Operation::CloseFont) : result;
}

uint32_t ui_backend_select_font(uint32_t, uint32_t font, uint32_t *previous) {
  uint32_t result = status(Operation::SelectFont);
  if (result != UI_STATUS_OK) return result;
  ++selectFontCalls;
  *previous = returnedPreviousFont;
  selectedToken = font;
  selectedTokenValid = true;
  return UI_STATUS_OK;
}

uint32_t ui_backend_get_font_metrics(uint32_t, uint32_t, uint32_t,
                                     UiTextMetrics *metrics) {
  const uint32_t result = status(Operation::GetFontMetrics);
  if (result != UI_STATUS_OK) return result;
  *metrics = {9, 12, 4, 0, 16, 12, 8, 16, 0, 0, 8, 16};
  return UI_STATUS_OK;
}

uint32_t ui_backend_measure_text(uint32_t, uint32_t, uint32_t, uint32_t,
                                 const char *text, UiTextExtent *extent) {
  ++fontMeasureCalls;
  uint32_t result = status(Operation::MeasureText);
  if (result != UI_STATUS_OK) return result;
  uint32_t width = 42;
  if (variableTextMetrics) {
    width = 0;
    for (const unsigned char *p =
             reinterpret_cast<const unsigned char *>(text); *p;) {
      if (*p < 0x80) {
        width += *p == 'i' ? 3u : *p == 'W' ? 10u : *p == '.' ? 4u : 7u;
        ++p;
      } else {
        width += 8;
        p += (*p & 0xE0) == 0xC0 ? 2 :
             (*p & 0xF0) == 0xE0 ? 3 : 4;
      }
    }
  }
  *extent = {9, width, 16, 12, 0, 0,
             static_cast<int32_t>(width), 16};
  return UI_STATUS_OK;
}

uint32_t ui_backend_draw_text(uint32_t, int32_t x, int32_t y, uint32_t, uint32_t,
                              uint32_t, const char *text) {
  uint32_t result = status(Operation::DrawText);
  if (result == UI_STATUS_OK) {
    gdiOps.push_back({4, x, y, 0, 0, static_cast<int32_t>(lastForeground)});
    ++drawCalls;
    drawnStrings.emplace_back(text);
  }
  return result;
}
}

TEST_CASE("C UI validates UTF-8 before mutation and maps errors separately from raw status",
          "[sdk-gui]") {
  resetBackend();
  WNDCLASS klass = test_class();
  const char malformed[] = "\xC0\xAF";
  klass.lpszClassName = malformed;
  REQUIRE(RegisterClass(&klass) == 0);
  REQUIRE(GetLastError() == ERROR_NO_UNICODE_TRANSLATION);
  REQUIRE(UndosGetLastStatus() == UI_STATUS_INVALID_ARGUMENT);
  REQUIRE(lastClassName.empty());

  klass = test_class();
  std::string exactName(31, 'n');
  klass.lpszClassName = exactName.c_str();
  REQUIRE(RegisterClass(&klass) != 0);
  REQUIRE(lastClassName == exactName);
  REQUIRE(lastRegisteredBackground == 7);
  REQUIRE(lastRegisteredProcedure == reinterpret_cast<uintptr_t>(test_proc));

  std::string tooLongName(32, 'n');
  klass = test_class(tooLongName.c_str());
  REQUIRE(RegisterClass(&klass) == 0);
  REQUIRE(GetLastError() == ERROR_INSUFFICIENT_BUFFER);

  HWND window = CreateWindow(exactName.c_str(), "ok", 0, 0, 0, 20, 20,
                             nullptr, nullptr, nullptr, nullptr);
  REQUIRE(window != nullptr);
  const uint32_t oldTitleCalls = static_cast<uint32_t>(lastSetTitle.size());
  REQUIRE_FALSE(SetWindowText(window, malformed));
  REQUIRE(GetLastError() == ERROR_NO_UNICODE_TRANSLATION);
  REQUIRE(lastSetTitle.empty());
  REQUIRE(oldTitleCalls == 0);
  const char valid[] = "A\u00e9\u20ac\xf0\x9f\x8c\x8d";
  REQUIRE(SetWindowText(window, valid));
  REQUIRE(lastSetTitle == valid);
}

TEST_CASE("C text fitting matches probe width, byte, scalar, and failure contracts",
          "[sdk-gui][sdk-probe]") {
  resetBackend();
  variableTextMetrics = true;
  UNDOS_FONT font = UINT32_MAX;
  uint32_t generation = 0;
  REQUIRE(UndosAcquireFont(UNDOS_FONT_SYSTEM_DEFAULT,
      UNDOS_GLYPH_FALLBACK_NOTDEF, &font, &generation));
  REQUIRE(font == 0);

  UNDOS_TEXT_EXTENT narrow{}, wide{};
  REQUIRE(UndosMeasureText(font, UNDOS_TEXT_REGULAR,
      UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC, "iiii", &narrow));
  REQUIRE(UndosMeasureText(font, UNDOS_TEXT_REGULAR,
      UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC, "WWWW", &wide));
  REQUIRE(narrow.advanceWidth == 12);
  REQUIRE(wide.advanceWidth == 40);
  REQUIRE(narrow.advanceWidth < wide.advanceWidth);

  char output[16] = "unchanged";
  UNDOS_TEXT_EXTENT extent{};
  REQUIRE(UndosFitTextPrefix(font, UNDOS_TEXT_REGULAR,
      UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC, "iiii", output,
      sizeof(output), narrow.advanceWidth, &extent));
  REQUIRE(std::string(output) == "iiii");
  REQUIRE(extent.advanceWidth == narrow.advanceWidth);

  REQUIRE(UndosFitTextPrefix(font, UNDOS_TEXT_REGULAR,
      UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC, "WWWW", output, 5, 22,
      &extent));
  REQUIRE(std::string(output) == "W...");
  REQUIRE(extent.advanceWidth == 22);
  REQUIRE(UndosFitTextPrefix(font, UNDOS_TEXT_REGULAR,
      UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC, "WWWW", output, 4, 40,
      &extent));
  REQUIRE(std::string(output) == "...");
  REQUIRE(extent.advanceWidth == 12);

  REQUIRE(UndosFitTextTail(font, UNDOS_TEXT_REGULAR,
      UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC, "WWWW", output,
      sizeof(output), 20, &extent));
  REQUIRE(std::string(output) == "WW");
  REQUIRE(extent.advanceWidth == 20);
  REQUIRE(UndosFitTextTail(font, UNDOS_TEXT_REGULAR,
      UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC, "WWWW", output, 3, 40,
      &extent));
  REQUIRE(std::string(output) == "WW");

  REQUIRE(UndosFitTextPrefix(font, UNDOS_TEXT_REGULAR,
      UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC, "\xC3\xA9\xC3\xA9\xC3\xA9",
      output, sizeof(output), 20, &extent));
  REQUIRE(std::string(output) == "\xC3\xA9...");
  REQUIRE(extent.advanceWidth == 20);
  REQUIRE(UndosFitTextTail(font, UNDOS_TEXT_REGULAR,
      UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC, "\xF0\x9F\x98\x80"
      "\xF0\x9F\x98\x80", output, sizeof(output), 8, &extent));
  REQUIRE(std::string(output) == "\xF0\x9F\x98\x80");
  REQUIRE(extent.advanceWidth == 8);

  REQUIRE(UndosFitTextPrefix(font, UNDOS_TEXT_REGULAR,
      UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC, "WW", output,
      sizeof(output), 0, &extent));
  REQUIRE(output[0] == 0);
  REQUIRE(extent.advanceWidth == 0);
  REQUIRE(extent.lineHeight == 16);
  REQUIRE(UndosFitTextTail(font, UNDOS_TEXT_REGULAR,
      UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC, "", output,
      sizeof(output), 0, &extent));
  REQUIRE(output[0] == 0);
  REQUIRE(extent.advanceWidth == 0);
  REQUIRE(extent.lineHeight == 16);
  REQUIRE(UndosFitTextPrefix(font, UNDOS_TEXT_REGULAR,
      UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC, "", output,
      sizeof(output), 20, &extent));
  REQUIRE(output[0] == 0);

  std::strcpy(output, "keep");
  extent = {1, 2, 3, 4, 5, 6, 7, 8};
  REQUIRE_FALSE(UndosFitTextPrefix(font, UNDOS_TEXT_REGULAR,
      UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC, "W", output, 3, 40,
      &extent));
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);
  REQUIRE(UndosGetLastStatus() == UI_STATUS_INVALID_ARGUMENT);
  REQUIRE(std::string(output) == "keep");
  REQUIRE(extent.generation == 1);
  REQUIRE(extent.advanceWidth == 2);

  const char malformed[] = "\xC0";
  REQUIRE_FALSE(UndosFitTextTail(font, UNDOS_TEXT_REGULAR,
      UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC, malformed, output,
      sizeof(output), 40, &extent));
  REQUIRE(GetLastError() == ERROR_NO_UNICODE_TRANSLATION);
  REQUIRE(std::string(output) == "keep");
  REQUIRE(extent.generation == 1);

  failOperation = Operation::MeasureText;
  failStatus = UI_STATUS_RETRY;
  REQUIRE_FALSE(UndosFitTextPrefix(font, UNDOS_TEXT_REGULAR,
      UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC, "WWWW", output,
      sizeof(output), 40, &extent));
  REQUIRE(GetLastError() == ERROR_RETRY);
  REQUIRE(UndosGetLastStatus() == UI_STATUS_RETRY);
  REQUIRE(std::string(output) == "keep");
  REQUIRE(extent.generation == 1);

  REQUIRE(UndosCloseFont(font));
  REQUIRE(closeFontCalls == 1);
  REQUIRE(lastClosedFont == 0);
}

TEST_CASE("UTF-8 ingress rejects integer resources, empty classes, and malformed scalars",
          "[sdk-gui]") {
  resetBackend();
  WNDCLASS klass = test_class();
  REQUIRE(CreateWindow(nullptr, "", 0, 0, 0, 20, 20,
      nullptr, nullptr, nullptr, nullptr) == nullptr);
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);
  REQUIRE(CreateWindow("", "", 0, 0, 0, 20, 20,
      nullptr, nullptr, nullptr, nullptr) == nullptr);
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);
  klass.lpszClassName = nullptr;
  REQUIRE(RegisterClass(&klass) == 0);
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);

  klass.lpszClassName = "";
  REQUIRE(RegisterClass(&klass) == 0);
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);
  klass.lpszClassName = reinterpret_cast<LPCSTR>(static_cast<uintptr_t>(1));
  REQUIRE(RegisterClass(&klass) == 0);
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);
  REQUIRE(lastClassName.empty());

  const char *invalid[] = {
      "\xED\xA0\x80",
      "\xF4\x90\x80\x80",
      "\xE2\x82",
  };
  for (const char *text : invalid) {
    klass = test_class(text);
    REQUIRE(RegisterClass(&klass) == 0);
    REQUIRE(GetLastError() == ERROR_NO_UNICODE_TRANSLATION);
    REQUIRE(lastClassName.empty());
  }

  const std::string nearLimit(29, 'n');
  const std::string scalarName = nearLimit + "\xCC\x81";
  klass = test_class(scalarName.c_str());
  REQUIRE(RegisterClass(&klass) != 0);
  REQUIRE(lastClassName == scalarName);
  const uint32_t beforeScalarOverflow = registerCalls;
  const std::string overScalarLimit = std::string(30, 'n') + "\xCC\x81";
  klass = test_class(overScalarLimit.c_str());
  REQUIRE(RegisterClass(&klass) == 0);
  REQUIRE(GetLastError() == ERROR_INSUFFICIENT_BUFFER);
  REQUIRE(registerCalls == beforeScalarOverflow);

  HWND window = CreateWindow(scalarName.c_str(), nullptr, 0,
      0, 0, 20, 20, nullptr, nullptr, nullptr, nullptr);
  REQUIRE(window != nullptr);
  REQUIRE(lastTitle.empty());
  REQUIRE(SetWindowText(window, ""));
  REQUIRE(lastSetTitle.empty());
  REQUIRE_FALSE(SetWindowText(window, nullptr));
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);
  REQUIRE_FALSE(SetWindowText(window,
      reinterpret_cast<LPCSTR>(static_cast<uintptr_t>(1))));
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);

  HMENU menu = CreateMenu();
  REQUIRE(AppendMenu(menu, MF_STRING, 0, ""));
  REQUIRE_FALSE(AppendMenu(menu, MF_STRING, 0,
      reinterpret_cast<LPCSTR>(static_cast<uintptr_t>(2))));
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);
  REQUIRE_FALSE(AppendMenu(menu, MF_STRING, 0, nullptr));
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);
  REQUIRE(AppendMenu(menu, MF_SEPARATOR, 1, nullptr));
}

TEST_CASE("native failures map every supported status without losing raw status",
          "[sdk-gui]") {
  resetBackend();
  HWND window = make_window("error-map");
  const std::array<std::pair<uint32_t, DWORD>, 10> cases{{
      {UI_STATUS_OUT_OF_MEMORY, ERROR_NOT_ENOUGH_MEMORY},
      {UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER},
      {UI_STATUS_INVALID_SIZE, ERROR_INSUFFICIENT_BUFFER},
      {UI_STATUS_INVALID_ADDRESS, ERROR_NOACCESS},
      {UI_STATUS_UNSUPPORTED, ERROR_CALL_NOT_IMPLEMENTED},
      {UI_STATUS_TIMEOUT, ERROR_TIMEOUT},
      {UI_STATUS_RETRY, ERROR_RETRY},
      {UI_STATUS_NOT_FOUND, ERROR_NOT_FOUND},
      {UI_STATUS_UNKNOWN, ERROR_GEN_FAILURE},
      {UINT32_C(0xFFFFFF00), ERROR_GEN_FAILURE},
  }};
  for (const auto &[nativeStatus, expectedError] : cases) {
    failOperation = Operation::SetText;
    failStatus = nativeStatus;
    REQUIRE_FALSE(SetWindowText(window, "failure"));
    REQUIRE(GetLastError() == expectedError);
    REQUIRE(UndosGetLastStatus() == nativeStatus);
  }
}

TEST_CASE("class and window records are bounded and failed setup can be retried",
          "[sdk-gui]") {
  resetBackend();
  WNDCLASS klass = test_class("register-retry");
  failOperation = Operation::RegisterClass;
  failStatus = UI_STATUS_RETRY;
  REQUIRE(RegisterClass(&klass) == 0);
  REQUIRE(GetLastError() == ERROR_RETRY);
  REQUIRE(lastClassName.empty());
  REQUIRE(RegisterClass(&klass) != 0);
  REQUIRE(registerCalls == 2);

  HWND failedWindow = CreateWindow("register-retry", "failed", 0,
      0, 0, 20, 20, nullptr, nullptr, nullptr, nullptr);
  REQUIRE(failedWindow != nullptr);
  REQUIRE(DestroyWindow(failedWindow));
  WNDCLASS creationClass = test_class("creation-retry");
  REQUIRE(RegisterClass(&creationClass) != 0);
  failOperation = Operation::CreateWindow;
  failStatus = UI_STATUS_OUT_OF_MEMORY;
  REQUIRE(CreateWindow("creation-retry", "", 0, 0, 0, 20, 20,
      nullptr, nullptr, nullptr, nullptr) == nullptr);
  REQUIRE(GetLastError() == ERROR_NOT_ENOUGH_MEMORY);
  HWND retry = CreateWindow("creation-retry", "", 0, 0, 0, 20, 20,
      nullptr, nullptr, nullptr, nullptr);
  REQUIRE(retry != nullptr);
  REQUIRE(DestroyWindow(retry));

  WNDCLASS showClass = test_class("show-rollback");
  REQUIRE(RegisterClass(&showClass) != 0);
  const uint32_t previousDestroyCalls = destroyCalls;
  failOperation = Operation::ShowWindow;
  failStatus = UI_STATUS_RETRY;
  REQUIRE(CreateWindow("show-rollback", "", WS_VISIBLE, 0, 0, 20, 20,
      nullptr, nullptr, nullptr, nullptr) == nullptr);
  REQUIRE(GetLastError() == ERROR_RETRY);
  REQUIRE(destroyCalls == previousDestroyCalls + 1);
  REQUIRE(nativeWindows.empty());
  UNDOS_WINDOW_CREATION_RECOVERY noRecovery{};
  REQUIRE_FALSE(UndosGetWindowCreationRecovery(&noRecovery));

  HMENU menu = CreateMenu();
  const uint32_t beforeMenuCreate = destroyCalls;
  failOperation = Operation::SetMenu;
  failStatus = UI_STATUS_OUT_OF_MEMORY;
  REQUIRE(CreateWindow("creation-retry", "", 0, 0, 0, 20, 20,
      nullptr, menu, nullptr, nullptr) == nullptr);
  REQUIRE(GetLastError() == ERROR_NOT_ENOUGH_MEMORY);
  REQUIRE(destroyCalls == beforeMenuCreate + 1);
  REQUIRE(nativeWindows.empty());
  REQUIRE_FALSE(UndosGetWindowCreationRecovery(&noRecovery));

  WNDCLASS rollbackClass = test_class("rollback-failure");
  REQUIRE(RegisterClass(&rollbackClass) != 0);
  failOperation = Operation::ShowWindow;
  failStatus = UI_STATUS_RETRY;
  forcedDestroyStatus = UI_STATUS_OUT_OF_MEMORY;
  REQUIRE(CreateWindow("rollback-failure", "", WS_VISIBLE, 0, 0, 20, 20,
      nullptr, nullptr, nullptr, nullptr) == nullptr);
  REQUIRE(GetLastError() == ERROR_RETRY);
  REQUIRE(UndosGetLastStatus() == UI_STATUS_RETRY);
  REQUIRE(nativeWindows.size() == 1);
  UNDOS_WINDOW_CREATION_RECOVERY recovery{};
  REQUIRE(UndosGetWindowCreationRecovery(&recovery));
  REQUIRE(recovery.window != nullptr);
  REQUIRE(recovery.setupError == ERROR_RETRY);
  REQUIRE(recovery.setupStatus == UI_STATUS_RETRY);
  REQUIRE(recovery.cleanupError == ERROR_NOT_ENOUGH_MEMORY);
  REQUIRE(recovery.cleanupStatus == UI_STATUS_OUT_OF_MEMORY);
  REQUIRE(DestroyWindow(recovery.window));
  REQUIRE(nativeWindows.empty());

  forcedDestroyStatus = UI_STATUS_RETRY;
  failOperation = Operation::SetMenu;
  failStatus = UI_STATUS_OUT_OF_MEMORY;
  REQUIRE(CreateWindow("creation-retry", "", 0, 0, 0, 20, 20,
      nullptr, menu, nullptr, nullptr) == nullptr);
  REQUIRE(GetLastError() == ERROR_NOT_ENOUGH_MEMORY);
  REQUIRE(UndosGetLastStatus() == UI_STATUS_OUT_OF_MEMORY);
  REQUIRE(nativeWindows.size() == 1);
  REQUIRE(UndosGetWindowCreationRecovery(&recovery));
  REQUIRE(recovery.setupError == ERROR_NOT_ENOUGH_MEMORY);
  REQUIRE(recovery.setupStatus == UI_STATUS_OUT_OF_MEMORY);
  REQUIRE(recovery.cleanupError == ERROR_RETRY);
  REQUIRE(recovery.cleanupStatus == UI_STATUS_RETRY);
  REQUIRE(DestroyWindow(recovery.window));
  REQUIRE(nativeWindows.empty());

  resetBackend();
  for (uint32_t i = 0; i < 32; ++i) {
    const std::string name = "class-" + std::to_string(i);
    WNDCLASS item = test_class(name.c_str());
    REQUIRE(RegisterClass(&item) != 0);
  }
  const uint32_t beforeClassOverflow = registerCalls;
  WNDCLASS overflowClass = test_class("overflow-class");
  REQUIRE(RegisterClass(&overflowClass) == 0);
  REQUIRE(GetLastError() == ERROR_NOT_ENOUGH_MEMORY);
  REQUIRE(registerCalls == beforeClassOverflow);

  resetBackend();
  WNDCLASS windowClass = test_class("window-pool");
  REQUIRE(RegisterClass(&windowClass) != 0);
  std::vector<HWND> windows;
  for (uint32_t i = 0; i < 64; ++i) {
    HWND value = CreateWindow("window-pool", "", 0, 0, 0, 20, 20,
        nullptr, nullptr, nullptr, nullptr);
    REQUIRE(value != nullptr);
    windows.push_back(value);
  }
  const uint32_t beforeWindowOverflow = createCalls;
  REQUIRE(CreateWindow("window-pool", "", 0, 0, 0, 20, 20,
      nullptr, nullptr, nullptr, nullptr) == nullptr);
  REQUIRE(GetLastError() == ERROR_NOT_ENOUGH_MEMORY);
  REQUIRE(createCalls == beforeWindowOverflow);
  for (HWND value : windows) REQUIRE(DestroyWindow(value));
}

TEST_CASE("creation recovery preserves multiple dual diagnostics until cleanup",
          "[sdk-gui]") {
  resetBackend();
  WNDCLASS klass = test_class("recovery-query");
  REQUIRE(RegisterClass(&klass) != 0);

  failOperation = Operation::ShowWindow;
  failStatus = UI_STATUS_RETRY;
  forcedDestroyStatus = UI_STATUS_OUT_OF_MEMORY;
  REQUIRE(CreateWindow("recovery-query", "show failure", WS_VISIBLE,
      0, 0, 20, 20, nullptr, nullptr, nullptr, nullptr) == nullptr);
  REQUIRE(GetLastError() == ERROR_RETRY);
  REQUIRE(UndosGetLastStatus() == UI_STATUS_RETRY);
  REQUIRE(destroyCalls == 1);

  HMENU menu = CreateMenu();
  failOperation = Operation::SetMenu;
  failStatus = UI_STATUS_OUT_OF_MEMORY;
  forcedDestroyStatus = UI_STATUS_INVALID_ADDRESS;
  REQUIRE(CreateWindow("recovery-query", "menu failure", 0,
      0, 0, 20, 20, nullptr, menu, nullptr, nullptr) == nullptr);
  REQUIRE(GetLastError() == ERROR_NOT_ENOUGH_MEMORY);
  REQUIRE(UndosGetLastStatus() == UI_STATUS_OUT_OF_MEMORY);
  REQUIRE(destroyCalls == 2);

  UNDOS_WINDOW_CREATION_RECOVERY first{};
  REQUIRE(UndosGetWindowCreationRecovery(&first));
  REQUIRE(first.window != nullptr);
  REQUIRE(first.setupError == ERROR_RETRY);
  REQUIRE(first.setupStatus == UI_STATUS_RETRY);
  REQUIRE(first.cleanupError == ERROR_NOT_ENOUGH_MEMORY);
  REQUIRE(first.cleanupStatus == UI_STATUS_OUT_OF_MEMORY);
  SetLastError(71);
  const DWORD rawBeforeQuery = UndosGetLastStatus();
  UNDOS_WINDOW_CREATION_RECOVERY repeated{};
  REQUIRE(UndosGetWindowCreationRecovery(&repeated));
  REQUIRE(repeated.window == first.window);
  REQUIRE(repeated.setupError == first.setupError);
  REQUIRE(repeated.setupStatus == first.setupStatus);
  REQUIRE(repeated.cleanupError == first.cleanupError);
  REQUIRE(repeated.cleanupStatus == first.cleanupStatus);
  REQUIRE(GetLastError() == 71);
  REQUIRE(UndosGetLastStatus() == rawBeforeQuery);

  UNDOS_WINDOW_CREATION_RECOVERY second{};
  REQUIRE(UndosGetWindowCreationRecovery(nullptr) == FALSE);
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);
  REQUIRE(UndosGetLastStatus() == UI_STATUS_INVALID_ARGUMENT);
  REQUIRE(UndosGetWindowCreationRecovery(&repeated));
  REQUIRE(repeated.window == first.window);

  HWND unrelated = CreateWindow("recovery-query", "ordinary", 0,
      0, 0, 20, 20, nullptr, nullptr, nullptr, nullptr);
  REQUIRE(unrelated != nullptr);
  REQUIRE(SetWindowText(unrelated, "updated"));
  failOperation = Operation::SetText;
  failStatus = UI_STATUS_TIMEOUT;
  REQUIRE_FALSE(SetWindowText(unrelated, "failure"));
  REQUIRE(GetLastError() == ERROR_TIMEOUT);
  REQUIRE(UndosGetLastStatus() == UI_STATUS_TIMEOUT);
  REQUIRE(UndosGetWindowCreationRecovery(&repeated));
  REQUIRE(repeated.window == first.window);

  const uint32_t priorShowCalls = showCalls;
  const uint32_t priorSetMenuCalls = setMenuCalls;
  const uint32_t priorSetTextCalls = setTextCalls;
  const uint32_t priorBeginCalls = beginCalls;
  const uint32_t priorLongRequest = longRequest[0];
  const uint32_t priorCallbacks = destroyCallbackCalls;
  const uint32_t priorPostCalls = postCalls;
  REQUIRE_FALSE(ShowWindow(first.window, SW_SHOW));
  REQUIRE(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
  REQUIRE_FALSE(SetWindowText(first.window, "not allowed"));
  REQUIRE(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
  REQUIRE(GetWindowLongPtr(first.window, GWLP_USERDATA) == 0);
  REQUIRE(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
  REQUIRE_FALSE(SetMenu(first.window, menu));
  REQUIRE(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
  REQUIRE_FALSE(PostMessage(first.window, WM_USER + 9, 17, 19));
  REQUIRE(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
  REQUIRE(UndosGetLastStatus() == UI_STATUS_INVALID_ARGUMENT);
  REQUIRE(DefWindowProc(first.window, WM_CLOSE, 0, 0) == 0);
  REQUIRE(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
  PAINTSTRUCT paint{};
  REQUIRE(BeginPaint(first.window, &paint) == nullptr);
  REQUIRE(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
  MSG message{};
  message.hwnd = first.window;
  message.message = WM_USER;
  REQUIRE(DispatchMessage(&message) == 0);
  REQUIRE(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
  REQUIRE(showCalls == priorShowCalls);
  REQUIRE(setMenuCalls == priorSetMenuCalls);
  REQUIRE(setTextCalls == priorSetTextCalls);
  REQUIRE(beginCalls == priorBeginCalls);
  REQUIRE(longRequest[0] == priorLongRequest);
  REQUIRE(destroyCallbackCalls == priorCallbacks);
  REQUIRE(postCalls == priorPostCalls);
  REQUIRE(UndosGetWindowCreationRecovery(&repeated));
  REQUIRE(repeated.window == first.window);
  REQUIRE(repeated.setupError == ERROR_RETRY);
  REQUIRE(repeated.setupStatus == UI_STATUS_RETRY);
  REQUIRE(repeated.cleanupError == ERROR_NOT_ENOUGH_MEMORY);
  REQUIRE(repeated.cleanupStatus == UI_STATUS_OUT_OF_MEMORY);

  forcedDestroyStatus = UI_STATUS_RETRY;
  const uint32_t beforeCleanup = destroyCalls;
  REQUIRE_FALSE(DestroyWindow(first.window));
  REQUIRE(GetLastError() == ERROR_RETRY);
  REQUIRE(UndosGetLastStatus() == UI_STATUS_RETRY);
  REQUIRE(destroyCalls == beforeCleanup + 1);
  REQUIRE(destroyRequests.back() ==
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(first.window)));
  REQUIRE(UndosGetWindowCreationRecovery(&repeated));
  REQUIRE(repeated.window == first.window);
  REQUIRE(repeated.setupError == ERROR_RETRY);
  REQUIRE(repeated.setupStatus == UI_STATUS_RETRY);
  REQUIRE(repeated.cleanupError == ERROR_RETRY);
  REQUIRE(repeated.cleanupStatus == UI_STATUS_RETRY);

  REQUIRE(DestroyWindow(first.window));
  REQUIRE(destroyCalls == beforeCleanup + 2);
  REQUIRE(destroyRequests.back() ==
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(first.window)));
  REQUIRE(UndosGetWindowCreationRecovery(&second));
  REQUIRE(second.window != nullptr);
  REQUIRE(second.setupError == ERROR_NOT_ENOUGH_MEMORY);
  REQUIRE(second.setupStatus == UI_STATUS_OUT_OF_MEMORY);
  REQUIRE(second.cleanupError == ERROR_NOACCESS);
  REQUIRE(second.cleanupStatus == UI_STATUS_INVALID_ADDRESS);
  REQUIRE(DestroyWindow(second.window));
  REQUIRE(nativeWindows.size() == 1);
  REQUIRE(DestroyWindow(unrelated));

  UNDOS_WINDOW_CREATION_RECOVERY untouched{
      reinterpret_cast<HWND>(static_cast<uintptr_t>(0x777)),
      81, 82, 83, 84};
  SetLastError(85);
  const DWORD emptyRawStatus = UndosGetLastStatus();
  REQUIRE_FALSE(UndosGetWindowCreationRecovery(&untouched));
  REQUIRE(untouched.window == reinterpret_cast<HWND>(
      static_cast<uintptr_t>(0x777)));
  REQUIRE(untouched.setupError == 81);
  REQUIRE(untouched.setupStatus == 82);
  REQUIRE(untouched.cleanupError == 83);
  REQUIRE(untouched.cleanupStatus == 84);
  REQUIRE(GetLastError() == 85);
  REQUIRE(UndosGetLastStatus() == emptyRawStatus);
}

TEST_CASE("recovery records exhaust bounded window capacity and release slots",
          "[sdk-gui]") {
  resetBackend();
  WNDCLASS klass = test_class("recovery-capacity");
  REQUIRE(RegisterClass(&klass) != 0);
  for (uint32_t i = 0; i < 64; ++i) {
    failOperation = Operation::ShowWindow;
    failStatus = UI_STATUS_RETRY;
    forcedDestroyStatus = UI_STATUS_OUT_OF_MEMORY;
    REQUIRE(CreateWindow("recovery-capacity", "", WS_VISIBLE,
        0, 0, 20, 20, nullptr, nullptr, nullptr, nullptr) == nullptr);
    REQUIRE(GetLastError() == ERROR_RETRY);
    REQUIRE(UndosGetLastStatus() == UI_STATUS_RETRY);
  }
  REQUIRE(nativeWindows.size() == 64);
  const uint32_t beforeExhaustion = createCalls;
  REQUIRE(CreateWindow("recovery-capacity", "", 0,
      0, 0, 20, 20, nullptr, nullptr, nullptr, nullptr) == nullptr);
  REQUIRE(GetLastError() == ERROR_NOT_ENOUGH_MEMORY);
  REQUIRE(createCalls == beforeExhaustion);

  UNDOS_WINDOW_CREATION_RECOVERY recovery{};
  uint32_t recovered = 0;
  while (UndosGetWindowCreationRecovery(&recovery)) {
    REQUIRE(recovery.window != nullptr);
    REQUIRE(DestroyWindow(recovery.window));
    REQUIRE(++recovered <= 64);
  }
  REQUIRE(recovered == 64);
  REQUIRE(nativeWindows.empty());
  HWND afterRecovery = CreateWindow("recovery-capacity", "", 0,
      0, 0, 20, 20, nullptr, nullptr, nullptr, nullptr);
  REQUIRE(afterRecovery != nullptr);
  REQUIRE_FALSE(UndosGetWindowCreationRecovery(&recovery));
  REQUIRE(DestroyWindow(afterRecovery));
}

TEST_CASE("recovery cleanup skips callbacks and parent teardown retires children",
          "[sdk-gui]") {
  resetBackend();
  WNDCLASS klass = test_class();
  klass.lpszClassName = "recovery-parent";
  klass.lpfnWndProc = destroy_parent_from_child_proc;
  REQUIRE(RegisterClass(&klass) != 0);
  HWND parent = CreateWindow("recovery-parent", "", 0,
      0, 0, 20, 20, nullptr, nullptr, nullptr, nullptr);
  REQUIRE(parent != nullptr);
  failOperation = Operation::ShowWindow;
  failStatus = UI_STATUS_RETRY;
  forcedDestroyStatus = UI_STATUS_OUT_OF_MEMORY;
  HWND child = CreateWindow("recovery-parent", "", WS_CHILD | WS_VISIBLE,
      0, 0, 20, 20, parent, nullptr, nullptr, nullptr);
  REQUIRE(child == nullptr);
  REQUIRE(destroyCallbackCalls == 0);
  UNDOS_WINDOW_CREATION_RECOVERY recovery{};
  REQUIRE(UndosGetWindowCreationRecovery(&recovery));
  REQUIRE(recovery.window != nullptr);
  REQUIRE(DestroyWindow(recovery.window));
  REQUIRE(destroyCallbackCalls == 0);
  REQUIRE_FALSE(UndosGetWindowCreationRecovery(&recovery));

  HWND liveChild = CreateWindow("recovery-parent", "", WS_CHILD,
      0, 0, 20, 20, parent, nullptr, nullptr, nullptr);
  REQUIRE(liveChild != nullptr);
  parentToDestroyFromChild = parent;
  const uint32_t beforeNestedDestroy = destroyCalls;
  REQUIRE(DestroyWindow(liveChild));
  REQUIRE(nestedParentDestroyResult);
  REQUIRE_FALSE(recursiveDestroyResult);
  REQUIRE(destroyCallbackCalls == 2);
  REQUIRE(destroyCalls == beforeNestedDestroy + 1);
  REQUIRE(destroyRequests.back() ==
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(parent)));
  REQUIRE_FALSE(nativeWindows.contains(
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(parent))));
  REQUIRE_FALSE(nativeWindows.contains(
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(liveChild))));
  const uint32_t callbackCount = destroyCallbackCalls;
  REQUIRE_FALSE(DestroyWindow(liveChild));
  REQUIRE(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
  REQUIRE(destroyCallbackCalls == callbackCount);

  resetBackend();
  klass = test_class("recovery-parent-cleanup");
  REQUIRE(RegisterClass(&klass) != 0);
  parent = CreateWindow("recovery-parent-cleanup", "", 0,
      0, 0, 20, 20, nullptr, nullptr, nullptr, nullptr);
  REQUIRE(parent != nullptr);
  failOperation = Operation::ShowWindow;
  failStatus = UI_STATUS_RETRY;
  forcedDestroyStatus = UI_STATUS_OUT_OF_MEMORY;
  REQUIRE(CreateWindow("recovery-parent-cleanup", "", WS_CHILD | WS_VISIBLE,
      0, 0, 20, 20, parent, nullptr, nullptr, nullptr) == nullptr);
  REQUIRE(UndosGetWindowCreationRecovery(&recovery));
  REQUIRE(DestroyWindow(parent));
  REQUIRE_FALSE(UndosGetWindowCreationRecovery(&recovery));
  REQUIRE(destroyCallbackCalls == 1);
  REQUIRE(nativeWindows.empty());
}

TEST_CASE("paint record exhaustion rejects before acquiring another native DC",
          "[sdk-gui]") {
  resetBackend();
  WNDCLASS klass = test_class("dc-pool");
  REQUIRE(RegisterClass(&klass) != 0);
  std::array<HWND, 17> windows{};
  std::array<PAINTSTRUCT, 16> paints{};
  for (HWND &window : windows) {
    window = CreateWindow("dc-pool", "", 0, 0, 0, 20, 20,
        nullptr, nullptr, nullptr, nullptr);
    REQUIRE(window != nullptr);
  }
  for (size_t i = 0; i < paints.size(); ++i)
    REQUIRE(BeginPaint(windows[i], &paints[i]) != nullptr);
  const uint32_t beforeOverflow = beginCalls;
  PAINTSTRUCT overflow{};
  REQUIRE(BeginPaint(windows.back(), &overflow) == nullptr);
  REQUIRE(GetLastError() == ERROR_NOT_ENOUGH_MEMORY);
  REQUIRE(beginCalls == beforeOverflow);
  for (size_t i = 0; i < paints.size(); ++i)
    REQUIRE(EndPaint(windows[i], &paints[i]));
  for (HWND window : windows) REQUIRE(DestroyWindow(window));
}

TEST_CASE("public font enums map explicitly to generated syscall enums",
          "[sdk-gui]") {
  enum FontSource source = FontSource_AcquireBuiltinBitmap;
  enum GlyphFallbackPolicy fallback =
      GlyphFallbackPolicy_FallbackBitmapForUnmapped;
  enum TextStyle style = TextStyle_StyleBold;
  enum TextLayoutMode layout = TextLayoutMode_LayoutUniformGrid;
  enum TextRepertoire repertoire = TextRepertoire_RepertoireTerminal;

  REQUIRE(ui_map_font_source(UNDOS_FONT_SYSTEM_DEFAULT, &source));
  REQUIRE(source == FontSource_AcquireSystemDefault);
  REQUIRE(ui_map_font_source(UNDOS_FONT_BUILTIN_BITMAP, &source));
  REQUIRE(source == FontSource_AcquireBuiltinBitmap);
  REQUIRE(ui_map_glyph_fallback(UNDOS_GLYPH_FALLBACK_NOTDEF, &fallback));
  REQUIRE(fallback == GlyphFallbackPolicy_FallbackFaceNotdef);
  REQUIRE(ui_map_glyph_fallback(UNDOS_GLYPH_FALLBACK_BITMAP, &fallback));
  REQUIRE(fallback == GlyphFallbackPolicy_FallbackBitmapForUnmapped);
  REQUIRE(ui_map_text_style(UNDOS_TEXT_REGULAR, &style));
  REQUIRE(style == TextStyle_StyleRegular);
  REQUIRE(ui_map_text_style(UNDOS_TEXT_BOLD, &style));
  REQUIRE(style == TextStyle_StyleBold);
  REQUIRE(ui_map_text_layout(UNDOS_TEXT_PROPORTIONAL, &layout));
  REQUIRE(layout == TextLayoutMode_LayoutProportional);
  REQUIRE(ui_map_text_layout(UNDOS_TEXT_UNIFORM_GRID, &layout));
  REQUIRE(layout == TextLayoutMode_LayoutUniformGrid);
  REQUIRE(ui_map_text_repertoire(UNDOS_TEXT_BASIC, &repertoire));
  REQUIRE(repertoire == TextRepertoire_RepertoireBasic);
  REQUIRE(ui_map_text_repertoire(UNDOS_TEXT_TERMINAL, &repertoire));
  REQUIRE(repertoire == TextRepertoire_RepertoireTerminal);

  const auto oldSource = source;
  const auto oldFallback = fallback;
  const auto oldStyle = style;
  const auto oldLayout = layout;
  const auto oldRepertoire = repertoire;
  REQUIRE_FALSE(ui_map_font_source(99, &source));
  REQUIRE_FALSE(ui_map_glyph_fallback(99, &fallback));
  REQUIRE_FALSE(ui_map_text_style(99, &style));
  REQUIRE_FALSE(ui_map_text_layout(99, &layout));
  REQUIRE_FALSE(ui_map_text_repertoire(99, &repertoire));
  REQUIRE(source == oldSource);
  REQUIRE(fallback == oldFallback);
  REQUIRE(style == oldStyle);
  REQUIRE(layout == oldLayout);
  REQUIRE(repertoire == oldRepertoire);
}

TEST_CASE("class and menu limits reject unsupported input before backend mutation",
          "[sdk-gui]") {
  resetBackend();
  WNDCLASS klass = test_class("duplicate");
  REQUIRE(RegisterClass(&klass) != 0);
  const uintptr_t nativeProcedure = lastRegisteredProcedure;
  REQUIRE(RegisterClass(&klass) == 0);
  REQUIRE(GetLastError() == ERROR_CLASS_ALREADY_EXISTS);
  REQUIRE(lastRegisteredProcedure == nativeProcedure);

  HWND window = CreateWindow("duplicate", nullptr, 0, 0, 0, 20, 20,
                             nullptr, nullptr, nullptr, nullptr);
  REQUIRE(window != nullptr);
  std::string exactTitle(63, 't');
  REQUIRE(SetWindowText(window, exactTitle.c_str()));
  const std::string multibyteExactTitle = std::string(61, 'u') + "\xC3\xA9";
  REQUIRE(SetWindowText(window, multibyteExactTitle.c_str()));
  REQUIRE(lastSetTitle == multibyteExactTitle);
  const std::string multibyteLongTitle = std::string(62, 'u') + "\xC3\xA9";
  REQUIRE_FALSE(SetWindowText(window, multibyteLongTitle.c_str()));
  REQUIRE(GetLastError() == ERROR_INSUFFICIENT_BUFFER);
  REQUIRE(lastSetTitle == multibyteExactTitle);
  std::string longTitle(64, 't');
  REQUIRE_FALSE(SetWindowText(window, longTitle.c_str()));
  REQUIRE(GetLastError() == ERROR_INSUFFICIENT_BUFFER);
  const uint32_t beforeWindowTitleOverflow = createCalls;
  REQUIRE(CreateWindow("duplicate", longTitle.c_str(), 0, 0, 0, 20, 20,
      nullptr, nullptr, nullptr, nullptr) == nullptr);
  REQUIRE(GetLastError() == ERROR_INSUFFICIENT_BUFFER);
  REQUIRE(createCalls == beforeWindowTitleOverflow);
  HWND titled = CreateWindow("duplicate", exactTitle.c_str(), 0,
      0, 0, 20, 20, nullptr, nullptr, nullptr, nullptr);
  REQUIRE(titled != nullptr);
  REQUIRE(lastTitle == exactTitle);

  HMENU menu = CreateMenu();
  REQUIRE(menu != nullptr);
  REQUIRE(AppendMenu(menu, MF_STRING | MF_DISABLED | MF_GRAYED, 42, "café"));
  REQUIRE(lastMenuFlags == (MF_DISABLED | MF_GRAYED));
  REQUIRE(lastMenuItem == 42);
  REQUIRE(lastMenuText == "café");
  std::string exactMenu(63, 'm');
  REQUIRE(AppendMenu(menu, MF_STRING, 43, exactMenu.c_str()));
  REQUIRE(lastMenuText == exactMenu);
  const std::string multibyteExactMenu = std::string(61, 'v') + "\xC3\xA9";
  REQUIRE(AppendMenu(menu, MF_STRING, 44, multibyteExactMenu.c_str()));
  REQUIRE(lastMenuText == multibyteExactMenu);
  const std::string multibyteLongMenu = std::string(62, 'v') + "\xC3\xA9";
  REQUIRE_FALSE(AppendMenu(menu, MF_STRING, 45, multibyteLongMenu.c_str()));
  REQUIRE(GetLastError() == ERROR_INSUFFICIENT_BUFFER);
  REQUIRE(lastMenuText == multibyteExactMenu);
  std::string longMenu(64, 'm');
  const uint32_t beforeMenuOverflowItem = lastMenuItem;
  REQUIRE_FALSE(AppendMenu(menu, MF_STRING, 44, longMenu.c_str()));
  REQUIRE(GetLastError() == ERROR_INSUFFICIENT_BUFFER);
  REQUIRE(lastMenuItem == beforeMenuOverflowItem);
  REQUIRE(AppendMenu(menu, MF_SEPARATOR, 0, "\xFF"));
  REQUIRE(lastMenuText.empty());
  const uint32_t previousItem = lastMenuItem;
  REQUIRE_FALSE(AppendMenu(menu, 0x0020, 43, "unsupported"));
  REQUIRE(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);
  REQUIRE(lastMenuItem == previousItem);
  REQUIRE(AppendMenu(menu, MF_STRING | MF_CHECKED, 47, "checked"));
  REQUIRE(lastMenuFlags == MF_CHECKED);
  REQUIRE_FALSE(AppendMenu(menu, MF_SEPARATOR | MF_CHECKED, 0, nullptr));
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);
  REQUIRE_FALSE(AppendMenu(menu, MF_BITMAP, 44, ""));
  REQUIRE(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);
  REQUIRE_FALSE(AppendMenu(menu, MF_OWNERDRAW, 45, ""));
  REQUIRE(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);
  REQUIRE_FALSE(AppendMenu(menu, MF_SEPARATOR | MF_DISABLED, 46, ""));
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);
}

TEST_CASE("C UI translates styles and show commands without conflating result and error",
          "[sdk-gui]") {
  resetBackend();
  HWND parent = make_window("show-parent");
  WNDCLASS klass = test_class("show-map");
  REQUIRE(RegisterClass(&klass) != 0);
  HWND window = CreateWindow("show-map", "sdk window",
      WS_CHILD | WS_CAPTION | WS_VISIBLE, 0, 0, 200, 100,
      parent, nullptr, nullptr, nullptr);
  REQUIRE(window != nullptr);
  REQUIRE(lastCreateStyle ==
          (UI_WINDOW_CHILD | UI_WINDOW_CAPTION | UI_WINDOW_BORDER |
           UI_WINDOW_VISIBLE));
  REQUIRE(lastCreateParent == static_cast<uint32_t>(
      reinterpret_cast<uintptr_t>(parent)));
  REQUIRE(ShowWindow(window, SW_HIDE));
  REQUIRE(lastShowCommand == UI_SHOW_HIDE);
  SetLastError(ERROR_SUCCESS);
  HWND hidden = make_window("show-hidden");
  REQUIRE_FALSE(ShowWindow(hidden, SW_SHOW));
  REQUIRE(lastShowCommand == UI_SHOW_SHOW);
  REQUIRE(GetLastError() == ERROR_SUCCESS);
  REQUIRE(UndosShowWindow(window, SW_SHOWNORMAL));
  REQUIRE(lastShowCommand == UI_SHOW_RESTORE);
  REQUIRE(UndosShowWindow(window, SW_SHOWMINIMIZED));
  REQUIRE(lastShowCommand == UI_SHOW_MINIMIZE);
  REQUIRE(UndosShowWindow(window, SW_MAXIMIZE));
  REQUIRE(lastShowCommand == UI_SHOW_MAXIMIZE);
  REQUIRE(UndosShowWindow(window, SW_SHOW));
  REQUIRE(lastShowCommand == UI_SHOW_SHOW);

  REQUIRE(GetWindowLongPtr(window, GWL_STYLE) ==
          static_cast<LONG_PTR>(WS_VISIBLE | WS_CHILD | WS_CAPTION));
  SetLastError(0);
  REQUIRE(SetWindowLongPtr(window, GWLP_USERDATA, 0) == 0);
  REQUIRE(GetLastError() == 0);
  REQUIRE(GetWindowLongPtr(window, -7) == 0);
  REQUIRE(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);
  REQUIRE_FALSE(UndosShowWindow(window, 4));
  REQUIRE(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);
  REQUIRE_FALSE(SetWindowLongPtr(window, GWL_STYLE, WS_VISIBLE));
  REQUIRE(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);

  HWND top = make_window("style-set");
  SetLastError(ERROR_SUCCESS);
  REQUIRE(SetWindowLongPtr(top, GWL_STYLE,
      WS_VISIBLE | WS_OVERLAPPEDWINDOW) == 0);
  REQUIRE(GetLastError() == ERROR_SUCCESS);
  REQUIRE(longRequest[0] == UI_WINDOW_STYLE);
  REQUIRE(longRequest[1] ==
      (UI_WINDOW_VISIBLE | UI_WINDOW_CAPTION | UI_WINDOW_BORDER));
  REQUIRE(GetWindowLongPtr(top, GWL_STYLE) ==
      static_cast<LONG_PTR>(WS_VISIBLE | WS_CAPTION));
  REQUIRE(SetWindowLongPtr(top, GWL_STYLE, 0) ==
      static_cast<LONG_PTR>(WS_VISIBLE | WS_CAPTION | WS_BORDER));
  REQUIRE(longRequest[1] == 0);

  REQUIRE_FALSE(SetWindowLongPtr(top, GWL_STYLE, WS_DISABLED));
  REQUIRE(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);
  REQUIRE_FALSE(SetWindowLongPtr(top, GWL_STYLE, WS_POPUP));
  REQUIRE(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);
}

TEST_CASE("metadata, invalidation and posted messages forward exact values",
          "[sdk-gui]") {
  resetBackend();
  HWND window = make_window("forwarding");
  WNDPROC old = reinterpret_cast<WNDPROC>(
      static_cast<uintptr_t>(GetWindowLongPtr(window, GWLP_WNDPROC)));
  REQUIRE(old == test_proc);
  REQUIRE(SetWindowLongPtr(window, GWLP_WNDPROC,
      static_cast<LONG_PTR>(reinterpret_cast<uintptr_t>(replacement_proc))) ==
      static_cast<LONG_PTR>(reinterpret_cast<uintptr_t>(test_proc)));
  REQUIRE(GetWindowLongPtr(window, GWLP_WNDPROC) ==
      static_cast<LONG_PTR>(reinterpret_cast<uintptr_t>(replacement_proc)));
  MSG callbackMessage{window, WM_USER, 0, 0, 0, {0, 0}};
  REQUIRE(DispatchMessage(&callbackMessage) == 101);

  REQUIRE(SetWindowLongPtr(window, GWLP_USERDATA, 0x1234) == 0);
  REQUIRE(longRequest[0] == UI_WINDOW_USERDATA);
  REQUIRE(longRequest[1] == 0x1234);
  REQUIRE(GetWindowLongPtr(window, GWLP_USERDATA) == 0x1234);
  REQUIRE(SetWindowLongPtr(window, GWLP_USERDATA, -2) == 0x1234);
  REQUIRE(longRequest[1] == UINT32_C(0xFFFFFFFE));
  REQUIRE(GetWindowLongPtr(window, GWLP_USERDATA) == -2);

  const RECT dirty{2, 3, 18, 19};
  REQUIRE(InvalidateRect(window, &dirty, FALSE));
  REQUIRE_FALSE(lastInvalidatedWhole);
  REQUIRE(lastInvalidatedErase == FALSE);
  REQUIRE(lastInvalidatedRect.left == 2);
  REQUIRE(lastInvalidatedRect.bottom == 19);
  REQUIRE(InvalidateRect(window, nullptr, TRUE));
  REQUIRE(lastInvalidatedWhole);
  REQUIRE(lastInvalidatedErase == TRUE);

  REQUIRE(PostMessage(window, WM_USER + 4, 17, 29));
  REQUIRE(lastPostedWindow == static_cast<uint32_t>(
      reinterpret_cast<uintptr_t>(window)));
  REQUIRE(lastPostedMessage == WM_USER + 4);
  REQUIRE(lastPostedWParam == 17);
  REQUIRE(lastPostedLParam == 29);
  REQUIRE(PostMessage(window, WM_USER + 5, 0, -7));
  REQUIRE(lastPostedLParam == UINT32_C(0xFFFFFFF9));
  HWND foreignWindow = reinterpret_cast<HWND>(static_cast<uintptr_t>(0xA001));
  const uint32_t priorPostCalls = postCalls;
  REQUIRE(PostMessage(foreignWindow, WM_USER + 6, 23, 31));
  REQUIRE(postCalls == priorPostCalls + 1);
  REQUIRE(lastPostedWindow == UINT32_C(0xA001));
  REQUIRE(lastPostedMessage == WM_USER + 6);
  REQUIRE(lastPostedWParam == 23);
  REQUIRE(lastPostedLParam == 31);
  REQUIRE_FALSE(AppendMenu(CreateMenu(), MF_POPUP, UINT64_C(0x100000001), ""));
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);
}

TEST_CASE("COLORREF values map by RGB distance, retain caller values, and roll back failures",
          "[sdk-gui]") {
  resetBackend();
  HWND window = make_window("color-map");
  PAINTSTRUCT paint{};
  HDC dc = BeginPaint(window, &paint);
  REQUIRE(dc != nullptr);
  REQUIRE(paint.fErase == TRUE);
  REQUIRE(paint.rcPaint.left == 4);
  REQUIRE(SetTextColor(dc, RGB(0, 0, 1)) == RGB(0, 0, 0));
  REQUIRE(lastForeground == 0);
  REQUIRE(SetTextColor(dc, RGB(0, 0, 170)) == RGB(0, 0, 1));
  REQUIRE(lastForeground == 1);
  REQUIRE(SetTextColor(dc, RGB(0, 0, 85)) == RGB(0, 0, 170));
  REQUIRE(lastForeground == 0);
  REQUIRE(UndosPaletteColor(1) == RGB(0, 0, 170));
  REQUIRE(UndosPaletteColor(16) == CLR_INVALID);
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);

  failOperation = Operation::SetColors;
  failStatus = UI_STATUS_OUT_OF_MEMORY;
  REQUIRE(SetTextColor(dc, RGB(255, 255, 255)) == CLR_INVALID);
  REQUIRE(GetLastError() == ERROR_NOT_ENOUGH_MEMORY);
  REQUIRE(UndosGetLastStatus() == UI_STATUS_OUT_OF_MEMORY);
  REQUIRE(UndosFillRectangle(dc, 0, 0, 2, 2));
  REQUIRE(lastForeground == 0);
  REQUIRE(EndPaint(window, &paint));
}

TEST_CASE("all palette colors are COLORREF RGB values, not small palette indexes",
          "[sdk-gui]") {
  resetBackend();
  const std::array<COLORREF, 16> palette{{
      RGB(0, 0, 0), RGB(0, 0, 170), RGB(0, 170, 0), RGB(0, 170, 170),
      RGB(170, 0, 0), RGB(170, 0, 170), RGB(170, 85, 0), RGB(170, 170, 170),
      RGB(85, 85, 85), RGB(85, 85, 255), RGB(85, 255, 85), RGB(85, 255, 255),
      RGB(255, 85, 85), RGB(255, 85, 255), RGB(255, 255, 85), RGB(255, 255, 255)}};
  HWND window = make_window("palette-all");
  PAINTSTRUCT paint{};
  HDC dc = BeginPaint(window, &paint);
  REQUIRE(dc != nullptr);
  COLORREF previous = RGB(0, 0, 0);
  for (uint32_t i = 0; i < palette.size(); ++i) {
    REQUIRE(UndosPaletteColor(i) == palette[i]);
    REQUIRE(SetTextColor(dc, palette[i]) == previous);
    REQUIRE(lastForeground == i);
    previous = palette[i];
  }
  for (COLORREF small = 0; small <= 15; ++small) {
    REQUIRE(SetTextColor(dc, small) == previous);
    REQUIRE(lastForeground == 0);
    previous = small;
  }
  REQUIRE(EndPaint(window, &paint));
}

TEST_CASE("font extensions preserve zero tokens and commit outputs only on success",
          "[sdk-gui]") {
  resetBackend();
  UNDOS_FONT font = 41;
  uint32_t generation = 33;
  REQUIRE(UndosAcquireFont(UNDOS_FONT_SYSTEM_DEFAULT,
      UNDOS_GLYPH_FALLBACK_NOTDEF, &font, &generation));
  REQUIRE(font == 0);
  REQUIRE(generation == 9);
  UNDOS_TEXT_METRICS metrics{};
  REQUIRE(UndosGetFontMetrics(font, UNDOS_TEXT_REGULAR,
                              UNDOS_TEXT_BASIC, &metrics));
  REQUIRE(metrics.generation == 9);

  UNDOS_TEXT_EXTENT extent{};
  const std::string maximum(1024, 'x');
  REQUIRE(UndosMeasureText(font, UNDOS_TEXT_REGULAR,
      UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC, maximum.c_str(), &extent));
  REQUIRE(extent.advanceWidth == 42);
  REQUIRE(fontMeasureCalls == 1);
  REQUIRE(UndosMeasureText(font, UNDOS_TEXT_REGULAR,
      UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC, "", &extent));
  REQUIRE(fontMeasureCalls == 2);
  const std::string multibyteExact = std::string(1022, 'x') + "\xC3\xA9";
  REQUIRE(UndosMeasureText(font, UNDOS_TEXT_REGULAR,
      UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC,
      multibyteExact.c_str(), &extent));
  REQUIRE(fontMeasureCalls == 3);
  REQUIRE_FALSE(UndosMeasureText(font, UNDOS_TEXT_REGULAR,
      UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC, nullptr, &extent));
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);
  REQUIRE_FALSE(UndosMeasureText(font, UNDOS_TEXT_REGULAR,
      UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC,
      reinterpret_cast<LPCSTR>(static_cast<uintptr_t>(1)), &extent));
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);
  const char multilingual[] = "A\xC3\xA9\xE2\x82\xAC\xf0\x9f\x8c\x8d\xCC\x81";
  REQUIRE(UndosMeasureText(font, UNDOS_TEXT_REGULAR,
      UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC, multilingual, &extent));
  REQUIRE(fontMeasureCalls == 4);
  const std::string overflow = std::string(1023, 'x') + "\xC3\xA9";
  extent.advanceWidth = 99;
  REQUIRE_FALSE(UndosMeasureText(font, UNDOS_TEXT_REGULAR,
      UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC, overflow.c_str(), &extent));
  REQUIRE(GetLastError() == ERROR_INSUFFICIENT_BUFFER);
  REQUIRE(extent.advanceWidth == 99);
  REQUIRE(fontMeasureCalls == 4);
  const char truncated[] = "\xE2\x82";
  REQUIRE_FALSE(UndosMeasureText(font, UNDOS_TEXT_REGULAR,
      UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC, truncated, &extent));
  REQUIRE(GetLastError() == ERROR_NO_UNICODE_TRANSLATION);

  HWND window = make_window("font-text");
  PAINTSTRUCT paint{};
  HDC dc = BeginPaint(window, &paint);
  REQUIRE(dc != nullptr);
  REQUIRE(UndosDrawText(dc, 0, 0, UNDOS_TEXT_REGULAR,
      UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC, ""));
  REQUIRE_FALSE(UndosDrawText(dc, 0, 0, UNDOS_TEXT_REGULAR,
      UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC, nullptr));
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);
  const uint32_t beforeInvalidDraw = drawCalls;
  REQUIRE_FALSE(UndosDrawText(dc, 0, 0, UNDOS_TEXT_REGULAR,
      UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC,
      reinterpret_cast<LPCSTR>(static_cast<uintptr_t>(1))));
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);
  REQUIRE(drawCalls == beforeInvalidDraw);
  REQUIRE(EndPaint(window, &paint));
}

TEST_CASE("font selection distinguishes no previous font from a zero token",
          "[sdk-gui]") {
  resetBackend();
  HWND window = make_window("font-presence");
  undosgui_test_track_dc(0x20001, static_cast<uint32_t>(
      reinterpret_cast<uintptr_t>(window)));
  UNDOS_FONT previous = 91;
  BOOL hadPrevious = TRUE;
  REQUIRE(UndosSelectFont(reinterpret_cast<HDC>(static_cast<uintptr_t>(0x20001)),
                          0, &previous, &hadPrevious));
  REQUIRE(previous == 0);
  REQUIRE(hadPrevious == FALSE);

  previous = 91;
  hadPrevious = FALSE;
  returnedPreviousFont = 0;
  REQUIRE(UndosSelectFont(reinterpret_cast<HDC>(static_cast<uintptr_t>(0x20001)),
                          7, &previous, &hadPrevious));
  REQUIRE(previous == 0);
  REQUIRE(hadPrevious == TRUE);

  previous = 73;
  hadPrevious = TRUE;
  failOperation = Operation::SelectFont;
  failStatus = UI_STATUS_OUT_OF_MEMORY;
  REQUIRE_FALSE(UndosSelectFont(
      reinterpret_cast<HDC>(static_cast<uintptr_t>(0x20001)), 8,
      &previous, &hadPrevious));
  REQUIRE(previous == 73);
  REQUIRE(hadPrevious == TRUE);
  REQUIRE(GetLastError() == ERROR_NOT_ENOUGH_MEMORY);
}

TEST_CASE("paint pairs retry after EndPaint failure and close zero font handles",
          "[sdk-gui]") {
  resetBackend();
  HWND window = make_window("paint-pair");
  PAINTSTRUCT paint{};
  HDC dc = BeginPaint(window, &paint);
  REQUIRE(dc != nullptr);
  REQUIRE(selectedTokenValid);
  REQUIRE(selectedToken == 0);
  REQUIRE(closeFontCalls == 0);

  UNDOS_FONT previous = 91;
  BOOL hadPrevious = FALSE;
  returnedPreviousFont = 0;
  REQUIRE(UndosSelectFont(dc, 7, &previous, &hadPrevious));
  REQUIRE(hadPrevious == TRUE);
  REQUIRE(previous == 0);
  REQUIRE(UndosCloseFont(previous));
  REQUIRE(closeFontCalls == 1);

  PAINTSTRUCT wrongPair = paint;
  REQUIRE_FALSE(EndPaint(reinterpret_cast<HWND>(
      static_cast<uintptr_t>(reinterpret_cast<uintptr_t>(window) + 1)), &wrongPair));
  REQUIRE(endCalls == 0);
  failOperation = Operation::EndPaint;
  failStatus = UI_STATUS_RETRY;
  REQUIRE_FALSE(EndPaint(window, &paint));
  REQUIRE(GetLastError() == ERROR_RETRY);
  REQUIRE(endCalls == 1);
  REQUIRE(EndPaint(window, &paint));
  REQUIRE(endCalls == 2);
  REQUIRE(closeFontCalls == 1);
  REQUIRE_FALSE(EndPaint(window, &paint));
  REQUIRE(endCalls == 2);

  PAINTSTRUCT closeRetry{};
  HDC retryDc = BeginPaint(window, &closeRetry);
  REQUIRE(retryDc != nullptr);
  failOperation = Operation::CloseFont;
  failStatus = UI_STATUS_RETRY;
  REQUIRE_FALSE(EndPaint(window, &closeRetry));
  REQUIRE(GetLastError() == ERROR_RETRY);
  REQUIRE(endCalls == 3);
  REQUIRE(EndPaint(window, &closeRetry));
  REQUIRE(closeFontCalls == 3);
  REQUIRE(endCalls == 3);
}

TEST_CASE("BeginPaint rolls back native paint and font setup failures",
          "[sdk-gui]") {
  resetBackend();
  HWND window = make_window("paint-rollback");
  PAINTSTRUCT paint{};
  failOperation = Operation::AcquireFont;
  failStatus = UI_STATUS_OUT_OF_MEMORY;
  REQUIRE(BeginPaint(window, &paint) == nullptr);
  REQUIRE(endCalls == 1);
  REQUIRE(closeFontCalls == 0);
  REQUIRE(GetLastError() == ERROR_NOT_ENOUGH_MEMORY);

  failOperation = Operation::SelectFont;
  failStatus = UI_STATUS_RETRY;
  REQUIRE(BeginPaint(window, &paint) == nullptr);
  REQUIRE(endCalls == 2);
  REQUIRE(closeFontCalls == 1);
  REQUIRE(GetLastError() == ERROR_RETRY);

  failOperation = Operation::BeginPaint;
  failStatus = UI_STATUS_INVALID_ARGUMENT;
  REQUIRE(BeginPaint(window, &paint) == nullptr);
  REQUIRE(endCalls == 2);
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);
}

TEST_CASE("BeginPaint rollback preserves the setup error when cleanup fails",
          "[sdk-gui]") {
  resetBackend();
  HWND window = make_window("paint-rollback-cleanup");
  PAINTSTRUCT paint{};
  paint.hdc = reinterpret_cast<HDC>(static_cast<uintptr_t>(0x777));
  paint.fErase = TRUE;
  paint.rcPaint = {11, 12, 13, 14};

  failOperation = Operation::SelectFont;
  failStatus = UI_STATUS_RETRY;
  cleanupFontStatus = UI_STATUS_OUT_OF_MEMORY;
  REQUIRE(BeginPaint(window, &paint) == nullptr);
  REQUIRE(GetLastError() == ERROR_RETRY);
  REQUIRE(UndosGetLastStatus() == UI_STATUS_RETRY);
  REQUIRE(paint.hdc == reinterpret_cast<HDC>(static_cast<uintptr_t>(0x777)));
  REQUIRE(paint.fErase == TRUE);
  REQUIRE(paint.rcPaint.left == 11);
  REQUIRE(paint.rcPaint.top == 12);
  REQUIRE(paint.rcPaint.right == 13);
  REQUIRE(paint.rcPaint.bottom == 14);
  REQUIRE(beginCalls == 1);
  REQUIRE(selectFontCalls == 0);
  REQUIRE(closeFontCalls == 1);
  REQUIRE(endCalls == 1);
  REQUIRE(lastClosedFont == 0);
  REQUIRE(lastEndWindow == static_cast<uint32_t>(
      reinterpret_cast<uintptr_t>(window)));
  REQUIRE(lastEndDc == 0x10001);
  REQUIRE(cleanupFontStatus == UI_STATUS_OK);
  REQUIRE_FALSE(selectedTokenValid);

  PAINTSTRUCT retryPaint{};
  REQUIRE(BeginPaint(window, &retryPaint) != nullptr);
  REQUIRE(EndPaint(window, &retryPaint));
  REQUIRE(beginCalls == 2);
  REQUIRE(closeFontCalls == 2);
  REQUIRE(endCalls == 2);
  REQUIRE(lastClosedFont == 0);

  resetBackend();
  window = make_window("paint-rollback-end-failure");
  failOperation = Operation::SelectFont;
  failStatus = UI_STATUS_OUT_OF_MEMORY;
  cleanupEndStatus = UI_STATUS_INVALID_ADDRESS;
  REQUIRE(BeginPaint(window, &paint) == nullptr);
  REQUIRE(GetLastError() == ERROR_NOT_ENOUGH_MEMORY);
  REQUIRE(UndosGetLastStatus() == UI_STATUS_OUT_OF_MEMORY);
  REQUIRE(paint.hdc == reinterpret_cast<HDC>(static_cast<uintptr_t>(0x777)));
  REQUIRE(paint.fErase == TRUE);
  REQUIRE(paint.rcPaint.left == 11);
  REQUIRE(paint.rcPaint.top == 12);
  REQUIRE(paint.rcPaint.right == 13);
  REQUIRE(paint.rcPaint.bottom == 14);
  REQUIRE(beginCalls == 1);
  REQUIRE(closeFontCalls == 1);
  REQUIRE(endCalls == 1);
  REQUIRE(lastClosedFont == 0);
  REQUIRE(lastEndWindow == static_cast<uint32_t>(
      reinterpret_cast<uintptr_t>(window)));
  REQUIRE(lastEndDc == 0x10001);
  REQUIRE(cleanupEndStatus == UI_STATUS_OK);
  REQUIRE_FALSE(selectedTokenValid);

  REQUIRE(BeginPaint(window, &retryPaint) != nullptr);
  REQUIRE(EndPaint(window, &retryPaint));
  REQUIRE(beginCalls == 2);
  REQUIRE(closeFontCalls == 2);
  REQUIRE(endCalls == 2);
}

TEST_CASE("dispatch validates callback routes and guards reentrant destruction",
          "[sdk-gui]") {
  resetBackend();
  HWND window = make_window("callback-route");
  MSG message{};
  message.hwnd = window;
  message.message = WM_USER;
  REQUIRE(DispatchMessage(&message) == 73);
  REQUIRE(DispatchMessage(nullptr) == 0);
  message.hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(0x7fff));
  REQUIRE(DispatchMessage(&message) == 0);
  REQUIRE(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
  REQUIRE(DestroyWindow(window));
  REQUIRE_FALSE(recursiveDestroyResult);
  REQUIRE_FALSE(PeekMessage(&message, nullptr, 0, 0, PM_REMOVE));
}

TEST_CASE("callback self-destruction and failed native destruction keep records safe",
          "[sdk-gui]") {
  resetBackend();
  HWND window = make_window("destroy-retry");
  failOperation = Operation::DestroyWindow;
  failStatus = UI_STATUS_OUT_OF_MEMORY;
  REQUIRE_FALSE(DestroyWindow(window));
  REQUIRE(GetLastError() == ERROR_NOT_ENOUGH_MEMORY);
  REQUIRE(DispatchMessage(new MSG{window, WM_USER, 0, 0, 0, {0, 0}}) == 73);
  REQUIRE(DestroyWindow(window));

  HWND self = make_window("self-destroy");
  REQUIRE(SetWindowLongPtr(self, GWLP_WNDPROC,
      static_cast<LONG_PTR>(reinterpret_cast<uintptr_t>(self_destroy_proc))) != 0);
  MSG message{self, WM_USER, 0, 0, 0, {0, 0}};
  REQUIRE(DispatchMessage(&message) == 102);
  REQUIRE_FALSE(operationAfterDestroyResult);
  REQUIRE(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
  REQUIRE_FALSE(DestroyWindow(self));
  REQUIRE(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);

  HWND parent = make_window("destroy-parent");
  WNDCLASS childClass = test_class("destroy-child");
  REQUIRE(RegisterClass(&childClass) != 0);
  HWND child = CreateWindow("destroy-child", "", WS_CHILD, 0, 0, 20, 20,
                            parent, nullptr, nullptr, nullptr);
  REQUIRE(child != nullptr);
  PAINTSTRUCT childPaint{};
  REQUIRE(BeginPaint(child, &childPaint) != nullptr);
  REQUIRE(DestroyWindow(parent));
  REQUIRE_FALSE(DestroyWindow(child));
  REQUIRE(endCalls == 0);
  REQUIRE(closeFontCalls == 1);
}

TEST_CASE("message filtering and empty PeekMessage preserve outputs and error state",
          "[sdk-gui]") {
  resetBackend();
  HWND window = make_window("messages");
  MSG message{};
  message.hwnd = window;
  message.message = WM_USER + 7;
  message.wParam = 23;
  SetLastError(77);
  REQUIRE_FALSE(PeekMessage(&message, nullptr, 0, 0, PM_REMOVE));
  REQUIRE(GetLastError() == 77);
  REQUIRE(message.message == WM_USER + 7);
  REQUIRE_FALSE(PeekMessage(&message, nullptr, 0, 0, 2));
  REQUIRE(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);

  message = {window, WM_USER + 3, 41, 43, 47, {53, 59}};
  SetLastError(71);
  failOperation = Operation::PeekMessage;
  failStatus = UI_STATUS_RETRY;
  REQUIRE_FALSE(PeekMessage(&message, nullptr, 0, 0, PM_REMOVE));
  REQUIRE(message.hwnd == window);
  REQUIRE(message.message == WM_USER + 3);
  REQUIRE(message.wParam == 41);
  REQUIRE(message.lParam == 43);
  REQUIRE(message.time == 47);
  REQUIRE(message.pt.x == 53);
  REQUIRE(message.pt.y == 59);
  REQUIRE(GetLastError() == ERROR_RETRY);
  REQUIRE(UndosGetLastStatus() == UI_STATUS_RETRY);

  queue.push_back({static_cast<uint32_t>(reinterpret_cast<uintptr_t>(window)),
                   WM_USER, 4, 5, 6});
  REQUIRE(PeekMessage(&message, nullptr, 0, 0, PM_NOREMOVE));
  REQUIRE(lastPeekRemove == 0);
  REQUIRE(message.message == WM_USER);
  REQUIRE(PeekMessage(&message, nullptr, 0, 0, PM_REMOVE));
  REQUIRE(lastPeekRemove == 1);
  REQUIRE(queue.empty());
  REQUIRE(PostMessage(window, WM_USER + 8, 31, 37));
  REQUIRE(GetMessage(&message, nullptr, 0, 0) == 1);
  REQUIRE(message.message == WM_USER + 8);
  REQUIRE(message.wParam == 31);
  message.message = WM_USER + 9;
  REQUIRE(GetMessage(&message, window, 0, 0) == -1);
  REQUIRE(message.message == WM_USER + 9);
  REQUIRE(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);
  REQUIRE(TranslateMessage(&message) == FALSE);

  queue.push_back({0, WM_QUIT, 11, 0, 12});
  REQUIRE(GetMessage(&message, nullptr, 0, 0) == 0);
  REQUIRE(message.hwnd == nullptr);
  REQUIRE(message.message == WM_QUIT);
  REQUIRE(message.wParam == 11);

  PostQuitMessage(5);
  REQUIRE(PeekMessage(&message, nullptr, 0, 0, PM_NOREMOVE));
  REQUIRE(message.message == WM_QUIT);
  REQUIRE(message.wParam == 5);
  REQUIRE(PeekMessage(&message, nullptr, 0, 0, PM_REMOVE));
  REQUIRE(message.message == WM_QUIT);
  REQUIRE(GetMessage(&message, nullptr, 0, 0) == -1);
}

TEST_CASE("default procedure closes windows without posting child quit messages",
          "[sdk-gui]") {
  resetBackend();
  HWND parent = make_window("default-parent");
  WNDCLASS childClass = test_class("default-child");
  REQUIRE(RegisterClass(&childClass) != 0);
  HWND child = CreateWindow("default-child", "", WS_CHILD,
      0, 0, 20, 20, parent, nullptr, nullptr, nullptr);
  REQUIRE(child != nullptr);

  REQUIRE(DefWindowProc(child, WM_DESTROY, 0, 0) == 0);
  MSG message{};
  REQUIRE_FALSE(PeekMessage(&message, nullptr, 0, 0, PM_REMOVE));
  REQUIRE(GetLastError() == ERROR_SUCCESS);

  REQUIRE(DefWindowProc(parent, WM_CLOSE, 0, 0) == 0);
  REQUIRE_FALSE(nativeWindows.contains(
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(parent))));
  REQUIRE_FALSE(DestroyWindow(child));
  REQUIRE(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
  REQUIRE_FALSE(PeekMessage(&message, nullptr, 0, 0, PM_REMOVE));
  REQUIRE_FALSE(DestroyWindow(parent));
}

TEST_CASE("CloseUi maps DeleteDC status and retains the DC for retry",
          "[sdk-gui]") {
  resetBackend();
  UNDOS_UI_CONTEXT context{};
  REQUIRE(UndosInitUi(&context));
  UINT width = 0, height = 0;
  REQUIRE(UndosGetDisplaySize(&context, &width, &height));
  REQUIRE(context.hasDisplayDc);

  failDeleteStatus = UI_STATUS_NOT_FOUND;
  SetLastError(ERROR_TIMEOUT);
  REQUIRE_FALSE(UndosCloseUi(&context));
  REQUIRE(context.hasDisplayDc);
  REQUIRE_FALSE(context.hasFont);
  REQUIRE(context.result == -1);
  REQUIRE(context.operationError == ERROR_NOT_FOUND);
  REQUIRE(context.operationStatus == UI_STATUS_NOT_FOUND);
  REQUIRE(context.cleanupError == 0);
  REQUIRE(context.cleanupStatus == 0);
  REQUIRE(GetLastError() == ERROR_NOT_FOUND);
  REQUIRE(UndosGetLastStatus() == UI_STATUS_NOT_FOUND);
  REQUIRE(displayDeleteCalls == 1);

  REQUIRE(UndosCloseUi(&context));
  REQUIRE_FALSE(context.hasDisplayDc);
  REQUIRE(context.result == 1);
  REQUIRE(displayDeleteCalls == 2);
}

TEST_CASE("UI context init, deferred font ownership, and attach guards are retryable",
          "[sdk-gui]") {
  resetBackend();
  UNDOS_UI_CONTEXT context{};
  failOperation = Operation::GetFontMetrics;
  failStatus = UI_STATUS_TIMEOUT;
  failCloseFontCall = 1;
  REQUIRE_FALSE(UndosInitUi(&context));
  REQUIRE(context.result == -1);
  REQUIRE_FALSE(context.hasFont);
  REQUIRE(context.hasDeferredFont);
  REQUIRE(context.operationError == ERROR_TIMEOUT);
  REQUIRE(context.operationStatus == UI_STATUS_TIMEOUT);
  REQUIRE(context.cleanupError == ERROR_NOT_ENOUGH_MEMORY);
  REQUIRE(context.cleanupStatus == UI_STATUS_OUT_OF_MEMORY);
  REQUIRE(UndosCloseUi(&context));

  REQUIRE(UndosInitUi(&context));
  const uint32_t acquisitionsAfterInit = acquireFontCalls;
  failCloseFontCall = closeFontCalls + 1;
  REQUIRE_FALSE(UndosRefreshUi(&context));
  REQUIRE(context.hasFont);
  REQUIRE(context.hasDeferredFont);
  REQUIRE(context.metricsValid);
  REQUIRE(context.operationError == ERROR_NOT_ENOUGH_MEMORY);
  REQUIRE(context.operationStatus == UI_STATUS_OUT_OF_MEMORY);
  const uint32_t acquisitionsWithDeferred = acquireFontCalls;

  failCloseFontCall = closeFontCalls + 1;
  REQUIRE_FALSE(UndosRefreshUi(&context));
  REQUIRE(acquireFontCalls == acquisitionsWithDeferred);
  REQUIRE(context.hasDeferredFont);
  REQUIRE(context.hasFont);
  REQUIRE(context.metricsValid);

  REQUIRE(UndosRefreshUi(&context));
  REQUIRE(acquireFontCalls == acquisitionsWithDeferred + 1);
  REQUIRE(context.hasFont);
  REQUIRE_FALSE(context.hasDeferredFont);
  REQUIRE(context.metricsValid);
  REQUIRE(UndosCloseUi(&context));
  REQUIRE(acquireFontCalls > acquisitionsAfterInit);

  UNDOS_UI_CONTEXT attachedContext{};
  REQUIRE(UndosInitUi(&attachedContext));
  HWND owner = make_window("context-attach");
  attachedContext.activeDepth = 1;
  REQUIRE_FALSE(UndosAttachUiContext(owner, &attachedContext));
  REQUIRE(GetLastError() == ERROR_RETRY);
  attachedContext.activeDepth = 0;
  REQUIRE(UndosAttachUiContext(owner, &attachedContext));
  attachedContext.activeDepth = 1;
  REQUIRE_FALSE(UndosAttachUiContext(owner, nullptr));
  REQUIRE(GetLastError() == ERROR_RETRY);
  REQUIRE(attachedContext._sdkBorrowers == 1);
  attachedContext.activeDepth = 0;
  REQUIRE(UndosAttachUiContext(owner, nullptr));
  REQUIRE(UndosCloseUi(&attachedContext));
  REQUIRE(DestroyWindow(owner));
}

TEST_CASE("display-query failures preserve outputs and stale DC ownership",
          "[sdk-gui]") {
  resetBackend();
  UNDOS_UI_CONTEXT context{};
  REQUIRE(UndosInitUi(&context));
  UINT width = 17, height = 19;
  failOperation = Operation::GetDisplaySize;
  failStatus = UI_STATUS_NOT_FOUND;
  REQUIRE_FALSE(UndosGetDisplaySize(&context, &width, &height));
  REQUIRE(width == 17);
  REQUIRE(height == 19);
  REQUIRE(context.hasDisplayDc);
  REQUIRE(displayCreateCalls == 1);

  context.displayDcStale = TRUE;
  failDeleteStatus = UI_STATUS_RETRY;
  REQUIRE_FALSE(UndosGetDisplaySize(&context, &width, &height));
  REQUIRE(width == 17);
  REQUIRE(height == 19);
  REQUIRE(context.hasDisplayDc);
  REQUIRE(context.displayDcStale);
  REQUIRE(displayCreateCalls == 1);
  REQUIRE(displayDeleteCalls == 1);

  displayWidth = 800;
  displayHeight = 600;
  REQUIRE(UndosGetDisplaySize(&context, &width, &height));
  REQUIRE(width == 800);
  REQUIRE(height == 600);
  REQUIRE(displayDeleteCalls == 2);
  REQUIRE(displayCreateCalls == 2);
  REQUIRE(UndosCloseUi(&context));
}

TEST_CASE("system-menu paint cleanup failure is queryable and retries retained resources",
          "[sdk-gui]") {
  resetBackend();
  UNDOS_UI_CONTEXT context{};
  REQUIRE(UndosInitUi(&context));
  WNDCLASS klass = test_class("menu-recovery");
  REQUIRE(RegisterClass(&klass));
  HWND owner = CreateWindow("menu-recovery", "", 0, 0, 0, 240, 160,
                            nullptr, nullptr, nullptr, nullptr);
  REQUIRE(owner);
  REQUIRE(UndosAttachUiContext(owner, &context));
  returnedPreviousFont = 77;
  sdk_gui_script_menu_message(WM_PAINT, 0, 0);
  sdk_gui_script_menu_message(WM_KEYDOWN, VK_ESCAPE, 0);
  cleanupEndStatus = UI_STATUS_RETRY;
  failCloseFontCall = 2;

  REQUIRE(DefWindowProc(owner, WM_NCLBUTTONDOWN, HTSYSMENU,
                        pack_point(3, 4)) == 0);
  INFO("op=" << context.operationError << "/" << context.operationStatus
       << " cleanup=" << context.cleanupError << "/"
       << context.cleanupStatus << " creates=" << createCalls
       << " begins=" << beginCalls << " ends=" << endCalls
       << " destroys=" << destroyCalls);
  REQUIRE(context.result == -1);
  REQUIRE(context.operationError == 0);
  REQUIRE(context.operationStatus == 0);
  REQUIRE(context.cleanupError == ERROR_RETRY);
  REQUIRE(context.cleanupStatus == UI_STATUS_RETRY);
  REQUIRE(destroyCalls == 0);

  UNDOS_UI_CLEANUP_RECOVERY recovery{};
  REQUIRE(UndosGetUiCleanupRecovery(&recovery));
  const HWND helper = recovery.window;
  REQUIRE(helper);
  REQUIRE(recovery.operationError == 0);
  REQUIRE(recovery.operationStatus == 0);
  REQUIRE(recovery.cleanupError == ERROR_RETRY);
  REQUIRE(recovery.cleanupStatus == UI_STATUS_RETRY);
  REQUIRE_FALSE(ShowWindow(helper, SW_SHOW));
  REQUIRE(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
  REQUIRE_FALSE(PostMessage(helper, WM_USER, 0, 0));
  REQUIRE(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);

  const uint32_t beforeRetry = destroyCalls;
  REQUIRE_FALSE(DestroyWindow(helper));
  REQUIRE(destroyCalls == beforeRetry);
  REQUIRE(endCalls == 2);
  REQUIRE(UndosGetUiCleanupRecovery(&recovery));
  REQUIRE(recovery.window == helper);
  REQUIRE(recovery.cleanupError == ERROR_NOT_ENOUGH_MEMORY);
  REQUIRE(recovery.cleanupStatus == UI_STATUS_OUT_OF_MEMORY);

  REQUIRE(DestroyWindow(helper));
  REQUIRE(destroyCalls == beforeRetry + 1);
  REQUIRE(endCalls == 2);
  REQUIRE(closeFontCalls == 3);
  REQUIRE_FALSE(UndosGetUiCleanupRecovery(&recovery));
  REQUIRE(UndosAttachUiContext(owner, nullptr));
  REQUIRE(DestroyWindow(owner));
  REQUIRE(UndosCloseUi(&context));

  resetBackend();
  REQUIRE(UndosInitUi(&context));
  klass = test_class("menu-font-recovery");
  REQUIRE(RegisterClass(&klass));
  owner = CreateWindow("menu-font-recovery", "", 0, 0, 0, 240, 160,
                       nullptr, nullptr, nullptr, nullptr);
  REQUIRE(owner);
  REQUIRE(UndosAttachUiContext(owner, &context));
  returnedPreviousFont = 77;
  sdk_gui_script_menu_message(WM_PAINT, 0, 0);
  sdk_gui_script_menu_message(WM_KEYDOWN, VK_ESCAPE, 0);
  cleanupFontStatus = UI_STATUS_OUT_OF_MEMORY;
  REQUIRE(DefWindowProc(owner, WM_NCLBUTTONDOWN, HTSYSMENU,
                        pack_point(3, 4)) == 0);
  REQUIRE(context.result == -1);
  REQUIRE(context.operationError == 0);
  REQUIRE(context.operationStatus == 0);
  REQUIRE(context.cleanupError == ERROR_NOT_ENOUGH_MEMORY);
  REQUIRE(context.cleanupStatus == UI_STATUS_OUT_OF_MEMORY);
  REQUIRE(UndosGetUiCleanupRecovery(&recovery));
  REQUIRE(recovery.window);
  REQUIRE(recovery.operationStatus == 0);
  REQUIRE(recovery.cleanupStatus == UI_STATUS_OUT_OF_MEMORY);
  REQUIRE(DestroyWindow(recovery.window));
  REQUIRE(destroyCalls == 1);
  REQUIRE(UndosAttachUiContext(owner, nullptr));
  REQUIRE(DestroyWindow(owner));
  REQUIRE(UndosCloseUi(&context));
}

TEST_CASE("default system menu normalizes commands and keyboard geometry cancels safely",
          "[sdk-gui]") {
  resetBackend();
  HWND contextless = make_window("contextless-menu");
  const uint32_t beforeCreate = createCalls;
  REQUIRE(DefWindowProc(contextless, WM_NCLBUTTONDOWN, HTSYSMENU, 0) == 0);
  REQUIRE(createCalls == beforeCreate);
  REQUIRE(GetLastError() == ERROR_SUCCESS);
  REQUIRE(DefWindowProc(contextless, WM_SYSCOMMAND,
                        SC_RESTORE | 7u, 0) == 1);
  REQUIRE(lastShowCommand == UI_SHOW_RESTORE);
  REQUIRE(DefWindowProc(contextless, WM_SYSCOMMAND,
                        SC_MINIMIZE | 3u, 0) == 1);
  REQUIRE(lastShowCommand == UI_SHOW_MINIMIZE);
  REQUIRE(DefWindowProc(contextless, WM_SYSCOMMAND,
                        SC_MAXIMIZE | 5u, 0) == 1);
  REQUIRE(lastShowCommand == UI_SHOW_MAXIMIZE);
  REQUIRE(DefWindowProc(contextless, WM_SYSCOMMAND, SC_SIZE | 1u, 0) == 0);
  REQUIRE(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);

  UNDOS_UI_CONTEXT context{};
  REQUIRE(UndosInitUi(&context));
  WNDCLASS klass = test_class("menu-interaction");
  REQUIRE(RegisterClass(&klass));
  HWND owner = CreateWindow("menu-interaction", "", 0, 0, 0, 240, 160,
                            nullptr, nullptr, nullptr, nullptr);
  REQUIRE(owner);
  REQUIRE(UndosAttachUiContext(owner, &context));

  sdk_gui_script_menu_message(WM_KEYDOWN, VK_UP, 0);
  sdk_gui_script_menu_message(WM_KEYDOWN, VK_RETURN, 0);
  REQUIRE(DefWindowProc(owner, WM_NCLBUTTONDOWN, HTSYSMENU,
                        pack_point(5, 6)) == 0);
  REQUIRE(lastPostedWindow == static_cast<uint32_t>(
      reinterpret_cast<uintptr_t>(owner)));
  REQUIRE(lastPostedMessage == WM_SYSCOMMAND);
  REQUIRE(lastPostedWParam == SC_CLOSE);
  REQUIRE(context.result == 1);

  scriptedMenuMessages.clear();
  sdk_gui_script_menu_message(WM_LBUTTONDOWN, 0, pack_point(0, 2 * 20 + 1));
  REQUIRE(DefWindowProc(owner, WM_NCLBUTTONDOWN, HTSYSMENU,
                        pack_point(5, 6)) == 0);
  REQUIRE(lastPostedWParam == SC_SIZE);

  scriptedMenuMessages.clear();
  sdk_gui_script_menu_message(WM_ACTIVATE, 0, 0);
  const uint32_t priorPosts = postCalls;
  REQUIRE(DefWindowProc(owner, WM_NCLBUTTONDOWN, HTSYSMENU,
                        pack_point(5, 6)) == 0);
  REQUIRE(postCalls == priorPosts);
  REQUIRE(context.result == 0);

  scriptedMenuMessages.clear();
  moveRequests.clear();
  moveCalls = 0;
  sdk_gui_script_menu_message(WM_DISPLAYCHANGE, 0, 0);
  sdk_gui_script_menu_message(WM_KEYDOWN, VK_ESCAPE, 0);
  sdk_gui_change_display_on_message(320, 240);
  const uint32_t priorDisplayCreates = displayCreateCalls;
  const uint32_t priorDisplayDeletes = displayDeleteCalls;
  REQUIRE(DefWindowProc(owner, WM_NCLBUTTONDOWN, HTSYSMENU,
                        pack_point(600, 450)) == 0);
  REQUIRE(displayCreateCalls == priorDisplayCreates + 1);
  REQUIRE(displayDeleteCalls == priorDisplayDeletes + 1);
  REQUIRE(moveCalls == 1);
  REQUIRE(lastMove[0] >= 0);
  REQUIRE(lastMove[1] >= 0);
  REQUIRE(lastMove[0] + lastMove[2] <= 320);
  REQUIRE(lastMove[1] + lastMove[3] <= 240);

  moveRequests.clear();
  moveCalls = 0;
  backendWindowRect = {100, 100, 300, 220};
  backendClientRect = {102, 122, 298, 218};
  sdk_gui_queue_for_migration(
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(owner)),
      WM_KEYDOWN, VK_RIGHT, 0);
  sdk_gui_queue_for_migration(
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(owner)),
      WM_KEYDOWN, VK_ESCAPE, 0);
  REQUIRE(DefWindowProc(owner, WM_SYSCOMMAND, SC_SIZE | 0xBu, 0) == 0);
  REQUIRE(moveCalls == 2);
  const std::array<int32_t, 4> expectedSizeMove{100, 100, 208, 120};
  const std::array<int32_t, 4> expectedSizeRestore{100, 100, 200, 120};
  REQUIRE(moveRequests[0] == expectedSizeMove);
  REQUIRE(moveRequests[1] == expectedSizeRestore);
  REQUIRE(context.result == 1);

  moveRequests.clear();
  moveCalls = 0;
  sdk_gui_queue_for_migration(
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(owner)),
      WM_KEYDOWN, VK_RIGHT, 0);
  sdk_gui_queue_for_migration(
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(owner)),
      WM_KEYDOWN, VK_RETURN, 0);
  REQUIRE(DefWindowProc(owner, WM_SYSCOMMAND, SC_MOVE | 0xFu, 0) == 0);
  const std::vector<std::array<int32_t, 4>> expectedMove{
      std::array<int32_t, 4>{108, 100, 200, 120}};
  REQUIRE(moveRequests == expectedMove);
  REQUIRE(context.result == 1);

  moveRequests.clear();
  moveCalls = 0;
  for (int i = 0; i < 7; ++i)
    sdk_gui_queue_for_migration(
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(owner)),
        WM_KEYDOWN, VK_LEFT, 0);
  sdk_gui_queue_for_migration(
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(owner)),
      WM_KEYDOWN, VK_RETURN, 0);
  REQUIRE(DefWindowProc(owner, WM_SYSCOMMAND, SC_SIZE, 0) == 0);
  REQUIRE(moveCalls == 7);
  for (const auto &request : moveRequests) REQUIRE(request[2] >= 150);
  REQUIRE(moveRequests.back()[2] == 152);
  REQUIRE(context.result == 1);

  moveRequests.clear();
  moveCalls = 0;
  sdk_gui_change_display_on_message(250, 200);
  sdk_gui_queue_for_migration(
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(owner)),
      WM_DISPLAYCHANGE, 0, 0);
  sdk_gui_queue_for_migration(
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(owner)),
      WM_KEYDOWN, VK_RETURN, 0);
  REQUIRE(DefWindowProc(owner, WM_SYSCOMMAND, SC_MOVE, 0) == 0);
  REQUIRE(moveCalls == 1);
  const std::array<int32_t, 4> expectedDisplayClamp{50, 80, 200, 120};
  REQUIRE(moveRequests[0] == expectedDisplayClamp);
  REQUIRE(context.result == 1);

  moveRequests.clear();
  moveCalls = 0;
  failMoveCall = 2;
  failMoveStatus = UI_STATUS_RETRY;
  sdk_gui_queue_for_migration(
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(owner)),
      WM_KEYDOWN, VK_RIGHT, 0);
  sdk_gui_queue_for_migration(
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(owner)),
      WM_KEYDOWN, VK_ESCAPE, 0);
  REQUIRE(DefWindowProc(owner, WM_SYSCOMMAND, SC_MOVE, 0) == 0);
  REQUIRE(context.result == -1);
  REQUIRE(context.operationError == ERROR_RETRY);
  REQUIRE(context.operationStatus == UI_STATUS_RETRY);
  REQUIRE(moveCalls == 2);

  REQUIRE(UndosAttachUiContext(owner, nullptr));
  REQUIRE(DestroyWindow(owner));
  REQUIRE(UndosCloseUi(&context));
  REQUIRE(DestroyWindow(contextless));
}

TEST_CASE("system-menu WM_QUIT cancels without posting and preserves outer exit code",
          "[sdk-gui]") {
  resetBackend();
  UNDOS_UI_CONTEXT context{};
  REQUIRE(UndosInitUi(&context));
  HWND owner = make_ui_owner(&context, "menu-quit");
  sdk_gui_script_menu_message(WM_QUIT, 37, 0);
  const uint32_t postsBefore = postCalls;

  REQUIRE(DefWindowProc(owner, WM_NCLBUTTONDOWN, HTSYSMENU, 0) == 0);
  REQUIRE(context.result == 0);
  REQUIRE(context.operationError == ERROR_SUCCESS);
  REQUIRE(context.operationStatus == UI_STATUS_OK);
  REQUIRE(postCalls == postsBefore);
  REQUIRE(ownerSystemCommandCalls == 0);

  MSG message{};
  REQUIRE(GetMessage(&message, nullptr, 0, 0) == 0);
  REQUIRE(message.hwnd == nullptr);
  REQUIRE(message.message == WM_QUIT);
  REQUIRE(message.wParam == 37);
  REQUIRE(UndosAttachUiContext(owner, nullptr));
  REQUIRE(DestroyWindow(owner));
  REQUIRE(UndosCloseUi(&context));
}

TEST_CASE("Move and Size WM_QUIT unwind and preserve outer exit codes",
          "[sdk-gui]") {
  for (const UINT command : {SC_MOVE, SC_SIZE}) {
    resetBackend();
    UNDOS_UI_CONTEXT context{};
    REQUIRE(UndosInitUi(&context));
    HWND owner = make_ui_owner(&context, "geometry-quit");
    backendWindowRect = {100, 100, 300, 220};
    backendClientRect = {102, 122, 298, 218};
    const uint32_t exitCode = command == SC_MOVE ? 41u : 42u;
    queue.push_back({0, WM_QUIT, exitCode, 0, 0});

    REQUIRE(DefWindowProc(owner, WM_SYSCOMMAND, command, 0) == 0);
    REQUIRE(context.result == 1);
    REQUIRE(context.operationError == ERROR_SUCCESS);
    REQUIRE(context.operationStatus == UI_STATUS_OK);
    REQUIRE(moveCalls == 0);

    RECT finalRect{};
    REQUIRE(GetWindowRect(owner, &finalRect));
    REQUIRE(finalRect.left == 100);
    REQUIRE(finalRect.top == 100);
    REQUIRE(finalRect.right == 300);
    REQUIRE(finalRect.bottom == 220);
    MSG message{};
    REQUIRE(GetMessage(&message, nullptr, 0, 0) == 0);
    REQUIRE(message.hwnd == nullptr);
    REQUIRE(message.message == WM_QUIT);
    REQUIRE(message.wParam == exitCode);

    REQUIRE(UndosAttachUiContext(owner, nullptr));
    REQUIRE(DestroyWindow(owner));
    REQUIRE(UndosCloseUi(&context));
  }
}

TEST_CASE("field dialog edits UTF-8 by scalar and commits only on explicit acceptance",
          "[sdk-gui]") {
  resetBackend();
  UNDOS_UI_CONTEXT context{};
  REQUIRE(UndosInitUi(&context));
  HWND owner = make_ui_owner(&context, "dialog-scalar",
                             count_owner_char_proc);

  char value[12] = "A\xc3\xa9";
  UNDOS_DIALOG_FIELD field{"Name", value, sizeof(value)};
  sdk_gui_script_dialog_message(WM_CHAR, 0x1F600, 0);
  sdk_gui_script_dialog_message(WM_CHAR, 8, 0);
  sdk_gui_script_dialog_message(WM_CHAR, 8, 0);
  sdk_gui_script_dialog_message(WM_CHAR, 'z', 0);
  sdk_gui_script_dialog_message(WM_KEYDOWN, VK_RETURN, 0);
  REQUIRE(UndosRunDialog(&context, owner, "Scalar", "Edit a value", &field,
                         1, TRUE) == 1);
  REQUIRE(std::string(value) == "Az");
  REQUIRE(context.result == 1);

  scriptedDialogMessages.clear();
  std::strcpy(value, "unchanged");
  sdk_gui_script_dialog_message(WM_CHAR, 'x', 0);
  sdk_gui_script_dialog_message(WM_KEYDOWN, VK_ESCAPE, 0);
  REQUIRE(UndosRunDialog(&context, owner, "Cancel", "No commit", &field,
                         1, TRUE) == 0);
  REQUIRE(std::string(value) == "unchanged");
  REQUIRE(context.result == 0);

  scriptedDialogMessages.clear();
  std::strcpy(value, "ab");
  field.capacity = 3;
  sdk_gui_script_dialog_message(WM_CHAR, 0xE9, 0);
  sdk_gui_script_dialog_message(WM_KEYDOWN, VK_RETURN, 0);
  REQUIRE(UndosRunDialog(&context, owner, "Full", "Bounded field", &field,
                         1, FALSE) == 1);
  REQUIRE(std::string(value) == "ab");

  scriptedDialogMessages.clear();
  queue.push_back({static_cast<uint32_t>(reinterpret_cast<uintptr_t>(owner)),
                   WM_CHAR, 'b', 0, 0});
  sdk_gui_script_dialog_message(WM_KEYDOWN, VK_RETURN, 0);
  std::strcpy(value, "a");
  field.capacity = sizeof(value);
  REQUIRE(UndosRunDialog(&context, owner, "Gate", "Owner input is gated",
                         &field, 1, FALSE) == 1);
  REQUIRE(std::string(value) == "ab");
  REQUIRE(ownerCharCalls == 0);

  scriptedDialogMessages.clear();
  sdk_gui_script_dialog_message(WM_DISPLAYCHANGE, 0, 0);
  sdk_gui_script_dialog_message(WM_KEYDOWN, VK_RETURN, 0);
  sdk_gui_change_display_on_message(320, 240);
  moveRequests.clear();
  moveCalls = 0;
  REQUIRE(UndosRunDialog(&context, owner, "Display", "Reflow on display change",
                         &field, 1, FALSE) == 1);
  REQUIRE(moveCalls == 1);
  REQUIRE(lastMove[0] >= 0);
  REQUIRE(lastMove[1] >= 0);
  REQUIRE(lastMove[0] + lastMove[2] <= 320);
  REQUIRE(lastMove[1] + lastMove[3] <= 240);

  REQUIRE(UndosAttachUiContext(owner, nullptr));
  REQUIRE(DestroyWindow(owner));
  REQUIRE(UndosCloseUi(&context));
}

TEST_CASE("field dialog validates before creation and preserves failure diagnostics",
          "[sdk-gui]") {
  resetBackend();
  UNDOS_UI_CONTEXT context{};
  REQUIRE(UndosInitUi(&context));
  HWND owner = make_ui_owner(&context, "dialog-failure");
  char value[4] = "old";
  UNDOS_DIALOG_FIELD field{"Name", value, sizeof(value)};
  const uint32_t beforeInvalid = createCalls;
  field.capacity = 1026;
  REQUIRE(UndosRunDialog(&context, owner, "Bad", "No create", &field,
                         1, TRUE) == -1);
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);
  REQUIRE(context.operationError == ERROR_INVALID_PARAMETER);
  REQUIRE(std::string(value) == "old");
  REQUIRE(createCalls == beforeInvalid);

  field.capacity = sizeof(value);
  std::strcpy(value, "old");
  failOperation = Operation::SetFocus;
  failStatus = UI_STATUS_OUT_OF_MEMORY;
  REQUIRE(UndosRunDialog(&context, owner, "Focus", "Focus failure", &field,
                         1, TRUE) == -1);
  REQUIRE(context.result == -1);
  REQUIRE(context.operationError == ERROR_NOT_ENOUGH_MEMORY);
  REQUIRE(context.operationStatus == UI_STATUS_OUT_OF_MEMORY);
  REQUIRE(std::string(value) == "old");
  REQUIRE_FALSE(UndosGetUiCleanupRecovery(
      static_cast<UNDOS_UI_CLEANUP_RECOVERY *>(nullptr)));
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);

  failOperation = Operation::GetMessage;
  failStatus = UI_STATUS_TIMEOUT;
  REQUIRE(UndosRunDialog(&context, owner, "Queue", "GetMessage failure",
                         &field, 1, TRUE) == -1);
  REQUIRE(context.operationError == ERROR_TIMEOUT);
  REQUIRE(context.operationStatus == UI_STATUS_TIMEOUT);
  REQUIRE(std::string(value) == "old");

  REQUIRE(UndosAttachUiContext(owner, nullptr));
  REQUIRE(DestroyWindow(owner));
  REQUIRE(UndosCloseUi(&context));
}

TEST_CASE("dialog quit, owner close, owner destruction, and paint recovery are preserved",
          "[sdk-gui]") {
  resetBackend();
  UNDOS_UI_CONTEXT context{};
  REQUIRE(UndosInitUi(&context));
  HWND owner = make_ui_owner(&context, "dialog-quit");
  char value[8] = "old";
  UNDOS_DIALOG_FIELD field{"Name", value, sizeof(value)};

  queue.push_back({0, WM_QUIT, 23, 0, 0});
  REQUIRE(UndosRunDialog(&context, owner, "Quit", "Preserve quit",
                         &field, 1, TRUE) == 0);
  REQUIRE(std::string(value) == "old");
  MSG message{};
  REQUIRE(GetMessage(&message, nullptr, 0, 0) == 0);
  REQUIRE(message.message == WM_QUIT);
  REQUIRE(message.wParam == 23);
  queue.clear();

  scriptedDialogMessages.clear();
  queue.push_back({static_cast<uint32_t>(reinterpret_cast<uintptr_t>(owner)),
                   WM_CLOSE, 0, 0, 0});
  sdk_gui_script_dialog_message(WM_CHAR, 'x', 0);
  const uint32_t posts = postCalls;
  REQUIRE(UndosRunDialog(&context, owner, "Close", "Requeue close",
                         &field, 1, TRUE) == 0);
  REQUIRE(std::string(value) == "old");
  REQUIRE(postCalls == posts + 1);
  REQUIRE(lastPostedWindow == static_cast<uint32_t>(
      reinterpret_cast<uintptr_t>(owner)));
  REQUIRE(lastPostedMessage == WM_CLOSE);
  queue.clear();

  REQUIRE(UndosAttachUiContext(owner, nullptr));
  REQUIRE(DestroyWindow(owner));
  owner = make_ui_owner(&context, "dialog-disappear", destroy_on_user_proc);
  std::strcpy(value, "old");
  queue.push_back({static_cast<uint32_t>(reinterpret_cast<uintptr_t>(owner)),
                   WM_USER, 0, 0, 0});
  const uint32_t postsBeforeDisappear = postCalls;
  REQUIRE(UndosRunDialog(&context, owner, "Disappear", "Owner is destroyed",
                         &field, 1, TRUE) == 0);
  REQUIRE(std::string(value) == "old");
  REQUIRE(postCalls == postsBeforeDisappear);
  REQUIRE_FALSE(nativeWindows.contains(
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(owner))));

  owner = make_ui_owner(&context, "dialog-paint-recovery");
  std::strcpy(value, "old");
  scriptedDialogMessages.clear();
  sdk_gui_script_dialog_message(WM_PAINT, 0, 0);
  sdk_gui_script_dialog_message(WM_KEYDOWN, VK_RETURN, 0);
  cleanupEndStatus = UI_STATUS_RETRY;
  REQUIRE(UndosRunDialog(&context, owner, "Recover", "Paint cleanup",
                         &field, 1, TRUE) == -1);
  REQUIRE(std::string(value) == "old");
  REQUIRE(context.result == -1);
  REQUIRE(context.operationStatus == 0);
  REQUIRE(context.cleanupStatus == UI_STATUS_RETRY);
  UNDOS_UI_CLEANUP_RECOVERY recovery{};
  REQUIRE(UndosGetUiCleanupRecovery(&recovery));
  REQUIRE(recovery.operationStatus == 0);
  REQUIRE(recovery.cleanupStatus == UI_STATUS_RETRY);
  const uint32_t beforeRetry = destroyCalls;
  REQUIRE(DestroyWindow(recovery.window));
  REQUIRE(destroyCalls == beforeRetry + 1);
  REQUIRE_FALSE(UndosGetUiCleanupRecovery(&recovery));

  REQUIRE(UndosAttachUiContext(owner, nullptr));
  REQUIRE(DestroyWindow(owner));
  REQUIRE(UndosCloseUi(&context));
}

TEST_CASE("dialog final destruction failure withholds field commit and recovers publicly",
          "[sdk-gui]") {
  resetBackend();
  UNDOS_UI_CONTEXT context{};
  REQUIRE(UndosInitUi(&context));
  HWND owner = make_ui_owner(&context, "dialog-destroy-recovery");
  char value[8] = "old";
  UNDOS_DIALOG_FIELD field{"Name", value, sizeof(value)};
  sdk_gui_script_dialog_message(WM_CHAR, 'x', 0);
  sdk_gui_script_dialog_message(WM_KEYDOWN, VK_RETURN, 0);
  forcedDestroyStatus = UI_STATUS_RETRY;
  REQUIRE(UndosRunDialog(&context, owner, "Destroy", "Cleanup failure",
                         &field, 1, TRUE) == -1);
  REQUIRE(std::string(value) == "old");
  REQUIRE(context.result == -1);
  REQUIRE(context.operationError == ERROR_SUCCESS);
  REQUIRE(context.operationStatus == UI_STATUS_OK);
  REQUIRE(context.cleanupError == ERROR_RETRY);
  REQUIRE(context.cleanupStatus == UI_STATUS_RETRY);
  UNDOS_UI_CLEANUP_RECOVERY recovery{};
  REQUIRE(UndosGetUiCleanupRecovery(&recovery));
  REQUIRE(recovery.window);
  REQUIRE(recovery.operationError == ERROR_SUCCESS);
  REQUIRE(recovery.operationStatus == UI_STATUS_OK);
  REQUIRE(recovery.cleanupError == ERROR_RETRY);
  REQUIRE(recovery.cleanupStatus == UI_STATUS_RETRY);
  const uint32_t beforeRetry = destroyCalls;
  REQUIRE(DestroyWindow(recovery.window));
  REQUIRE(destroyCalls == beforeRetry + 1);
  REQUIRE_FALSE(UndosGetUiCleanupRecovery(&recovery));
  REQUIRE(UndosAttachUiContext(owner, nullptr));
  REQUIRE(DestroyWindow(owner));
  REQUIRE(UndosCloseUi(&context));

  resetBackend();
  REQUIRE(UndosInitUi(&context));
  owner = make_ui_owner(&context, "dialog-dual-failure");
  std::strcpy(value, "old");
  failOperation = Operation::GetMessage;
  failStatus = UI_STATUS_TIMEOUT;
  forcedDestroyStatus = UI_STATUS_RETRY;
  REQUIRE(UndosRunDialog(&context, owner, "Dual", "Primary and cleanup",
                         &field, 1, TRUE) == -1);
  REQUIRE(std::string(value) == "old");
  REQUIRE(context.operationError == ERROR_TIMEOUT);
  REQUIRE(context.operationStatus == UI_STATUS_TIMEOUT);
  REQUIRE(context.cleanupError == ERROR_RETRY);
  REQUIRE(context.cleanupStatus == UI_STATUS_RETRY);
  REQUIRE(UndosGetUiCleanupRecovery(&recovery));
  REQUIRE(recovery.operationError == ERROR_TIMEOUT);
  REQUIRE(recovery.operationStatus == UI_STATUS_TIMEOUT);
  REQUIRE(recovery.cleanupError == ERROR_RETRY);
  REQUIRE(recovery.cleanupStatus == UI_STATUS_RETRY);
  REQUIRE(DestroyWindow(recovery.window));
  REQUIRE(UndosAttachUiContext(owner, nullptr));
  REQUIRE(DestroyWindow(owner));
  REQUIRE(UndosCloseUi(&context));

  resetBackend();
  REQUIRE(UndosInitUi(&context));
  owner = make_ui_owner(&context, "dialog-font-recovery");
  std::strcpy(value, "old");
  returnedPreviousFont = 77;
  sdk_gui_script_dialog_message(WM_PAINT, 0, 0);
  sdk_gui_script_dialog_message(WM_KEYDOWN, VK_RETURN, 0);
  cleanupFontStatus = UI_STATUS_RETRY;
  REQUIRE(UndosRunDialog(&context, owner, "Font", "Previous font cleanup",
                         &field, 1, TRUE) == -1);
  REQUIRE(std::string(value) == "old");
  REQUIRE(context.cleanupStatus == UI_STATUS_RETRY);
  REQUIRE(UndosGetUiCleanupRecovery(&recovery));
  REQUIRE(recovery.cleanupStatus == UI_STATUS_RETRY);
  const uint32_t beforeFontRetry = closeFontCalls;
  REQUIRE(DestroyWindow(recovery.window));
  REQUIRE(closeFontCalls == beforeFontRetry + 1);
  REQUIRE_FALSE(UndosGetUiCleanupRecovery(&recovery));
  REQUIRE(UndosAttachUiContext(owner, nullptr));
  REQUIRE(DestroyWindow(owner));
  REQUIRE(UndosCloseUi(&context));
}

TEST_CASE("system-menu final destroy failure reports recovery and dispatches selection",
          "[sdk-gui]") {
  resetBackend();
  UNDOS_UI_CONTEXT context{};
  REQUIRE(UndosInitUi(&context));
  WNDCLASS klass = test_class("menu-final-destroy");
  klass.lpfnWndProc = system_command_owner_proc;
  REQUIRE(RegisterClass(&klass));
  HWND owner = CreateWindow("menu-final-destroy", "", 0, 0, 0, 240, 160,
                            nullptr, nullptr, nullptr, nullptr);
  REQUIRE(owner);
  REQUIRE(UndosAttachUiContext(owner, &context));
  sdk_gui_script_menu_message(WM_KEYDOWN, VK_UP, 0);
  sdk_gui_script_menu_message(WM_KEYDOWN, VK_RETURN, 0);
  forcedDestroyStatus = UI_STATUS_RETRY;
  REQUIRE(DefWindowProc(owner, WM_NCLBUTTONDOWN, HTSYSMENU, 0) == 0);
  REQUIRE(context.result == -1);
  REQUIRE(context.operationError == ERROR_SUCCESS);
  REQUIRE(context.operationStatus == UI_STATUS_OK);
  REQUIRE(context.cleanupError == ERROR_RETRY);
  REQUIRE(context.cleanupStatus == UI_STATUS_RETRY);
  REQUIRE(postCalls == 1);
  REQUIRE(lastPostedWindow == static_cast<uint32_t>(
      reinterpret_cast<uintptr_t>(owner)));
  REQUIRE(lastPostedMessage == WM_SYSCOMMAND);
  REQUIRE(lastPostedWParam == SC_CLOSE);
  UNDOS_UI_CLEANUP_RECOVERY recovery{};
  REQUIRE(UndosGetUiCleanupRecovery(&recovery));
  REQUIRE(recovery.cleanupStatus == UI_STATUS_RETRY);
  REQUIRE(UndosGetUiCleanupRecovery(&recovery));
  const uint32_t beforeRetry = destroyCalls;
  REQUIRE(DestroyWindow(recovery.window));
  REQUIRE(destroyCalls == beforeRetry + 1);
  REQUIRE_FALSE(UndosGetUiCleanupRecovery(&recovery));

  MSG command{};
  REQUIRE(GetMessage(&command, nullptr, 0, 0) == 1);
  REQUIRE(command.message == WM_SYSCOMMAND);
  REQUIRE(DispatchMessage(&command) == 0);
  REQUIRE(ownerSystemCommandCalls == 1);
  REQUIRE(ownerCloseCalls == 1);
  REQUIRE_FALSE(nativeWindows.contains(static_cast<uint32_t>(
      reinterpret_cast<uintptr_t>(owner))));
  REQUIRE(UndosCloseUi(&context));
}

TEST_CASE("shared dialog pump gates owner input, dispatches unrelated events, and rejects nesting",
          "[sdk-gui]") {
  resetBackend();
  UNDOS_UI_CONTEXT context{};
  REQUIRE(UndosInitUi(&context));
  WNDCLASS ownerClass = test_class("dialog-gated-owner");
  ownerClass.lpfnWndProc = dialog_gate_owner_proc;
  REQUIRE(RegisterClass(&ownerClass));
  HWND owner = CreateWindow("dialog-gated-owner", "", 0, 0, 0, 300, 180,
                            nullptr, nullptr, nullptr, nullptr);
  REQUIRE(owner);
  REQUIRE(UndosAttachUiContext(owner, &context));
  WNDCLASS otherClass = test_class("dialog-unrelated-window");
  otherClass.lpfnWndProc = unrelated_message_proc;
  REQUIRE(RegisterClass(&otherClass));
  HWND unrelated = CreateWindow("dialog-unrelated-window", "", 0, 0, 0,
                                100, 80, nullptr, nullptr, nullptr, nullptr);
  REQUIRE(unrelated);
  char first[8] = "a", second[8] = "b";
  UNDOS_DIALOG_FIELD fields[] = {
      {"First", first, sizeof(first)}, {"Second", second, sizeof(second)}};
  queue.push_back({static_cast<uint32_t>(reinterpret_cast<uintptr_t>(owner)),
                   WM_COMMAND, 7, 0, 0});
  queue.push_back({static_cast<uint32_t>(reinterpret_cast<uintptr_t>(owner)),
                   WM_LBUTTONDOWN, 0,
                   static_cast<uint32_t>(pack_point(20, 50)), 0});
  queue.push_back({static_cast<uint32_t>(reinterpret_cast<uintptr_t>(unrelated)),
                   WM_USER, 0, 0, 0});
  sdk_gui_script_dialog_message(WM_CHAR, '1', 0);
  sdk_gui_script_dialog_message(WM_KEYDOWN, VK_TAB, 0);
  sdk_gui_script_dialog_message(WM_CHAR, '2', 0);
  sdk_gui_script_dialog_message(WM_KEYDOWN, VK_SHIFT, 0);
  sdk_gui_script_dialog_message(WM_KEYDOWN, VK_TAB, 0);
  sdk_gui_script_dialog_message(WM_KEYUP, VK_SHIFT, 0);
  sdk_gui_script_dialog_message(WM_CHAR, 'x', 0);
  sdk_gui_script_dialog_message(WM_LBUTTONDOWN, 0, pack_point(20, 81));
  sdk_gui_script_dialog_message(WM_CHAR, '3', 0);
  sdk_gui_script_dialog_message(WM_KEYDOWN, VK_RETURN, 0);
  REQUIRE(UndosRunDialog(&context, owner, "Fields", "Edit fields", fields, 2,
                         TRUE) == 1);
  REQUIRE(std::string(first) == "a1x");
  REQUIRE(std::string(second) == "b23");
  REQUIRE(ownerCommandCalls == 0);
  REQUIRE(ownerMouseCalls == 0);
  REQUIRE(unrelatedMessageCalls == 1);

  REQUIRE(UndosAttachUiContext(owner, nullptr));
  REQUIRE(DestroyWindow(owner));
  REQUIRE(DestroyWindow(unrelated));
  REQUIRE(UndosCloseUi(&context));

  resetBackend();
  REQUIRE(UndosInitUi(&context));
  owner = make_ui_owner(&context, "dialog-nested-field",
                        nested_dialog_owner_proc);
  nestedDialogContext = &context;
  queue.push_back({static_cast<uint32_t>(reinterpret_cast<uintptr_t>(owner)),
                   WM_USER, 0, 0, 0});
  sdk_gui_script_dialog_message(WM_KEYDOWN, VK_RETURN, 0);
  char value[8] = "keep";
  UNDOS_DIALOG_FIELD field{"Value", value, sizeof(value)};
  REQUIRE(UndosRunDialog(&context, owner, "Outer", "Nested rejected",
                         &field, 1, TRUE) == 1);
  REQUIRE(nestedDialogError == ERROR_RETRY);
  REQUIRE(nestedDialogStatus == UI_STATUS_RETRY);
  REQUIRE(std::string(value) == "keep");
  REQUIRE(context.result == 1);
  REQUIRE(UndosAttachUiContext(owner, nullptr));
  REQUIRE(DestroyWindow(owner));
  REQUIRE(UndosCloseUi(&context));
}

TEST_CASE("UI cleanup recovery uses public queries through capacity exhaustion",
          "[sdk-gui]") {
  resetBackend();
  UNDOS_UI_CONTEXT context{};
  REQUIRE(UndosInitUi(&context));
  HWND owner = make_ui_owner(&context, "menu-recovery-capacity");
  UNDOS_UI_CLEANUP_RECOVERY first{};

  for (uint32_t i = 0; i < 63; ++i) {
    scriptedMenuMessages.clear();
    sdk_gui_script_menu_message(WM_PAINT, 0, 0);
    returnedPreviousFont = 77;
    cleanupFontStatus = UI_STATUS_OUT_OF_MEMORY;
    REQUIRE(DefWindowProc(owner, WM_NCLBUTTONDOWN, HTSYSMENU,
                          pack_point(1, 1)) == 0);
    REQUIRE(context.result == -1);
    UNDOS_UI_CLEANUP_RECOVERY current{};
    REQUIRE(UndosGetUiCleanupRecovery(&current));
    REQUIRE(current.window);
    REQUIRE(current.cleanupStatus == UI_STATUS_OUT_OF_MEMORY);
    if (i == 0) first = current;
  }
  REQUIRE(nativeWindows.size() == 64);
  const uint32_t beforeNativeCreate = createCalls;
  scriptedMenuMessages.clear();
  sdk_gui_script_menu_message(WM_PAINT, 0, 0);
  REQUIRE(DefWindowProc(owner, WM_NCLBUTTONDOWN, HTSYSMENU,
                        pack_point(1, 1)) == 0);
  REQUIRE(context.result == -1);
  REQUIRE(context.operationError == ERROR_NOT_ENOUGH_MEMORY);
  REQUIRE(context.operationStatus == UI_STATUS_OUT_OF_MEMORY);
  REQUIRE(createCalls == beforeNativeCreate);
  UNDOS_UI_CLEANUP_RECOVERY current{};
  REQUIRE(UndosGetUiCleanupRecovery(&current));
  REQUIRE(current.window == first.window);
  REQUIRE(current.cleanupStatus == first.cleanupStatus);

  uint32_t cleaned = 0;
  while (UndosGetUiCleanupRecovery(&current)) {
    REQUIRE(current.window);
    REQUIRE(DestroyWindow(current.window));
    ++cleaned;
  }
  REQUIRE(cleaned == 63);
  REQUIRE(nativeWindows.size() == 1);
  REQUIRE(UndosAttachUiContext(owner, nullptr));
  REQUIRE(DestroyWindow(owner));
  REQUIRE(UndosCloseUi(&context));
}

namespace {
std::vector<std::array<uintptr_t, 3>> sentMessages;
LRESULT CALLBACK recording_proc(HWND, UINT message, WPARAM wParam,
                                LPARAM lParam) {
  sentMessages.push_back({message, wParam, static_cast<uintptr_t>(lParam)});
  return message == WM_USER ? static_cast<LRESULT>(wParam + 1) : 0;
}

HWND make_recording_window(const char *name) {
  WNDCLASS klass = test_class(name);
  klass.lpfnWndProc = recording_proc;
  REQUIRE(RegisterClass(&klass) != 0);
  HWND window = CreateWindow(name, "recording", 0, 0, 0, 200, 100, nullptr,
                             nullptr, nullptr, nullptr);
  REQUIRE(window);
  sentMessages.clear();
  return window;
}
} // namespace

TEST_CASE("SendMessage calls the window procedure synchronously", "[sdk-gui]") {
  resetBackend();
  HWND window = make_recording_window("send-target");
  REQUIRE(SendMessage(window, WM_USER, 41, -3) == 42);
  REQUIRE(sentMessages.size() == 1);
  REQUIRE(sentMessages[0][1] == 41);
  REQUIRE(static_cast<LPARAM>(sentMessages[0][2]) == -3);
  REQUIRE(queue.empty());
  SetLastError(ERROR_SUCCESS);
  REQUIRE(SendMessage(nullptr, WM_USER, 0, 0) == 0);
  REQUIRE(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
  REQUIRE(DestroyWindow(window));
  REQUIRE(SendMessage(window, WM_USER, 0, 0) == 0);
  REQUIRE(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
}

TEST_CASE("menu state changes in place and reports the previous state",
          "[sdk-gui]") {
  resetBackend();
  HMENU view = CreatePopupMenu();
  REQUIRE(AppendMenu(view, MF_STRING | MF_CHECKED, 100, "Name"));
  REQUIRE(AppendMenu(view, MF_SEPARATOR, 0, nullptr));
  REQUIRE(AppendMenu(view, MF_STRING, 101, "Details"));
  HMENU bar = CreateMenu();
  REQUIRE(AppendMenu(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(view), "View"));

  REQUIRE(CheckMenuItem(bar, 100, MF_BYCOMMAND | MF_UNCHECKED) == MF_CHECKED);
  REQUIRE(CheckMenuItem(bar, 100, MF_BYCOMMAND | MF_UNCHECKED) == MF_UNCHECKED);
  REQUIRE(CheckMenuItem(view, 2, MF_BYPOSITION | MF_CHECKED) == MF_UNCHECKED);
  REQUIRE(GetMenuState(view, 101, MF_BYCOMMAND) == MF_CHECKED);
  REQUIRE(EnableMenuItem(bar, 101, MF_GRAYED) == MF_ENABLED);
  REQUIRE(EnableMenuItem(bar, 101, MF_ENABLED) == MF_GRAYED);
  REQUIRE(GetMenuState(view, 1, MF_BYPOSITION) == MF_SEPARATOR);
  REQUIRE(GetMenuItemCount(view) == 3);
  REQUIRE(GetMenuItemID(view, 2) == 101);
  REQUIRE(GetMenuItemID(view, 1) == 0);
  REQUIRE(GetMenuItemID(bar, 0) == static_cast<UINT>(-1));
  REQUIRE(GetSubMenu(bar, 0) == view);
  REQUIRE(GetSubMenu(view, 0) == nullptr);

  REQUIRE(CheckMenuItem(bar, 999, MF_BYCOMMAND) == static_cast<DWORD>(-1));
  REQUIRE(GetLastError() == ERROR_MENU_ITEM_NOT_FOUND);
  REQUIRE(EnableMenuItem(view, 1, MF_BYPOSITION | MF_GRAYED) == -1);
  REQUIRE(CheckMenuItem(view, 0, MF_POPUP) == static_cast<DWORD>(-1));
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);
  REQUIRE(GetMenuState(nullptr, 0, MF_BYPOSITION) == static_cast<UINT>(-1));
  REQUIRE(GetLastError() == ERROR_INVALID_MENU_HANDLE);
}

TEST_CASE("accelerator tables translate keys into WM_COMMAND", "[sdk-gui]") {
  resetBackend();
  HMENU file = CreatePopupMenu();
  REQUIRE(AppendMenu(file, MF_STRING, 200, "Copy"));
  REQUIRE(AppendMenu(file, MF_STRING | MF_GRAYED, 201, "Format"));
  HMENU bar = CreateMenu();
  REQUIRE(AppendMenu(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file), "File"));
  HWND window = make_recording_window("accel-target");
  REQUIRE(SetMenu(window, bar));

  ACCEL entries[] = {{FVIRTKEY, VK_F8, 200},
                     {FVIRTKEY | FCONTROL, 'F', 201},
                     {0, '*', 202}};
  HACCEL table = CreateAcceleratorTable(entries, 3);
  REQUIRE(table);

  MSG message{};
  message.hwnd = window;
  message.message = WM_KEYDOWN;
  message.wParam = VK_F8;
  REQUIRE(TranslateAccelerator(window, table, &message) == 1);
  REQUIRE(sentMessages.size() == 2);
  REQUIRE(sentMessages[0][0] == WM_INITMENU);
  REQUIRE(sentMessages[1][0] == WM_COMMAND);
  REQUIRE(sentMessages[1][1] == MAKEWPARAM(200, 1));

  sentMessages.clear();
  message.wParam = 'F';
  REQUIRE(TranslateAccelerator(window, table, &message) == 0);
  queue.push_back({static_cast<uint32_t>(reinterpret_cast<uintptr_t>(window)),
                   WM_KEYDOWN, VK_CONTROL, 0, 0});
  MSG pumped{};
  REQUIRE(GetMessage(&pumped, nullptr, 0, 0) == 1);
  REQUIRE((GetKeyState(VK_CONTROL) & 0x8000) != 0);
  REQUIRE(TranslateAccelerator(window, table, &message) == 1);
  REQUIRE(sentMessages.size() == 1);
  REQUIRE(sentMessages[0][0] == WM_INITMENU);

  sentMessages.clear();
  queue.push_back({static_cast<uint32_t>(reinterpret_cast<uintptr_t>(window)),
                   WM_KEYUP, VK_CONTROL, 0, 0});
  REQUIRE(GetMessage(&pumped, nullptr, 0, 0) == 1);
  REQUIRE(GetKeyState(VK_CONTROL) == 1);
  message.message = WM_CHAR;
  message.wParam = '*';
  REQUIRE(TranslateAccelerator(window, table, &message) == 1);
  REQUIRE(sentMessages.back()[1] == MAKEWPARAM(202, 1));

  REQUIRE(DestroyAcceleratorTable(table));
  REQUIRE(TranslateAccelerator(window, table, &message) == 0);
  REQUIRE(GetLastError() == ERROR_INVALID_ACCEL_HANDLE);
  REQUIRE_FALSE(DestroyAcceleratorTable(table));
  REQUIRE(CreateAcceleratorTable(entries, 0) == nullptr);
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);
  REQUIRE(LoadAccelerators(nullptr, "WFACCEL") == nullptr);
  REQUIRE(GetLastError() == ERROR_RESOURCE_TYPE_NOT_FOUND);
  std::vector<HACCEL> tables;
  for (;;) {
    HACCEL next = CreateAcceleratorTable(entries, 1);
    if (!next) break;
    tables.push_back(next);
  }
  REQUIRE(GetLastError() == ERROR_NOT_ENOUGH_MEMORY);
  REQUIRE(tables.size() == 8);
}

TEST_CASE("kernel modifier mask drives key state and FCONTROL/FALT accelerators", "[sdk-gui]") {
  resetBackend();
  HWND window = make_recording_window("accel-mask");
  ACCEL entries[] = {{FVIRTKEY | FCONTROL, 'Z', 300}, {FVIRTKEY | FALT, VK_RETURN, 301}};
  HACCEL table = CreateAcceleratorTable(entries, 2);
  REQUIRE(table);
  const auto hwnd = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(window));
  // Win32 lParam: repeat 1, scan code in bits 16-23, Alt context bit 29.
  queue.push_back({hwnd, WM_KEYDOWN, 'Z', 1u | (0x2Cu << 16), 0, 4});
  MSG pumped{};
  REQUIRE(GetMessage(&pumped, nullptr, 0, 0) == 1);
  REQUIRE(((pumped.lParam >> 16) & 0xFF) == 0x2C);
  REQUIRE(GetKeyState(VK_CONTROL) < 0);
  REQUIRE(GetKeyState(VK_LCONTROL) < 0);
  REQUIRE(GetKeyState(VK_MENU) >= 0);
  REQUIRE(TranslateAccelerator(window, table, &pumped) == 1);
  REQUIRE(sentMessages.back()[1] == MAKEWPARAM(300, 1));

  sentMessages.clear();
  queue.push_back({hwnd, WM_SYSKEYDOWN, VK_RETURN, 1u | (1u << 29), 0, 32 | 64});
  REQUIRE(GetMessage(&pumped, nullptr, 0, 0) == 1);
  REQUIRE(GetKeyState(VK_CONTROL) >= 0);
  REQUIRE(GetKeyState(VK_MENU) < 0);
  REQUIRE(GetKeyState(VK_RMENU) < 0);
  REQUIRE((GetKeyState(VK_CAPITAL) & 1) == 1);
  REQUIRE(TranslateAccelerator(window, table, &pumped) == 1);
  REQUIRE(sentMessages.back()[1] == MAKEWPARAM(301, 1));
  REQUIRE(DefWindowProc(window, WM_SYSKEYDOWN, VK_RETURN, pumped.lParam) == 0);

  queue.push_back({hwnd, WM_SYSKEYUP, VK_MENU, 1u | (3u << 30), 0, 64});
  REQUIRE(GetMessage(&pumped, nullptr, 0, 0) == 1);
  REQUIRE(GetKeyState(VK_MENU) >= 0);
  REQUIRE(DestroyAcceleratorTable(table));
}

TEST_CASE("Win32 GDI text calls measure, align and validate", "[sdk-gui][gdi]") {
  resetBackend();
  variableTextMetrics = true;
  HWND window = make_window("gdi-text");
  PAINTSTRUCT paint{};
  HDC dc = BeginPaint(window, &paint);
  REQUIRE(dc != nullptr);

  SIZE size{};
  REQUIRE(GetTextExtentPoint32(dc, "Wi.x", 3, &size));
  REQUIRE(size.cx == 17);
  REQUIRE(size.cy == 16);

  TEXTMETRIC metrics{};
  REQUIRE(GetTextMetrics(dc, &metrics));
  REQUIRE(metrics.tmAscent == 12);
  REQUIRE(metrics.tmDescent == 4);
  REQUIRE(metrics.tmHeight == 16);
  REQUIRE(metrics.tmAveCharWidth == 8);

  RECT calc{10, 20, 10, 20};
  REQUIRE(DrawText(dc, "&File", -1, &calc, DT_CALCRECT | DT_SINGLELINE) == 16);
  REQUIRE(GetTextExtentPoint32(dc, "File", 4, &size));
  REQUIRE(calc.right - calc.left == size.cx);
  REQUIRE(calc.bottom == 36);

  drawnStrings.clear();
  gdiOps.clear();
  RECT box{0, 0, 100, 40};
  REQUIRE(DrawText(dc, "a&&b", -1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE) == 16);
  REQUIRE(drawnStrings.back() == "a&b");
  REQUIRE(gdiOps.back()[1] == (100 - 21) / 2);
  REQUIRE(gdiOps.back()[2] == 12);

  drawnStrings.clear();
  RECT narrow{0, 0, 30, 0};
  REQUIRE(DrawText(dc, "aaa bbb ccc", -1, &narrow, DT_WORDBREAK | DT_CALCRECT) == 48);
  REQUIRE(DrawText(dc, "one\ntwo", -1, &box, DT_RIGHT) == 32);
  REQUIRE(drawnStrings == std::vector<std::string>{"one", "two"});

  REQUIRE(TextOut(dc, 3, 4, "hello", 2));
  REQUIRE(drawnStrings.back() == "he");
  REQUIRE(gdiOps.back()[1] == 3);

  gdiOps.clear();
  REQUIRE(SetBkColor(dc, RGB(0, 0, 170)) != CLR_INVALID);
  RECT opaque{1, 2, 3, 4};
  REQUIRE(ExtTextOut(dc, 0, 0, ETO_OPAQUE, &opaque, "x", 1, nullptr));
  REQUIRE(gdiOps[0][0] == 1);
  REQUIRE(gdiOps[0][5] == 1);
  REQUIRE(gdiOps[1][0] == 4);

  SetLastError(0);
  REQUIRE_FALSE(TextOut(nullptr, 0, 0, "x", 1));
  REQUIRE(GetLastError() == ERROR_INVALID_HANDLE);
  REQUIRE_FALSE(GetTextExtentPoint32(dc, "x", 1, nullptr));
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);
  REQUIRE_FALSE(ExtTextOut(dc, 0, 0, 0x8000u, nullptr, "x", 1, nullptr));
  REQUIRE(GetLastError() == ERROR_INVALID_FLAGS);
  REQUIRE(EndPaint(window, &paint));
}

TEST_CASE("Win32 GDI pens, brushes and stock objects", "[sdk-gui][gdi]") {
  resetBackend();
  HWND window = make_window("gdi-shapes");
  PAINTSTRUCT paint{};
  HDC dc = BeginPaint(window, &paint);
  REQUIRE(dc != nullptr);

  HPEN red = CreatePen(PS_SOLID, 1, RGB(255, 85, 85));
  REQUIRE(red != nullptr);
  HGDIOBJ oldPen = SelectObject(dc, red);
  REQUIRE(oldPen == GetStockObject(BLACK_PEN));
  POINT previous{};
  REQUIRE(MoveToEx(dc, 5, 6, &previous));
  REQUIRE(previous.x == 0);
  gdiOps.clear();
  REQUIRE(LineTo(dc, 20, 6));
  REQUIRE(gdiOps.size() == 2);
  REQUIRE(gdiOps[0] == std::array<int32_t, 6>{2, 5, 6, 0, 0, 12});
  REQUIRE(gdiOps[1] == std::array<int32_t, 6>{3, 20, 6, 0, 0, 12});
  REQUIRE(MoveToEx(dc, 0, 0, &previous));
  REQUIRE(previous.x == 20);

  SelectObject(dc, GetStockObject(NULL_PEN));
  gdiOps.clear();
  REQUIRE(LineTo(dc, 9, 9));
  REQUIRE(gdiOps.empty());

  HPEN wide = CreatePen(PS_SOLID, 3, RGB(0, 0, 0));
  SelectObject(dc, wide);
  REQUIRE(LineTo(dc, 9, 30));
  REQUIRE(gdiOps.size() == 6);
  REQUIRE(gdiOps[0][1] == 8);

  HBRUSH blue = CreateSolidBrush(RGB(0, 0, 170));
  RECT rect{1, 2, 11, 12};
  gdiOps.clear();
  REQUIRE(FillRect(dc, &rect, blue) == 1);
  REQUIRE(gdiOps[0] == std::array<int32_t, 6>{1, 1, 2, 11, 12, 1});
  REQUIRE(FillRect(dc, &rect, UNDOS_PALETTE_BRUSH(14)) == 1);
  REQUIRE(gdiOps.back()[5] == 14);
  REQUIRE(FillRect(dc, &rect, (HBRUSH)GetStockObject(NULL_BRUSH)) == 1);
  REQUIRE(gdiOps.size() == 2);

  REQUIRE(SelectObject(dc, blue) == GetStockObject(WHITE_BRUSH));
  REQUIRE(PatBlt(dc, 10, 10, -4, 4, PATCOPY));
  REQUIRE(gdiOps.back() == std::array<int32_t, 6>{1, 6, 10, 10, 14, 1});
  REQUIRE(PatBlt(dc, 0, 0, 2, 2, WHITENESS));
  REQUIRE(gdiOps.back()[5] == 15);
  SetLastError(0);
  REQUIRE_FALSE(PatBlt(dc, 0, 0, 2, 2, DSTINVERT));
  REQUIRE(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);

  REQUIRE_FALSE(DeleteObject(blue));
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);
  REQUIRE(SelectObject(dc, GetStockObject(WHITE_BRUSH)) == blue);
  REQUIRE(DeleteObject(blue));
  REQUIRE_FALSE(DeleteObject(blue));
  REQUIRE(GetLastError() == ERROR_INVALID_HANDLE);
  REQUIRE(SelectObject(dc, blue) == nullptr);
  REQUIRE(GetLastError() == ERROR_INVALID_HANDLE);
  HBRUSH reused = CreateSolidBrush(RGB(1, 2, 3));
  REQUIRE(reused != blue);
  REQUIRE(DeleteObject(GetStockObject(BLACK_BRUSH)));
  REQUIRE(GetStockObject(SYSTEM_FONT) != nullptr);
  REQUIRE(SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT)) ==
          GetStockObject(SYSTEM_FONT));
  REQUIRE(GetStockObject(15) == nullptr);
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);
  REQUIRE(CreatePen(99, 1, 0) == nullptr);

  SelectObject(dc, GetStockObject(BLACK_PEN));
  REQUIRE(DeleteObject(red));
  REQUIRE(DeleteObject(wide));
  std::vector<HBRUSH> brushes;
  for (;;) {
    HBRUSH brush = CreateSolidBrush(0);
    if (!brush) break;
    brushes.push_back(brush);
  }
  REQUIRE(GetLastError() == ERROR_NOT_ENOUGH_MEMORY);
  REQUIRE(brushes.size() == 63);
  for (HBRUSH brush : brushes) REQUIRE(DeleteObject(brush));
  REQUIRE(EndPaint(window, &paint));
}

std::vector<uint8_t> four_bit_dib() {
  BITMAPINFOHEADER header{};
  header.biSize = sizeof(header);
  header.biWidth = 2;
  header.biHeight = 2;
  header.biPlanes = 1;
  header.biBitCount = 4;
  header.biCompression = BI_RGB;
  std::vector<uint8_t> data(sizeof(header) + 16 * 4 + 2 * 4);
  std::memcpy(data.data(), &header, sizeof(header));
  RGBQUAD *palette = reinterpret_cast<RGBQUAD *>(data.data() + sizeof(header));
  palette[1] = {0, 0, 255, 0};
  palette[2] = {0, 255, 0, 0};
  palette[3] = {255, 0, 0, 0};
  uint8_t *bits = data.data() + sizeof(header) + 16 * 4;
  bits[0] = 0x12;  // bottom row: red, green
  bits[4] = 0x30;  // top row: blue, black
  return data;
}

TEST_CASE("Win32 bitmaps load from resources and blit", "[sdk-gui][bitmap]") {
  resetBackend();
  std::vector<uint8_t> dib = four_bit_dib();
  std::vector<uint8_t> file(14, 0);
  file[0] = 'B';
  file[1] = 'M';
  file.insert(file.end(), dib.begin(), dib.end());
  static const char hello[] = "Hello";
  static const ACCEL accel[] = {{FVIRTKEY, VK_F7, 101}, {FVIRTKEY, VK_F8, 102}};
  const UNDOS_RESOURCE resources[] = {
      {RT_BITMAP, MAKEINTRESOURCE(1), dib.data(), static_cast<DWORD>(dib.size())},
      {RT_BITMAP, "TOOLBAR", file.data(), static_cast<DWORD>(file.size())},
      {RT_STRING, MAKEINTRESOURCE(7), hello, sizeof(hello)},
      {RT_ACCELERATOR, "WFACCEL", accel, sizeof(accel)},
  };
  REQUIRE(LoadBitmap(nullptr, MAKEINTRESOURCE(1)) == nullptr);
  REQUIRE(GetLastError() == ERROR_RESOURCE_TYPE_NOT_FOUND);
  REQUIRE(UndosRegisterResources(resources, 4));

  HBITMAP strip = LoadBitmap(nullptr, MAKEINTRESOURCE(1));
  REQUIRE(strip != nullptr);
  BITMAP info{};
  REQUIRE(GetObject(strip, sizeof(info), &info) == sizeof(BITMAP));
  REQUIRE(info.bmWidth == 2);
  REQUIRE(info.bmHeight == 2);
  REQUIRE(fakeBitmaps.size() == 1);
  const std::vector<uint8_t> expected{255, 0, 0, 255, 0, 0, 0, 255,
                                      0, 0, 255, 255, 0, 255, 0, 255};
  REQUIRE(fakeBitmaps.begin()->second == expected);
  HBITMAP named = LoadBitmap(nullptr, "toolbar");
  REQUIRE(named != nullptr);
  HBITMAP byNumber = LoadBitmap(nullptr, "#1");
  REQUIRE(byNumber != nullptr);
  REQUIRE(DeleteObject(byNumber));
  REQUIRE(LoadBitmap(nullptr, "missing") == nullptr);
  REQUIRE(GetLastError() == ERROR_RESOURCE_NAME_NOT_FOUND);

  char text[4];
  REQUIRE(LoadString(nullptr, 7, text, sizeof(text)) == 3);
  REQUIRE(std::string(text) == "Hel");
  REQUIRE(LoadString(nullptr, 8, text, sizeof(text)) == 0);
  HACCEL table = LoadAccelerators(nullptr, "WFACCEL");
  REQUIRE(table != nullptr);
  REQUIRE(DestroyAcceleratorTable(table));

  HWND window = make_window("bitmap-blit");
  PAINTSTRUCT paint{};
  HDC dc = BeginPaint(window, &paint);
  REQUIRE(dc != nullptr);
  HDC memory = CreateCompatibleDC(dc);
  REQUIRE(memory != nullptr);
  REQUIRE(SelectObject(dc, strip) == nullptr);
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);
  HGDIOBJ initial = SelectObject(memory, strip);
  REQUIRE(initial != nullptr);
  HDC other = CreateCompatibleDC(dc);
  REQUIRE(SelectObject(other, strip) == nullptr);
  REQUIRE(DeleteDC(other));
  REQUIRE(memoryDcBitmaps[static_cast<uint32_t>(reinterpret_cast<uintptr_t>(memory))] != 0);
  REQUIRE(BitBlt(dc, 5, 6, 2, 2, memory, 0, 0, SRCCOPY));
  REQUIRE(blits.size() == 1);
  REQUIRE(blits[0][1] == 5);
  REQUIRE(blits[0][2] == 6);
  REQUIRE(blits[0][5] == static_cast<int32_t>(reinterpret_cast<uintptr_t>(memory)));
  REQUIRE_FALSE(BitBlt(dc, 0, 0, 2, 2, memory, 0, 0, 0x008800C6u));
  REQUIRE(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);
  REQUIRE_FALSE(BitBlt(dc, 0, 0, 2, 2, nullptr, 0, 0, SRCCOPY));
  REQUIRE(GetLastError() == ERROR_INVALID_HANDLE);

  REQUIRE_FALSE(DeleteObject(strip));
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);
  REQUIRE(SelectObject(memory, initial) == strip);
  REQUIRE_FALSE(DeleteObject(initial));
  REQUIRE(DeleteObject(strip));
  REQUIRE(GetObject(strip, sizeof(info), &info) == 0);
  REQUIRE(GetLastError() == ERROR_INVALID_HANDLE);

  HBITMAP canvas = CreateCompatibleBitmap(dc, 2, 2);
  REQUIRE(canvas != nullptr);
  const uint32_t canvasId = nextBitmap - 1;
  dib[sizeof(BITMAPINFOHEADER) + 16 * 4 + 4] = 0x11;
  const BITMAPINFO *bmi = reinterpret_cast<const BITMAPINFO *>(dib.data());
  const uint8_t *bits = dib.data() + sizeof(BITMAPINFOHEADER) + 16 * 4;
  REQUIRE(SetDIBits(dc, canvas, 1, 1, bits + 4, bmi, DIB_RGB_COLORS) == 1);
  const auto &pixels = fakeBitmaps.at(canvasId);
  REQUIRE(pixels[2] == 255);
  REQUIRE(pixels[6] == 255);
  REQUIRE(pixels[8] == 0);
  REQUIRE(SetDIBits(dc, canvas, 0, 2, bits, bmi, 99) == 0);
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);
  REQUIRE(CreateBitmap(4, 4, 1, 8, nullptr) == nullptr);
  REQUIRE(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);
  const uint8_t mono[4] = {0x80, 0, 0x40, 0};
  HBITMAP check = CreateBitmap(2, 2, 1, 1, mono);
  REQUIRE(check != nullptr);
  REQUIRE(fakeBitmaps.at(nextBitmap - 1)[0] == 255);
  REQUIRE(fakeBitmaps.at(nextBitmap - 1)[12] == 255);
  REQUIRE(fakeBitmaps.at(nextBitmap - 1)[4] == 0);

  REQUIRE(DeleteObject(canvas));
  REQUIRE(DeleteObject(check));
  REQUIRE(DeleteObject(named));
  REQUIRE(DeleteDC(memory));
  REQUIRE_FALSE(DeleteDC(memory));
  REQUIRE(GetLastError() == ERROR_INVALID_HANDLE);
  REQUIRE_FALSE(DeleteDC(dc));
  REQUIRE(EndPaint(window, &paint));
}

namespace {
std::vector<UINT> drawnItems;
LRESULT CALLBACK owner_proc(HWND window, UINT message, WPARAM wParam,
                            LPARAM lParam) {
  if (message == WM_MEASUREITEM) {
    reinterpret_cast<MEASUREITEMSTRUCT *>(lParam)->itemHeight = 20;
    return TRUE;
  }
  if (message == WM_DRAWITEM) {
    drawnItems.push_back(reinterpret_cast<DRAWITEMSTRUCT *>(lParam)->itemID);
    return TRUE;
  }
  return recording_proc(window, message, wParam, lParam);
}

HWND make_child(const char *className, const char *text, DWORD style,
                HWND parent, UINT id) {
  return CreateWindow(className, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 80,
                      20, parent, reinterpret_cast<HMENU>(uintptr_t{id}),
                      nullptr, nullptr);
}

bool sent_command(UINT id, UINT code) {
  for (const auto &m : sentMessages)
    if (m[0] == WM_COMMAND && m[1] == MAKEWPARAM(id, code)) return true;
  return false;
}
} // namespace

TEST_CASE("standard controls keep state and notify their parent",
          "[sdk-gui][controls]") {
  resetBackend();
  backendClientRect = {0, 0, 80, 100};
  HWND parent = make_recording_window("control-parent");

  HWND check = make_child("button", "&Hidden", BS_AUTOCHECKBOX | WS_TABSTOP,
                          parent, 10);
  REQUIRE(check);
  REQUIRE(GetDlgCtrlID(check) == 10);
  REQUIRE(GetParent(check) == parent);
  REQUIRE((GetWindowLong(check, GWL_STYLE) & BS_TYPEMASK) == BS_AUTOCHECKBOX);
  REQUIRE(SendMessage(check, BM_CLICK, 0, 0) == 0);
  REQUIRE(SendMessage(check, BM_GETCHECK, 0, 0) == BST_CHECKED);
  REQUIRE(sent_command(10, BN_CLICKED));
  char text[32];
  REQUIRE(GetWindowText(check, text, sizeof(text)) == 7);
  REQUIRE(std::string(text) == "&Hidden");
  REQUIRE(EnableWindow(check, FALSE) == FALSE);
  REQUIRE_FALSE(IsWindowEnabled(check));
  REQUIRE((GetWindowLong(check, GWL_STYLE) & WS_DISABLED) != 0);
  REQUIRE(EnableWindow(check, TRUE) == TRUE);

  HWND first = make_child("BUTTON", "A", BS_AUTORADIOBUTTON | WS_GROUP,
                          parent, 20);
  HWND second = make_child("Button", "B", BS_AUTORADIOBUTTON, parent, 21);
  HWND after = make_child("Button", "C", BS_AUTORADIOBUTTON | WS_GROUP,
                          parent, 22);
  SendMessage(first, BM_SETCHECK, BST_CHECKED, 0);
  SendMessage(after, BM_SETCHECK, BST_CHECKED, 0);
  SendMessage(second, WM_LBUTTONDOWN, 0, 0);
  REQUIRE(GetFocus() == second);
  SendMessage(second, WM_LBUTTONUP, 0, 0);
  REQUIRE(SendMessage(second, BM_GETCHECK, 0, 0) == BST_CHECKED);
  REQUIRE(SendMessage(first, BM_GETCHECK, 0, 0) == BST_UNCHECKED);
  REQUIRE(SendMessage(after, BM_GETCHECK, 0, 0) == BST_CHECKED);
  REQUIRE(GetWindow(first, GW_HWNDNEXT) == second);
  REQUIRE(GetWindow(parent, GW_CHILD) == check);

  HWND edit = make_child("edit", "", WS_BORDER | ES_UPPERCASE, parent, 30);
  sentMessages.clear();
  SendMessage(edit, WM_CHAR, 'a', 0);
  SendMessage(edit, WM_CHAR, 'b', 0);
  SendMessage(edit, WM_CHAR, VK_BACK, 0);
  SendMessage(edit, WM_CHAR, 'c', 0);
  REQUIRE(GetWindowTextLength(edit) == 2);
  REQUIRE(GetWindowText(edit, text, sizeof(text)) == 2);
  REQUIRE(std::string(text) == "AC");
  REQUIRE(sent_command(30, EN_CHANGE));
  REQUIRE(SendMessage(edit, EM_GETMODIFY, 0, 0));
  SendMessage(edit, EM_SETSEL, 0, 1);
  SendMessage(edit, EM_REPLACESEL, 0, reinterpret_cast<LPARAM>("xy"));
  GetWindowText(edit, text, sizeof(text));
  REQUIRE(std::string(text) == "XYC");
  DWORD start = 0, end = 0;
  SendMessage(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&start),
              reinterpret_cast<LPARAM>(&end));
  REQUIRE(start == 2);
  REQUIRE(end == 2);
  SendMessage(edit, EM_LIMITTEXT, 3, 0);
  sentMessages.clear();
  SendMessage(edit, WM_CHAR, 'z', 0);
  REQUIRE(sent_command(30, EN_MAXTEXT));
  REQUIRE(SetWindowText(edit, "abcdef"));
  REQUIRE(GetWindowTextLength(edit) == 3);
  SendMessage(edit, WM_PAINT, 0, 0);

  HWND list = make_child("listbox", "", LBS_SORT | LBS_NOTIFY | WS_VSCROLL,
                         parent, 40);
  REQUIRE(SendMessage(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>("pear")) == 0);
  REQUIRE(SendMessage(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>("Apple")) == 0);
  REQUIRE(SendMessage(list, LB_INSERTSTRING, 1, reinterpret_cast<LPARAM>("fig")) == 1);
  REQUIRE(SendMessage(list, LB_GETCOUNT, 0, 0) == 3);
  REQUIRE(SendMessage(list, LB_GETTEXT, 2, reinterpret_cast<LPARAM>(text)) == 4);
  REQUIRE(std::string(text) == "pear");
  REQUIRE(SendMessage(list, LB_GETCURSEL, 0, 0) == LB_ERR);
  REQUIRE(SendMessage(list, LB_FINDSTRING, static_cast<WPARAM>(-1),
                      reinterpret_cast<LPARAM>("AP")) == 0);
  REQUIRE(SendMessage(list, LB_SETCURSEL, 0, 0) == 0);
  sentMessages.clear();
  SendMessage(list, WM_KEYDOWN, VK_DOWN, 0);
  REQUIRE(SendMessage(list, LB_GETCURSEL, 0, 0) == 1);
  REQUIRE(sent_command(40, LBN_SELCHANGE));
  SendMessage(list, WM_KEYDOWN, VK_END, 0);
  REQUIRE(SendMessage(list, LB_GETCURSEL, 0, 0) == 2);
  SendMessage(list, WM_CHAR, 'f', 0);
  REQUIRE(SendMessage(list, LB_GETCURSEL, 0, 0) == 1);
  REQUIRE(SendMessage(list, LB_SETITEMDATA, 1, 77) == LB_OKAY);
  REQUIRE(SendMessage(list, LB_GETITEMDATA, 1, 0) == 77);
  REQUIRE(SendMessage(list, LB_DELETESTRING, 0, 0) == 2);
  REQUIRE(SendMessage(list, LB_GETCURSEL, 0, 0) == 0);
  REQUIRE(SendMessage(list, LB_GETTEXT, 9, reinterpret_cast<LPARAM>(text)) == LB_ERR);
  SendMessage(list, WM_PAINT, 0, 0);

  HWND multi = make_child("listbox", "", LBS_EXTENDEDSEL, parent, 41);
  for (const char *item : {"a", "b", "c", "d"})
    SendMessage(multi, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
  REQUIRE(SendMessage(multi, LB_SETCURSEL, 0, 0) == LB_ERR);
  REQUIRE(SendMessage(multi, LB_SETSEL, TRUE, 1) == LB_OKAY);
  REQUIRE(SendMessage(multi, LB_SETSEL, TRUE, 3) == LB_OKAY);
  int selection[4] = {};
  REQUIRE(SendMessage(multi, LB_GETSELCOUNT, 0, 0) == 2);
  REQUIRE(SendMessage(multi, LB_GETSELITEMS, 4,
                      reinterpret_cast<LPARAM>(selection)) == 2);
  REQUIRE(selection[0] == 1);
  REQUIRE(selection[1] == 3);

  HWND scroll = make_child("scrollbar", "", SBS_VERT, parent, 50);
  REQUIRE(SetScrollRange(scroll, SB_CTL, 0, 10, FALSE));
  REQUIRE(SetScrollPos(scroll, SB_CTL, 40, TRUE) == 0);
  REQUIRE(GetScrollPos(scroll, SB_CTL) == 10);
  SCROLLINFO info{sizeof(info), SIF_PAGE | SIF_POS, 0, 0, 5, 9, 0};
  REQUIRE(SetScrollInfo(scroll, SB_CTL, &info, TRUE) == 6);
  sentMessages.clear();
  SendMessage(scroll, WM_LBUTTONDOWN, 0, MAKELPARAM(5, 2));
  REQUIRE(sentMessages.size() == 2);
  REQUIRE(sentMessages[0][0] == WM_VSCROLL);
  REQUIRE(sentMessages[0][1] == SB_LINEUP);
  REQUIRE(sentMessages[0][2] == reinterpret_cast<uintptr_t>(scroll));
  REQUIRE(SetScrollPos(parent, SB_VERT, 30, FALSE) == 0);
  REQUIRE(GetScrollPos(parent, SB_VERT) == 30);
  REQUIRE(SetScrollPos(check, SB_CTL, 1, FALSE) == 0);
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);

  REQUIRE(DestroyWindow(parent));
  REQUIRE_FALSE(IsWindow(list));
  REQUIRE(GetFocus() == nullptr);
}

TEST_CASE("owner-draw list boxes measure and draw through the parent",
          "[sdk-gui][controls]") {
  resetBackend();
  backendClientRect = {0, 0, 80, 50};
  WNDCLASS klass = test_class("owner-parent");
  klass.lpfnWndProc = owner_proc;
  REQUIRE(RegisterClass(&klass));
  HWND parent = CreateWindow("owner-parent", "", 0, 0, 0, 200, 100, nullptr,
                             nullptr, nullptr, nullptr);
  sentMessages.clear();
  drawnItems.clear();
  HWND list = make_child("listbox", "", LBS_OWNERDRAWFIXED, parent, 60);
  REQUIRE(SendMessage(list, LB_GETITEMHEIGHT, 0, 0) == 20);
  for (LPARAM data : {101, 102, 103, 104})
    REQUIRE(SendMessage(list, LB_ADDSTRING, 0, data) >= 0);
  REQUIRE(SendMessage(list, LB_GETITEMDATA, 2, 0) == 103);
  REQUIRE(SendMessage(list, LB_FINDSTRING, static_cast<WPARAM>(-1), 104) == 3);
  RECT item{};
  REQUIRE(SendMessage(list, LB_GETITEMRECT, 1, reinterpret_cast<LPARAM>(&item)) == LB_OKAY);
  REQUIRE(item.top == 20);
  REQUIRE(item.bottom == 40);
  SendMessage(list, WM_PAINT, 0, 0);
  REQUIRE(drawnItems == std::vector<UINT>{0, 1, 2});
  SendMessage(list, WM_LBUTTONDOWN, 0, MAKELPARAM(4, 45));
  REQUIRE(SendMessage(list, LB_GETCURSEL, 0, 0) == 2);
  REQUIRE(DestroyWindow(parent));
}

namespace {
struct TemplateBuilder {
  std::vector<WORD> words;
  void dword(DWORD v) {
    words.push_back(static_cast<WORD>(v & 0xFFFF));
    words.push_back(static_cast<WORD>(v >> 16));
  }
  void text(const char *s) {
    for (; *s; ++s) words.push_back(static_cast<WORD>(*s));
    words.push_back(0);
  }
  void align() {
    if (words.size() % 2) words.push_back(0);
  }
  void item(DWORD style, short x, short y, short cx, short cy, WORD id,
            WORD atom, const char *title) {
    align();
    dword(style | WS_CHILD | WS_VISIBLE);
    dword(0);
    for (short v : {x, y, cx, cy}) words.push_back(static_cast<WORD>(v));
    words.push_back(id);
    words.push_back(0xFFFF);
    words.push_back(atom);
    text(title);
    words.push_back(0);
  }
};

std::vector<WORD> test_template() {
  TemplateBuilder b;
  b.dword(WS_CAPTION | DS_MODALFRAME | DS_SETFONT);
  b.dword(0);
  b.words.push_back(3);
  for (short v : {10, 10, 160, 60}) b.words.push_back(static_cast<WORD>(v));
  b.words.push_back(0);
  b.words.push_back(0);
  b.text("Rename");
  b.words.push_back(8);
  b.text("Helv");
  b.item(SS_LEFT, 4, 4, 30, 10, 99, 0x0082, "&To:");
  b.item(WS_TABSTOP | WS_BORDER, 40, 4, 100, 12, 100, 0x0081, "old.txt");
  b.item(BS_DEFPUSHBUTTON | WS_TABSTOP, 40, 30, 40, 14, IDOK, 0x0080, "OK");
  return b.words;
}

std::vector<std::pair<UINT, WPARAM>> dialogMessages;
INT_PTR CALLBACK modal_proc(HWND dialog, UINT message, WPARAM wParam,
                            LPARAM lParam) {
  dialogMessages.push_back({message, wParam});
  if (message == WM_INITDIALOG) {
    char text[32];
    REQUIRE(lParam == 1234);
    REQUIRE(GetDlgItemText(dialog, 100, text, sizeof(text)) == 7);
    REQUIRE(std::string(text) == "old.txt");
    REQUIRE(SetDlgItemInt(dialog, 100, static_cast<UINT>(-42), TRUE));
    BOOL ok = FALSE;
    REQUIRE(static_cast<int>(GetDlgItemInt(dialog, 100, &ok, TRUE)) == -42);
    REQUIRE(ok);
    GetDlgItemInt(dialog, 99, &ok, FALSE);
    REQUIRE_FALSE(ok);
    REQUIRE(GetDlgItem(dialog, 555) == nullptr);
    REQUIRE(GetLastError() == ERROR_CONTROL_ID_NOT_FOUND);
    SetWindowLongPtr(dialog, DWLP_USER, 5);
    REQUIRE(GetWindowLongPtr(dialog, DWLP_USER) == 5);
    REQUIRE(EndDialog(dialog, 7));
    return TRUE;
  }
  return FALSE;
}

INT_PTR CALLBACK modeless_proc(HWND dialog, UINT message, WPARAM wParam,
                               LPARAM) {
  dialogMessages.push_back({message, wParam});
  if (message == WM_INITDIALOG) return TRUE;
  if (message == WM_COMMAND && LOWORD(wParam) == IDCANCEL) {
    SetWindowLongPtr(dialog, DWLP_MSGRESULT, 0);
    return TRUE;
  }
  return FALSE;
}
} // namespace

TEST_CASE("dialog templates run modally and modelessly",
          "[sdk-gui][dialog]") {
  resetBackend();
  backendClientRect = {0, 0, 320, 120};
  backendWindowRect = {0, 0, 322, 142};
  std::vector<WORD> words = test_template();
  auto *dialogTemplate = reinterpret_cast<LPCDLGTEMPLATE>(words.data());

  dialogMessages.clear();
  REQUIRE(DialogBoxIndirectParam(nullptr, dialogTemplate, nullptr, modal_proc,
                                 1234) == 7);
  REQUIRE(dialogMessages.front().first == WM_INITDIALOG);

  UNDOS_RESOURCE resources[] = {
      {RT_DIALOG, "RENAME", words.data(),
       static_cast<DWORD>(words.size() * sizeof(WORD))}};
  REQUIRE(UndosRegisterResources(resources, 1));
  dialogMessages.clear();
  REQUIRE(DialogBoxParam(nullptr, "rename", nullptr, modal_proc, 1234) == 7);
  REQUIRE(DialogBoxParam(nullptr, "missing", nullptr, modal_proc, 0) == -1);
  REQUIRE(GetLastError() == ERROR_RESOURCE_NAME_NOT_FOUND);

  dialogMessages.clear();
  HWND dialog = CreateDialogIndirectParam(nullptr, dialogTemplate, nullptr,
                                          modeless_proc, 0);
  REQUIRE(dialog);
  HWND edit = GetDlgItem(dialog, 100);
  HWND ok = GetDlgItem(dialog, IDOK);
  REQUIRE(GetFocus() == edit);
  REQUIRE(LOWORD(SendMessage(dialog, DM_GETDEFID, 0, 0)) == IDOK);
  REQUIRE(GetNextDlgTabItem(dialog, edit, FALSE) == ok);
  REQUIRE(GetNextDlgTabItem(dialog, ok, FALSE) == edit);
  REQUIRE(GetNextDlgTabItem(dialog, edit, TRUE) == ok);
  RECT units{0, 0, 4, 8};
  REQUIRE(MapDialogRect(dialog, &units));
  REQUIRE(units.right == LOWORD(GetDialogBaseUnits()));
  REQUIRE(units.bottom == HIWORD(GetDialogBaseUnits()));

  MSG message{};
  message.hwnd = dialog;
  message.message = WM_KEYDOWN;
  message.wParam = VK_TAB;
  REQUIRE(IsDialogMessage(dialog, &message));
  REQUIRE(GetFocus() == ok);
  message.wParam = VK_RETURN;
  dialogMessages.clear();
  REQUIRE(IsDialogMessage(dialog, &message));
  REQUIRE(dialogMessages.size() == 1);
  REQUIRE(dialogMessages[0].first == WM_COMMAND);
  REQUIRE(dialogMessages[0].second == MAKEWPARAM(IDOK, BN_CLICKED));

  SetFocus(edit);
  message.message = WM_CHAR;
  message.wParam = 'x';
  REQUIRE(IsDialogMessage(dialog, &message));
  char text[32];
  GetDlgItemText(dialog, 100, text, sizeof(text));
  REQUIRE(std::string(text) == "x");
  message.wParam = '\r';
  REQUIRE(IsDialogMessage(dialog, &message));
  GetDlgItemText(dialog, 100, text, sizeof(text));
  REQUIRE(std::string(text) == "x");

  dialogMessages.clear();
  SendMessage(dialog, WM_CLOSE, 0, 0);
  REQUIRE(dialogMessages.back().second == MAKEWPARAM(IDCANCEL, BN_CLICKED));
  REQUIRE(IsWindow(dialog));

  HWND outside = make_recording_window("outside-dialog");
  message.hwnd = outside;
  REQUIRE_FALSE(IsDialogMessage(dialog, &message));

  REQUIRE(DestroyWindow(dialog));
  REQUIRE_FALSE(IsWindow(edit));
  REQUIRE_FALSE(EndDialog(dialog, 0));
  REQUIRE(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
  REQUIRE(CreateDialogIndirectParam(nullptr, nullptr, nullptr, nullptr, 0) ==
          nullptr);
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);
}

extern "C" const UNDOS_RESOURCE winfile_resources[];
extern "C" const UINT winfile_resources_count;

TEST_CASE("rc2c output for WINFILE res.rc loads through the SDK",
          "[sdk-gui][resource][winfile]") {
  resetBackend();
  backendClientRect = {0, 0, 320, 120};
  backendWindowRect = {0, 0, 322, 142};
  REQUIRE(UndosRegisterResources(winfile_resources, winfile_resources_count));

  char text[128];
  REQUIRE(LoadString(nullptr, 50 /* IDS_COPYDISK */, text, sizeof(text)) > 0);
  REQUIRE(std::string(text) == "Copy Disk");
  HACCEL accel = LoadAccelerators(nullptr, MAKEINTRESOURCE(400 /* WFACCELTABLE */));
  REQUIRE(accel);
  REQUIRE(DestroyAcceleratorTable(accel));
  HBITMAP strip = LoadBitmap(nullptr, MAKEINTRESOURCE(100 /* BITMAPS */));
  REQUIRE(strip);
  BITMAP info{};
  REQUIRE(GetObject(strip, sizeof(info), &info) == sizeof(info));
  REQUIRE(info.bmWidth == 288);
  REQUIRE(info.bmHeight == 55);
  REQUIRE(DeleteObject(strip));

  HMENU frameMenu = LoadMenu(nullptr, MAKEINTRESOURCE(500 /* FRAMEMENU */));
  REQUIRE(frameMenu);
  REQUIRE(GetMenuItemCount(frameMenu) >= 6);
  HMENU fileMenu = GetSubMenu(frameMenu, 0);
  REQUIRE(fileMenu);
  // The UnDOS File menu drops the NT-era commands (Edit, links, compress...).
  REQUIRE(GetMenuItemCount(fileMenu) == 16);
  bool sawSeparator = false;
  for (auto &entry : fakeMenus[(uint32_t)(uintptr_t)fileMenu])
    if (entry.first & UI_MENU_SEPARATOR) sawSeparator = true;
  REQUIRE(sawSeparator);
  REQUIRE(LoadMenu(nullptr, "CTXMENU"));
  REQUIRE(LoadMenu(nullptr, "NOSUCHMENU") == nullptr);
  REQUIRE(InsertMenu(fileMenu, (UINT)-1, MF_BYPOSITION, 9999, "End"));
  REQUIRE_FALSE(InsertMenu(fileMenu, 0, MF_BYPOSITION, 9998, "Front"));
  REQUIRE(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);
  const WORD bad[] = {1, 0};
  REQUIRE(LoadMenuIndirect(bad) == nullptr);
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);

  UINT dialogs = 0, created = 0;
  std::vector<std::string> failures;
  for (UINT i = 0; i < winfile_resources_count; ++i) {
    const UNDOS_RESOURCE &entry = winfile_resources[i];
    if (entry.type != RT_DIALOG) continue;
    ++dialogs;
    HWND dialog = CreateDialogIndirectParam(
        nullptr, static_cast<LPCDLGTEMPLATE>(entry.data), nullptr,
        modeless_proc, 0);
    if (dialog) {
      ++created;
      REQUIRE(DestroyWindow(dialog));
    } else {
      const DWORD error = GetLastError();
      failures.push_back(std::to_string(reinterpret_cast<uintptr_t>(entry.name)) +
                         ":" + std::to_string(error));
      // Known SDK gaps (Step 3): missing classes (combo box, WINFILE's
      // DialogResize* classes registered by Resize.c), unsupported control
      // styles, and PREFDLG's zero-size control. Anything else would point
      // at a malformed template.
      CHECK((error == ERROR_CLASS_DOES_NOT_EXIST || error == ERROR_CALL_NOT_IMPLEMENTED ||
             error == ERROR_INVALID_PARAMETER));
    }
  }
  INFO("failed dialogs (id:error): " << [&] {
    std::string all;
    for (const auto &f : failures) all += f + " ";
    return all;
  }());
  REQUIRE(dialogs == 41);
  REQUIRE(created >= 24);
}

namespace {
std::vector<std::pair<HWND, UINT_PTR>> timerCalls;
void CALLBACK timer_callback(HWND window, UINT message, UINT_PTR id, DWORD) {
  REQUIRE(message == WM_TIMER);
  timerCalls.push_back({window, id});
}
} // namespace

TEST_CASE("timers, capture, cursors and drag detection follow Win32",
          "[sdk-gui][input]") {
  resetBackend();
  HWND window = make_recording_window("input-window");

  REQUIRE(SetTimer(window, 7, 100, nullptr) == 7);
  REQUIRE(SetTimer(reinterpret_cast<HWND>(uintptr_t{0x999}), 1, 10, nullptr) == 0);
  REQUIRE(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
  MSG message{};
  REQUIRE_FALSE(PeekMessage(&message, nullptr, 0, 0, PM_REMOVE));
  fakeTicks = 100;
  REQUIRE(PeekMessage(&message, nullptr, 0, 0, PM_NOREMOVE));
  REQUIRE(message.message == WM_TIMER);
  REQUIRE(GetMessage(&message, nullptr, 0, 0) == 1);
  REQUIRE(message.message == WM_TIMER);
  REQUIRE(message.hwnd == window);
  REQUIRE(message.wParam == 7);
  sentMessages.clear();
  DispatchMessage(&message);
  REQUIRE(sentMessages.size() == 1);
  REQUIRE(sentMessages[0][0] == WM_TIMER);
  // Posted messages come before timers.
  PostMessage(window, WM_USER, 0, 0);
  fakeTicks = 400;
  REQUIRE(GetMessage(&message, nullptr, 0, 0) == 1);
  REQUIRE(message.message == WM_USER);
  // GetMessage waits for the next expiry when the queue is empty.
  REQUIRE(GetMessage(&message, nullptr, 0, 0) == 1);
  REQUIRE(message.message == WM_TIMER);
  REQUIRE(GetMessage(&message, nullptr, 0, 0) == 1);
  REQUIRE(message.message == WM_TIMER);
  REQUIRE(fakeTicks >= 500);
  // The empty-queue wait sleeps in the kernel until the next expiry.
  REQUIRE(waitMessageCalls > 0);
  REQUIRE(lastWaitTimeout <= 100);
  // WaitMessage returns at once for a queued message and leaves it queued.
  PostMessage(window, WM_USER + 1, 0, 0);
  waitMessageCalls = 0;
  REQUIRE(WaitMessage());
  REQUIRE(queue.size() == 1);
  REQUIRE(GetMessage(&message, nullptr, 0, 0) == 1);
  REQUIRE(message.message == WM_USER + 1);
  // With a timer running it waits no longer than the next expiry.
  const uint32_t before = fakeTicks;
  REQUIRE(WaitMessage());
  REQUIRE(lastWaitTimeout <= 100);
  REQUIRE(fakeTicks - before <= 100);
  REQUIRE(KillTimer(window, 7));
  // Without timers it waits forever; a pending quit returns at once.
  REQUIRE(WaitMessage());
  REQUIRE(lastWaitTimeout == UINT32_MAX);
  PostQuitMessage(4);
  waitMessageCalls = 0;
  REQUIRE(WaitMessage());
  REQUIRE(waitMessageCalls == 0);
  REQUIRE(GetMessage(&message, nullptr, 0, 0) == 0);
  REQUIRE_FALSE(KillTimer(window, 7));
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);

  const UINT_PTR thread = SetTimer(nullptr, 0, 1, timer_callback);
  REQUIRE(thread != 0);
  fakeTicks += 10;
  timerCalls.clear();
  REQUIRE(GetMessage(&message, nullptr, 0, 0) == 1);
  REQUIRE(message.hwnd == nullptr);
  DispatchMessage(&message);
  REQUIRE(timerCalls.size() == 1);
  REQUIRE(timerCalls[0].second == thread);
  REQUIRE(KillTimer(nullptr, thread));
  PostQuitMessage(3);
  REQUIRE(GetMessage(&message, nullptr, 0, 0) == 0);

  HWND other = make_recording_window("input-other");
  REQUIRE(SetCapture(window) == nullptr);
  REQUIRE(GetCapture() == window);
  REQUIRE(fakeCapture == reinterpret_cast<uintptr_t>(window));
  REQUIRE(SetCapture(other) == window);
  REQUIRE(PeekMessage(&message, nullptr, 0, 0, PM_REMOVE));
  REQUIRE(message.message == WM_CAPTURECHANGED);
  REQUIRE(message.hwnd == window);
  REQUIRE(message.lParam == static_cast<LPARAM>(reinterpret_cast<uintptr_t>(other)));
  REQUIRE(ReleaseCapture());
  queue.clear();
  REQUIRE(GetCapture() == nullptr);
  REQUIRE(fakeCapture == 0);
  SetCapture(other);
  REQUIRE(DestroyWindow(other));
  REQUIRE(GetCapture() == nullptr);
  queue.clear();

  HCURSOR arrow = LoadCursor(nullptr, IDC_ARROW);
  REQUIRE(arrow);
  REQUIRE(LoadCursor(nullptr, IDC_WAIT) != arrow);
  REQUIRE(LoadCursor(nullptr, MAKEINTRESOURCE(32647)) == nullptr);
  REQUIRE(SetCursor(arrow) == nullptr);
  REQUIRE(SetCursor(nullptr) == arrow);
  REQUIRE(GetCursor() == nullptr);
  REQUIRE(ShowCursor(FALSE) == -1);
  REQUIRE(ShowCursor(TRUE) == 0);
  REQUIRE(GetSystemMetrics(SM_CXDRAG) > 0);

  POINT origin{10, 10};
  queue.push_back({static_cast<uint32_t>(reinterpret_cast<uintptr_t>(window)),
                   WM_MOUSEMOVE, MK_LBUTTON,
                   static_cast<uint32_t>(MAKELPARAM(12, 11)), 0});
  queue.push_back({static_cast<uint32_t>(reinterpret_cast<uintptr_t>(window)),
                   WM_MOUSEMOVE, MK_LBUTTON,
                   static_cast<uint32_t>(MAKELPARAM(30, 10)), 0});
  REQUIRE(DragDetect(window, origin));
  REQUIRE(GetCapture() == nullptr);
  queue.push_back({static_cast<uint32_t>(reinterpret_cast<uintptr_t>(window)),
                   WM_LBUTTONUP, 0, static_cast<uint32_t>(MAKELPARAM(11, 10)), 0});
  REQUIRE_FALSE(DragDetect(window, origin));
}

TEST_CASE("menu items can be removed and deleted", "[sdk-gui][menu]") {
  resetBackend();
  HMENU bar = CreateMenu();
  HMENU popup = CreatePopupMenu();
  REQUIRE(AppendMenu(popup, MF_STRING, 101, "&Open"));
  REQUIRE(AppendMenu(popup, MF_SEPARATOR, 0, nullptr));
  REQUIRE(AppendMenu(popup, MF_STRING, 102, "E&xit"));
  REQUIRE(AppendMenu(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(popup), "&File"));
  REQUIRE(RemoveMenu(bar, 101, MF_BYCOMMAND) == FALSE);
  REQUIRE(GetLastError() == ERROR_MENU_ITEM_NOT_FOUND);
  REQUIRE(RemoveMenu(popup, 101, MF_BYCOMMAND));
  REQUIRE(GetMenuItemCount(popup) == 2);
  REQUIRE(DeleteMenu(popup, 0, MF_BYPOSITION));
  REQUIRE(GetMenuItemID(popup, 0) == 102);
  REQUIRE_FALSE(RemoveMenu(popup, 5, MF_BYPOSITION));
  REQUIRE_FALSE(RemoveMenu(popup, 0, MF_CHECKED));
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);
  REQUIRE(RemoveMenu(bar, 0, MF_BYPOSITION));
  REQUIRE(GetMenuItemCount(bar) == 0);
}

namespace {
std::vector<std::pair<HWND, UINT>> mdiMessages;
LRESULT CALLBACK mdi_child_proc(HWND window, UINT message, WPARAM wParam,
                                LPARAM lParam) {
  if (message == WM_MDIACTIVATE || message == WM_CLOSE)
    mdiMessages.push_back({window, message});
  return DefMDIChildProc(window, message, wParam, lParam);
}

std::string menu_text_count(HMENU menu, int &count) {
  count = GetMenuItemCount(menu);
  return lastMenuText;
}
} // namespace

TEST_CASE("MDI client creates, activates and arranges children",
          "[sdk-gui][mdi]") {
  resetBackend();
  backendClientRect = {0, 0, 300, 200};
  HWND frame = make_recording_window("mdi-frame");
  HMENU windowMenu = CreatePopupMenu();
  REQUIRE(AppendMenu(windowMenu, MF_STRING, 1, "&Cascade"));
  CLIENTCREATESTRUCT ccs{windowMenu, 500};
  HWND client = CreateWindow("MDICLIENT", nullptr, WS_CHILD | WS_VISIBLE, 0, 0,
                             300, 200, frame, reinterpret_cast<HMENU>(uintptr_t{9}),
                             nullptr, &ccs);
  REQUIRE(client);
  WNDCLASS klass = test_class("mdi-doc");
  klass.lpfnWndProc = mdi_child_proc;
  REQUIRE(RegisterClass(&klass));

  mdiMessages.clear();
  MDICREATESTRUCT create{"mdi-doc", "/", nullptr, CW_USEDEFAULT,
                         CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0};
  HWND first = reinterpret_cast<HWND>(
      SendMessage(client, WM_MDICREATE, 0, reinterpret_cast<LPARAM>(&create)));
  REQUIRE(first);
  REQUIRE(GetParent(first) == client);
  REQUIRE(GetDlgCtrlID(first) == 500);
  HWND second = CreateMDIWindow("mdi-doc", "/SYSTEM", 0, CW_USEDEFAULT,
                                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                client, nullptr, 0);
  HWND third = CreateMDIWindow("mdi-doc", "/MNT/CD", 0, CW_USEDEFAULT,
                               CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                               client, nullptr, 0);
  REQUIRE(third);
  REQUIRE(GetDlgCtrlID(third) == 502);
  BOOL maximized = TRUE;
  REQUIRE(reinterpret_cast<HWND>(SendMessage(
              client, WM_MDIGETACTIVE, 0,
              reinterpret_cast<LPARAM>(&maximized))) == third);
  REQUIRE_FALSE(maximized);
  REQUIRE(GetFocus() == third);
  REQUIRE(std::find(mdiMessages.begin(), mdiMessages.end(),
                    std::make_pair(second, static_cast<UINT>(WM_MDIACTIVATE))) !=
          mdiMessages.end());

  // Window menu: Cascade, separator, then one entry per child.
  int count = 0;
  menu_text_count(windowMenu, count);
  REQUIRE(count == 5);
  REQUIRE(GetMenuItemID(windowMenu, 4) == 502);
  REQUIRE(GetMenuState(windowMenu, 502, MF_BYCOMMAND) & MF_CHECKED);
  REQUIRE_FALSE(GetMenuState(windowMenu, 500, MF_BYCOMMAND) & MF_CHECKED);

  DefFrameProc(frame, client, WM_COMMAND, MAKEWPARAM(500, 0), 0);
  REQUIRE(reinterpret_cast<HWND>(SendMessage(client, WM_MDIGETACTIVE, 0, 0)) ==
          first);
  REQUIRE(GetMenuState(windowMenu, 500, MF_BYCOMMAND) & MF_CHECKED);
  SendMessage(client, WM_MDINEXT, 0, 0);
  REQUIRE(reinterpret_cast<HWND>(SendMessage(client, WM_MDIGETACTIVE, 0, 0)) ==
          second);

  moveRequests.clear();
  REQUIRE(SendMessage(client, WM_MDITILE, MDITILE_VERTICAL, 0));
  REQUIRE(moveRequests.size() == 3);
  REQUIRE(moveRequests[0] == std::array<int32_t, 4>{0, 0, 100, 200});
  REQUIRE(moveRequests[1] == std::array<int32_t, 4>{100, 0, 100, 200});
  REQUIRE(moveRequests[2] == std::array<int32_t, 4>{200, 0, 100, 200});
  moveRequests.clear();
  REQUIRE(SendMessage(client, WM_MDITILE, MDITILE_HORIZONTAL, 0));
  REQUIRE(moveRequests[1] == std::array<int32_t, 4>{0, 66, 300, 67});
  moveRequests.clear();
  REQUIRE(SendMessage(client, WM_MDICASCADE, 0, 0));
  REQUIRE(moveRequests.size() == 3);
  REQUIRE(moveRequests[0][0] == 0);
  REQUIRE(moveRequests[1][0] == moveRequests[1][1]);
  REQUIRE(moveRequests[1][0] > 0);
  REQUIRE(moveRequests[2][0] > moveRequests[1][0]);

  moveRequests.clear();
  SendMessage(client, WM_MDIMAXIMIZE, reinterpret_cast<WPARAM>(second), 0);
  REQUIRE(moveRequests.back() == std::array<int32_t, 4>{0, 0, 300, 200});
  SendMessage(client, WM_MDIGETACTIVE, 0, reinterpret_cast<LPARAM>(&maximized));
  REQUIRE(maximized);
  SendMessage(client, WM_MDIRESTORE, reinterpret_cast<WPARAM>(second), 0);
  REQUIRE(moveRequests.back() != std::array<int32_t, 4>{0, 0, 300, 200});

  // Closing a child renumbers the others and updates the Window menu.
  mdiMessages.clear();
  SendMessage(first, WM_CLOSE, 0, 0);
  REQUIRE_FALSE(IsWindow(first));
  REQUIRE(GetDlgCtrlID(second) == 500);
  REQUIRE(GetDlgCtrlID(third) == 501);
  menu_text_count(windowMenu, count);
  REQUIRE(count == 4);
  REQUIRE(SetWindowText(third, "/MNT/HD"));
  REQUIRE(lastMenuText == "&2 /MNT/HD");
  SendMessage(client, WM_MDIDESTROY, reinterpret_cast<WPARAM>(second), 0);
  REQUIRE(reinterpret_cast<HWND>(SendMessage(client, WM_MDIGETACTIVE, 0, 0)) ==
          third);
  REQUIRE(CreateMDIWindow("mdi-doc", "x", 0, 0, 0, 10, 10, frame, nullptr, 0) ==
          nullptr);
  REQUIRE(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
  REQUIRE(DestroyWindow(frame));
  REQUIRE_FALSE(IsWindow(third));
  REQUIRE_FALSE(IsWindow(client));
}

TEST_CASE("kernel32 file API maps onto the UnDOS file syscalls",
          "[sdk-gui][file]") {
  resetBackend();
  fakeFiles["/WINDOWS"] = {0, FILE_ATTRIBUTE_DIRECTORY};
  fakeFiles["/WINDOWS/WINFILE.ELF"] = {4096, FILE_ATTRIBUTE_ARCHIVE};
  fakeFiles["/WINDOWS/README.TXT"] = {10, 0};
  fakeFiles["/WINDOWS/SYSTEM.INI"] = {20, FILE_ATTRIBUTE_READONLY};
  fakeFiles["/WINDOWS/NOEXT"] = {1, 0};

  WIN32_FIND_DATA data{};
  HANDLE find = FindFirstFile("\\WINDOWS\\*.*", &data);
  REQUIRE(find != INVALID_HANDLE_VALUE);
  std::vector<std::string> names{data.cFileName};
  while (FindNextFile(find, &data)) names.push_back(data.cFileName);
  REQUIRE(GetLastError() == ERROR_NO_MORE_FILES);
  REQUIRE(names.size() == 4);
  REQUIRE(FindClose(find));
  REQUIRE_FALSE(FindClose(find));
  REQUIRE(GetLastError() == ERROR_INVALID_HANDLE);
  REQUIRE(fakeOpen.empty());

  find = FindFirstFile("/WINDOWS/*.elf", &data);
  REQUIRE(find != INVALID_HANDLE_VALUE);
  REQUIRE(std::string(data.cFileName) == "WINFILE.ELF");
  REQUIRE(data.nFileSizeLow == 4096);
  REQUIRE(data.dwFileAttributes == FILE_ATTRIBUTE_ARCHIVE);
  REQUIRE_FALSE(FindNextFile(find, &data));
  FindClose(find);
  find = FindFirstFile("\\WINDOWS\\NOEXT.*", &data);
  REQUIRE(find != INVALID_HANDLE_VALUE);
  FindClose(find);
  REQUIRE(FindFirstFile("\\WINDOWS\\*.BMP", &data) == INVALID_HANDLE_VALUE);
  REQUIRE(GetLastError() == ERROR_FILE_NOT_FOUND);
  REQUIRE(FindFirstFile("\\NOPE\\*.*", &data) == INVALID_HANDLE_VALUE);
  REQUIRE(GetLastError() == ERROR_PATH_NOT_FOUND);
  REQUIRE(FindFirstFile("/BAD:NAME/*.*", &data) == INVALID_HANDLE_VALUE);
  REQUIRE(GetLastError() == ERROR_INVALID_NAME);
  REQUIRE(GetFileAttributes("/BAD:NAME") == INVALID_FILE_ATTRIBUTES);
  REQUIRE(GetLastError() == ERROR_INVALID_NAME);

  REQUIRE(GetFileAttributes("/") == FILE_ATTRIBUTE_DIRECTORY);
  REQUIRE(GetFileAttributes("\\WINDOWS\\README.TXT") == FILE_ATTRIBUTE_NORMAL);
  REQUIRE(GetFileAttributes("\\WINDOWS\\GONE.TXT") == INVALID_FILE_ATTRIBUTES);
  REQUIRE(GetLastError() == ERROR_FILE_NOT_FOUND);
  REQUIRE(GetFileAttributes("\\GONE\\A.TXT") == INVALID_FILE_ATTRIBUTES);
  REQUIRE(GetLastError() == ERROR_PATH_NOT_FOUND);
  fakeFileOps.clear();
  REQUIRE(SetFileAttributes("\\WINDOWS\\SYSTEM.INI", FILE_ATTRIBUTE_READONLY));
  REQUIRE(fakeFileOps.empty());  // unchanged: no kernel call
  REQUIRE(SetFileAttributes("\\WINDOWS\\SYSTEM.INI",
                            FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_ARCHIVE |
                                FILE_ATTRIBUTE_DIRECTORY));
  REQUIRE(fakeFileOps.back()[2] == "34");  // directory bit dropped
  REQUIRE(GetFileAttributes("/WINDOWS/SYSTEM.INI") ==
          (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_ARCHIVE));
  REQUIRE(SetFileAttributes("\\WINDOWS\\SYSTEM.INI", FILE_ATTRIBUTE_NORMAL));
  REQUIRE(GetFileAttributes("/WINDOWS/SYSTEM.INI") == FILE_ATTRIBUTE_NORMAL);
  REQUIRE(SetFileAttributes("\\WINDOWS\\SYSTEM.INI", FILE_ATTRIBUTE_READONLY));
  REQUIRE_FALSE(SetFileAttributes("/", FILE_ATTRIBUTE_HIDDEN));
  REQUIRE(GetLastError() == ERROR_ACCESS_DENIED);
  REQUIRE_FALSE(SetFileAttributes("/WINDOWS/GONE.TXT", 0));
  REQUIRE(GetLastError() == ERROR_FILE_NOT_FOUND);

  // 1992-04-06 13:45:30 as a DOS stamp.
  const WORD dosDate = (12 << 9) | (4 << 5) | 6, dosTime = (13 << 11) | (45 << 5) | 15;
  fakeFiles["/WINDOWS/README.TXT"].writeTime = (uint32_t(dosDate) << 16) | dosTime;
  WIN32_FILE_ATTRIBUTE_DATA attributes{};
  REQUIRE(GetFileAttributesEx("\\WINDOWS\\README.TXT", GetFileExInfoStandard, &attributes));
  SYSTEMTIME written{};
  REQUIRE(FileTimeToSystemTime(&attributes.ftLastWriteTime, &written));
  REQUIRE(written.wYear == 1992);
  REQUIRE(written.wMonth == 4);
  REQUIRE(written.wDay == 6);
  REQUIRE(written.wHour == 13);
  REQUIRE(written.wMinute == 45);
  REQUIRE(written.wSecond == 30);
  REQUIRE(FileTimeToSystemTime(&attributes.ftLastAccessTime, &written));
  REQUIRE((written.wDay == 6 && written.wHour == 0));
  WORD roundDate = 0, roundTime = 0;
  REQUIRE(FileTimeToDosDateTime(&attributes.ftLastWriteTime, &roundDate, &roundTime));
  REQUIRE((roundDate == dosDate && roundTime == dosTime));
  find = FindFirstFile("/WINDOWS/README.TXT", &data);
  REQUIRE(find != INVALID_HANDLE_VALUE);
  REQUIRE(CompareFileTime(&data.ftLastWriteTime, &attributes.ftLastWriteTime) == 0);
  FindClose(find);
  REQUIRE(GetFileAttributesEx("/WINDOWS/SYSTEM.INI", GetFileExInfoStandard, &attributes));
  REQUIRE((attributes.ftLastWriteTime.dwLowDateTime == 0 &&
           attributes.ftLastWriteTime.dwHighDateTime == 0));
  REQUIRE_FALSE(GetFileAttributesEx("/GONE", GetFileExInfoStandard, &attributes));
  REQUIRE(GetLastError() == ERROR_FILE_NOT_FOUND);

  REQUIRE(CopyFile("\\WINDOWS\\README.TXT", "\\README.TXT", TRUE));
  REQUIRE(fakeFiles.count("/README.TXT"));
  REQUIRE_FALSE(CopyFile("\\WINDOWS\\README.TXT", "\\README.TXT", TRUE));
  REQUIRE(GetLastError() == ERROR_FILE_EXISTS);
  fakeFileOps.clear();
  REQUIRE(CopyFile("\\WINDOWS\\README.TXT", "\\README.TXT", FALSE));
  REQUIRE(fakeFileOps.size() == 2);
  REQUIRE_FALSE(CopyFile("\\WINDOWS", "\\W2", FALSE));
  REQUIRE(GetLastError() == ERROR_ACCESS_DENIED);
  REQUIRE_FALSE(CopyFile("\\README.TXT", "\\NONE\\X.TXT", FALSE));
  REQUIRE(GetLastError() == ERROR_PATH_NOT_FOUND);
  REQUIRE_FALSE(MoveFile("\\README.TXT", "\\WINDOWS\\README.TXT"));
  REQUIRE(GetLastError() == ERROR_ALREADY_EXISTS);
  REQUIRE(MoveFile("\\README.TXT", "\\READ.ME"));
  REQUIRE(fakeFiles.count("/READ.ME"));
  REQUIRE_FALSE(fakeFiles.count("/README.TXT"));
  REQUIRE(MoveFileEx("\\READ.ME", "\\WINDOWS\\README.TXT",
                     MOVEFILE_REPLACE_EXISTING));
  REQUIRE_FALSE(DeleteFile("\\WINDOWS\\SYSTEM.INI"));
  REQUIRE(GetLastError() == ERROR_ACCESS_DENIED);
  REQUIRE(DeleteFile("\\WINDOWS\\NOEXT"));
  REQUIRE_FALSE(DeleteFile("\\WINDOWS\\NOEXT"));
  REQUIRE(GetLastError() == ERROR_FILE_NOT_FOUND);

  REQUIRE(CreateDirectory("\\TEMP", nullptr));
  REQUIRE_FALSE(CreateDirectory("\\TEMP", nullptr));
  REQUIRE(GetLastError() == ERROR_ALREADY_EXISTS);
  REQUIRE_FALSE(CreateDirectory("\\A\\B", nullptr));
  REQUIRE(GetLastError() == ERROR_PATH_NOT_FOUND);
  REQUIRE_FALSE(RemoveDirectory("\\WINDOWS"));
  REQUIRE(GetLastError() == ERROR_DIR_NOT_EMPTY);
  REQUIRE_FALSE(RemoveDirectory("\\WINDOWS\\README.TXT"));
  REQUIRE(GetLastError() == ERROR_DIRECTORY);
  REQUIRE(RemoveDirectory("\\TEMP"));

  fakeMounts.push_back({"/MNT/CD", "", "FAT12", 0, UI_VOLUME_READ_ONLY});
  REQUIRE(GetDriveType("/") == DRIVE_FIXED);
  REQUIRE(GetDriveType("/MNT/NONE") == DRIVE_NO_ROOT_DIR);
  REQUIRE(GetDriveType("\\") == DRIVE_FIXED);
  REQUIRE(GetDriveType("/WINDOWS") == DRIVE_NO_ROOT_DIR);
  REQUIRE(GetDriveType("\\MNT\\CD\\") == DRIVE_FIXED);
  char roots[32];
  REQUIRE(GetLogicalDriveStrings(sizeof(roots), roots) == 10);
  REQUIRE(std::memcmp(roots, "/\0/MNT/CD\0\0", 11) == 0);
  REQUIRE(GetLogicalDriveStrings(5, roots) == 11);
  REQUIRE(GetLogicalDriveStrings(0, nullptr) == 11);
  char label[16], fileSystem[8];
  DWORD serial = 0, component = 0, flags = 0;
  REQUIRE(GetVolumeInformation("/", label, sizeof(label), &serial, &component,
                               &flags, fileSystem, sizeof(fileSystem)));
  REQUIRE(std::string(label) == "UNDOS");
  REQUIRE(std::string(fileSystem) == "FAT16");
  REQUIRE(serial == 0x1234ABCDu);
  REQUIRE(component == 255);
  REQUIRE(flags == FILE_CASE_PRESERVED_NAMES);
  REQUIRE(GetVolumeInformation("\\MNT\\CD", label, sizeof(label), nullptr,
                               nullptr, &flags, nullptr, 0));
  REQUIRE(label[0] == 0);
  REQUIRE(flags == FILE_READ_ONLY_VOLUME);
  REQUIRE_FALSE(GetVolumeInformation("/", label, 3, nullptr, nullptr, nullptr,
                                     nullptr, 0));
  REQUIRE(GetLastError() == ERROR_BAD_LENGTH);
  REQUIRE_FALSE(GetVolumeInformation("/WINDOWS", label, sizeof(label), nullptr,
                                     nullptr, nullptr, nullptr, 0));
  REQUIRE(GetLastError() == ERROR_PATH_NOT_FOUND);

  SYSTEMTIME time{1992, 4, 0, 6, 13, 45, 30, 250};
  FILETIME file{}, local{};
  REQUIRE(SystemTimeToFileTime(&time, &file));
  REQUIRE(FileTimeToLocalFileTime(&file, &local));
  SYSTEMTIME back{};
  REQUIRE(FileTimeToSystemTime(&local, &back));
  REQUIRE(back.wYear == 1992);
  REQUIRE(back.wMonth == 4);
  REQUIRE(back.wDay == 6);
  REQUIRE(back.wDayOfWeek == 1);  // Windows 3.1 shipped on a Monday.
  REQUIRE(back.wHour == 13);
  REQUIRE(back.wMinute == 45);
  REQUIRE(back.wSecond == 30);
  REQUIRE(back.wMilliseconds == 250);
  FILETIME epoch{0, 0};
  REQUIRE(FileTimeToSystemTime(&epoch, &back));
  REQUIRE(back.wYear == 1601);
  REQUIRE(back.wDayOfWeek == 1);
  REQUIRE(CompareFileTime(&epoch, &file) == -1);
  time.wMonth = 2;
  time.wDay = 30;
  REQUIRE_FALSE(SystemTimeToFileTime(&time, &file));
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);
}

TEST_CASE("ShellExecute runs programs and associated documents",
          "[sdk-gui][shell]") {
  resetBackend();
  fakeFiles["/APPS"] = {0, FILE_ATTRIBUTE_DIRECTORY};
  fakeFiles["/APPS/NOTEPAD.ELF"] = {100, 0};
  fakeFiles["/APPS/README.TXT"] = {10, 0};
  fakeFiles["/APPS/PIC.BMP"] = {10, 0};

  REQUIRE(reinterpret_cast<uintptr_t>(ShellExecute(
              nullptr, "open", "NOTEPAD.ELF", "/x", "\\APPS", 1)) > 32);
  REQUIRE(fakeProcesses.back() ==
          std::make_pair(std::string("/APPS/NOTEPAD.ELF"), std::string("/x")));
  REQUIRE(reinterpret_cast<uintptr_t>(ShellExecute(
              nullptr, nullptr, "\\APPS\\README.TXT", nullptr, nullptr, 1)) ==
          SE_ERR_NOASSOC);
  REQUIRE(UndosSetAssociation("txt", "/APPS/NOTEPAD.ELF"));
  char program[MAX_PATH];
  REQUIRE(reinterpret_cast<uintptr_t>(
              FindExecutable("\\APPS\\README.TXT", nullptr, program)) > 32);
  REQUIRE(std::string(program) == "/APPS/NOTEPAD.ELF");
  REQUIRE(reinterpret_cast<uintptr_t>(ShellExecute(
              nullptr, "open", "README.TXT", nullptr, "/APPS", 1)) > 32);
  REQUIRE(fakeProcesses.back().second == "/APPS/README.TXT");
  REQUIRE(reinterpret_cast<uintptr_t>(
              FindExecutable("\\APPS\\MISSING.TXT", nullptr, program)) ==
          SE_ERR_FNF);
  REQUIRE(program[0] == 0);
  REQUIRE(reinterpret_cast<uintptr_t>(
              ShellExecute(nullptr, "print", "/APPS/README.TXT", nullptr,
                           nullptr, 1)) == SE_ERR_NOASSOC);
  UndosSetAssociation("BMP", "/APPS/PAINT.ELF");
  REQUIRE(reinterpret_cast<uintptr_t>(ShellExecute(
              nullptr, "open", "/APPS/PIC.BMP", nullptr, nullptr, 1)) == SE_ERR_FNF);
  REQUIRE(UndosSetAssociation("TXT", nullptr));
  REQUIRE(reinterpret_cast<uintptr_t>(
              FindExecutable("/APPS/README.TXT", nullptr, program)) == SE_ERR_NOASSOC);
  REQUIRE_FALSE(UndosSetAssociation(".TXT", "/X.ELF"));
}

TEST_CASE("kernel32 string and memory helpers follow Win32",
          "[sdk-gui][kernel32]") {
  resetBackend();
  char buffer[64];
  REQUIRE(lstrlen(nullptr) == 0);
  REQUIRE(lstrcpy(buffer, "Winfile") == buffer);
  REQUIRE(lstrcat(buffer, ".INI") == buffer);
  REQUIRE(std::string(buffer) == "Winfile.INI");
  REQUIRE(lstrlen(buffer) == 11);
  REQUIRE(lstrcpyn(buffer, "abcdef", 4) == buffer);
  REQUIRE(std::string(buffer) == "abc");
  REQUIRE(lstrcmpi("README.TXT", "readme.txt") == 0);
  REQUIRE(lstrcmpi("A", "b") < 0);
  REQUIRE(lstrcmp("b", "a") == 1);
  REQUIRE(wsprintf(buffer, "%s=%08x %-3d|", "serial", 0xABCDu, 7) == 20);
  REQUIRE(std::string(buffer) == "serial=0000abcd 7  |");
  std::string longFormat(1500, 'x');
  std::vector<char> large(2048);
  REQUIRE(wsprintf(large.data(), longFormat.c_str()) == 1023);
  REQUIRE(std::strlen(large.data()) == 1023);
  REQUIRE(std::string(CharUpper(buffer)) == "SERIAL=0000ABCD 7  |");
  REQUIRE(CharLower(reinterpret_cast<LPSTR>(static_cast<uintptr_t>('Q'))) ==
          reinterpret_cast<LPSTR>(static_cast<uintptr_t>('q')));
  REQUIRE(CharNext("a") != nullptr);
  REQUIRE(IsCharAlpha('z'));
  REQUIRE_FALSE(IsCharAlphaNumeric('-'));

  auto *local = static_cast<BYTE *>(LocalAlloc(LPTR, 16));
  REQUIRE(local);
  REQUIRE(LocalSize(local) == 16);
  REQUIRE(local[15] == 0);
  REQUIRE(LocalLock(local) == local);
  local = static_cast<BYTE *>(LocalReAlloc(local, 32, LMEM_ZEROINIT));
  REQUIRE(LocalSize(local) == 32);
  REQUIRE(local[31] == 0);
  REQUIRE(LocalFree(local) == nullptr);
  int notOurs = 0;
  REQUIRE(LocalFree(&notOurs + 4) == &notOurs + 4);
  REQUIRE(GetLastError() == ERROR_INVALID_HANDLE);
  HGLOBAL global = GlobalAlloc(GHND, 8);
  REQUIRE(GlobalLock(global) == global);
  REQUIRE(GlobalSize(global) == 8);
  REQUIRE(GlobalFree(global) == nullptr);
  void *heap = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 4);
  REQUIRE(HeapSize(GetProcessHeap(), 0, heap) == 4);
  REQUIRE(HeapFree(GetProcessHeap(), 0, heap));
  REQUIRE(HeapAlloc(99, 0, 4) == nullptr);
  REQUIRE(GetLastError() == ERROR_INVALID_HANDLE);
}

TEST_CASE("current directory resolves relative paths",
          "[sdk-gui][kernel32]") {
  resetBackend();
  fakeFiles["/System"] = {0, 0x10};
  fakeFiles["/System/winfile.ini"] = {10, 0x20};
  char path[MAX_PATH];
  REQUIRE(GetCurrentDirectory(sizeof(path), path) == 1);
  REQUIRE(std::string(path) == "/");
  REQUIRE(SetCurrentDirectory("\\System\\."));
  REQUIRE(GetCurrentDirectory(sizeof(path), path) == 7);
  REQUIRE(std::string(path) == "/System");
  REQUIRE(GetCurrentDirectory(3, path) == 8);
  REQUIRE(GetFileAttributes("winfile.ini") == FILE_ATTRIBUTE_ARCHIVE);
  LPSTR part = nullptr;
  REQUIRE(GetFullPathName("..\\System\\winfile.ini", sizeof(path), path,
                          &part) == 19);
  REQUIRE(std::string(path) == "/System/winfile.ini");
  REQUIRE(std::string(part) == "winfile.ini");
  REQUIRE(GetFullPathName("../../..", sizeof(path), path, &part) == 1);
  REQUIRE(std::string(path) == "/");
  REQUIRE_FALSE(SetCurrentDirectory("winfile.ini"));
  REQUIRE(GetLastError() == ERROR_DIRECTORY);
  REQUIRE_FALSE(SetCurrentDirectory("/MISSING"));
  REQUIRE(GetLastError() == ERROR_FILE_NOT_FOUND);
  REQUIRE(GetFullPathName("BAD:X", sizeof(path), path, &part) == 0);
  REQUIRE(GetLastError() == ERROR_INVALID_NAME);
  REQUIRE(SetCurrentDirectory("/"));
}

TEST_CASE("rectangles and coordinate mapping follow Win32",
          "[sdk-gui][user32]") {
  resetBackend();
  RECT a{0, 0, 10, 10}, b{5, 5, 20, 20}, out{};
  REQUIRE(IntersectRect(&out, &a, &b));
  REQUIRE(EqualRect(&out, std::array<RECT, 1>{RECT{5, 5, 10, 10}}.data()));
  RECT c{30, 30, 40, 40};
  REQUIRE_FALSE(IntersectRect(&out, &a, &c));
  REQUIRE(IsRectEmpty(&out));
  REQUIRE(UnionRect(&out, &a, &c));
  REQUIRE(out.right == 40);
  REQUIRE(PtInRect(&a, POINT{0, 9}));
  REQUIRE_FALSE(PtInRect(&a, POINT{10, 0}));
  REQUIRE(CopyRect(&out, &b));
  REQUIRE(SetRectEmpty(&out));
  REQUIRE(IsRectEmpty(&out));

  // Window at (100,50) 200x150 with a 1-pixel border and 20 pixels of
  // caption and menu bar: the client area starts at (101,71).
  backendWindowRect = {100, 50, 300, 200};
  backendClientRect = {0, 0, 198, 128};
  HWND window = make_recording_window("coords");
  POINT point{5, 6};
  REQUIRE(ClientToScreen(window, &point));
  REQUIRE(point.x == 106);
  REQUIRE(point.y == 77);
  REQUIRE(ScreenToClient(window, &point));
  REQUIRE(point.x == 5);
  REQUIRE(point.y == 6);
  POINT points[2] = {{0, 0}, {1, 1}};
  const int offset = MapWindowPoints(window, nullptr, points, 2);
  REQUIRE(points[1].x == 102);
  REQUIRE(LOWORD(offset) == 101);
  REQUIRE(HIWORD(offset) == 71);
  REQUIRE_FALSE(IsIconic(window));
  backendClientRect = {0, 0, 0, 0};
  REQUIRE(IsIconic(window));
  backendClientRect = {0, 0, 198, 128};

  moveCalls = 0;
  REQUIRE(SetWindowPos(window, HWND_TOP, 0, 0, 80, 60, SWP_NOMOVE));
  REQUIRE(moveCalls == 1);
  REQUIRE(lastMove == std::array<int32_t, 4>{100, 50, 80, 60});
  REQUIRE(SetWindowPos(window, nullptr, 7, 8, 0, 0, SWP_NOSIZE | SWP_NOZORDER));
  REQUIRE(lastMove == std::array<int32_t, 4>{7, 8, 200, 150});
  REQUIRE(SetWindowPos(window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE));
  REQUIRE(moveCalls == 2);
  REQUIRE_FALSE(SetWindowPos(window, nullptr, 0, 0, 0, 0,
                             SWP_SHOWWINDOW | SWP_HIDEWINDOW));
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);
  REQUIRE(UpdateWindow(window));
  REQUIRE(MessageBeep(MB_OK));
  REQUIRE(DestroyWindow(window));
  REQUIRE_FALSE(UpdateWindow(window));
  REQUIRE(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
}

namespace {
std::string messageBoxText;
int messageBoxButtons = 0;
// Fires inside MessageBox's modal loop: records the dialog and presses the
// focused (default) button.
void CALLBACK press_default(HWND, UINT, UINT_PTR id, DWORD) {
  KillTimer(nullptr, id);
  HWND focus = GetFocus();
  HWND dialog = GetParent(focus);
  char text[128] = {};
  GetDlgItemText(dialog, 0xFFFF, text, sizeof(text));
  messageBoxText = text;
  messageBoxButtons = 0;
  for (int id2 : {IDOK, IDCANCEL, IDYES, IDNO, IDRETRY})
    if (GetDlgItem(dialog, id2)) ++messageBoxButtons;
  SendMessage(dialog, WM_COMMAND,
              MAKEWPARAM(GetDlgCtrlID(focus), BN_CLICKED),
              reinterpret_cast<LPARAM>(focus));
}
} // namespace

TEST_CASE("MessageBox runs a modal dialog and returns the button",
          "[sdk-gui][user32][dialog]") {
  resetBackend();
  backendClientRect = {0, 0, 200, 80};
  backendWindowRect = {0, 0, 202, 102};
  REQUIRE(SetTimer(nullptr, 1, 10, press_default));
  REQUIRE(MessageBox(nullptr, "Delete file?", "Confirm", MB_YESNO) == IDYES);
  REQUIRE(messageBoxText == "Delete file?");
  REQUIRE(messageBoxButtons == 2);
  REQUIRE(SetTimer(nullptr, 2, 10, press_default));
  REQUIRE(MessageBox(nullptr, "Retry?", nullptr,
                     MB_RETRYCANCEL | MB_DEFBUTTON2) == IDCANCEL);
  REQUIRE(MessageBox(nullptr, "x", "y", 9) == 0);
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);
}

namespace {
DWORD WINAPI worker(LPVOID) { return 5; }
} // namespace

TEST_CASE("threads, events and critical sections map to kernel objects",
          "[sdk-gui][kernel32][threads]") {
  resetBackend();
  DWORD id = 0;
  int argument = 3;
  HANDLE thread = CreateThread(nullptr, 0, worker, &argument, 0, &id);
  REQUIRE(thread);
  REQUIRE(id == 40 + thread);
  REQUIRE(lastThreadEntry != 0);
  REQUIRE(lastThreadStack == 0);
  // The start block is passed as a 32-bit kernel argument; host pointers
  // are wider, so only its presence is checked here.
  REQUIRE(lastThreadParameter != 0);
  DWORD code = 0;
  REQUIRE(GetExitCodeThread(thread, &code));
  REQUIRE(code == STILL_ACTIVE);
  REQUIRE(WaitForSingleObject(thread, 0) == WAIT_TIMEOUT);
  fakeThreadExit[thread] = 5;
  REQUIRE(WaitForSingleObject(thread, INFINITE) == WAIT_OBJECT_0);
  REQUIRE(GetExitCodeThread(thread, &code));
  REQUIRE(code == 5);
  REQUIRE(CloseHandle(thread));
  REQUIRE_FALSE(CloseHandle(thread));
  REQUIRE(CreateThread(nullptr, 0, nullptr, nullptr, 0, &id) == 0);
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);
  REQUIRE(CreateThread(nullptr, 0, worker, nullptr, CREATE_SUSPENDED, &id) == 0);
  REQUIRE(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);
  REQUIRE(CreateThread(nullptr, 0x200000, worker, nullptr, 0, nullptr) == 0);
  REQUIRE(GetLastError() == ERROR_NOT_ENOUGH_MEMORY);
  REQUIRE(GetCurrentThreadId() == 7);

  HANDLE manual = CreateEvent(nullptr, TRUE, FALSE, nullptr);
  HANDLE automatic = CreateEvent(nullptr, FALSE, TRUE, nullptr);
  REQUIRE(WaitForSingleObject(manual, 0) == WAIT_TIMEOUT);
  REQUIRE(SetEvent(manual));
  REQUIRE(WaitForSingleObject(manual, 0) == WAIT_OBJECT_0);
  REQUIRE(WaitForSingleObject(manual, 0) == WAIT_OBJECT_0);
  REQUIRE(ResetEvent(manual));
  REQUIRE(WaitForSingleObject(manual, 0) == WAIT_TIMEOUT);
  REQUIRE(WaitForSingleObject(automatic, 0) == WAIT_OBJECT_0);
  REQUIRE(WaitForSingleObject(automatic, 0) == WAIT_TIMEOUT);
  REQUIRE(WaitForSingleObject(0x9999, 0) == WAIT_FAILED);
  REQUIRE(CreateEvent(nullptr, TRUE, FALSE, "named") == 0);
  REQUIRE(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);
  REQUIRE(CloseHandle(manual));
  REQUIRE(CloseHandle(automatic));

  CRITICAL_SECTION section;
  InitializeCriticalSection(&section);
  REQUIRE(section.LockSemaphore);
  EnterCriticalSection(&section);
  EnterCriticalSection(&section);
  REQUIRE(section.RecursionCount == 2);
  REQUIRE(section.OwningThread == 7);
  REQUIRE(TryEnterCriticalSection(&section));
  LeaveCriticalSection(&section);
  LeaveCriticalSection(&section);
  LeaveCriticalSection(&section);
  REQUIRE(section.LockCount == -1);
  REQUIRE(section.OwningThread == 0);
  fakeThreadId = 8;
  REQUIRE(TryEnterCriticalSection(&section));
  fakeThreadId = 7;
  REQUIRE_FALSE(TryEnterCriticalSection(&section));
  fakeThreadId = 8;
  LeaveCriticalSection(&section);
  fakeThreadId = 7;
  DeleteCriticalSection(&section);
  REQUIRE(fakeEvents.empty());

  REQUIRE(InterlockedIncrement(&section.LockCount) == 0);
  REQUIRE(InterlockedCompareExchange(&section.LockCount, 9, 0) == 0);
  REQUIRE(InterlockedExchange(&section.LockCount, 1) == 9);
}

TEST_CASE("GetDC draws outside WM_PAINT and ReleaseDC returns it",
          "[sdk-gui][user32][paint]") {
  resetBackend();
  windowDcCalls = windowDcReleases = 0;
  HWND window = make_recording_window("getdc");
  HDC dc = GetDC(window);
  REQUIRE(dc);
  REQUIRE(windowDcCalls == 1);
  RECT box{1, 1, 5, 5};
  REQUIRE(FillRect(dc, &box, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH))));
  REQUIRE(TextOut(dc, 0, 0, "x", 1));
  PAINTSTRUCT fake{};
  fake.hdc = dc;
  REQUIRE_FALSE(EndPaint(window, &fake));
  REQUIRE(ReleaseDC(nullptr, dc) == 0);
  REQUIRE(ReleaseDC(window, dc) == 1);
  REQUIRE(windowDcReleases == 1);
  REQUIRE(lastReleasedDc == reinterpret_cast<uintptr_t>(dc));
  REQUIRE(ReleaseDC(window, dc) == 0);
  REQUIRE(GetLastError() == ERROR_INVALID_HANDLE);
  REQUIRE(GetDC(nullptr) == nullptr);
  REQUIRE(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);
  windowDcStatus = UI_STATUS_INVALID_ARGUMENT;
  REQUIRE(GetDC(window) == nullptr);
  windowDcStatus = UI_STATUS_OK;
  REQUIRE(DestroyWindow(window));
  REQUIRE(GetDC(window) == nullptr);
  REQUIRE(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
}

TEST_CASE("C UI RegisterClass accepts WINFILE-style class fields and window extra bytes",
          "[sdk-gui]") {
  resetBackend();
  WNDCLASS klass = test_class("extra-class");
  klass.style = CS_DBLCLKS;
  klass.cbWndExtra = 3 * sizeof(LONG);
  klass.hIcon = reinterpret_cast<HICON>(static_cast<uintptr_t>(5));
  klass.hCursor = LoadCursor(nullptr, IDC_ARROW);
  klass.lpszMenuName = MAKEINTRESOURCE(999);  // missing: window gets no menu
  REQUIRE(RegisterClass(&klass) != 0);

  HWND window = CreateWindow("extra-class", "x", 0, 0, 0, 20, 20,
                             nullptr, nullptr, nullptr, nullptr);
  REQUIRE(window != nullptr);
  REQUIRE(GetWindowLong(window, 0) == 0);
  REQUIRE(GetWindowLong(window, 8) == 0);
  REQUIRE(SetWindowLong(window, 4, 0x1234) == 0);
  REQUIRE(SetWindowLong(window, 4, -7) == 0x1234);
  REQUIRE(GetWindowLong(window, 4) == -7);
  SetLastError(0);
  REQUIRE(SetWindowLong(window, 12, 1) == 0);
  REQUIRE(GetLastError() == ERROR_INVALID_INDEX);
  REQUIRE(DestroyWindow(window));

  HWND clipped = CreateWindow("extra-class", "frame",
                              WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                              0, 0, 20, 20, nullptr, nullptr, nullptr, nullptr);
  REQUIRE(clipped != nullptr);
  REQUIRE(DestroyWindow(clipped));

  // WINFILE's default placement: CW_USEDEFAULT width with height 0.
  HWND defaulted = CreateWindow("extra-class", "frame", WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, 0, CW_USEDEFAULT, 0,
                                nullptr, nullptr, nullptr, nullptr);
  REQUIRE(defaulted != nullptr);
  REQUIRE(DestroyWindow(defaulted));

  klass = test_class("too-much-extra");
  klass.cbWndExtra = 65;
  REQUIRE(RegisterClass(&klass) == 0);
  REQUIRE(GetLastError() == ERROR_INVALID_PARAMETER);
}

namespace {
HWND createdChildDuringCreate = nullptr;
LONG extraDuringCreate = 0;
LRESULT CALLBACK create_children_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  if (message == WM_CREATE) {
    SetWindowLong(window, 0, 42);
    extraDuringCreate = GetWindowLong(window, 0);
    createdChildDuringCreate = CreateWindow("button", "c", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10,
                                            window, reinterpret_cast<HMENU>(7), nullptr, nullptr);
    return createdChildDuringCreate ? 0 : -1;
  }
  return DefWindowProc(window, message, wParam, lParam);
}
}  // namespace

TEST_CASE("C UI WM_CREATE can use window extra bytes and create child windows", "[sdk-gui]") {
  resetBackend();
  WNDCLASS klass = test_class("create-children");
  klass.lpfnWndProc = create_children_proc;
  klass.cbWndExtra = sizeof(LONG);
  REQUIRE(RegisterClass(&klass) != 0);
  createdChildDuringCreate = nullptr;
  extraDuringCreate = 0;
  HWND window = CreateWindow("create-children", "p", WS_OVERLAPPEDWINDOW, 0, 0, 50, 50,
                             nullptr, nullptr, nullptr, nullptr);
  INFO("error " << GetLastError());
  REQUIRE(window != nullptr);
  REQUIRE(extraDuringCreate == 42);
  REQUIRE(createdChildDuringCreate != nullptr);
  REQUIRE(GetParent(createdChildDuringCreate) == window);
  REQUIRE(DestroyWindow(window));
}
