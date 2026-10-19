#include "user/user.hpp"

#include <kernel/mutex.hpp>
#include <kernel/kobject/KProcessObject.hpp>

namespace kernel::user {
namespace {
Mutex g_lock;

// hwnd = (generation << 16) | (index + 1); never 0.
KObjectPtr<KWindowObject> g_windows[kMaxWindows];
uint16_t g_generation[kMaxWindows];
KWindowObject *g_top{nullptr};
WindowClass g_classes[kMaxClasses];
uint32_t g_capture{0};
uint32_t g_activeHwnd{0};
uint32_t g_focusHwnd{0};

bool name_equal(const char *a, const char *b) noexcept {
  for (uint32_t i = 0; i < kNameLen; ++i) {
    if (a[i] != b[i]) return false;
    if (a[i] == 0) return true;
  }
  return true;
}

void copy_name(char *dst, const char *src, uint32_t len) noexcept {
  uint32_t i = 0;
  for (; i + 1 < len && src[i]; ++i) dst[i] = src[i];
  dst[i] = 0;
}

void unlink_z(KWindowObject &w) noexcept {
  if (w.above) w.above->below = w.below;
  else if (g_top == &w) g_top = w.below;
  if (w.below) w.below->above = w.above;
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
  for (uint32_t i = 0; i < kMaxWindows && r->parent; ++i) {
    auto *p = lookup_locked(r->parent);
    if (!p) break;
    r = p;
  }
  return r;
}

// Each subtree is contiguous, children ahead of their parent. Raising a
// child never crosses a different root's subtree.
void raise_subtree(KWindowObject &w) noexcept {
  KWindowObject *members[kMaxWindows];
  uint32_t count = 0;
  for (auto *it = g_top; it; it = it->below) {
    if (it == &w || descendant_locked(*it, w)) members[count++] = it;
  }
  for (uint32_t i = 0; i < count; ++i) unlink_z(*members[i]);
  auto *p = lookup_locked(w.parent);
  auto *before = g_top;
  if (p) {
    before = p;
    for (auto *it = g_top; it; it = it->below) {
      if (descendant_locked(*it, *p)) { before = it; break; }
    }
  }
  if (!before) {
    for (uint32_t i = count; i > 0; --i) link_top(*members[i - 1]);
    return;
  }
  for (uint32_t i = 0; i < count; ++i) {
    auto &it = *members[i];
    it.above = before->above;
    it.below = before;
    if (it.above) it.above->below = &it;
    else g_top = &it;
    before->above = &it;
  }
}

void raise_branch(KWindowObject &w) noexcept {
  if (auto *p = lookup_locked(w.parent)) raise_branch(*p);
  raise_subtree(w);
}

void clear_input_for(KWindowObject &w) noexcept {
  if (auto *c = lookup_locked(g_capture); c && (c == &w || descendant_locked(*c, w))) set_capture_locked(0);
  if (auto *f = lookup_locked(g_focusHwnd); f && (f == &w || descendant_locked(*f, w))) set_focus_locked(0);
  if (auto *a = lookup_locked(g_activeHwnd); a && (a == &w || descendant_locked(*a, w))) set_active_window_locked(0);
}

void choose_active() noexcept {
  if (g_activeHwnd) return;
  for (auto *w = g_top; w; w = w->below) {
    if (effectively_visible_locked(*w) && w->state != 2) {
      set_focus_locked(w->hwnd);
      return;
    }
  }
}
}// namespace

void lock() noexcept { g_lock.lock(); }
void unlock() noexcept { g_lock.unlock(); }

KWindowObject *lookup_locked(uint32_t hwnd) noexcept {
  const uint32_t index = (hwnd & 0xFFFF) - 1;
  if (index >= kMaxWindows) return nullptr;
  if (g_generation[index] != (hwnd >> 16) || !g_windows[index]) return nullptr;
  return g_windows[index].get();
}

KWindowObject *topmost_locked() noexcept { return g_top; }

KWindowObject *active_window_locked() noexcept {
  return lookup_locked(g_activeHwnd);
}

bool descendant_locked(const KWindowObject &window, const KWindowObject &ancestor) noexcept {
  auto *w = lookup_locked(window.parent);
  for (uint32_t i = 0; w && i < kMaxWindows; ++i) {
    if (w == &ancestor) return true;
    w = lookup_locked(w->parent);
  }
  return false;
}

bool effectively_visible_locked(const KWindowObject &window) noexcept {
  if (!window.Visible()) return false;
  auto *w = lookup_locked(window.parent);
  for (uint32_t i = 0; w && i < kMaxWindows; ++i) {
    if (!w->Visible() || w->state == 2) return false;
    w = lookup_locked(w->parent);
  }
  return true;
}

URect clipped_rect_locked(const KWindowObject &window) noexcept {
  if (!effectively_visible_locked(window)) return {};
  URect rect = window.rect.Intersect(screen_rect());
  auto *p = lookup_locked(window.parent);
  for (uint32_t i = 0; p && i < kMaxWindows; ++i) {
    rect = rect.Intersect(client_to_screen_locked(*p, client_rect_locked(*p)));
    p = lookup_locked(p->parent);
  }
  return rect;
}

bool placement_rect_locked(const KWindowObject &window, int32_t x, int32_t y,
                           int32_t width, int32_t height, URect &out) noexcept {
  if (width <= 0 || height <= 0) return false;
  int64_t left = x, top = y;
  if (window.parent) {
    auto *p = lookup_locked(window.parent);
    if (!p) return false;
    const auto origin = client_to_screen_locked(*p, {});
    left += origin.left;
    top += origin.top;
  }
  const int64_t right = left + width, bottom = top + height;
  // Bound dimensions and coordinates so all later frame/client differences
  // and packed message coordinates remain representable.
  if (left < -32768 || top < -32768 || right > 32767 || bottom > 32767 ||
      width > 32767 || height > 32767) return false;
  out = {static_cast<int32_t>(left), static_cast<int32_t>(top),
         static_cast<int32_t>(right), static_cast<int32_t>(bottom)};
  return true;
}

KWindowObject *focused_window_locked() noexcept { return lookup_locked(g_focusHwnd); }

bool set_focus_locked(uint32_t hwnd) noexcept {
  auto *w = lookup_locked(hwnd);
  if (hwnd && (!w || !effectively_visible_locked(*w) || (w->state == 2 && !w->parent))) return false;
  const uint32_t oldHwnd = g_focusHwnd;
  if (oldHwnd == hwnd) return true;
  g_focusHwnd = hwnd;
  if (auto *old = lookup_locked(oldHwnd)) {
    if (old->queue) post_locked(*old->queue, oldHwnd, kMsgKillFocus, hwnd, 0);
    draw_nonclient_locked(*old);
  }
  if (w) {
    set_active_window_locked(hwnd);
    if (w->queue) post_locked(*w->queue, hwnd, kMsgSetFocus, oldHwnd, 0);
    draw_nonclient_locked(*w);
  }
  return true;
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
  if (hwnd == g_activeHwnd) return;
  KWindowObject *old = active_window_locked();
  g_activeHwnd = hwnd; // Both captions must observe the new activation state.
  if (old) {
    if (old->queue) post_locked(*old->queue, old->hwnd, kMsgActivate, 0, 0);
    draw_nonclient_locked(*old);
  }
  if (next) {
    if (next->queue) post_locked(*next->queue, next->hwnd, kMsgActivate, 1, 0);
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

int32_t find_class_locked(const char *name, const KProcessObject *process) noexcept {
  for (uint32_t i = 0; i < kMaxClasses; ++i) {
    if (g_classes[i].used && (!process || g_classes[i].process == process) &&
        name_equal(g_classes[i].name, name)) return static_cast<int32_t>(i);
  }
  return -1;
}

WindowClass *class_at_locked(uint32_t index) noexcept {
  return index < kMaxClasses && g_classes[index].used ? &g_classes[index] : nullptr;
}

bool register_class_locked(KProcessObject &proc, const char *name, uint32_t wndProc, uint32_t background) noexcept {
  int32_t index = find_class_locked(name, &proc);
  if (index < 0) {
    for (uint32_t i = 0; i < kMaxClasses && index < 0; ++i) {
      if (!g_classes[i].used) index = static_cast<int32_t>(i);
    }
    if (index < 0) return false;
  }
  auto &c = g_classes[index];
  c.used = true;
  copy_name(c.name, name, kNameLen);
  c.wndProc = wndProc;
  c.background = background;
  c.process = &proc;
  return true;
}

uint32_t insert_window_locked(const KObjectPtr<KWindowObject> &window) noexcept {
  for (uint32_t i = 0; i < kMaxWindows; ++i) {
    if (g_windows[i]) continue;
    if (++g_generation[i] == 0) g_generation[i] = 1;
    g_windows[i] = window;
    window->hwnd = (static_cast<uint32_t>(g_generation[i]) << 16) | (i + 1);
    link_top(*window);
    raise_branch(*window);
    if (effectively_visible_locked(*window)) {
      invalidate_locked(*window, client_rect_locked(*window));
      draw_nonclient_locked(*window);
    }
    return window->hwnd;
  }
  return 0;
}

void destroy_window_locked(KWindowObject &window) noexcept {
  const uint32_t index = (window.hwnd & 0xFFFF) - 1;
  if (index >= kMaxWindows || g_windows[index].get() != &window) return;
  const URect old = window.rect;
  const bool wasVisible = effectively_visible_locked(window);
  clear_input_for(window);
  window.style &= ~kStyleVisible;
  // Destroy children first; no surviving handle may name a dead parent.
  for (uint32_t i = 0; i < kMaxWindows; ++i) {
    if (g_windows[i] && g_windows[i]->parent == window.hwnd) destroy_window_locked(*g_windows[i]);
  }
  unlink_z(window);
  if (window.queue) window.queue->ring.RemoveFor(window.hwnd);
  if (window.paintDC && window.process) window.process->dcTable.Close(window.paintDC);
  const auto keep = g_windows[index];
  g_windows[index] = nullptr;
  if (wasVisible) expose_locked(old);
  choose_active();
}

bool move_window_locked(KWindowObject &window, const URect &rect) noexcept {
  const URect old = window.rect;
  if (old == rect) return true;
  if (rect.Empty() || rect.left < -32768 || rect.top < -32768 || rect.right > 32767 ||
      rect.bottom > 32767 || static_cast<int64_t>(rect.right) - rect.left > 32767 ||
      static_cast<int64_t>(rect.bottom) - rect.top > 32767) return false;
  const int32_t dx = rect.left - old.left, dy = rect.top - old.top;
  // Reject a move atomically if a descendant would exceed the supported range.
  for (auto *w = g_top; w; w = w->below) {
    if (!descendant_locked(*w, window)) continue;
    if (static_cast<int64_t>(w->rect.left) + dx < -32768 ||
        static_cast<int64_t>(w->rect.top) + dy < -32768 ||
        static_cast<int64_t>(w->rect.right) + dx > 32767 ||
        static_cast<int64_t>(w->rect.bottom) + dy > 32767) return false;
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
    window.moveParam = (static_cast<uint32_t>(relative.top & 0xFFFF) << 16) | (relative.left & 0xFFFF);
    window.sizeParam = (static_cast<uint32_t>(client.bottom & 0xFFFF) << 16) | (client.right & 0xFFFF);
    window.pendingGeometry = 3;
    wake_locked(*window.queue);
  }
  if (old.right - old.left != rect.right - rect.left || old.bottom - old.top != rect.bottom - rect.top) {
    const auto bounds = client_to_screen_locked(window, client_rect_locked(window));
    for (auto *w = g_top; w; w = w->below) {
      if (w->parent != window.hwnd || bounds.Empty()) continue;
      if (w->state == 3) move_window_locked(*w, bounds);
      else if (w->state == 2) {
        const int32_t iconWidth = w->parent ? 88 : 144;
        const int32_t iconHeight = w->parent ? 56 : kCaptionHeight + 2 * kBorderWidth;
        const int32_t width = bounds.right - bounds.left < iconWidth ? bounds.right - bounds.left : iconWidth;
        move_window_locked(*w, {bounds.left, bounds.bottom - iconHeight,
                                bounds.left + width, bounds.bottom});
      }
    }
  }
  return true;
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

bool show_command_locked(KWindowObject &window, uint32_t command) noexcept {
  if (command > 4) return false;
  if (command <= 1) { show_window_locked(window, command == 1); return true; }
  const uint32_t next = command == 4 ? 0 : command;
  if (window.state == next) { show_window_locked(window, true); return true; }
  URect bounds = screen_rect();
  if (auto *p = lookup_locked(window.parent)) bounds = client_to_screen_locked(*p, client_rect_locked(*p));
  if (bounds.Empty()) return false;
  const uint32_t oldState = window.state;
  const auto oldRestored = window.restoredRect;
  if (window.state == 0) window.restoredRect = window.rect;
  window.state = next;
  auto target = window.restoredRect;
  if (next == 3) target = bounds;
  if (next == 2) {
    const int32_t iconWidth = window.parent ? 88 : 144;
    const int32_t width = bounds.right - bounds.left < iconWidth ? bounds.right - bounds.left : iconWidth;
    const int32_t height = window.parent ? 56 : kCaptionHeight + 2 * kBorderWidth;
    uint32_t icon = 0;
    for (auto *w = g_top; w; w = w->below)
      if (w != &window && w->parent == window.parent && w->state == 2 && w->Visible()) ++icon;
    const int32_t columns = (bounds.right - bounds.left) / width;
    const int32_t column = static_cast<int32_t>(icon % static_cast<uint32_t>(columns));
    const int32_t row = static_cast<int32_t>(icon / static_cast<uint32_t>(columns));
    target = {bounds.left + column * width, bounds.bottom - (row + 1) * height,
              bounds.left + (column + 1) * width, bounds.bottom - row * height};
    if (target.top < bounds.top) target = target.Offset(0, bounds.top - target.top);
  }
  if (!move_window_locked(window, target)) {
    window.state = oldState;
    window.restoredRect = oldRestored;
    return false;
  }
  if (next == 2) clear_input_for(window);
  show_window_locked(window, true);
  // State changes can leave the outer rectangle identical (e.g. tiny frames).
  expose_locked(window.rect);
  const auto client = client_rect_locked(window);
  window.sizeParam = (static_cast<uint32_t>(client.bottom & 0xFFFF) << 16) | (client.right & 0xFFFF);
  window.pendingGeometry |= 2;
  if (window.queue) wake_locked(*window.queue);
  if (next != 2) set_focus_locked(window.hwnd);
  else choose_active();
  return true;
}

void cycle_window_locked(bool children, bool reverse) noexcept {
  auto *focus = focused_window_locked();
  auto *active = active_window_locked();
  KWindowObject *list[kMaxWindows];
  uint32_t count = 0, current = 0;
  bool found = false;
  // Stable slot order prevents repeated cycling from just toggling the two
  // most recently raised windows.
  for (const auto &entry : g_windows) {
    auto *w = entry.get();
    if (!w) continue;
    if (!effectively_visible_locked(*w) || (w->state == 2 && !children)) continue;
    if (children ? (!active || w->parent != active->hwnd) : w->parent != 0) continue;
    if (w == (children ? focus : active)) { current = count; found = true; }
    list[count++] = w;
  }
  if (count) {
    const uint32_t index = found ? (current + (reverse ? count - 1 : 1)) % count : (reverse ? count - 1 : 0);
    set_focus_locked(list[index]->hwnd);
  }
}

void set_capture_locked(uint32_t hwnd) noexcept {
  if (hwnd == g_capture) return;
  const auto old = g_capture;
  g_capture = hwnd;
  if (auto *w = lookup_locked(old); w && w->queue)
    post_locked(*w->queue, old, kMsgCaptureChanged, 0, hwnd);
}
uint32_t capture_locked() noexcept { return g_capture; }

void on_process_exit(KProcessObject &proc) noexcept {
  Guard guard;
  for (uint32_t i = 0; i < kMaxWindows; ++i) {
    if (g_windows[i] && g_windows[i]->process == &proc) destroy_window_locked(*g_windows[i]);
  }
  for (auto &c : g_classes) {
    if (c.used && c.process == &proc) c = WindowClass{};
  }
  free_queues_locked(proc);
}

}// namespace kernel::user
