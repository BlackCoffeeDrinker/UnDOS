#include <catch2/catch_test_macros.hpp>

// HAL-free window manager helpers from the kernel source tree.
#include <kernel/gfx/msgring.hpp>
#include "../../kernel/src/user/user.hpp"
#include "../../kernel_api/include/kernel/gfx/region.hpp"
#include "gfx/gfx.hpp"
#include "ob_test_support.hpp"
#include <kernel/kobject/KDisplay.hpp>
#include <kernel/kobject/KGraphicsAdapter.hpp>
#include <kernel/kobject/KMemoryBackingObject.hpp>
#include <kernel/kobject/KProcessObject.hpp>
#include <kernel/object_manager.hpp>
#include <undos/syscalls/__enum_WindowLongIndex.h>

using kernel::user::MessageRing;
using kernel::user::QueuedMessage;
using kernel::user::URect;
using kernel::user::URegion;
using kernel::user::HitTest;
using kernel::KWindowObject;
using kernel::user::hit_test_locked;
using kernel::user::client_to_screen_locked;
using kernel::user::screen_to_client_locked;

namespace {
constexpr uint32_t kMove = 0x200;
using Ring = MessageRing<4, kMove>;

uint64_t area(const URegion &r) {
  uint64_t a = 0;
  for (uint32_t i = 0; i < r.count; ++i) {
    a += static_cast<uint64_t>(r.rects[i].right - r.rects[i].left) *
         static_cast<uint64_t>(r.rects[i].bottom - r.rects[i].top);
  }
  return a;
}
} // namespace

TEST_CASE("URegion subtract leaves the uncovered area", "[user]") {
  URegion r;
  r.Set({0, 0, 100, 100});
  r.Subtract({25, 25, 75, 75});
  REQUIRE(r.count == 4);
  REQUIRE(area(r) == 100 * 100 - 50 * 50);
  for (uint32_t i = 0; i < r.count; ++i) {
    REQUIRE(r.rects[i].Intersect({25, 25, 75, 75}).Empty());
  }
}

TEST_CASE("URegion subtract of a covering rect empties it", "[user]") {
  URegion r;
  r.Set({10, 10, 20, 20});
  r.Subtract({0, 0, 50, 50});
  REQUIRE(r.Empty());
}

TEST_CASE("URegion intersect clips to the paint rect", "[user]") {
  URegion r;
  r.Set({0, 0, 100, 100});
  r.Subtract({50, 0, 100, 100});
  r.Intersect({40, 40, 60, 60});
  REQUIRE(r.count == 1);
  REQUIRE(r.rects[0] == URect{40, 40, 50, 60});
}

TEST_CASE("MessageRing reports overflow", "[user]") {
  Ring ring;
  for (uint32_t i = 0; i < 4; ++i) REQUIRE(ring.Push({1, 0x400 + i}) == kernel::KernelError::Success);
  REQUIRE(ring.Push({1, 0x500}) == kernel::KernelError::QueueFull);
  QueuedMessage m;
  REQUIRE(ring.Pop(m));
  REQUIRE(m.message == 0x400);
}

TEST_CASE("MessageRing coalesces consecutive mouse moves per window", "[user]") {
  Ring ring;
  REQUIRE(ring.Push({1, kMove, 0, 1}) == kernel::KernelError::Success);
  REQUIRE(ring.Push({1, kMove, 0, 2}) == kernel::KernelError::Success);
  REQUIRE(ring.count == 1);
  REQUIRE(ring.Push({2, kMove, 0, 3}) == kernel::KernelError::Success);
  REQUIRE(ring.count == 2);
  QueuedMessage m;
  REQUIRE(ring.Pop(m));
  REQUIRE(m.lParam == 2);
}

TEST_CASE("MessageRing drops a destroyed window's messages", "[user]") {
  Ring ring;
  ring.Push({1, 0x400});
  ring.Push({2, 0x401});
  ring.Push({1, 0x402});
  ring.RemoveFor(1);
  REQUIRE(ring.count == 1);
  QueuedMessage m;
  REQUIRE(ring.Pop(m));
  REQUIRE(m.hwnd == 2);
}

TEST_CASE("Window hit testing", "[user]") {
  KWindowObject w;
  w.rect = {100, 100, 300, 200}; // Width 200, Height 100
  w.style = kernel::user::kStyleVisible | kernel::user::kStyleCaption | kernel::user::kStyleBorder;

  // Border is 3px, Caption is 18px.
  // Client area starts at (103, 100 + 3 + 18) = (103, 121)

  REQUIRE(hit_test_locked(w, 50, 50) == HitTest::Nowhere);
  REQUIRE(hit_test_locked(w, 101, 101) == HitTest::TopLeft);
  REQUIRE(hit_test_locked(w, 150, 101) == HitTest::Top);
  REQUIRE(hit_test_locked(w, 150, 110) == HitTest::Caption);
  REQUIRE(hit_test_locked(w, 150, 150) == HitTest::Client);
  REQUIRE(hit_test_locked(w, 105, 110) == HitTest::SysMenu);
  REQUIRE(hit_test_locked(w, 270, 110) == HitTest::MinButton);
  REQUIRE(hit_test_locked(w, 300 - 5, 110) == HitTest::MaxButton);
}

namespace {
using namespace kernel;
using namespace kernel::user;

template<typename T>
T TakeExpected(kstd::expected<T, kernel::KernelError> result) {
  REQUIRE(result);
  return kstd::move(*result);
}

bool BeginPaintInto(KProcessObject &process, KWindowObject &window, uint32_t &dc, URect &paint) {
  const auto begun = kernel::user::begin_paint_locked(process, window);
  if (!begun) return false;
  dc = begun->dc;
  paint = begun->paint;
  return true;
}

KernelError UserProbe(const KObjectPtr<KGraphicsAdapter> &, const KGraphicsAdapterDisplayEnumInfo &info,
                      KGraphicsAdapterDisplayInfo &out) {
  if (info.displayIndex) return KernelError::OutOfRange;
  out.isPresent = true;
  out.name = "UserTestHead";
  return KernelError::Success;
}

kstd::expected<KDisplayModeInfo, KernelError> UserMode(const KObjectPtr<KDisplay> &, size_t index) {
  if (index) return kstd::unexpected(KernelError::OutOfRange);
  KDisplayModeInfo out{};
  out.width = 640; out.height = 480; out.pitch = 640;
  out.bpp = 8; out.refreshRate = 60; out.pixelFormat = PixelFormat::INDEX8;
  return out;
}

kstd::expected<KDisplaySurfaceBinding, KernelError> UserSetMode(const KObjectPtr<KDisplay> &, size_t) {
  KDisplaySurfaceBinding binding{};
  binding.type = SurfaceType::OffscreenBitmap;
  binding.flags = SurfaceFlags::Linear | SurfaceFlags::TopDown;
  return binding;
}

KernelError UserConfigure(const KObjectPtr<KGraphicsAdapter> &, const KObjectPtr<KDisplay> &display) {
  display->GetModeInfo = UserMode;
  display->SetMode = UserSetMode;
  return KernelError::Success;
}

kstd::expected<uint32_t, KernelError> UserReadPixel(const KObjectPtr<KGraphicsAdapter> &, const KObjectPtr<KSurface> &surface,
                                                    int x, int y) {
  return surface->backing->directAddress.as_ptr<uint8_t>()[y * 640 + x];
}

KernelError UserPlanarFill(const KObjectPtr<KGraphicsAdapter> &, const KObjectPtr<KSurface> &surface,
                    int x, int y, int width, int height, uint32_t color) {
  auto *pixels = surface->backing->directAddress.as_ptr<uint8_t>();
  for (int row = y; row < y + height; ++row)
    for (int column = x; column < x + width; ++column)
      pixels[row * 640 + column] = static_cast<uint8_t>(color);
  return KernelError::Success;
}

uint8_t *g_presentedPixels{nullptr};
URect g_presentedBounds{};

void UserPresent(const KObjectPtr<KGraphicsAdapter> &, const KObjectPtr<KSurface> &surface,
                  const KClipRect *rects, size_t count) {
  const auto *shadow = surface->backing->directAddress.as_ptr<uint8_t>();
  for (size_t i = 0; i < count; ++i) {
    const auto &r = rects[i];
    g_presentedBounds = g_presentedBounds.Union({r.left, r.top, r.right, r.bottom});
    if (!g_presentedPixels) continue;
    for (int32_t y = r.top; y < r.bottom; ++y)
      for (int32_t x = r.left; x < r.right; ++x)
        g_presentedPixels[y * 640 + x] = shadow[y * 640 + x];
  }
}

struct Scene {
  KObjectPtr<KDisplay> display;
  KObjectPtr<KSurface> oldSurface;
  bool oldAttached{};
  KObjectPtr<KGraphicsAdapter> oldAdapter;
  KObjectPtr<KSurface> surface;
  KObjectPtr<KProcessObject> process;
  KObjectPtr<KWindowObject> windows[16];
  uint32_t count{0};
  MessageQueue queue{};
  uint8_t pixels[640 * 480]{};

  Scene() {
    EnsureObjectManagerInitialized();
    display = KE_GFX_GetPrimaryDisplay();
    if (!display) {
      auto adapter = TakeExpected(KE_GFX_RegisterGraphicsAdapter(nullptr, KDevicePtr<KDevice>(), "UserTestAdapter"));
      adapter->ProbeDisplay = UserProbe;
      adapter->ConfigureDisplay = UserConfigure;
      gfx::start_adapter(adapter);
      display = KE_GFX_GetPrimaryDisplay();
    }
    REQUIRE(display);
    oldSurface = display->surface;
    oldAttached = display->isAttachedToDesktop;
    oldAdapter = display->adapter;
    surface = TakeExpected(CreateKObject<KSurface>("user-test-surface"));
    process = TakeExpected(CreateKObject<KProcessObject>("user-test-process"));
    auto backing = TakeExpected(CreateKObject<KMemoryBackingObject>("user-test-pixels"));
    backing->directAddress = VirtualAddress(reinterpret_cast<uintptr_t>(pixels));
    backing->size = sizeof(pixels);
    surface->backing = backing;
    surface->width = 640; surface->height = 480; surface->pitch = 640;
    surface->bpp = 4; surface->pixelFormat = PixelFormat::INDEX8;
    surface->sizeInBytes = sizeof(pixels);
    surface->flags = SurfaceFlags::Linear | SurfaceFlags::TopDown;
    display->surface = surface;
    display->isAttachedToDesktop = true;
    display->adapter = nullptr; // exercise real software raster, not fake success
  }

  KWindowObject &add(URect rect, uint32_t parent = 0, uint32_t style = kStyleVisible) {
    REQUIRE(count < 16);
    auto window = TakeExpected(CreateKObject<KWindowObject>("user-test-window"));
    window->rect = rect;
    window->style = style | (parent ? kStyleChild : 0);
    window->parent = parent;
    window->queue = &queue;
    window->process = process.get();
    window->background = 7;
    REQUIRE(insert_window_locked(window));
    windows[count++] = window;
    return *window;
  }

  ~Scene() {
    Guard guard;
    cursor_move_locked(-1000, -1000);
    for (uint32_t i = 0; i < count; ++i)
      if (lookup_locked(windows[i]->hwnd)) destroy_window_locked(*windows[i]);
    display->surface = oldSurface;
    display->isAttachedToDesktop = oldAttached;
    display->adapter = oldAdapter;
  }
};
} // namespace

TEST_CASE("USER display changes refit roots and refresh outstanding paint DCs", "[user][scene][mode]") {
  Scene scene;
  kernel::user::Guard guard;
  kernel::user::SceneDrawGuard drawing;
  auto &root = scene.add({450, 350, 630, 470});
  auto &maximized = scene.add({0, 0, 640, 480});
  maximized.state = KWindowObject::State::Maximized;
  maximized.restoredRect = {400, 300, 600, 460};
  auto &child = scene.add({5, 5, 100, 100}, maximized.hwnd);
  child.state = KWindowObject::State::Maximized;
  root.invalid = kernel::user::client_rect_locked(root);
  uint32_t dc = 0;
  URect paint{};
  REQUIRE(BeginPaintInto(*scene.process, root, dc, paint));
  auto *slot = scene.process->dcTable.Resolve(dc);
  REQUIRE(slot);
  slot->presentationDirty = true;
  slot->presentationBounds = {450, 350, 630, 470};

  scene.surface->width = 320;
  scene.surface->height = 240;
  kernel::user::display_changed_locked(8);
  REQUIRE(root.rect.right <= 320);
  REQUIRE(root.rect.bottom <= 240);
  REQUIRE(root.rect.left >= 0);
  REQUIRE(root.rect.top >= 0);
  REQUIRE(maximized.rect == URect{0, 0, 320, 240});
  REQUIRE(maximized.restoredRect.right <= 320);
  REQUIRE(child.rect == URect{0, 0, 320, 240});
  REQUIRE(slot->target == scene.surface);
  REQUIRE_FALSE(slot->presentationDirty);
  REQUIRE(root.invalid == kernel::user::client_rect_locked(root));
  for (uint32_t i = 0; i < slot->clipCount; ++i) {
    REQUIRE(slot->clipRects[i].right <= 320);
    REQUIRE(slot->clipRects[i].bottom <= 240);
  }
  REQUIRE(kernel::user::end_paint_locked(*scene.process, root, dc) == KernelError::Success);
}

TEST_CASE("USER display notifications coalesce outside a full message ring", "[user][scene][mode]") {
  Scene scene;
  kernel::user::Guard guard;
  kernel::user::SceneDrawGuard drawing;
  auto &window = scene.add({10, 10, 200, 150});
  scene.queue.ring = {};
  for (uint32_t i = 0; i < kernel::kQueueSize; ++i)
    REQUIRE(kernel::user::post_locked(scene.queue, window.hwnd, 0x400, i, 0) == KernelError::Success);
  REQUIRE(kernel::user::post_locked(scene.queue, window.hwnd, 0x400, 0, 0) == KernelError::QueueFull);
  scene.surface->width = 320;
  scene.surface->height = 240;
  kernel::user::display_changed_locked(8);
  scene.surface->width = 400;
  scene.surface->height = 300;
  kernel::user::display_changed_locked(16);
  kernel::user::QueuedMessage message{};
  REQUIRE(kernel::user::geometry_message_locked(scene.queue, message, false));
  REQUIRE(message.message == kernel::user::kMsgDisplayChange);
  REQUIRE(message.wParam == 16);
  REQUIRE(message.lParam == ((300u << 16) | 400u));
  REQUIRE(window.pendingDisplayChange);
  REQUIRE(kernel::user::geometry_message_locked(scene.queue, message, true));
  REQUIRE_FALSE(window.pendingDisplayChange);
  REQUIRE(scene.queue.ring.count == kernel::kQueueSize);
}

TEST_CASE("USER children use parent client placement and clip every ancestor", "[user][scene]") {
  Scene scene;
  kernel::user::Guard guard;
  auto &root = scene.add({100, 100, 400, 350}, 0,
                         kernel::user::kStyleVisible | kernel::user::kStyleBorder | kernel::user::kStyleCaption);
  URect rect = TakeExpected(kernel::user::placement_rect_locked(root, -10, -20, 100, 80));
  REQUIRE(rect == URect{-10, -20, 90, 60});
  auto &child = scene.add({0, 0, 1, 1}, root.hwnd);
  rect = TakeExpected(kernel::user::placement_rect_locked(child, -10, -10, 100, 80));
  REQUIRE(rect == URect{93, 111, 193, 191});
  REQUIRE(kernel::user::move_window_locked(child, rect) == KernelError::Success);
  REQUIRE(kernel::user::clipped_rect_locked(child) == URect{103, 121, 193, 191});
  auto &grandchild = scene.add({90, 110, 220, 220}, child.hwnd);
  REQUIRE(kernel::user::clipped_rect_locked(grandchild) == URect{103, 121, 193, 191});
  REQUIRE(kernel::user::window_from_point_locked(95, 130) == nullptr);
  REQUIRE(kernel::user::window_from_point_locked(110, 130) == &grandchild);
  REQUIRE(kernel::user::placement_rect_locked(child, 32767, 0, 100, 10).error() == KernelError::OutOfRange);
  REQUIRE(kernel::user::placement_rect_locked(child, 0, 0, 0, 10).error() == KernelError::InvalidSize);
}

TEST_CASE("USER root z order keeps descendants grouped and focus is independent", "[user][scene]") {
  Scene scene;
  kernel::user::Guard guard;
  auto &first = scene.add({10, 10, 200, 200});
  auto &a = scene.add({20, 20, 150, 150}, first.hwnd);
  auto &nested = scene.add({25, 25, 100, 100}, a.hwnd);
  auto &second = scene.add({10, 10, 200, 200});
  auto &b = scene.add({30, 30, 180, 180}, second.hwnd);
  REQUIRE(kernel::user::topmost_locked() == &b);
  REQUIRE(b.below == &second);
  REQUIRE(second.below == &nested);
  REQUIRE(nested.below == &a);
  REQUIRE(a.below == &first);
  REQUIRE(kernel::user::set_focus_locked(nested.hwnd) == KernelError::Success);
  REQUIRE(kernel::user::active_window_locked() == &first);
  REQUIRE(kernel::user::focused_window_locked() == &nested);
  REQUIRE(kernel::user::topmost_locked() == &nested);
  REQUIRE(first.below == &b);
  REQUIRE(kernel::user::window_from_point_locked(40, 40) == &nested);
  // Capture affects routing, not physical hit testing.
  kernel::user::set_capture_locked(b.hwnd);
  REQUIRE(kernel::user::window_from_point_locked(40, 40) == &nested);
  REQUIRE(kernel::user::capture_locked() == b.hwnd);
  REQUIRE(kernel::user::mouse_target_locked(-200, -200) == &b);
  kernel::user::set_capture_locked(0);
  REQUIRE(kernel::user::mouse_target_locked(-200, -200) == nullptr);
}

TEST_CASE("USER hide move and recursive destruction preserve hierarchy lifetime", "[user][scene]") {
  Scene scene;
  kernel::user::Guard guard;
  auto &root = scene.add({10, 10, 250, 250});
  auto &child = scene.add({20, 30, 100, 110}, root.hwnd);
  auto &nested = scene.add({25, 35, 75, 85}, child.hwnd);
  REQUIRE(kernel::user::set_focus_locked(nested.hwnd) == KernelError::Success);
  kernel::user::set_capture_locked(nested.hwnd);
  REQUIRE(kernel::user::move_window_locked(root, {30, 40, 270, 280}) == KernelError::Success);
  REQUIRE(child.rect == URect{40, 60, 120, 140});
  REQUIRE(nested.rect == URect{45, 65, 95, 115});
  kernel::user::show_window_locked(root, false);
  REQUIRE(child.Visible());
  REQUIRE_FALSE(kernel::user::effectively_visible_locked(child));
  REQUIRE(kernel::user::capture_locked() == 0);
  REQUIRE(kernel::user::focused_window_locked() == nullptr);
  REQUIRE(kernel::user::set_focus_locked(child.hwnd) == KernelError::InvalidState);
  kernel::user::show_window_locked(root, true);
  REQUIRE(kernel::user::effectively_visible_locked(nested));
  const auto stale = nested.hwnd;
  kernel::user::destroy_window_locked(root);
  REQUIRE(kernel::user::lookup_locked(root.hwnd) == nullptr);
  REQUIRE(kernel::user::lookup_locked(child.hwnd) == nullptr);
  REQUIRE(kernel::user::lookup_locked(stale) == nullptr);
  auto &replacement = scene.add({10, 10, 50, 50});
  REQUIRE(replacement.hwnd != stale);
}

TEST_CASE("USER invalidation and paint use client coordinates and exact DC pairing", "[user][scene]") {
  Scene scene;
  kernel::user::Guard guard;
  auto &window = scene.add({100, 100, 300, 250}, 0,
                           kernel::user::kStyleVisible | kernel::user::kStyleBorder | kernel::user::kStyleCaption);
  REQUIRE(kernel::user::client_rect_locked(window) == URect{0, 0, 194, 126});
  window.invalid = {};
  kernel::user::invalidate_locked(window, {-10, -10, 12, 14});
  REQUIRE(window.invalid == URect{0, 0, 12, 14});
  uint32_t dc = 0;
  URect paint;
  REQUIRE(BeginPaintInto(*scene.process, window, dc, paint));
  REQUIRE(paint == URect{0, 0, 12, 14});
  REQUIRE(window.invalid.Empty());
  REQUIRE(window.paintClip.Bounds() == URect{103, 121, 115, 135});
  REQUIRE(kernel::user::begin_paint_locked(*scene.process, window).error() == KernelError::InvalidState);
  auto &other = scene.add({350, 100, 450, 250});
  REQUIRE(kernel::user::end_paint_locked(*scene.process, other, dc) == KernelError::InvalidHandle);
  REQUIRE(scene.process->dcTable.Resolve(dc) != nullptr);
  REQUIRE(kernel::user::move_window_locked(window, {110, 110, 310, 260}) == KernelError::Success);
  const auto *slot = scene.process->dcTable.Resolve(dc);
  REQUIRE(slot->originX == 113);
  REQUIRE(slot->originY == 131);
  REQUIRE(kernel::user::end_paint_locked(*scene.process, window, dc) == KernelError::Success);
  REQUIRE(scene.process->dcTable.Resolve(dc) == nullptr);
  REQUIRE(kernel::user::end_paint_locked(*scene.process, window, dc) == KernelError::InvalidHandle);
}

TEST_CASE("USER minimize maximize restore retain original child bounds", "[user][scene]") {
  Scene scene;
  kernel::user::Guard guard;
  auto &root = scene.add({10, 10, 500, 400}, 0,
                         kernel::user::kStyleVisible | kernel::user::kStyleBorder | kernel::user::kStyleCaption);
  const URect original{40, 60, 250, 200};
  auto &child = scene.add(original, root.hwnd,
                          kernel::user::kStyleVisible | kernel::user::kStyleBorder | kernel::user::kStyleCaption);
  auto &nested = scene.add({50, 90, 100, 140}, child.hwnd);
  REQUIRE(kernel::user::show_command_locked(child, 3) == KernelError::Success);
  REQUIRE(child.rect == kernel::user::client_to_screen_locked(root, kernel::user::client_rect_locked(root)));
  REQUIRE(kernel::user::show_command_locked(child, 2) == KernelError::Success);
  REQUIRE(child.state == KWindowObject::State::Minimized);
  REQUIRE(kernel::user::client_rect_locked(child).Empty());
  REQUIRE_FALSE(kernel::user::effectively_visible_locked(nested));
  REQUIRE(kernel::user::show_command_locked(child, 4) == KernelError::Success);
  REQUIRE(child.rect == original);
  REQUIRE(nested.rect == URect{50, 90, 100, 140});
  REQUIRE(kernel::user::focused_window_locked() == &child);
  REQUIRE(kernel::user::show_command_locked(child, 77) == KernelError::InvalidArgument);
}

TEST_CASE("USER exposure repaints captions and activation removes old highlight", "[user][scene]") {
  Scene scene;
  kernel::user::Guard guard;
  const uint32_t style = kernel::user::kStyleVisible | kernel::user::kStyleCaption | kernel::user::kStyleBorder;
  auto &first = scene.add({10, 10, 250, 150}, 0, style);
  REQUIRE(kernel::user::set_focus_locked(first.hwnd) == KernelError::Success);
  REQUIRE(scene.pixels[15 * 640 + 60] == kernel::user::kActiveCaptionColor);
  auto &second = scene.add({300, 10, 550, 150}, 0, style);
  REQUIRE(kernel::user::set_focus_locked(second.hwnd) == KernelError::Success);
  REQUIRE(scene.pixels[15 * 640 + 60] == kernel::user::kInactiveCaptionColor);
  scene.pixels[15 * 640 + 60] = 0xEE;
  kernel::user::expose_locked({55, 14, 65, 17});
  REQUIRE(scene.pixels[15 * 640 + 60] == kernel::user::kInactiveCaptionColor);
}

TEST_CASE("USER NT-style chrome has beveled frames and centered bold captions", "[user][scene][chrome]") {
  Scene scene;
  kernel::user::Guard guard;
  using namespace kernel::user;
  auto &window = scene.add({10, 10, 250, 150}, 0, kStyleVisible | kStyleCaption | kStyleBorder);
  window.title[0] = 'I';
  window.title[1] = 0;
  REQUIRE(set_focus_locked(window.hwnd) == KernelError::Success);
  draw_nonclient_locked(window);
  REQUIRE(scene.pixels[10 * 640 + 80] == kWindowFrameColor);
  REQUIRE(scene.pixels[11 * 640 + 80] == 15);
  REQUIRE(scene.pixels[148 * 640 + 80] == 8);
  REQUIRE(scene.pixels[14 * 640 + 60] == 1);
  REQUIRE(scene.pixels[30 * 640 + 60] == kWindowFrameColor);
  // The centered I's three-pixel top stroke is emboldened to four pixels.
  for (int x = 127; x < 131; ++x) REQUIRE(scene.pixels[13 * 640 + x] == kCaptionTextColor);
  REQUIRE(scene.pixels[13 * 640 + 126] == kActiveCaptionColor);
  REQUIRE(scene.pixels[13 * 640 + 131] == kActiveCaptionColor);
  REQUIRE(hit_test_locked(window, 20, 20) == HitTest::SysMenu);
  REQUIRE(hit_test_locked(window, 220, 20) == HitTest::MinButton);
  REQUIRE(hit_test_locked(window, 240, 20) == HitTest::MaxButton);
  REQUIRE(scene.pixels[20 * 640 + 18] == 15);
  REQUIRE(scene.pixels[23 * 640 + 219] == 0);
  REQUIRE(scene.pixels[20 * 640 + 237] == 0);
  REQUIRE(show_command_locked(window, 3) == KernelError::Success);
  const int32_t maxX = window.rect.right - kBorderWidth - kCaptionHeight;
  const int32_t captionY = window.rect.top + kBorderWidth;
  REQUIRE(scene.pixels[(captionY + 3) * 640 + maxX + 8] == 0);
  REQUIRE(scene.pixels[(captionY + 13) * 640 + maxX + 8] == 0);
}

TEST_CASE("USER identical class names are scoped to each process", "[user][classes]") {
  EnsureObjectManagerInitialized();
  auto first = TakeExpected(kernel::CreateKObject<kernel::KProcessObject>("user-class-first"));
  auto second = TakeExpected(kernel::CreateKObject<kernel::KProcessObject>("user-class-second"));
  {
    kernel::user::Guard guard;
    REQUIRE(kernel::user::register_class_locked(*first, "GuiDialog", 10, 7) == KernelError::Success);
    REQUIRE(kernel::user::register_class_locked(*second, "GuiDialog", 20, 8) == KernelError::Success);
    const auto a = kernel::user::find_class_locked("GuiDialog", first.get());
    const auto b = kernel::user::find_class_locked("GuiDialog", second.get());
    REQUIRE(a >= 0);
    REQUIRE(b >= 0);
    REQUIRE(a != b);
    REQUIRE(kernel::user::class_at_locked(static_cast<uint32_t>(a))->wndProc == 10);
    REQUIRE(kernel::user::class_at_locked(static_cast<uint32_t>(b))->wndProc == 20);
  }
  kernel::user::on_process_exit(*first);
  kernel::user::on_process_exit(*second);
}

TEST_CASE("USER subsystem operations validate ownership and arguments", "[user][classes][kernel-error]") {
  Scene scene;
  auto stranger = TakeExpected(kernel::CreateKObject<kernel::KProcessObject>("user-ops-stranger"));
  KWindowObject *root = nullptr;
  KWindowObject *child = nullptr;
  {
    kernel::user::Guard guard;
    root = &scene.add({10, 10, 200, 200}, 0, kStyleVisible | kStyleCaption | kStyleBorder);
    child = &scene.add({20, 40, 60, 80}, root->hwnd);
  }
  auto &caller = *scene.process;

  REQUIRE(kernel::user::register_class(caller, "", 1, 0) == KernelError::InvalidArgument);
  REQUIRE(kernel::user::register_class(caller, "ThisClassNameIsDefinitelyTooLongForUSER", 1, 0) ==
          KernelError::NameTooLong);

  REQUIRE(kernel::user::destroy_window(caller, 0xDEAD0001) == KernelError::InvalidHandle);
  REQUIRE(kernel::user::show_window(*stranger, root->hwnd, 1) == KernelError::AccessDenied);
  REQUIRE(kernel::user::show_window(caller, root->hwnd, 77) == KernelError::InvalidArgument);
  REQUIRE(kernel::user::move_window(caller, root->hwnd, 0, 0, 0, 10) == KernelError::InvalidSize);
  REQUIRE(kernel::user::move_window(caller, child->hwnd, 32767, 0, 10, 10) == KernelError::OutOfRange);

  const URect inverted{10, 10, 5, 20};
  REQUIRE(kernel::user::invalidate_rect(caller, root->hwnd, &inverted) == KernelError::InvalidArgument);
  REQUIRE(kernel::user::invalidate_rect(caller, root->hwnd, nullptr) == KernelError::Success);

  REQUIRE(kernel::user::get_window_rect(0xDEAD0001).error() == KernelError::InvalidHandle);
  REQUIRE(kernel::user::get_window_rect(root->hwnd).value() == root->rect);
  REQUIRE(kernel::user::get_window_long(root->hwnd, 99).error() == KernelError::InvalidArgument);
  REQUIRE(kernel::user::get_window_long(child->hwnd, WindowLongIndex_Parent).value() == root->hwnd);
  REQUIRE(kernel::user::set_window_long(caller, child->hwnd, WindowLongIndex_Parent, 0).error() ==
          KernelError::InvalidArgument);
  REQUIRE(kernel::user::set_window_long(caller, root->hwnd, WindowLongIndex_Style, root->style | kStyleChild).error() ==
          KernelError::InvalidArgument);
  REQUIRE(kernel::user::set_window_long(caller, root->hwnd, WindowLongIndex_UserData, 42).value() == 0);
  REQUIRE(kernel::user::get_window_long(root->hwnd, WindowLongIndex_UserData).value() == 42);

  REQUIRE(kernel::user::set_window_text(caller, root->hwnd,
                                        "0123456789012345678901234567890123456789012345678901234567890123456789") ==
          KernelError::InvalidSize);
  REQUIRE(kernel::user::set_window_text(caller, root->hwnd, "Title") == KernelError::Success);
  REQUIRE(root->title == kstd::string_view("Title"));

  REQUIRE(kernel::user::set_capture(*stranger, root->hwnd) == KernelError::AccessDenied);
  REQUIRE(kernel::user::set_capture(caller, root->hwnd) == KernelError::Success);
  REQUIRE(kernel::user::release_capture(*stranger) == KernelError::Success);
  {
    kernel::user::Guard guard;
    REQUIRE(kernel::user::capture_locked() == root->hwnd);
  }
  REQUIRE(kernel::user::release_capture(caller) == KernelError::Success);

  REQUIRE(kernel::user::set_focus(caller, root->hwnd) == KernelError::Success);
  REQUIRE(kernel::user::set_focus(*stranger, 0) == KernelError::AccessDenied);
  REQUIRE(kernel::user::set_focus(caller, 0) == KernelError::Success);

  REQUIRE(kernel::user::destroy_window(caller, child->hwnd) == KernelError::Success);
  REQUIRE(kernel::user::destroy_window(caller, child->hwnd) == KernelError::InvalidHandle);
  kernel::user::on_process_exit(*stranger);
}

TEST_CASE("USER facade validates class and title boundaries before creating a window", "[user][classes][kernel-error]") {
  Scene scene;
  auto &caller = *scene.process;
  char className[kernel::kWindowClassNameLen];
  char title[kernel::kWindowTitleLen];
  for (auto &c : className) c = 'C';
  for (auto &c : title) c = 'T';
  const kstd::string_view validClass(className, sizeof(className) - 1);
  const kstd::string_view oversizedClass(className, sizeof(className));
  const kstd::string_view validTitle(title, sizeof(title) - 1);
  const kstd::string_view oversizedTitle(title, sizeof(title));

  REQUIRE(kernel::user::register_class(caller, validClass, 1, 0) == KernelError::Success);
  REQUIRE(kernel::user::register_class(caller, oversizedClass, 1, 0) == KernelError::NameTooLong);

  kernel::user::CreateWindowParams params{
      .className = oversizedClass, .title = validTitle, .style = kStyleVisible,
      .parent = 0, .x = 0, .y = 0, .width = 100, .height = 100, .userData = 0};
  REQUIRE(kernel::user::create_window(caller, params).error() == KernelError::NameTooLong);
  params.className = validClass;
  params.title = oversizedTitle;
  REQUIRE(kernel::user::create_window(caller, params).error() == KernelError::InvalidSize);
  params.title = validTitle;
  const int32_t invalidDimensions[] = {0, -1};
  for (const int32_t invalid : invalidDimensions) {
    params.width = invalid;
    REQUIRE(kernel::user::create_window(caller, params).error() == KernelError::InvalidSize);
    params.width = 100;
    params.height = invalid;
    REQUIRE(kernel::user::create_window(caller, params).error() == KernelError::InvalidSize);
    params.height = 100;
  }

  KWindowObject *window = nullptr;
  {
    kernel::user::Guard guard;
    window = &scene.add({10, 10, 110, 110});
  }
  REQUIRE(kernel::user::set_window_text(caller, window->hwnd, validTitle) == KernelError::Success);
  REQUIRE(window->title == validTitle);
  REQUIRE(kernel::user::set_window_text(caller, window->hwnd, oversizedTitle) == KernelError::InvalidSize);
  REQUIRE(window->title == validTitle);
  kernel::user::on_process_exit(caller);
}

TEST_CASE("USER facade distinguishes invalid handles from foreign ownership", "[user][handles][kernel-error]") {
  Scene scene;
  auto stranger = TakeExpected(CreateKObject<KProcessObject>("user-facade-foreign"));
  KWindowObject *window = nullptr;
  {
    kernel::user::Guard guard;
    window = &scene.add({10, 10, 110, 110});
  }
  const uint32_t hwnd = window->hwnd;
  constexpr uint32_t invalid = 0xDEAD0001;
  auto &caller = *scene.process;
  REQUIRE(kernel::user::destroy_window(caller, invalid) == KernelError::InvalidHandle);
  REQUIRE(kernel::user::destroy_window(*stranger, hwnd) == KernelError::AccessDenied);
  REQUIRE(kernel::user::set_window_text(caller, invalid, "text") == KernelError::InvalidHandle);
  REQUIRE(kernel::user::set_window_text(*stranger, hwnd, "text") == KernelError::AccessDenied);
  REQUIRE(kernel::user::move_window(caller, invalid, 0, 0, 10, 10) == KernelError::InvalidHandle);
  REQUIRE(kernel::user::move_window(*stranger, hwnd, 0, 0, 10, 10) == KernelError::AccessDenied);

  kernel::user::CreateWindowParams params{
      .className = "unused", .title = "", .style = kStyleChild,
      .parent = invalid, .x = 0, .y = 0, .width = 10, .height = 10, .userData = 0};
  REQUIRE(kernel::user::create_window(caller, params).error() == KernelError::InvalidHandle);
  params.parent = hwnd;
  REQUIRE(kernel::user::create_window(*stranger, params).error() == KernelError::AccessDenied);
  REQUIRE(kernel::user::get_window_rect(hwnd).value() == URect{10, 10, 110, 110});
}

TEST_CASE("USER facade reports queue exhaustion without reporting allocation failure", "[user][queue][kernel-error]") {
  Scene scene;
  uint32_t hwnd = 0;
  {
    kernel::user::Guard guard;
    hwnd = scene.add({10, 10, 110, 110}).hwnd;
  }
  for (uint32_t i = 0; i < kernel::kQueueSize; ++i)
    REQUIRE(kernel::user::post_message(hwnd, 0x400 + i, i, 0) == KernelError::Success);
  REQUIRE(kernel::user::post_message(hwnd, 0x500, 0, 0) == KernelError::QueueFull);
  REQUIRE(scene.queue.ring.count == kernel::kQueueSize);
  QueuedMessage first{};
  REQUIRE(scene.queue.ring.Pop(first));
  REQUIRE(first.message == 0x400);
  REQUIRE(kernel::user::post_message(hwnd, 0x501, 0, 0) == KernelError::Success);
  REQUIRE(kernel::user::post_message(0xDEAD0001, 0x400, 0, 0) == KernelError::InvalidHandle);
}

TEST_CASE("USER fragmented visibility never paints over an occluder", "[user][region]") {
  URegion region;
  region.Set({0, 0, 640, 100});
  for (int32_t x = 1; x < 640; x += 3) region.Subtract({x, 0, x + 1, 100});
  for (uint32_t i = 0; i < region.count; ++i)
    for (int32_t x = 1; x < 640; x += 3)
      REQUIRE(region.rects[i].Intersect({x, 0, x + 1, 100}).Empty());
}

TEST_CASE("USER cursor save-under restores pixels without invalidating application content", "[user][scene]") {
  Scene scene;
  kernel::user::Guard guard;
  auto &window = scene.add({0, 0, 640, 480});
  window.invalid = {};
  for (auto &pixel : scene.pixels) pixel = 6;
  kernel::user::cursor_move_locked(80, 80);
  REQUIRE(scene.pixels[80 * 640 + 80] == 15);
  kernel::user::cursor_move_locked(100, 100);
  for (int32_t y = 76; y < 85; ++y)
    for (int32_t x = 76; x < 85; ++x)
      REQUIRE(scene.pixels[y * 640 + x] == 6);
  REQUIRE(window.invalid.Empty());
  // USER drawing underneath the cursor must replace the saved pixels.
  kernel::user::fill_screen_locked({98, 98, 103, 103}, 2);
  kernel::user::cursor_hide_locked();
  REQUIRE(scene.pixels[100 * 640 + 100] == 2);
  kernel::user::cursor_move_locked(0, 0);
  REQUIRE(scene.pixels[0] == 15);
  kernel::user::cursor_hide_locked();
  REQUIRE(scene.pixels[0] == 6);
  // A planar pitch is not safe for the packed save-under path.
  scene.surface->pitch = 80;
  kernel::user::cursor_move_locked(100, 100);
  REQUIRE(scene.pixels[100 * 640 + 100] == 2);
  scene.surface->pitch = 640;
}

TEST_CASE("USER planar cursor uses adapter readback and restores native pixels", "[user][scene]") {
  Scene scene;
  kernel::user::Guard guard;
  auto adapter = TakeExpected(kernel::CreateKObject<kernel::KGraphicsAdapter>("user-planar-adapter"));
  adapter->ReadPixel = UserReadPixel;
  adapter->FillRect = UserPlanarFill;
  scene.surface->pitch = 80;
  scene.display->adapter = adapter;
  for (auto &pixel : scene.pixels) pixel = 6;
  kernel::user::cursor_move_locked(80, 80);
  REQUIRE(scene.pixels[80 * 640 + 80] == 15);
  kernel::user::cursor_move_locked(100, 100);
  for (int y = 76; y < 85; ++y)
    for (int x = 76; x < 85; ++x)
      REQUIRE(scene.pixels[y * 640 + x] == 6);
  kernel::user::fill_screen_locked({98, 98, 103, 103}, 2);
  kernel::user::cursor_hide_locked();
  REQUIRE(scene.pixels[100 * 640 + 100] == 2);
}

TEST_CASE("USER scene changes refresh live paint clips and recover deleted DC damage", "[user][scene]") {
  Scene scene;
  kernel::user::Guard guard;
  auto &root = scene.add({20, 20, 300, 300});
  auto &child = scene.add({40, 40, 180, 180}, root.hwnd);
  uint32_t dc = 0;
  URect paint;
  REQUIRE(BeginPaintInto(*scene.process, child, dc, paint));
  REQUIRE(scene.process->dcTable.Resolve(dc)->clipCount != 0);
  kernel::user::show_window_locked(root, false);
  REQUIRE(scene.process->dcTable.Resolve(dc)->clipCount == 0);
  kernel::user::show_window_locked(root, true);
  REQUIRE(scene.process->dcTable.Resolve(dc)->clipCount != 0);
  REQUIRE(scene.process->dcTable.Close(dc) == KernelError::Success);
  child.invalid = {};
  uint32_t fresh = 0;
  REQUIRE(BeginPaintInto(*scene.process, child, fresh, paint));
  REQUIRE(fresh != dc);
  REQUIRE(paint == URect{0, 0, 140, 140});
  REQUIRE(kernel::user::end_paint_locked(*scene.process, child, fresh) == KernelError::Success);
}

TEST_CASE("USER rejected hierarchy moves are atomic and destruction selects a surviving root", "[user][scene]") {
  Scene scene;
  kernel::user::Guard guard;
  auto &survivor = scene.add({0, 0, 200, 200});
  auto &root = scene.add({100, 100, 300, 300});
  auto &child = scene.add({32700, 100, 32760, 160}, root.hwnd);
  const auto oldRoot = root.rect, oldChild = child.rect;
  REQUIRE(kernel::user::move_window_locked(root, {200, 100, 400, 300}) == KernelError::OutOfRange);
  REQUIRE(root.rect == oldRoot);
  REQUIRE(child.rect == oldChild);
  REQUIRE(kernel::user::set_focus_locked(root.hwnd) == KernelError::Success);
  kernel::user::destroy_window_locked(root);
  REQUIRE(kernel::user::active_window_locked() == &survivor);
  REQUIRE(kernel::user::focused_window_locked() == &survivor);
}

TEST_CASE("USER keyboard cycle visits all sibling groups in a stable order", "[user][scene]") {
  Scene scene;
  kernel::user::Guard guard;
  auto &root = scene.add({0, 0, 500, 400});
  auto &one = scene.add({10, 10, 100, 100}, root.hwnd);
  auto &two = scene.add({20, 20, 120, 120}, root.hwnd);
  auto &three = scene.add({30, 30, 140, 140}, root.hwnd);
  REQUIRE(kernel::user::set_focus_locked(one.hwnd) == KernelError::Success);
  kernel::user::cycle_window_locked(true, false);
  REQUIRE(kernel::user::focused_window_locked() == &two);
  kernel::user::cycle_window_locked(true, false);
  REQUIRE(kernel::user::focused_window_locked() == &three);
  kernel::user::cycle_window_locked(true, false);
  REQUIRE(kernel::user::focused_window_locked() == &one);
  kernel::user::cycle_window_locked(true, true);
  REQUIRE(kernel::user::focused_window_locked() == &three);
}

TEST_CASE("USER geometry notifications survive a full ring and coalesce latest client size", "[user][scene]") {
  Scene scene;
  kernel::user::Guard guard;
  auto &window = scene.add({10, 10, 110, 110}, 0,
                           kernel::user::kStyleVisible | kernel::user::kStyleCaption | kernel::user::kStyleBorder);
  for (uint32_t i = 0; i < kernel::kQueueSize; ++i)
    REQUIRE(scene.queue.ring.Push({window.hwnd, 0x400 + i}) == KernelError::Success);
  REQUIRE(kernel::user::move_window_locked(window, {30, 40, 230, 240}) == KernelError::Success);
  REQUIRE(kernel::user::move_window_locked(window, {50, 60, 350, 360}) == KernelError::Success);
  QueuedMessage message;
  REQUIRE(kernel::user::geometry_message_locked(scene.queue, message, false));
  REQUIRE(message.message == kernel::user::kMsgMove);
  REQUIRE(message.lParam == ((60u << 16) | 50u));
  REQUIRE(kernel::user::geometry_message_locked(scene.queue, message, true));
  REQUIRE(message.message == kernel::user::kMsgMove);
  REQUIRE(kernel::user::geometry_message_locked(scene.queue, message, true));
  REQUIRE(message.message == kernel::user::kMsgSize);
  REQUIRE(message.lParam == ((276u << 16) | 294u));
  REQUIRE_FALSE(kernel::user::geometry_message_locked(scene.queue, message, true));
}

TEST_CASE("USER shrinking a dirty window never erases its new nonclient border", "[user][scene]") {
  Scene scene;
  kernel::user::Guard guard;
  auto &window = scene.add({10, 10, 400, 300}, 0,
                           kernel::user::kStyleVisible | kernel::user::kStyleCaption | kernel::user::kStyleBorder);
  REQUIRE(kernel::user::move_window_locked(window, {10, 10, 200, 150}) == KernelError::Success);
  uint32_t dc = 0;
  URect paint;
  REQUIRE(BeginPaintInto(*scene.process, window, dc, paint));
  REQUIRE(paint == kernel::user::client_rect_locked(window));
  REQUIRE(window.paintClip.Bounds() == kernel::user::client_to_screen_locked(window, paint));
  REQUIRE(scene.pixels[100 * 640 + 199] == kernel::user::kWindowFrameColor);
  REQUIRE(scene.pixels[149 * 640 + 100] == kernel::user::kWindowFrameColor);
  REQUIRE(kernel::user::end_paint_locked(*scene.process, window, dc) == KernelError::Success);
}

TEST_CASE("USER EndPaint publishes raster damage independently of the final live clip", "[user][scene]") {
  Scene scene;
  kernel::user::Guard guard;
  auto adapter = TakeExpected(kernel::CreateKObject<kernel::KGraphicsAdapter>("user-present-adapter"));
  adapter->Present = UserPresent;
  scene.display->adapter = adapter;
  auto &window = scene.add({20, 20, 300, 200});
  uint32_t dc = 0;
  URect paint;
  REQUIRE(BeginPaintInto(*scene.process, window, dc, paint));
  auto *slot = scene.process->dcTable.Resolve(dc);
  REQUIRE(slot);
  // Software glyphs live in the shadow until EndPaint, unlike accelerated
  // rectangle fills. GFX records their actual clipped damage in the DC.
  REQUIRE(KE_GFX_SoftwareFillRect(scene.surface, 40, 40, 5, 7, 15) == KernelError::Success);
  slot->presentationDirty = true;
  slot->presentationBounds = {40, 40, 45, 47};
  // A concurrent visibility update is allowed to narrow the remaining clip.
  window.paintClip.Set({200, 100, 220, 120});
  slot->clipCount = window.paintClip.count;
  uint8_t presented[640 * 480]{};
  g_presentedPixels = presented;
  g_presentedBounds = {};
  REQUIRE(kernel::user::end_paint_locked(*scene.process, window, dc) == KernelError::Success);
  g_presentedPixels = nullptr;
  REQUIRE(presented[40 * 640 + 40] == 15);
  REQUIRE(presented[46 * 640 + 44] == 15);
  REQUIRE(g_presentedBounds == URect{40, 40, 220, 120});
  // Reusing a slot must not republish damage from the prior paint.
  REQUIRE(BeginPaintInto(*scene.process, window, dc, paint));
  slot = scene.process->dcTable.Resolve(dc);
  REQUIRE(slot);
  REQUIRE_FALSE(slot->presentationDirty);
  REQUIRE(kernel::user::end_paint_locked(*scene.process, window, dc) == KernelError::Success);
}

TEST_CASE("USER minimized children paint selectable icons with labels and preserve restore bounds", "[user][scene]") {
  Scene scene;
  kernel::user::Guard guard;
  auto &root = scene.add({10, 10, 500, 400});
  auto &first = scene.add({30, 40, 250, 240}, root.hwnd,
                          kernel::user::kStyleVisible | kernel::user::kStyleCaption | kernel::user::kStyleBorder);
  auto &second = scene.add({40, 50, 260, 250}, root.hwnd,
                           kernel::user::kStyleVisible | kernel::user::kStyleCaption | kernel::user::kStyleBorder);
  first.title[0] = 'M'; first.title[1] = 'a'; first.title[2] = 'i'; first.title[3] = 'n';
  const auto restored = first.rect;
  REQUIRE(kernel::user::show_command_locked(first, 2) == KernelError::Success);
  REQUIRE(first.rect.right - first.rect.left == 88);
  REQUIRE(first.rect.bottom - first.rect.top == 56);
  REQUIRE(kernel::user::client_rect_locked(first).Empty());
  const auto r = first.rect;
  REQUIRE(kernel::user::hit_test_locked(first, r.left + 44, r.top + 10) == HitTest::Caption);
  REQUIRE(scene.pixels[(r.top + 3) * 640 + r.left + 29] == 15);
  REQUIRE(kernel::user::set_focus_locked(first.hwnd) == KernelError::Success);
  REQUIRE(kernel::user::focused_window_locked() == &first);
  REQUIRE(kernel::user::active_window_locked() == &root);
  bool highlight = false;
  for (int32_t y = r.top + 36; y < r.bottom; ++y)
    for (int32_t x = r.left; x < r.right; ++x)
      highlight = highlight || scene.pixels[y * 640 + x] == kernel::user::kActiveCaptionColor;
  REQUIRE(highlight);
  REQUIRE(kernel::user::show_command_locked(second, 2) == KernelError::Success);
  REQUIRE(first.rect.Intersect(second.rect).Empty());
  REQUIRE(kernel::user::set_focus_locked(root.hwnd) == KernelError::Success);
  kernel::user::cycle_window_locked(true, false);
  REQUIRE(kernel::user::focused_window_locked() == &first);
  REQUIRE(kernel::user::show_command_locked(first, 4) == KernelError::Success);
  REQUIRE(first.rect == restored);
  REQUIRE(first.state == KWindowObject::State::Restored);
}

TEST_CASE("USER raising the only root preserves all children above their parent", "[user][scene]") {
  Scene scene;
  kernel::user::Guard guard;
  auto &root = scene.add({10, 10, 500, 400});
  auto &first = scene.add({30, 40, 200, 200}, root.hwnd);
  auto &second = scene.add({220, 40, 400, 200}, root.hwnd);
  auto &other = scene.add({50, 50, 450, 350});
  REQUIRE(kernel::user::set_focus_locked(other.hwnd) == KernelError::Success);
  kernel::user::destroy_window_locked(other);
  REQUIRE(kernel::user::set_focus_locked(root.hwnd) == KernelError::Success);
  REQUIRE(kernel::user::topmost_locked() == &second);
  REQUIRE(second.below == &first);
  REQUIRE(first.below == &root);
  for (uint32_t i = 0; i < 4; ++i) {
    REQUIRE(kernel::user::set_focus_locked(first.hwnd) == KernelError::Success);
    REQUIRE(kernel::user::set_focus_locked(second.hwnd) == KernelError::Success);
    URegion visible;
    kernel::user::visible_region_locked(first, visible);
    REQUIRE(visible.Bounds() == first.rect);
    kernel::user::visible_region_locked(second, visible);
    REQUIRE(visible.Bounds() == second.rect);
  }
}

TEST_CASE("Coordinate mapping", "[user]") {
  KWindowObject w;
  w.rect = {100, 100, 300, 200};
  w.style = kernel::user::kStyleVisible | kernel::user::kStyleCaption | kernel::user::kStyleBorder;

  URect client = {0, 0, 10, 10};
  URect screen = client_to_screen_locked(w, client);
  REQUIRE(screen.left == 100 + 3);
  REQUIRE(screen.top == 100 + 3 + 18);

  URect back = screen_to_client_locked(w, screen);
  REQUIRE(back == client);
}

TEST_CASE("region_merges_to_bounds collapses near-solid regions only", "[user][region]") {
  using kernel::user::region_merges_to_bounds;

  // A single rectangle is always its own bounding box.
  URegion one;
  one.Set({0, 0, 100, 100});
  REQUIRE(region_merges_to_bounds(one));

  // Two halves of a rectangle with a one-pixel gap waste almost nothing.
  URegion adjacent;
  adjacent.Set({0, 0, 100, 49});
  adjacent.rects[adjacent.count++] = {0, 50, 100, 100};
  REQUIRE(region_merges_to_bounds(adjacent));

  // Two small rectangles at opposite screen corners: the bounding box is
  // hundreds of times the covered area and must not be merged.
  URegion sparse;
  sparse.Set({0, 0, 10, 10});
  sparse.rects[sparse.count++] = {600, 460, 610, 470};
  REQUIRE_FALSE(region_merges_to_bounds(sparse));
}
