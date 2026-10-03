#pragma once

#include "stdkrn.hpp"

#include <kernel/kobject/KDispatcherHeader.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/kobject/ObjectType.hpp>

#include "user/msgring.hpp"
#include "user/region.hpp"

namespace kernel {
struct KProcessObject;
struct KThreadObject;
}// namespace kernel

namespace kernel::user {

constexpr uint32_t kMaxWindows = 64;
constexpr uint32_t kMaxClasses = 16;
constexpr uint32_t kQueueSize = 64;
constexpr uint32_t kNameLen = 32;
constexpr uint32_t kTitleLen = 64;

// Mirrors of the WindowMessage/WindowStyle IDL values (tools/usersyscall.txt).
constexpr uint32_t kMsgCreate = 0x0001;
constexpr uint32_t kMsgDestroy = 0x0002;
constexpr uint32_t kMsgMove = 0x0003;
constexpr uint32_t kMsgSize = 0x0005;
constexpr uint32_t kMsgActivate = 0x0006;
constexpr uint32_t kMsgPaint = 0x000F;
constexpr uint32_t kMsgClose = 0x0010;
constexpr uint32_t kMsgKeyDown = 0x0100;
constexpr uint32_t kMsgKeyUp = 0x0101;
constexpr uint32_t kMsgChar = 0x0102;
constexpr uint32_t kMsgMouseMove = 0x0200;
constexpr uint32_t kMsgLButtonDown = 0x0201;
constexpr uint32_t kMsgLButtonUp = 0x0202;
constexpr uint32_t kMsgRButtonDown = 0x0204;
constexpr uint32_t kMsgRButtonUp = 0x0205;

constexpr uint32_t kStyleVisible = 0x00000001;
constexpr uint32_t kStyleCaption = 0x00000002;
constexpr uint32_t kStyleBorder = 0x00000004;
constexpr uint32_t kStyleChild = 0x00000008;

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
constexpr uint32_t kActiveCaptionColor = 9;   // Blue
constexpr uint32_t kInactiveCaptionColor = 8; // Grey
constexpr uint32_t kCaptionTextColor = 15;    // White
constexpr uint32_t kWindowFrameColor = 0;     // Black
constexpr uint32_t kWindowFaceColor = 7;      // Light grey

using Ring = MessageRing<kQueueSize, kMsgMouseMove>;

// One per thread that owns windows or pumps messages; one allocation per
// thread, none per message.
struct MessageQueue {
  KThreadObject *thread{nullptr};
  KProcessObject *process{nullptr};
  Ring ring;
  KDispatcherHeader header;// manual reset: signalled while work is pending
  MessageQueue *next{nullptr};
};

struct WindowClass {
  bool used{false};
  char name[kNameLen]{};
  uint32_t wndProc{0};
  uint32_t background{0};
  KProcessObject *process{nullptr};
};

struct KWindowObject : KObjectT<KWindowObject, 1, TYPE_WINDOW> {
  using KObjectT::KObjectT;

  uint32_t hwnd{0};
  uint32_t classIndex{0};
  char title[kTitleLen]{};
  uint32_t style{0};
  URect rect{};// screen coordinates
  uint32_t parent{0};
  uint32_t userData{0};
  uint32_t wndProc{0};
  uint32_t background{0};

  KProcessObject *process{nullptr};
  MessageQueue *queue{nullptr};

  URect invalid{};// window coordinates
  URegion paintClip;// screen coordinates, referenced by the BeginPaint DC

  // z-order, topmost first; the window table holds the reference.
  KWindowObject *above{nullptr};
  KWindowObject *below{nullptr};

  [[nodiscard]] bool Visible() const noexcept { return (style & kStyleVisible) != 0; }
};

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
KWindowObject *lookup_locked(uint32_t hwnd) noexcept;
KWindowObject *topmost_locked() noexcept;
KWindowObject *active_window_locked() noexcept;
void set_active_window_locked(uint32_t hwnd) noexcept;
KWindowObject *window_from_point_locked(int32_t x, int32_t y) noexcept;
int32_t find_class_locked(const char *name) noexcept;
WindowClass *class_at_locked(uint32_t index) noexcept;
bool register_class_locked(KProcessObject &proc, const char *name, uint32_t wndProc, uint32_t background) noexcept;
uint32_t insert_window_locked(const KObjectPtr<KWindowObject> &window) noexcept;
void destroy_window_locked(KWindowObject &window) noexcept;
void move_window_locked(KWindowObject &window, const URect &rect) noexcept;
void show_window_locked(KWindowObject &window, bool show) noexcept;
void set_capture_locked(uint32_t hwnd) noexcept;
uint32_t capture_locked() noexcept;

// paint.cpp: fill a screen rectangle through the adapter (planar-safe), clipped to the screen.
void fill_screen_locked(const URect &r, uint32_t color) noexcept;

// nonclient.cpp (callers hold the lock)
URect client_to_screen_locked(const KWindowObject &window, const URect &clientRect) noexcept;
URect screen_to_client_locked(const KWindowObject &window, const URect &screenRect) noexcept;
HitTest hit_test_locked(const KWindowObject &window, int32_t x, int32_t y) noexcept;
void draw_nonclient_locked(KWindowObject &window) noexcept;

// rawinput.cpp
void init_raw_input() noexcept;

// msgqueue.cpp
MessageQueue *queue_for_current_locked(bool create) noexcept;
bool post_locked(MessageQueue &queue, uint32_t hwnd, uint32_t message, uint32_t wParam, uint32_t lParam) noexcept;
void wake_locked(MessageQueue &queue) noexcept;
// Returns false when nothing is available and block is false.
bool get_message(QueuedMessage &out, bool block, bool remove) noexcept;
void free_queues_locked(KProcessObject &proc) noexcept;

// paint.cpp (callers hold the lock)
URect screen_rect() noexcept;
void invalidate_locked(KWindowObject &window, const URect &windowRect) noexcept;
void expose_locked(const URect &screenRect) noexcept;
void visible_region_locked(const KWindowObject &window, URegion &out) noexcept;
bool begin_paint_locked(KProcessObject &proc, KWindowObject &window, uint32_t &hdc, URect &paint) noexcept;
// Hands the damaged screen rectangles to the adapter; one call per paint cycle.
void present_locked(const URegion &region) noexcept;
void present_locked(const URect &r) noexcept;

// Destroys every window, class and queue owned by proc.
void on_process_exit(KProcessObject &proc) noexcept;

}// namespace kernel::user
