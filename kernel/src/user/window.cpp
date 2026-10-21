#include "user/user.hpp"

#include <kernel/kobject/KProcessObject.hpp>
#include <kernel/kobject/KThreadObject.hpp>
#include <kernel/mutex.hpp>
#include <undos/syscalls/__enum_WindowLongIndex.h>

namespace kernel::user {
namespace {
constexpr uint32_t kIndexOffset = 1;
constexpr uint32_t kFirstGeneration = 1;
constexpr uint32_t kActiveWindowActivation = 1;
constexpr uint32_t kCycleIndexStep = 1;
constexpr int32_t kNextGridPositionOffset = 1;
constexpr uint32_t kHandleIndexMask = 0xFFFF;
constexpr uint32_t kHandleFieldShift = 16;
constexpr uint32_t kPackedFieldMask = 0xFFFF;
constexpr uint32_t kPackedFieldShift = 16;
constexpr int32_t kMinWindowCoordinate = -32768;
constexpr uint32_t pack_fields(int32_t high, int32_t low) noexcept {
  return ((static_cast<uint32_t>(high) & kPackedFieldMask) << kPackedFieldShift) |
         (static_cast<uint32_t>(low) & kPackedFieldMask);
}
constexpr int32_t kMaxWindowCoordinate = 32767;

constexpr uint32_t kShowCommandHide = 0;
constexpr uint32_t kShowCommandShow = 1;
constexpr uint32_t kShowCommandMinimize = 2;
constexpr uint32_t kShowCommandRestore = 4;

constexpr uint8_t kPendingGeometryMove = 1;
constexpr uint8_t kPendingGeometrySize = 2;
constexpr uint8_t kPendingGeometryMoveAndSize = kPendingGeometryMove | kPendingGeometrySize;

constexpr int32_t kChildIconWidth = 88;
constexpr int32_t kRootIconWidth = 144;
constexpr int32_t kChildIconHeight = 56;
constexpr int32_t kFrameBorderTotal = 2 * kBorderWidth;

Mutex g_lock;

// The handle combines a generation with the one-based window-table index.
kstd::array<KObjectPtr<KWindowObject>, kMaxWindows> g_windows;
kstd::array<uint16_t, kMaxWindows> g_generation;
kstd::array<WindowClass, kMaxClasses> g_classes;
KWindowObject *g_top{nullptr};
uint32_t g_capture{0};
uint32_t g_activeHwnd{0};
uint32_t g_focusHwnd{0};

void unlink_z(KWindowObject &w) noexcept {
  if (w.above) {
    w.above->below = w.below;
  } else if (g_top == &w) {
    g_top = w.below;
  }

  if (w.below) {
    w.below->above = w.above;
  }
  w.above = w.below = nullptr;
}

void link_top(KWindowObject &w) noexcept {
  w.above = nullptr;
  w.below = g_top;
  if (g_top) g_top->above = &w;
  g_top = &w;
}

KWindowObject *root(KWindowObject &w) noexcept {
  auto *r = &w;
  while (r->parent) {
    auto *p = lookup_locked(r->parent);
    if (!p) {
      break;
    }
    r = p;
  }
  return r;
}

// Each subtree is contiguous, children ahead of their parent. Raising a
// child never crosses a different root's subtree.
void raise_subtree(KWindowObject &w) noexcept {
  KWindowObject *members[decltype(g_windows)::CONST_SIZE];

  size_t count = 0;
  for (auto *it = g_top; it; it = it->below) {
    if (it == &w || descendant_locked(*it, w)) members[count++] = it;
  }
  for (size_t i = 0; i < count; ++i) {
    unlink_z(*members[i]);
  }

  auto *p = lookup_locked(w.parent);
  auto *before = g_top;

  if (p) {
    before = p;
    for (auto *it = g_top; it; it = it->below) {
      if (descendant_locked(*it, *p)) {
        before = it;
        break;
      }
    }
  }

  if (!before) {
    for (size_t i = count; i > 0;) link_top(*members[--i]);
    return;
  }

  for (size_t i = 0; i < count; ++i) {
    auto &it = *members[i];
    it.above = before->above;
    it.below = before;
    if (it.above) {
      it.above->below = &it;
    } else {
      g_top = &it;
    }
    before->above = &it;
  }
}

void raise_branch(KWindowObject &w) noexcept {
  if (auto *p = lookup_locked(w.parent)) raise_branch(*p);
  raise_subtree(w);
}

void clear_input_for(KWindowObject &w) noexcept {
  if (const auto *c = lookup_locked(g_capture); c && (c == &w || descendant_locked(*c, w))) set_capture_locked(0);
  if (const auto *f = lookup_locked(g_focusHwnd); f && (f == &w || descendant_locked(*f, w))) (void) set_focus_locked(0);
  if (const auto *a = lookup_locked(g_activeHwnd); a && (a == &w || descendant_locked(*a, w))) set_active_window_locked(0);
}

void choose_active() noexcept {
  if (g_activeHwnd) return;
  for (const auto *w = g_top; w; w = w->below) {
    if (effectively_visible_locked(*w) && w->state != KWindowObject::State::Minimized) {
      (void) set_focus_locked(w->hwnd);
      return;
    }
  }
}
}// namespace

void lock() noexcept { g_lock.lock(); }
void unlock() noexcept { g_lock.unlock(); }

KWindowObject *lookup_locked(uint32_t hwnd) noexcept {
  const uint32_t index = (hwnd & kHandleIndexMask) - kIndexOffset;
  if (index >= kMaxWindows) return nullptr;
  if (g_generation[index] != (hwnd >> kHandleFieldShift) || !g_windows[index]) return nullptr;
  return g_windows[index].get();
}

KWindowObject *topmost_locked() noexcept { return g_top; }

KWindowObject *active_window_locked() noexcept {
  return lookup_locked(g_activeHwnd);
}

bool descendant_locked(const KWindowObject &window, const KWindowObject &ancestor) noexcept {
  const auto *w = lookup_locked(window.parent);
  for (uint32_t i = 0; w && i < kMaxWindows; ++i) {
    if (w == &ancestor) return true;
    w = lookup_locked(w->parent);
  }
  return false;
}

bool effectively_visible_locked(const KWindowObject &window) noexcept {
  if (!window.Visible()) return false;
  const auto *w = lookup_locked(window.parent);
  for (uint32_t i = 0; w && i < kMaxWindows; ++i) {
    if (!w->Visible() || w->state == KWindowObject::State::Minimized) return false;
    w = lookup_locked(w->parent);
  }
  return true;
}

URect clipped_rect_locked(const KWindowObject &window) noexcept {
  if (!effectively_visible_locked(window)) return {};
  URect rect = window.rect.Intersect(screen_rect());
  const auto *p = lookup_locked(window.parent);

  for (uint32_t i = 0; p && i < decltype(g_windows)::CONST_SIZE; ++i) {
    rect = rect.Intersect(client_to_screen_locked(*p, client_rect_locked(*p)));
    p = lookup_locked(p->parent);
  }
  return rect;
}

kstd::expected<URect, kstd::error_code> placement_rect_locked(const KWindowObject &window, int32_t x, int32_t y,
                                                         int32_t width, int32_t height) noexcept {
  if (width <= 0 || height <= 0) {
    return kstd::unexpected(KernelError::InvalidSize);
  }

  int64_t left = x, top = y;

  if (window.parent) {
    const auto *p = lookup_locked(window.parent);

    if (!p) {
      return kstd::unexpected(KernelError::InvalidHandle);
    }

    const auto origin = client_to_screen_locked(*p, {});
    left += origin.left;
    top += origin.top;
  }

  const auto right = left + width, bottom = top + height;

  // Bound dimensions and coordinates so all later frame/client differences
  // and packed message coordinates remain representable.
  if (left < kMinWindowCoordinate || top < kMinWindowCoordinate ||
      right > kMaxWindowCoordinate || bottom > kMaxWindowCoordinate ||
      width > kMaxWindowCoordinate || height > kMaxWindowCoordinate) {
    return kstd::unexpected(KernelError::OutOfRange);
  }

  return URect{
      .left = static_cast<int32_t>(left),
      .top = static_cast<int32_t>(top),
      .right = static_cast<int32_t>(right),
      .bottom = static_cast<int32_t>(bottom)};
}

KWindowObject *focused_window_locked() noexcept { return lookup_locked(g_focusHwnd); }

kstd::error_code set_focus_locked(uint32_t hwnd) noexcept {
  auto *w = lookup_locked(hwnd);

  if (hwnd && !w) return KernelError::InvalidHandle;
  if (w && (!effectively_visible_locked(*w) ||
            (w->state == KWindowObject::State::Minimized && !w->parent))) return KernelError::InvalidState;

  const uint32_t oldHwnd = g_focusHwnd;

  if (oldHwnd == hwnd) return {};
  g_focusHwnd = hwnd;

  // Focus notifications are best effort: a full queue must not veto the
  // focus change the user already sees.
  if (auto *old = lookup_locked(oldHwnd)) {
    if (old->queue) (void) post_locked(*old->queue, oldHwnd, kMsgKillFocus, hwnd, 0);
    draw_nonclient_locked(*old);
  }

  if (w) {
    set_active_window_locked(hwnd);
    if (w->queue) {
      (void) post_locked(*w->queue, hwnd, kMsgSetFocus, oldHwnd, 0);
    }
    draw_nonclient_locked(*w);
  }

  return {};
}

void set_active_window_locked(uint32_t hwnd) noexcept {
  auto *next = lookup_locked(hwnd);
  if (hwnd && (!next || !effectively_visible_locked(*next))) return;

  if (next) {
    raise_branch(*next);
    expose_locked(root(*next)->rect);
    next = root(*next);
    hwnd = next->hwnd;
  }

  if (hwnd == g_activeHwnd) {
    return;
  }

  auto *old = active_window_locked();
  g_activeHwnd = hwnd;// Both captions must observe the new activation state.

  if (old) {
    if (old->queue) (void) post_locked(*old->queue, old->hwnd, kMsgActivate, 0, 0);
    draw_nonclient_locked(*old);
  }
  
  if (next) {
    if (next->queue) (void) post_locked(*next->queue, next->hwnd, kMsgActivate, kActiveWindowActivation, 0);
    draw_nonclient_locked(*next);
  }
}

KWindowObject *window_from_point_locked(int32_t x, int32_t y) noexcept {
  for (KWindowObject *w = g_top; w; w = w->below) {
    const auto r = clipped_rect_locked(*w);
    if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return w;
  }
  return nullptr;
}

KWindowObject *mouse_target_locked(int32_t x, int32_t y) noexcept {
  if (auto *window = lookup_locked(g_capture); window && effectively_visible_locked(*window)) return window;
  return window_from_point_locked(x, y);
}

int32_t find_class_locked(const kstd::string_view &name, const KProcessObject *process) noexcept {
  for (uint32_t i = 0; i < kMaxClasses; ++i) {
    if (g_classes[i].used && (!process || g_classes[i].process == process) &&
        g_classes[i].name == name) return static_cast<int32_t>(i);
  }
  return -1;
}

WindowClass *class_at_locked(uint32_t index) noexcept {
  return index < kMaxClasses && g_classes[index].used ? &g_classes[index] : nullptr;
}

/**
 * Registers a window class for the specified process.
 *
 * This function attempts to register a window class by associating the
 * provided name, procedure, and background with the given process. If a
 * class with the same name is already registered for the process, it reuses
 * that class slot. If no available slot exists in the class registry, the
 * function fails.
 *
 * Preconditions:
 * - The caller must ensure thread-safety when invoking this function.
 * - The `KProcessObject` and `kstd::string_view` references must remain valid
 *   during execution.
 *
 * Postconditions:
 * - On Success the window class is registered.
 * - ResourceLimitExceeded means no class slot was available.
 *
 * @param proc       The process object to associate the window class with.
 * @param name       The name of the window class to register.
 * @param wndProc    The window procedure to associate with the window class.
 * @param background The background identifier for the window class.
 * @return           Success, or ResourceLimitExceeded when the table is full.
 */
kstd::error_code register_class_locked(KProcessObject &proc, const kstd::string_view &name, uint32_t wndProc, uint32_t background) noexcept {
  auto index = find_class_locked(name, &proc);

  if (index < 0) {
    for (uint32_t i = 0; i < kMaxClasses && index < 0; ++i) {
      if (!g_classes[i].used) {
        index = static_cast<int32_t>(i);
      }
    }

    if (index < 0) {
      return KernelError::ResourceLimitExceeded;
    }
  }

  auto &c = g_classes[index];
  c.used = true;
  c.name = name;
  c.wndProc = wndProc;
  c.background = background;
  c.process = &proc;

  return {};
}

kstd::expected<uint32_t, kstd::error_code> insert_window_locked(const KObjectPtr<KWindowObject> &window) noexcept {
  for (uint32_t i = 0; i < kMaxWindows; ++i) {
    if (g_windows[i]) continue;
    if (++g_generation[i] == 0) g_generation[i] = kFirstGeneration;
    g_windows[i] = window;
    window->hwnd = (static_cast<uint32_t>(g_generation[i]) << kHandleFieldShift) |
                   (i + kIndexOffset);
    link_top(*window);
    raise_branch(*window);
    if (effectively_visible_locked(*window)) {
      invalidate_locked(*window, client_rect_locked(*window));
      draw_nonclient_locked(*window);
    }
    return window->hwnd;
  }
  return kstd::unexpected(KernelError::ResourceLimitExceeded);
}

void destroy_window_locked(KWindowObject &window) noexcept {
  const uint32_t index = (window.hwnd & kHandleIndexMask) - kIndexOffset;
  if (index >= kMaxWindows || g_windows[index].get() != &window) return;
  const URect old = window.rect;
  const bool wasVisible = effectively_visible_locked(window);
  clear_input_for(window);
  window.style &= ~kStyleVisible;
  // Destroy children first; no surviving handle may name a dead parent.
  for (const auto &g_window: g_windows) {
    if (g_window && g_window->parent == window.hwnd) destroy_window_locked(*g_window);
  }
  unlink_z(window);
  if (window.queue) window.queue->ring.RemoveFor(window.hwnd);
  // A paint DC already closed by the owner is not an error during teardown.
  if (window.paintDC && window.process) (void) window.process->dcTable.Close(window.paintDC);
  const auto keep = g_windows[index];
  g_windows[index] = nullptr;
  if (wasVisible) expose_locked(old);
  choose_active();
}

kstd::error_code move_window_locked(KWindowObject &window, const URect &rect) noexcept {
  const URect old = window.rect;
  if (old == rect) return {};
  if (rect.Empty()) return KernelError::InvalidSize;
  if (rect.left < kMinWindowCoordinate || rect.top < kMinWindowCoordinate ||
      rect.right > kMaxWindowCoordinate || rect.bottom > kMaxWindowCoordinate ||
      static_cast<int64_t>(rect.right) - rect.left > kMaxWindowCoordinate ||
      static_cast<int64_t>(rect.bottom) - rect.top > kMaxWindowCoordinate) return KernelError::OutOfRange;
  const int32_t dx = rect.left - old.left, dy = rect.top - old.top;
  // Reject a move atomically if a descendant would exceed the supported range.
  for (const auto *w = g_top; w; w = w->below) {
    if (!descendant_locked(*w, window)) continue;
    if (static_cast<int64_t>(w->rect.left) + dx < kMinWindowCoordinate ||
        static_cast<int64_t>(w->rect.top) + dy < kMinWindowCoordinate ||
        static_cast<int64_t>(w->rect.right) + dx > kMaxWindowCoordinate ||
        static_cast<int64_t>(w->rect.bottom) + dy > kMaxWindowCoordinate) return KernelError::OutOfRange;
  }
  window.rect = rect;
  for (auto *w = g_top; w; w = w->below) {
    if (descendant_locked(*w, window)) {
      w->rect = w->rect.Offset(dx, dy);
      if (!w->restoredRect.Empty()) w->restoredRect = w->restoredRect.Offset(dx, dy);
    }
  }
  // Screen copies are unsafe with occlusion, ancestor clipping, cursor pixels,
  // outstanding paint DCs, and planar adapters. Repaint the damaged scene.
  expose_locked(old.Union(rect));

  if (window.queue) {
    const auto relative = window.parent ? screen_to_client_locked(*lookup_locked(window.parent), rect) : rect;
    const auto client = client_rect_locked(window);
    window.moveParam = pack_fields(relative.top, relative.left);
    window.sizeParam = pack_fields(client.bottom, client.right);
    window.pendingGeometry = kPendingGeometryMoveAndSize;
    wake_locked(*window.queue);
  }

  if (old.right - old.left != rect.right - rect.left || old.bottom - old.top != rect.bottom - rect.top) {
    const auto bounds = client_to_screen_locked(window, client_rect_locked(window));
    for (auto *w = g_top; w; w = w->below) {
      if (w->parent != window.hwnd || bounds.Empty()) {
        continue;
      }

      // Children were range-checked with the parent move above; refitting
      // within the parent's client area cannot leave the coordinate space.
      if (w->state == KWindowObject::State::Maximized) {
        (void) move_window_locked(*w, bounds);
      } else if (w->state == KWindowObject::State::Minimized) {
        const auto iconWidth = w->parent ? kChildIconWidth : kRootIconWidth;
        const auto iconHeight = w->parent ? kChildIconHeight : kCaptionHeight + kFrameBorderTotal;
        const auto width = bounds.right - bounds.left < iconWidth ? bounds.right - bounds.left : iconWidth;

        (void) move_window_locked(*w, {.left = bounds.left,
                                .top = bounds.bottom - iconHeight,
                                .right = bounds.left + width,
                                .bottom = bounds.bottom});
      }
    }
  }
  return {};
}

void show_window_locked(KWindowObject &window, bool show) noexcept {
  if (show == window.Visible()) return;
  if (show) {
    window.style |= kStyleVisible;
    expose_locked(window.rect);
  } else {
    window.style &= ~kStyleVisible;
    clear_input_for(window);
    expose_locked(window.rect);
    choose_active();
  }
}

kstd::error_code show_command_locked(KWindowObject &window, uint32_t command) noexcept {
  if (command > kShowCommandRestore) return KernelError::InvalidArgument;
  if (command == kShowCommandHide || command == kShowCommandShow) {
    show_window_locked(window, command == kShowCommandShow);
    return {};
  }
  const auto next = command == kShowCommandRestore ? KWindowObject::State::Restored : command == kShowCommandMinimize ? KWindowObject::State::Minimized
                                                                                                                      : KWindowObject::State::Maximized;
  if (window.state == next) {
    show_window_locked(window, true);
    return {};
  }
  URect bounds = screen_rect();
  if (const auto *p = lookup_locked(window.parent)) bounds = client_to_screen_locked(*p, client_rect_locked(*p));
  if (bounds.Empty()) return KernelError::InvalidState;
  const auto oldState = window.state;
  const auto oldRestored = window.restoredRect;
  if (window.state == KWindowObject::State::Restored) window.restoredRect = window.rect;
  window.state = next;
  auto target = window.restoredRect;

  if (next == KWindowObject::State::Maximized) { target = bounds; }

  if (next == KWindowObject::State::Minimized) {
    const int32_t iconWidth = window.parent ? kChildIconWidth : kRootIconWidth;
    const int32_t width = bounds.right - bounds.left < iconWidth ? bounds.right - bounds.left : iconWidth;
    const int32_t height = window.parent ? kChildIconHeight : kCaptionHeight + kFrameBorderTotal;
    uint32_t icon = 0;
    for (const auto *w = g_top; w; w = w->below)
      if (w != &window && w->parent == window.parent &&
          w->state == KWindowObject::State::Minimized && w->Visible()) ++icon;
    const int32_t columns = (bounds.right - bounds.left) / width;
    const auto column = static_cast<int32_t>(icon % static_cast<uint32_t>(columns));
    const auto row = static_cast<int32_t>(icon / static_cast<uint32_t>(columns));
    target = {bounds.left + column * width, bounds.bottom - (row + kNextGridPositionOffset) * height,
              bounds.left + (column + kNextGridPositionOffset) * width, bounds.bottom - row * height};
    if (target.top < bounds.top) target = target.Offset(0, bounds.top - target.top);
  }

  if (const kstd::error_code moved = move_window_locked(window, target); moved) {
    window.state = oldState;
    window.restoredRect = oldRestored;
    return moved;
  }
  if (next == KWindowObject::State::Minimized) {
    clear_input_for(window);
  }

  show_window_locked(window, true);

  // State changes can leave the outer rectangle identical (e.g. tiny frames).
  expose_locked(window.rect);

  const auto client = client_rect_locked(window);
  window.sizeParam = pack_fields(client.bottom, client.right);
  window.pendingGeometry |= kPendingGeometrySize;

  if (window.queue) {
    wake_locked(*window.queue);
  }

  if (next != KWindowObject::State::Minimized) {
    (void) set_focus_locked(window.hwnd);
  } else {
    choose_active();
  }
  return {};
}

void cycle_window_locked(bool children, bool reverse) noexcept {
  const auto *focus = focused_window_locked();
  const auto *active = active_window_locked();
  KWindowObject *list[kMaxWindows];
  uint32_t count = 0, current = 0;
  bool found = false;
  // Stable slot order prevents repeated cycling from just toggling the two
  // most recently raised windows.
  for (const auto &entry: g_windows) {
    auto *w = entry.get();
    if (!w) continue;
    if (!effectively_visible_locked(*w) || (w->state == KWindowObject::State::Minimized && !children)) continue;
    if (children ? (!active || w->parent != active->hwnd) : w->parent != 0) continue;
    if (w == (children ? focus : active)) {
      current = count;
      found = true;
    }
    list[count++] = w;
  }
  if (count) {
    const uint32_t index = found ? (current + (reverse ? count - kCycleIndexStep : kCycleIndexStep)) % count : (reverse ? count - kCycleIndexStep : 0);
    (void) set_focus_locked(list[index]->hwnd);
  }
}

void set_capture_locked(uint32_t hwnd) noexcept {
  if (hwnd == g_capture) return;
  const auto old = g_capture;
  g_capture = hwnd;
  if (const auto *w = lookup_locked(old); w && w->queue)
    (void) post_locked(*w->queue, old, kMsgCaptureChanged, 0, hwnd);
}
uint32_t capture_locked() noexcept { return g_capture; }

void display_changed_locked(uint32_t bpp) noexcept {
  const auto bounds = screen_rect();
  // Refit roots bottom-to-top so parent moves update descendants first.
  auto *bottom = g_top;
  while (bottom && bottom->below) bottom = bottom->below;
  for (auto *w = bottom; w; w = w->above) {
    if (w->parent || bounds.Empty()) continue;
    const auto fit = [&](const URect &rect) {
      const auto width = rect.right - rect.left < bounds.right ? rect.right - rect.left : bounds.right;
      const auto height = rect.bottom - rect.top < bounds.bottom ? rect.bottom - rect.top : bounds.bottom;
      const auto left = rect.left < 0 ? 0 : rect.left > bounds.right - width ? bounds.right - width
                                                                             : rect.left;
      const auto top = rect.top < 0 ? 0 : rect.top > bounds.bottom - height ? bounds.bottom - height
                                                                            : rect.top;
      return URect{.left = left, .top = top, .right = left + width, .bottom = top + height};
    };
    if (!w->restoredRect.Empty()) w->restoredRect = fit(w->restoredRect);
    auto target = w->state == KWindowObject::State::Maximized ? bounds : fit(w->rect);
    if (w->state == KWindowObject::State::Minimized) target = target.Offset(0, bounds.bottom - target.bottom);
    if (const kstd::error_code moved = move_window_locked(*w, target); moved)
      KE_Print("USER: could not refit window {} after display change: {}", w->hwnd, moved.message());
    if (w->state == KWindowObject::State::Restored) w->restoredRect = w->rect;
  }
  for (auto *w = g_top; w; w = w->below) {
    w->invalid = client_rect_locked(*w);
    if (w->paintDC && w->process) {
      if (auto *slot = w->process->dcTable.Resolve(w->paintDC)) {
        slot->lock.lock();
        slot->presentationDirty = false;
        slot->presentationBounds = {};
        slot->lock.unlock();
      }
    }
    w->pendingDisplayChange = true;
    w->displayBpp = bpp;
    w->displaySize = (static_cast<uint32_t>(bounds.bottom) << kPackedFieldShift) |
                     static_cast<uint32_t>(bounds.right);
    if (w->queue) wake_locked(*w->queue);
  }
  // Also refresh active paint clips when output became inactive.
  expose_locked(bounds);
}

void on_process_exit(KProcessObject &proc) noexcept {
  Guard guard;
  for (const auto &g_window: g_windows) {
    if (g_window && g_window->process == &proc) destroy_window_locked(*g_window);
  }
  for (auto &c: g_classes) {
    if (c.used && c.process == &proc) c = WindowClass{};
  }
  free_queues_locked(proc);
}

namespace {
// Resolves a window the caller may modify.
kstd::expected<KWindowObject *, kstd::error_code> owned_window_locked(const KProcessObject &caller, uint32_t hwnd) noexcept {
  auto *w = lookup_locked(hwnd);
  if (!w) return kstd::unexpected(KernelError::InvalidHandle);
  if (w->process != &caller) return kstd::unexpected(KernelError::AccessDenied);
  return w;
}
}// namespace

kstd::expected<KProcessObject *, kstd::error_code> caller_process() noexcept {
  const auto thread = KE_SCHED_GetCurrentThread();
  if (!thread) return kstd::unexpected(KernelError::NoCurrentThread);
  if (!thread->owner_process) return kstd::unexpected(KernelError::NoCurrentProcess);
  return thread->owner_process;
}

kstd::error_code register_class(KProcessObject &caller, const kstd::string_view &name, uint32_t wndProc,
                           uint32_t background) noexcept {
  if (name.empty()) return KernelError::InvalidArgument;
  if (name.size() >= kWindowClassNameLen) return KernelError::NameTooLong;
  Guard guard;
  return register_class_locked(caller, name, wndProc, background);
}

kstd::expected<uint32_t, kstd::error_code> create_window(KProcessObject &caller, const CreateWindowParams &params) noexcept {
  if (params.width <= 0 || params.height <= 0) return kstd::unexpected(KernelError::InvalidSize);
  if (params.className.size() >= kWindowClassNameLen) return kstd::unexpected(KernelError::NameTooLong);
  if (params.title.size() >= kWindowTitleLen) return kstd::unexpected(KernelError::InvalidSize);
  const bool child = (params.style & kStyleChild) != 0;
  if (child != (params.parent != 0)) return kstd::unexpected(KernelError::ParentMismatch);

  // Allocate outside the lock; only the table insert happens under it.
  auto created = CreateKObject<KWindowObject>("window");
  if (!created) return kstd::unexpected(created.error());
  const auto window = kstd::move(*created);
  window->title = params.title;
  window->style = params.style;
  window->parent = params.parent;
  window->userData = params.userData;
  window->process = &caller;

  Guard guard;
  if (child) {
    if (const auto parent = owned_window_locked(caller, params.parent); !parent) return kstd::unexpected(parent.error());
  }

  const auto placement = placement_rect_locked(*window, params.x, params.y, params.width, params.height);
  if (!placement) return kstd::unexpected(placement.error());
  window->rect = *placement;

  const int32_t cls = find_class_locked(params.className, &caller);
  if (cls < 0) return kstd::unexpected(KernelError::NotFound);
  const auto *wc = class_at_locked(static_cast<uint32_t>(cls));

  window->classIndex = static_cast<uint32_t>(cls);
  window->wndProc = wc->wndProc;
  window->background = wc->background;
  const auto queue = queue_for_current_locked(true);
  if (!queue) return kstd::unexpected(queue.error());
  // The WM_CREATE posted below must not be dropped once the window exists.
  if ((*queue)->ring.Full()) return kstd::unexpected(KernelError::QueueFull);
  window->queue = *queue;

  const auto hwnd = insert_window_locked(window);
  if (!hwnd) return kstd::unexpected(hwnd.error());

  (void) post_locked(*window->queue, *hwnd, kMsgCreate, 0, 0);
  return *hwnd;
}

kstd::error_code destroy_window(KProcessObject &caller, uint32_t hwnd) noexcept {
  Guard guard;
  const auto w = owned_window_locked(caller, hwnd);
  if (!w) return w.error();
  destroy_window_locked(**w);
  return {};
}

kstd::error_code show_window(KProcessObject &caller, uint32_t hwnd, uint32_t command) noexcept {
  Guard guard;
  const auto w = owned_window_locked(caller, hwnd);
  if (!w) return w.error();
  return show_command_locked(**w, command);
}

kstd::error_code move_window(KProcessObject &caller, uint32_t hwnd, int32_t x, int32_t y, int32_t width,
                        int32_t height) noexcept {
  if (width <= 0 || height <= 0) return KernelError::InvalidSize;
  Guard guard;
  const auto w = owned_window_locked(caller, hwnd);
  if (!w) return w.error();
  KWindowObject &window = **w;
  const auto rect = placement_rect_locked(window, x, y, width, height);
  if (!rect) return rect.error();
  if (const kstd::error_code moved = move_window_locked(window, *rect); moved) return moved;
  if (window.state == KWindowObject::State::Restored) window.restoredRect = *rect;
  return {};
}

kstd::expected<URect, kstd::error_code> get_window_rect(uint32_t hwnd) noexcept {
  Guard guard;
  const auto *w = lookup_locked(hwnd);
  if (!w) return kstd::unexpected(KernelError::InvalidHandle);
  return w->rect;
}

kstd::expected<URect, kstd::error_code> get_client_rect(uint32_t hwnd) noexcept {
  Guard guard;
  const auto *w = lookup_locked(hwnd);
  if (!w) return kstd::unexpected(KernelError::InvalidHandle);
  return client_rect_locked(*w);
}

kstd::error_code invalidate_rect(KProcessObject &caller, uint32_t hwnd, const URect *clientRect) noexcept {
  if (clientRect && (clientRect->right < clientRect->left || clientRect->bottom < clientRect->top))
    return KernelError::InvalidArgument;
  Guard guard;
  const auto w = owned_window_locked(caller, hwnd);
  if (!w) return w.error();
  invalidate_locked(**w, clientRect ? *clientRect : client_rect_locked(**w));
  return {};
}

kstd::expected<PaintBegin, kstd::error_code> begin_paint(KProcessObject &caller, uint32_t hwnd) noexcept {
  Guard guard;
  const auto w = owned_window_locked(caller, hwnd);
  if (!w) return kstd::unexpected(w.error());
  return begin_paint_locked(caller, **w);
}

kstd::error_code end_paint(KProcessObject &caller, uint32_t hwnd, HDC dc) noexcept {
  // The area was validated by BeginPaint; the painted region is flushed to
  // the hardware here, once per paint cycle, and only the DC is released.
  Guard guard;
  const auto w = owned_window_locked(caller, hwnd);
  if (!w) return w.error();
  return end_paint_locked(caller, **w, dc);
}

kstd::expected<uint32_t, kstd::error_code> get_window_long(uint32_t hwnd, uint32_t index) noexcept {
  Guard guard;
  const auto *w = lookup_locked(hwnd);
  if (!w) return kstd::unexpected(KernelError::InvalidHandle);
  switch (index) {
    case WindowLongIndex_WndProc: return w->wndProc;
    case WindowLongIndex_UserData: return w->userData;
    case WindowLongIndex_Style: return w->style;
    case WindowLongIndex_Parent: return w->parent;
    default: return kstd::unexpected(KernelError::InvalidArgument);
  }
}

namespace {
// Applies a new style, shifting descendants with the client origin. The
// visible bit is applied through show_window_locked so input state and
// exposure stay consistent.
kstd::expected<uint32_t, kstd::error_code> set_style_locked(KWindowObject &w, uint32_t value) noexcept {
  if ((value ^ w.style) & kStyleChild) return kstd::unexpected(KernelError::InvalidArgument);
  const int32_t borderDelta = ((value & kStyleBorder) ? kBorderWidth : 0) -
                              ((w.style & kStyleBorder) ? kBorderWidth : 0);
  const int32_t captionDelta = ((value & kStyleCaption) ? kCaptionHeight : 0) -
                               ((w.style & kStyleCaption) ? kCaptionHeight : 0);
  for (const auto *child = g_top; child; child = child->below) {
    if (!descendant_locked(*child, w)) continue;
    if (child->rect.left + borderDelta < kMinWindowCoordinate || child->rect.right + borderDelta > kMaxWindowCoordinate ||
        child->rect.top + borderDelta + captionDelta < kMinWindowCoordinate ||
        child->rect.bottom + borderDelta + captionDelta > kMaxWindowCoordinate)
      return kstd::unexpected(KernelError::OutOfRange);
  }
  const uint32_t previous = w.style;
  const auto oldOrigin = client_to_screen_locked(w, {});
  const bool show = (value & kStyleVisible) != 0;
  w.style = (value & ~kStyleVisible) | (w.style & kStyleVisible);
  const auto newOrigin = client_to_screen_locked(w, {});
  const int32_t dx = newOrigin.left - oldOrigin.left, dy = newOrigin.top - oldOrigin.top;
  for (auto *child = g_top; child; child = child->below) {
    if (!descendant_locked(*child, w)) continue;
    child->rect = child->rect.Offset(dx, dy);
    if (!child->restoredRect.Empty()) child->restoredRect = child->restoredRect.Offset(dx, dy);
  }
  show_window_locked(w, show);
  expose_locked(w.rect);
  return previous;
}
}// namespace

kstd::expected<uint32_t, kstd::error_code> set_window_long(KProcessObject &caller, uint32_t hwnd, uint32_t index,
                                                      uint32_t value) noexcept {
  Guard guard;
  const auto owned = owned_window_locked(caller, hwnd);
  if (!owned) return kstd::unexpected(owned.error());
  KWindowObject &w = **owned;
  uint32_t previous = 0;
  switch (index) {
    case WindowLongIndex_WndProc:
      previous = w.wndProc;
      w.wndProc = value;
      return previous;
    case WindowLongIndex_UserData:
      previous = w.userData;
      w.userData = value;
      return previous;
    case WindowLongIndex_Style:
      return set_style_locked(w, value);
    default:// WindowLongIndex_Parent is read-only.
      return kstd::unexpected(KernelError::InvalidArgument);
  }
}

kstd::error_code set_capture(KProcessObject &caller, uint32_t hwnd) noexcept {
  Guard guard;
  const auto w = owned_window_locked(caller, hwnd);
  if (!w) return w.error();
  if (!effectively_visible_locked(**w) || (*w)->state == KWindowObject::State::Minimized) return KernelError::InvalidState;
  set_capture_locked(hwnd);
  return {};
}

kstd::error_code release_capture(KProcessObject &caller) noexcept {
  Guard guard;
  if (const auto *w = lookup_locked(g_capture); w && w->process == &caller) set_capture_locked(0);
  return {};
}

kstd::error_code set_focus(KProcessObject &caller, uint32_t hwnd) noexcept {
  Guard guard;
  if (hwnd) {
    if (const auto w = owned_window_locked(caller, hwnd); !w) return w.error();
  } else if (const auto *focused = focused_window_locked(); focused && focused->process != &caller) {
    return KernelError::AccessDenied;
  }
  return set_focus_locked(hwnd);
}

kstd::error_code set_window_text(KProcessObject &caller, uint32_t hwnd, const kstd::string_view &text) noexcept {
  if (text.size() >= kWindowTitleLen) return KernelError::InvalidSize;
  Guard guard;
  const auto w = owned_window_locked(caller, hwnd);
  if (!w) return w.error();
  (*w)->title = text;
  draw_nonclient_locked(**w);
  return {};
}

}// namespace kernel::user
