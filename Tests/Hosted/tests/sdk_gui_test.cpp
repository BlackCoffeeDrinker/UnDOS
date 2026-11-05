#include <catch2/catch_test_macros.hpp>
#include <windows.h>
#include <undosgui/extensions.h>

#include "private/backend.h"
#include "private/font_mapping.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
extern "C" void undosgui_test_reset(void);
extern "C" void undosgui_ui_test_reset(void);
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
uint32_t failDeleteStatus{};
std::vector<uint32_t> menuHandles;
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

void resetBackend() {
  undosgui_test_reset();
  undosgui_ui_test_reset();
  failOperation = Operation::None;
  failStatus = UI_STATUS_INVALID_ARGUMENT;
  failCreateClass.clear();
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

uint32_t ui_backend_append_menu(uint32_t, uint32_t flags, uint32_t item,
                                const char *text) {
  uint32_t result = status(Operation::AppendMenu);
  if (result == UI_STATUS_OK) {
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

uint32_t ui_backend_fill_rectangle(uint32_t, int32_t, int32_t, int32_t, int32_t) {
  return UI_STATUS_OK;
}

uint32_t ui_backend_move_to(uint32_t, int32_t, int32_t) { return UI_STATUS_OK; }
uint32_t ui_backend_line_to(uint32_t, int32_t, int32_t) { return UI_STATUS_OK; }

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

uint32_t ui_backend_draw_text(uint32_t, int32_t, int32_t, uint32_t, uint32_t,
                              uint32_t, const char *text) {
  uint32_t result = status(Operation::DrawText);
  if (result == UI_STATUS_OK) {
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
  REQUIRE_FALSE(AppendMenu(menu, 0x0008, 43, "unsupported"));
  REQUIRE(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);
  REQUIRE(lastMenuItem == previousItem);
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
