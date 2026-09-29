#include "user/user.hpp"

#include <kernel/mutex.hpp>

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

void set_active_window_locked(uint32_t hwnd) noexcept {
  if (hwnd == g_activeHwnd) return;

  KWindowObject *old = active_window_locked();
  if (old) {
    if (old->queue) post_locked(*old->queue, old->hwnd, kMsgActivate, 0, 0);
    draw_nonclient_locked(*old);
  }

  g_activeHwnd = hwnd;

  KWindowObject *next = lookup_locked(hwnd);
  if (next) {
    unlink_z(*next);
    link_top(*next);
    if (next->queue) post_locked(*next->queue, next->hwnd, kMsgActivate, 1, 0);
    draw_nonclient_locked(*next);
    invalidate_locked(*next, {0, 0, next->rect.right - next->rect.left, next->rect.bottom - next->rect.top});
  }
}

KWindowObject *window_from_point_locked(int32_t x, int32_t y) noexcept {
  if (g_capture) return lookup_locked(g_capture);
  for (KWindowObject *w = g_top; w; w = w->below) {
    if (w->Visible() && x >= w->rect.left && x < w->rect.right && y >= w->rect.top && y < w->rect.bottom) return w;
  }
  return nullptr;
}

int32_t find_class_locked(const char *name) noexcept {
  for (uint32_t i = 0; i < kMaxClasses; ++i) {
    if (g_classes[i].used && name_equal(g_classes[i].name, name)) return static_cast<int32_t>(i);
  }
  return -1;
}

WindowClass *class_at_locked(uint32_t index) noexcept {
  return index < kMaxClasses && g_classes[index].used ? &g_classes[index] : nullptr;
}

bool register_class_locked(KProcessObject &proc, const char *name, uint32_t wndProc, uint32_t background) noexcept {
  int32_t index = find_class_locked(name);
  if (index >= 0 && g_classes[index].process != &proc) return false;
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
    if (window->Visible()) {
      invalidate_locked(*window, {0, 0, window->rect.right - window->rect.left, window->rect.bottom - window->rect.top});
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
  const bool wasVisible = window.Visible();
  unlink_z(window);
  if (window.queue) window.queue->ring.RemoveFor(window.hwnd);
  if (g_capture == window.hwnd) g_capture = 0;
  window.style &= ~kStyleVisible;
  const auto keep = g_windows[index];
  g_windows[index] = nullptr;
  if (wasVisible) expose_locked(old);
}

void move_window_locked(KWindowObject &window, const URect &rect) noexcept {
  const URect old = window.rect;
  if (old == rect) return;
  window.rect = rect;
  if (!window.Visible()) return;

  const auto display = KE_GFX_GetPrimaryDisplay();
  bool blitted = false;
  if (display && display->surface && rect.right - rect.left == old.right - old.left &&
      rect.bottom - rect.top == old.bottom - old.top) {
    if (g_top == &window) {
      KE_GFX_SoftwareBitBlt(display->surface, rect.left, rect.top, display->surface, old.left, old.top,
                            static_cast<uint32_t>(rect.right - rect.left),
                            static_cast<uint32_t>(rect.bottom - rect.top), RasterOp::SRCCOPY);
      blitted = true;
    }
  }

  expose_locked(old);
  if (!blitted) invalidate_locked(window, {0, 0, rect.right - rect.left, rect.bottom - rect.top});
  draw_nonclient_locked(window);
  if (window.queue) {
    post_locked(*window.queue, window.hwnd, kMsgMove, 0,
                (static_cast<uint32_t>(rect.top & 0xFFFF) << 16) | (rect.left & 0xFFFF));
    post_locked(*window.queue, window.hwnd, kMsgSize, 0,
                (static_cast<uint32_t>((rect.bottom - rect.top) & 0xFFFF) << 16) | ((rect.right - rect.left) & 0xFFFF));
  }
}

void show_window_locked(KWindowObject &window, bool show) noexcept {
  if (show == window.Visible()) return;
  if (show) {
    window.style |= kStyleVisible;
    invalidate_locked(window, {0, 0, window.rect.right - window.rect.left, window.rect.bottom - window.rect.top});
    draw_nonclient_locked(window);
  } else {
    window.style &= ~kStyleVisible;
    expose_locked(window.rect);
  }
}

void set_capture_locked(uint32_t hwnd) noexcept { g_capture = hwnd; }
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
