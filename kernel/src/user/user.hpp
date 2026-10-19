#pragma once

#include "stdkrn.hpp"

#include <expected.hpp>
#include <string_view.hpp>

#include <kernel/kerror.hpp>
#include <kernel/kobject/KDispatcherHeader.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/kobject/ObjectType.hpp>

namespace kernel {
struct KProcessObject;
struct KThreadObject;
}// namespace kernel

namespace kernel::user {
constexpr uint32_t kMaxWindows = 64;
constexpr uint32_t kMaxClasses = 16;


enum class HitTest : int32_t {
  Transparent = -1,
  Nowhere = 0,
  Client = 1,
  Caption = 2,
  SysMenu = 3,
  MinButton = 8,
  MaxButton = 9,
  Left = 10,
  Right = 11,
  Top = 12,
  TopLeft = 13,
  TopRight = 14,
  Bottom = 15,
  BottomLeft = 16,
  BottomRight = 17,
  Border = 18,
  Close = 20,
};

constexpr int32_t kCaptionHeight = 18;
constexpr int32_t kBorderWidth = 3;

constexpr uint32_t kDesktopColor = 3;
constexpr uint32_t kActiveCaptionColor = 1;  // Dark blue
constexpr uint32_t kInactiveCaptionColor = 8;// Grey
constexpr uint32_t kCaptionTextColor = 15;   // White
constexpr uint32_t kWindowFrameColor = 0;    // Black
constexpr uint32_t kWindowFaceColor = 7;     // Light grey


// All window manager state is guarded by this lock.
void lock() noexcept;
void unlock() noexcept;

struct Guard {
  Guard() noexcept { lock(); }
  ~Guard() { unlock(); }
  Guard(const Guard &) = delete;
  Guard &operator=(const Guard &) = delete;
};

// window.cpp (callers hold the lock)

/**
 * Retrieves a `KWindowObject` pointer corresponding to a given window handle.
 *
 * This function looks up a window object in the global window table based on
 * the specified handle. The lookup verifies that the handle's index is within
 * bounds, the generation of the handle matches, and the window object exists.
 * If any of these conditions are not met, the function returns `nullptr`.
 *
 * Preconditions:
 * - The caller must hold the appropriate lock to ensure thread-safety during
 *   this lookup operation.
 *
 * @param hwnd The handle of the window to look up. The handle encodes the
 *             index and generation of the target window.
 * @return     A pointer to the `KWindowObject` if the lookup succeeds;
 *             otherwise, `nullptr`.
 */
KWindowObject *lookup_locked(uint32_t hwnd) noexcept;
KWindowObject *topmost_locked() noexcept;
KWindowObject *active_window_locked() noexcept;
void set_active_window_locked(uint32_t hwnd) noexcept;
KWindowObject *focused_window_locked() noexcept;
// Errors: InvalidHandle (unknown hwnd), InvalidState (hidden, or a minimized
// root window). hwnd 0 clears the focus.
KernelError set_focus_locked(uint32_t hwnd) noexcept;
bool effectively_visible_locked(const KWindowObject &window) noexcept;
bool descendant_locked(const KWindowObject &window, const KWindowObject &ancestor) noexcept;
URect clipped_rect_locked(const KWindowObject &window) noexcept;
URect client_rect_locked(const KWindowObject &window) noexcept;
// Converts parent-client placement to a screen rectangle. Errors:
// InvalidSize (non-positive extent), InvalidHandle (stale parent),
// OutOfRange (outside the 16-bit coordinate space).
kstd::expected<URect, KernelError> placement_rect_locked(const KWindowObject &window, int32_t x, int32_t y,
                                                         int32_t width, int32_t height) noexcept;
KWindowObject *window_from_point_locked(int32_t x, int32_t y) noexcept;
KWindowObject *mouse_target_locked(int32_t x, int32_t y) noexcept;
int32_t find_class_locked(const kstd::string_view &name, const KProcessObject *process = nullptr) noexcept;
WindowClass *class_at_locked(uint32_t index) noexcept;
// Errors: ResourceLimitExceeded (class table full).
KernelError register_class_locked(KProcessObject &proc, const kstd::string_view &name, uint32_t wndProc, uint32_t background) noexcept;
// Returns the new hwnd. Errors: ResourceLimitExceeded (window table full).
kstd::expected<uint32_t, KernelError> insert_window_locked(const KObjectPtr<KWindowObject> &window) noexcept;
void destroy_window_locked(KWindowObject &window) noexcept;
// Errors: InvalidSize (empty rect), OutOfRange (window or a descendant would
// leave the 16-bit coordinate space). Nothing changes on failure.
KernelError move_window_locked(KWindowObject &window, const URect &rect) noexcept;
void show_window_locked(KWindowObject &window, bool show) noexcept;
// Errors: InvalidArgument (unknown command), InvalidState (no room in the
// parent client area), or the move_window_locked error.
KernelError show_command_locked(KWindowObject &window, uint32_t command) noexcept;
void cycle_window_locked(bool children, bool reverse) noexcept;
void set_capture_locked(uint32_t hwnd) noexcept;
uint32_t capture_locked() noexcept;
void display_changed_locked(uint32_t bpp) noexcept;

// paint.cpp: fill a screen rectangle through the adapter (planar-safe), clipped to the screen.
void fill_screen_locked(const URect &r, uint32_t color) noexcept;
void cursor_hide_locked() noexcept;
void cursor_show_locked() noexcept;
void cursor_move_locked(int32_t x, int32_t y) noexcept;
void cursor_reset_locked() noexcept;
void begin_scene_draw_locked() noexcept;
void end_scene_draw_locked() noexcept;

struct SceneDrawGuard {
  SceneDrawGuard() noexcept { begin_scene_draw_locked(); }
  ~SceneDrawGuard() { end_scene_draw_locked(); }
};

// nonclient.cpp (callers hold the lock)
URect client_to_screen_locked(const KWindowObject &window, const URect &clientRect) noexcept;
URect screen_to_client_locked(const KWindowObject &window, const URect &screenRect) noexcept;
HitTest hit_test_locked(const KWindowObject &window, int32_t x, int32_t y) noexcept;
void draw_nonclient_locked(KWindowObject &window) noexcept;

// rawinput.cpp
void init_raw_input() noexcept;
void display_input_changed_locked(bool firstActivation) noexcept;

// msgqueue.cpp
// Errors: NoCurrentThread, NotFound (no queue and !create), NoCurrentProcess
// (kernel thread cannot own a queue), OutOfMemory.
kstd::expected<MessageQueue *, KernelError> queue_for_current_locked(bool create) noexcept;
// Errors: QueueFull (message dropped).
KernelError post_locked(MessageQueue &queue, uint32_t hwnd, uint32_t message, uint32_t wParam, uint32_t lParam) noexcept;
void wake_locked(MessageQueue &queue) noexcept;
// Self-locking. Errors: WouldBlock (nothing pending and !block), the
// queue_for_current_locked errors, or the scheduler's wait error.
kstd::expected<QueuedMessage, KernelError> get_message(bool block, bool remove) noexcept;
bool geometry_message_locked(const MessageQueue &queue, QueuedMessage &out, bool remove) noexcept;
void free_queues_locked(KProcessObject &proc) noexcept;

// paint.cpp (callers hold the lock)
URect screen_rect() noexcept;
void invalidate_locked(KWindowObject &window, const URect &windowRect) noexcept;
void expose_locked(const URect &screenRect) noexcept;
void visible_region_locked(const KWindowObject &window, URegion &out) noexcept;
struct PaintBegin {
  HDC dc{0};
  URect paint{};// client coordinates
};
// Errors: InvalidState (a paint DC is already live), DeviceNotReady (no
// primary display surface), or the DC table allocation error.
kstd::expected<PaintBegin, KernelError> begin_paint_locked(KProcessObject &proc, KWindowObject &window) noexcept;
// Errors: InvalidHandle (hdc is not this window's live paint DC) or the DC
// table close error.
KernelError end_paint_locked(KProcessObject &proc, KWindowObject &window, uint32_t hdc) noexcept;
// Hands the damaged screen rectangles to the adapter; one call per paint cycle.
void present_locked(const URegion &region) noexcept;
void present_locked(const URect &r) noexcept;

// Destroys every window, class and queue owned by proc.
void on_process_exit(KProcessObject &proc) noexcept;

// ---------------------------------------------------------------------------
// Subsystem operations (self-locking: callers must NOT hold the lock). These
// own all USER validation: ownership, sizes, ranges and window relationships.
// Common errors: InvalidHandle (unknown hwnd), AccessDenied (window owned by
// another process).
// ---------------------------------------------------------------------------

// The calling thread's process. Errors: NoCurrentThread, NoCurrentProcess.
kstd::expected<KProcessObject *, KernelError> caller_process() noexcept;

// Errors: InvalidArgument (empty name), NameTooLong, ResourceLimitExceeded.
KernelError register_class(KProcessObject &caller, const kstd::string_view &name, uint32_t wndProc,
                           uint32_t background) noexcept;

struct CreateWindowParams {
  kstd::string_view className;
  kstd::string_view title;
  uint32_t style{0};
  uint32_t parent{0};
  int32_t x{0};
  int32_t y{0};
  int32_t width{0};
  int32_t height{0};
  uint32_t userData{0};
};
// Returns the new hwnd. Errors: InvalidSize (extent, title too long),
// NameTooLong (class name), ParentMismatch (child style vs parent handle),
// InvalidHandle/AccessDenied (parent), OutOfRange (placement), NotFound
// (class not registered by the caller), OutOfMemory, QueueFull,
// ResourceLimitExceeded (window table full), NoCurrentThread.
kstd::expected<uint32_t, KernelError> create_window(KProcessObject &caller, const CreateWindowParams &params) noexcept;
KernelError destroy_window(KProcessObject &caller, uint32_t hwnd) noexcept;
// See show_command_locked.
KernelError show_window(KProcessObject &caller, uint32_t hwnd, uint32_t command) noexcept;
// Placement is relative to the parent client area. See placement_rect_locked
// and move_window_locked.
KernelError move_window(KProcessObject &caller, uint32_t hwnd, int32_t x, int32_t y, int32_t width,
                        int32_t height) noexcept;
// Any process may query geometry. Errors: InvalidHandle.
kstd::expected<URect, KernelError> get_window_rect(uint32_t hwnd) noexcept;
kstd::expected<URect, KernelError> get_client_rect(uint32_t hwnd) noexcept;
// `clientRect` null invalidates the whole client area. Errors:
// InvalidArgument (inverted rectangle).
KernelError invalidate_rect(KProcessObject &caller, uint32_t hwnd, const URect *clientRect) noexcept;
// See begin_paint_locked / end_paint_locked.
kstd::expected<PaintBegin, KernelError> begin_paint(KProcessObject &caller, uint32_t hwnd) noexcept;
KernelError end_paint(KProcessObject &caller, uint32_t hwnd, HDC dc) noexcept;
// hwnd 0 posts to the calling thread's queue. Errors: InvalidState (target
// window has no queue), QueueFull, plus queue_for_current_locked errors.
KernelError post_message(uint32_t hwnd, uint32_t message, uint32_t wParam, uint32_t lParam) noexcept;
// `index` is a WindowLongIndex. Errors: InvalidArgument (unknown index).
kstd::expected<uint32_t, KernelError> get_window_long(uint32_t hwnd, uint32_t index) noexcept;
// Returns the previous value. Errors: InvalidArgument (unknown or read-only
// index, or toggling the child style), OutOfRange (a descendant would leave
// the coordinate space after a frame change).
kstd::expected<uint32_t, KernelError> set_window_long(KProcessObject &caller, uint32_t hwnd, uint32_t index,
                                                      uint32_t value) noexcept;
// Errors: InvalidState (window hidden or minimized).
KernelError set_capture(KProcessObject &caller, uint32_t hwnd) noexcept;
// Releases the capture only when it is held by one of the caller's windows.
KernelError release_capture(KProcessObject &caller) noexcept;
// hwnd 0 clears focus; fails with AccessDenied while another process's
// window is focused. Otherwise see set_focus_locked.
KernelError set_focus(KProcessObject &caller, uint32_t hwnd) noexcept;
// Errors: InvalidSize (text longer than the title capacity).
KernelError set_window_text(KProcessObject &caller, uint32_t hwnd, const kstd::string_view &text) noexcept;

}// namespace kernel::user
