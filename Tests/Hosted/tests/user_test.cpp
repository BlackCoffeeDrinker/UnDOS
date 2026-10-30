#include <catch2/catch_test_macros.hpp>

// HAL-free window manager helpers from the kernel source tree.
#include <kernel/gfx/msgring.hpp>
#include <kernel/io.hpp>
#include "../../../Kernel/Source/src/user/user.hpp"
#include "../../../Kernel/API/include/kernel/gfx/region.hpp"
#include "../../../Apps/Fontloader/decoder.hpp"
#include <string.h>
#include "font_corpus.hpp"
#include "gfx/font/default_font.hpp"
#include "user/ui_text.hpp"
#include <vector.hpp>
#include "gfx/gfx.hpp"
#include "ob_test_support.hpp"
#include <kernel/kobject/KDisplay.hpp>
#include <kernel/kobject/KGraphicsAdapter.hpp>
#include <kernel/kobject/KMemoryBackingObject.hpp>
#include <kernel/kobject/KProcessObject.hpp>
#include <kernel/object_manager.hpp>
#include <undos/syscalls/__enum_WindowLongIndex.h>
#include <kernel/kobject/KThreadObject.hpp>
#include <kernel/kobject/FontObject.hpp>
#include "sched_test_hooks.hpp"
#include "syscalls/syscalls.hpp"

using kernel::user::MessageRing;
using kernel::user::QueuedMessage;
using kernel::user::URect;
using kernel::user::URegion;
using UserHit = kernel::user::HitTest;
using kernel::KWindowObject;
using kernel::user::hit_test_locked;
using kernel::user::client_to_screen_locked;
using kernel::user::screen_to_client_locked;

namespace {
uint8_t g_userDisplayBuffer[640 * 480]{};
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
  for (uint32_t i = 0; i < 4; ++i) REQUIRE_FALSE(ring.Push({1, 0x400 + i}));
  REQUIRE(ring.Push({1, 0x500}) == kernel::KernelError::QueueFull);
  QueuedMessage m;
  REQUIRE(ring.Pop(m));
  REQUIRE(m.message == 0x400);
}

TEST_CASE("MessageRing coalesces consecutive mouse moves per window", "[user]") {
  Ring ring;
  REQUIRE_FALSE(ring.Push({1, kMove, 0, 1}));
  REQUIRE_FALSE(ring.Push({1, kMove, 0, 2}));
  REQUIRE(ring.count == 1);
  REQUIRE_FALSE(ring.Push({2, kMove, 0, 3}));
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

  // Border is 3px. The caption uses the current bitmap face's measured height.

  REQUIRE(hit_test_locked(w, 50, 50) == UserHit::Nowhere);
  REQUIRE(hit_test_locked(w, 101, 101) == UserHit::TopLeft);
  REQUIRE(hit_test_locked(w, 150, 101) == UserHit::Top);
  REQUIRE(hit_test_locked(w, 150, 110) == UserHit::Caption);
  REQUIRE(hit_test_locked(w, 150, 150) == UserHit::Client);
  REQUIRE(hit_test_locked(w, 105, 110) == UserHit::SysMenu);
  REQUIRE(hit_test_locked(w, 270, 110) == UserHit::MinButton);
  REQUIRE(hit_test_locked(w, 300 - 5, 110) == UserHit::MaxButton);
}

namespace {
using namespace kernel;
using namespace kernel::user;

template<typename T>
T TakeExpected(kstd::expected<T, kstd::error_code> result) {
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

kstd::error_code UserProbe(const KObjectPtr<KGraphicsAdapter> &, const KGraphicsAdapterDisplayEnumInfo &info,
                      KGraphicsAdapterDisplayInfo &out) {
  if (info.displayIndex) return KernelError::OutOfRange;
  out.isPresent = true;
  out.name = "UserTestHead";
  return KernelError::Success;
}

kstd::expected<KDisplayModeInfo, kstd::error_code> UserMode(const KObjectPtr<KDisplay> &, size_t index) {
  if (index) return kstd::unexpected(KernelError::OutOfRange);
  KDisplayModeInfo out{};
  out.width = 640; out.height = 480; out.pitch = 640;
  out.bpp = 8; out.refreshRate = 60; out.pixelFormat = kernel::PixelFormat::INDEX8;
  return out;
}

kstd::error_code UserPrepareMode(const KObjectPtr<KDisplay> &display, size_t index,
                                 KDisplayModeReservation &reservation) {
  const auto mode = UserMode(display, index);
  if (!mode) return mode.error();
  auto backing = KE_VMM_CreateDirectBacking(
      kernel::VirtualAddress::from_ptr(g_userDisplayBuffer), sizeof(g_userDisplayBuffer));
  if (!backing) return backing.error();
  reservation = {};
  reservation.modeIndex = index;
  reservation.mode = *mode;
  reservation.binding.backing = kstd::move(*backing);
  reservation.binding.type = SurfaceType::OffscreenBitmap;
  reservation.binding.flags = SurfaceFlags::Linear | SurfaceFlags::TopDown;
  return KernelError::Success;
}

kstd::error_code UserProgramMode(const KObjectPtr<KDisplay> &display,
                                 KDisplayModeReservation &reservation) {
  display->currentMode = reservation.mode;
  if (!display->modeGeneration) display->modeGeneration = 1;
  return KernelError::Success;
}

void UserFinishMode(const KObjectPtr<KDisplay> &, KDisplayModeReservation &, bool) {}

kstd::error_code UserRestoreMode(const KObjectPtr<KDisplay> &display, size_t index,
                                 KDisplayModeReservation &) {
  const auto mode = UserMode(display, index);
  if (!mode) return mode.error();
  display->currentMode = *mode;
  return KernelError::Success;
}

kstd::expected<KDisplayActualState, kstd::error_code>
UserQueryActualState(const KObjectPtr<KDisplay> &display) {
  return KDisplayActualState{
      .known = display->modeGeneration != 0,
      .enabled = display->isAttachedToDesktop,
      .modeIndex = display->currentMode.modeIndex};
}

kstd::error_code UserEnable(const KObjectPtr<KDisplay> &display) {
  display->isAttachedToDesktop = true;
  return KernelError::Success;
}

kstd::error_code UserDisable(const KObjectPtr<KDisplay> &display) {
  display->isAttachedToDesktop = false;
  return KernelError::Success;
}

kstd::error_code UserValidate(const KObjectPtr<KDisplay> &display) {
  return display && display->isPresent ? KernelError::Success
                                       : KernelError::NotFound;
}

kstd::error_code UserConfigure(const KObjectPtr<KGraphicsAdapter> &, const KObjectPtr<KDisplay> &display) {
  display->GetModeInfo = UserMode;
  display->PrepareMode = UserPrepareMode;
  display->ProgramMode = UserProgramMode;
  display->FinishMode = UserFinishMode;
  display->RestoreMode = UserRestoreMode;
  display->QueryActualState = UserQueryActualState;
  display->Enable = UserEnable;
  display->Disable = UserDisable;
  display->Validate = UserValidate;
  return KernelError::Success;
}

kstd::expected<uint32_t, kstd::error_code> UserReadPixel(const KObjectPtr<KGraphicsAdapter> &, const KObjectPtr<KSurface> &surface,
                                                    int x, int y) {
  return surface->backing->directAddress.as_ptr<uint8_t>()[y * 640 + x];
}

kstd::error_code UserPlanarFill(const KObjectPtr<KGraphicsAdapter> &, const KObjectPtr<KSurface> &surface,
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

KObjectPtr<KDisplay> ExistingVideoTarget() {
  auto active = kernel::gfx::active_video_present_target();
  if (active) return active;
  KObjectPtr<KDisplay> target;
  KE_OB_GetGfxDirectory()->Each<KDisplay>([&](const KObjectPtr<KDisplay> &candidate) {
    if (!target && candidate->videoPresentTargetId) target = candidate;
  });
  return target;
}

void EnsureCommittedVideoPath(const KObjectPtr<KDisplay> &target) {
  if (kernel::gfx::active_video_present_target() || !target) return;
  kernel::gfx::VideoPresentTopologyInfo topology{};
  REQUIRE_FALSE(kernel::gfx::get_video_present_topology(topology));
  const auto targetMode = kernel::gfx::video_present_target_mode(
      topology.generation, target->videoPresentTargetId, 0);
  REQUIRE(targetMode);
  kernel::gfx::VideoPresentSourceModeInfo sourceMode{};
  bool found = false;
  for (uint32_t i = 0; i < topology.sourceModeCount; ++i) {
    const auto candidate = kernel::gfx::video_present_source_mode(
        topology.generation, kernel::gfx::DesktopSourceId, i);
    REQUIRE(candidate);
    if (candidate->width == targetMode->mode.width &&
        candidate->height == targetMode->mode.height &&
        candidate->colorDepth == targetMode->mode.bpp &&
        candidate->pixelFormat == targetMode->mode.pixelFormat) {
      sourceMode = *candidate;
      found = true;
      break;
    }
  }
  REQUIRE(found);
  const kernel::gfx::VideoPresentPathInfo path{
      .sourceId = kernel::gfx::DesktopSourceId,
      .sourceModeId = sourceMode.modeId,
      .targetId = targetMode->targetId,
      .targetModeId = targetMode->modeId,
      .width = targetMode->mode.width,
      .height = targetMode->mode.height};
  kernel::KObjectPtr<kernel::KDisplay> preparedTarget;
  size_t modeIndex = 0;
  kernel::KDisplayModeReservation reservation{};
  REQUIRE_FALSE(kernel::gfx::video_present_prepare_path(
      topology.generation, path, preparedTarget, modeIndex, reservation));
  const auto committed = kernel::gfx::commit_video_present_path(
      topology.generation, path, preparedTarget, reservation);
  REQUIRE(committed);
  REQUIRE(committed->outcome == 0);
}

struct Scene {
  KObjectPtr<KDisplay> display;
  KObjectPtr<kernel::gfx::KVideoPresentSource> source;
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
    display = ExistingVideoTarget();
    if (!display) {
      auto device = TakeExpected(KE_IO_CreateDevice(nullptr, 0, "UserTestVideo", device_type::Video));
      auto adapter = TakeExpected(KE_GFX_RegisterGraphicsAdapter(nullptr, device, "UserTestAdapter"));
      adapter->ProbeDisplay = UserProbe;
      adapter->ConfigureDisplay = UserConfigure;
      gfx::start_adapter(adapter);
      display = ExistingVideoTarget();
    }
    REQUIRE(display);
    EnsureCommittedVideoPath(display);
    oldSurface = display->surface;
    oldAttached = display->isAttachedToDesktop;
    oldAdapter = display->adapter;
    KE_OB_GetGfxDirectory()->Each<kernel::gfx::KVideoPresentSource>(
        [&](const KObjectPtr<kernel::gfx::KVideoPresentSource> &candidate) {
          source = candidate;
        });
    REQUIRE(source);
    surface = TakeExpected(CreateKObject<KSurface>("user-test-surface"));
    process = TakeExpected(CreateKObject<KProcessObject>("user-test-process"));
    auto backing = TakeExpected(CreateKObject<KMemoryBackingObject>("user-test-pixels"));
    backing->directAddress = VirtualAddress(reinterpret_cast<uintptr_t>(pixels));
    backing->size = sizeof(pixels);
    surface->backing = backing;
    surface->width = 640; surface->height = 480; surface->pitch = 640;
    surface->bpp = 4; surface->pixelFormat = kernel::PixelFormat::INDEX8;
    surface->sizeInBytes = sizeof(pixels);
    surface->flags = SurfaceFlags::Linear | SurfaceFlags::TopDown;
    display->surface = surface;
    display->isAttachedToDesktop = true;
    source->surface = surface;
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
    source->surface = oldSurface;
  }
};

kstd::vector<uint8_t> decode_fixture(const uint8_t *data, size_t size) {
  kstd::vector<uint8_t> bytes;
  REQUIRE(undos::font::loader::decode({data, size}, bytes) ==
          undos::font::loader::DecodeError::None);
  return bytes;
}

bool pixels_match_mask_text(const uint8_t *pixels, const kernel::user::UiText &ui,
                            const kernel::font::UiFont &font, const char *text,
                            int32_t x, int32_t textTop, uint8_t foreground, uint8_t background) {
  constexpr int32_t maxWidth = 256;
  constexpr int32_t maxHeight = 128;
  const int32_t width = ui.width(text, strlen(text), false);
  const int32_t height = ui.text_height();
  if (width < 0 || width > maxWidth || height < 0 || height > maxHeight ||
      x < 0 || x + width > 640 || textTop < 0 || textTop + height > 480)
    return false;
  uint8_t expected[maxWidth * maxHeight]{};
  for (int32_t row = 0; row < height; ++row)
    for (int32_t column = 0; column < width; ++column)
      expected[row * maxWidth + column] = background;

  int32_t pen = 0;
  const int32_t baseline = textTop + ui.text_above();
  for (size_t i = 0; text[i]; ++i) {
    const auto ch = static_cast<uint8_t>(text[i]);
    const auto &mask = *font.ascii->masks[0][ch - kernel::font::UiGlyphs::FirstCodepoint];
    for (uint32_t row = 0; row < mask.height; ++row) {
      const int32_t targetY = baseline - mask.top + static_cast<int32_t>(row) - textTop;
      if (targetY < 0 || targetY >= height) continue;
      for (uint32_t column = 0; column < mask.width; ++column) {
        if (!(mask.bits[row * mask.stride + column / 8] & (0x80u >> (column % 8)))) continue;
        const int32_t targetX = pen + mask.left + static_cast<int32_t>(column);
        if (targetX >= 0 && targetX < width)
          expected[targetY * maxWidth + targetX] = foreground;
      }
    }
    pen += mask.advance;
  }
  for (int32_t row = 0; row < height; ++row)
    for (int32_t column = 0; column < width; ++column)
      if (pixels[(textTop + row) * 640 + x + column] != expected[row * maxWidth + column])
        return false;
  return true;
}

bool pixels_match_bitmap_text(const uint8_t *pixels, const char *text, int32_t x,
                              int32_t textTop, uint8_t foreground, uint8_t background) {
  constexpr int32_t maxWidth = 256;
  constexpr int32_t height = kernel::gfx::kFontCellHeight;
  const int32_t width =
      static_cast<int32_t>(strlen(text)) * static_cast<int32_t>(kernel::gfx::kFontCellWidth);
  if (width < 0 || width > maxWidth || x < 0 || x + width > 640 ||
      textTop < 0 || textTop + height > 480)
    return false;
  uint8_t expected[maxWidth * height]{};
  kernel::gfx::DrawBitmapText(0, 0, text, strlen(text), foreground, background, true,
                              [&](int64_t left, int64_t top, int64_t right, int64_t bottom, uint32_t color) {
                                for (int64_t y = top; y < bottom; ++y)
                                  for (int64_t column = left; column < right; ++column)
                                    if (column >= 0 && column < width && y >= 0 && y < height)
                                      expected[y * maxWidth + column] = static_cast<uint8_t>(color);
                              });
  for (int32_t row = 0; row < height; ++row)
    for (int32_t column = 0; column < width; ++column)
      if (pixels[(textTop + row) * 640 + x + column] != expected[row * maxWidth + column])
        return false;
  return true;
}

struct RestoreMenuFont {
  ~RestoreMenuFont() {
    kernel::font::UiFont empty;
    kernel::font::UiFont displaced;
    kernel::user::Guard guard;
    kernel::user::close_all_menus_locked();
    kernel::user::install_menu_font_locked(static_cast<kernel::font::UiFont &&>(empty), displaced);
    kernel::user::expose_locked(kernel::user::screen_rect());
  }
};

struct RestoreDefaultFont {
  kernel::font::FontSystem &system;
  ~RestoreDefaultFont() { (void)kernel::user::set_builtin_default_font(system.defaults); }
};

struct PublishedFaces {
  kernel::font::FaceRegistry &registry;
  uint32_t regular{};
  uint32_t bold{};
  ~PublishedFaces() {
    if (regular) (void)registry.close(regular);
    if (bold) (void)registry.close(bold);
  }
};

void clear_menu_font() {
  kernel::font::UiFont empty;
  kernel::font::UiFont displaced;
  kernel::user::Guard guard;
  kernel::user::close_all_menus_locked();
  kernel::user::install_menu_font_locked(static_cast<kernel::font::UiFont &&>(empty), displaced);
}
} // namespace

TEST_CASE("USER menus enforce ownership, attach to windows, and destroy submenu trees", "[user][menu]") {
  Scene scene;
  KWindowObject *root = nullptr;
  KWindowObject *child = nullptr;
  {
    kernel::user::Guard guard;
    root = &scene.add({20, 20, 300, 240}, 0,
                      kernel::user::kStyleVisible | kernel::user::kStyleBorder |
                          kernel::user::kStyleCaption);
    child = &scene.add({40, 60, 140, 120}, root->hwnd);
  }


  const uint32_t menu = TakeExpected(kernel::user::create_menu(*scene.process, false));
  const uint32_t popup = TakeExpected(kernel::user::create_menu(*scene.process, true));
  const uint32_t nestedPopup = TakeExpected(kernel::user::create_menu(*scene.process, true));
  const int32_t originalClientHeight =
      TakeExpected(kernel::user::get_client_rect(root->hwnd)).bottom;
  REQUIRE_FALSE(kernel::user::append_menu(*scene.process, popup, 0, 17, "Run"));
  REQUIRE_FALSE(kernel::user::append_menu(*scene.process, nestedPopup, 0, 99, "Quit"));
  REQUIRE_FALSE(kernel::user::append_menu(*scene.process, popup, kernel::user::kMenuItemPopup,
                                           nestedPopup, "More"));
  REQUIRE_FALSE(kernel::user::append_menu(*scene.process, menu, kernel::user::kMenuItemPopup,
                                           popup, "File"));
  REQUIRE(kernel::user::append_menu(*scene.process, popup, kernel::user::kMenuItemPopup,
                                    menu, "Cycle") == KernelError::InvalidArgument);
  REQUIRE(kernel::user::append_menu(*scene.process, menu, 0x1000, 1, "Invalid") ==
          KernelError::InvalidArgument);
  REQUIRE(kernel::user::destroy_menu(*scene.process, popup) == KernelError::InvalidState);
  for (uint32_t i = 0; i < kernel::user::kMaxMenuItems - 1; ++i)
    REQUIRE_FALSE(kernel::user::append_menu(*scene.process, menu, 0, i + 100, "Item"));
  REQUIRE(kernel::user::append_menu(*scene.process, menu, 0, 200, "Full") ==
          KernelError::ResourceLimitExceeded);

  auto stranger = TakeExpected(CreateKObject<KProcessObject>("user-menu-stranger"));
  REQUIRE(kernel::user::append_menu(*stranger, menu, 0, 1, "Denied") ==
          KernelError::AccessDenied);
  REQUIRE(kernel::user::set_menu(*stranger, root->hwnd, menu) == KernelError::AccessDenied);
  REQUIRE(kernel::user::set_menu(*scene.process, root->hwnd, popup) ==
          KernelError::InvalidArgument);
  REQUIRE_FALSE(kernel::user::set_menu(*scene.process, root->hwnd, menu));
  REQUIRE(TakeExpected(kernel::user::get_menu(root->hwnd)) == menu);
  REQUIRE(TakeExpected(kernel::user::get_window_rect(child->hwnd)) == URect{40, 80, 140, 140});
  int32_t menuHeight = 0;
  {
    kernel::user::Guard guard;
    menuHeight = kernel::user::menu_row_height_locked();
  }
  REQUIRE(TakeExpected(kernel::user::get_client_rect(root->hwnd)).bottom ==
          originalClientHeight - menuHeight);

  {
    kernel::user::Guard guard;
    REQUIRE_FALSE(kernel::user::set_focus_locked(child->hwnd));
    REQUIRE(kernel::user::menu_key_locked(0x79, false));
    REQUIRE(kernel::user::menu_key_locked(27, false));
    REQUIRE_FALSE(kernel::user::set_focus_locked(root->hwnd));
    scene.queue.ring = {};
    REQUIRE(kernel::user::menu_mouse_locked(25, 45, true));
    REQUIRE(kernel::user::menu_mouse_locked(25, 45, false));
    REQUIRE(kernel::user::menu_mouse_locked(30, 70, true));
    REQUIRE(kernel::user::menu_mouse_locked(30, 70, false));
    QueuedMessage message{};
    bool commandFound = false;
    while (scene.queue.ring.Pop(message))
      if (message.message == kernel::user::kMsgCommand && message.wParam == 17 &&
          message.hwnd == root->hwnd)
        commandFound = true;
    REQUIRE(commandFound);

    REQUIRE(kernel::user::menu_key_locked(0x79, false));
    const uint32_t queuedBeforeRelease = scene.queue.ring.count;
    REQUIRE(kernel::user::menu_key_locked(0x79, false, false));
    REQUIRE(scene.queue.ring.count == queuedBeforeRelease);
    REQUIRE(kernel::user::menu_key_locked(40, false));
    REQUIRE(kernel::user::menu_key_locked(39, false));
    REQUIRE(kernel::user::menu_key_locked(27, false));
    REQUIRE(kernel::user::menu_key_locked(39, false));
    REQUIRE(kernel::user::menu_key_locked(13, false));
    commandFound = false;
    while (scene.queue.ring.Pop(message))
      if (message.message == kernel::user::kMsgCommand && message.wParam == 99 &&
          message.hwnd == root->hwnd)
        commandFound = true;
    REQUIRE(commandFound);
  }

  REQUIRE_FALSE(kernel::user::destroy_menu(*scene.process, menu));
  REQUIRE(TakeExpected(kernel::user::get_menu(root->hwnd)) == 0);
  REQUIRE(TakeExpected(kernel::user::get_window_rect(child->hwnd)) == URect{40, 60, 140, 120});
  REQUIRE(TakeExpected(kernel::user::get_client_rect(root->hwnd)).bottom == originalClientHeight);
  REQUIRE(kernel::user::append_menu(*scene.process, menu, 0, 18, "Stale") ==
          KernelError::InvalidHandle);

  const uint32_t processMenu = TakeExpected(kernel::user::create_menu(*scene.process, false));
  REQUIRE_FALSE(kernel::user::set_menu(*scene.process, root->hwnd, processMenu));
  kernel::user::on_process_exit(*scene.process);
  REQUIRE(kernel::user::get_menu(root->hwnd).error() == KernelError::InvalidHandle);
  REQUIRE(kernel::user::destroy_menu(*scene.process, processMenu) == KernelError::InvalidHandle);
}


TEST_CASE("USER menu font publication reflows geometry atomically and clips oversized popups",
          "[user][menu][font]") {
  Scene scene;
  RestoreMenuFont restore;
  clear_menu_font();

  auto &system = *kernel::font::font_system();
  RestoreDefaultFont restoreDefault{system};
  auto &registry = system.faces;
  auto &defaults = system.defaults;
  uint32_t validation = 0;
  const auto regular = registry.publish(
      decode_fixture(font_fixture::SourceSans3_Regular, sizeof(font_fixture::SourceSans3_Regular)),
      validation);
  const auto bold = registry.publish(
      decode_fixture(font_fixture::SourceSans3_Bold, sizeof(font_fixture::SourceSans3_Bold)),
      validation);
  REQUIRE(regular);
  REQUIRE(bold);

  KWindowObject *root = nullptr;
  KWindowObject *child = nullptr;
  KWindowObject *grandchild = nullptr;
  {
    kernel::user::Guard guard;
    root = &scene.add({20, 20, 320, 240}, 0,
                      kernel::user::kStyleVisible | kernel::user::kStyleBorder |
                          kernel::user::kStyleCaption);
    root->title = "Caption";
    child = &scene.add({40, 60, 140, 120}, root->hwnd);
    grandchild = &scene.add({50, 100, 100, 130}, child->hwnd);
  }

  const uint32_t rootMenu = TakeExpected(kernel::user::create_menu(*scene.process, false));
  const uint32_t popup = TakeExpected(kernel::user::create_menu(*scene.process, true));
  const uint32_t nestedPopup = TakeExpected(kernel::user::create_menu(*scene.process, true));
  const uint32_t childMenu = TakeExpected(kernel::user::create_menu(*scene.process, false));
  REQUIRE_FALSE(kernel::user::append_menu(*scene.process, nestedPopup, kernel::user::kMenuItemDisabled,
                                           18, "Quit"));
  REQUIRE_FALSE(kernel::user::append_menu(*scene.process, popup, kernel::user::kMenuItemDisabled,
                                           17, "Run"));
  REQUIRE_FALSE(kernel::user::append_menu(*scene.process, popup, kernel::user::kMenuItemPopup,
                                           nestedPopup, "More"));
  REQUIRE_FALSE(kernel::user::append_menu(*scene.process, rootMenu, kernel::user::kMenuItemPopup,
                                           popup, "File"));
  REQUIRE_FALSE(kernel::user::append_menu(*scene.process, childMenu, 0, 19, "Child"));
  REQUIRE_FALSE(kernel::user::set_menu(*scene.process, root->hwnd, rootMenu));
  REQUIRE_FALSE(kernel::user::set_menu(*scene.process, child->hwnd, childMenu));
  {
    kernel::user::Guard guard;
    child->restoredRect = child->rect;
    grandchild->restoredRect = grandchild->rect;
    REQUIRE_FALSE(kernel::user::set_focus_locked(root->hwnd));
    REQUIRE(kernel::user::menu_key_locked(0x79, false));
  }

  {
    kernel::user::Guard guard;
    REQUIRE(kernel::user::menu_row_height_locked() == 20);
  }
  int32_t captionHeight = 0;
  {
    kernel::user::Guard guard;
    captionHeight = kernel::user::caption_height_locked();
  }
  int32_t menuTop = root->rect.top + 3 + captionHeight;
  REQUIRE(pixels_match_bitmap_text(scene.pixels, "File", root->rect.left + 3 + 8,
                                   menuTop + 2, 0, 7));
  REQUIRE(pixels_match_bitmap_text(scene.pixels, "Run", 23 + 8,
                                   menuTop + 20 + 4, 8, 15));
  const int32_t popupX = root->rect.left + kernel::user::kBorderWidth;
  const int32_t popupPixelTop = menuTop + 20;
  const int32_t staleX = popupX + 4;
  const int32_t staleY = popupPixelTop + 2 + 20 + 4;
  REQUIRE(scene.pixels[staleY * 640 + staleX] == kernel::user::kActiveCaptionColor);
  {
    kernel::user::Guard guard;
    root->invalid = {};
    REQUIRE(kernel::user::menu_key_locked(27, false));
    const URect stalePixel{staleX, staleY, staleX + 1, staleY + 1};
    const auto staleClientPixel = kernel::user::screen_to_client_locked(*root, stalePixel);
    REQUIRE_FALSE(root->invalid.Intersect(staleClientPixel).Empty());
    REQUIRE(kernel::user::menu_key_locked(0x79, false));
  }

  REQUIRE_FALSE(kernel::user::set_default_font(defaults, *regular, *bold, 15));
  auto font15 = defaults.snapshot();
  REQUIRE(font15.ppem == 15);
  const int32_t row15 = kernel::user::UiText::of(font15).row_height();
  REQUIRE(row15 >= 24);
  {
    kernel::user::Guard guard;
    captionHeight = kernel::user::caption_height_locked();
  }
  const int32_t captionHeight15 = captionHeight;
  menuTop = root->rect.top + 3 + captionHeight;
  {
    kernel::user::Guard guard;
    kernel::user::expose_locked(root->rect);
  }
  {
    kernel::user::Guard guard;
    REQUIRE(kernel::user::menu_row_height_locked() == row15);
  }
  {
    kernel::user::Guard guard;
    REQUIRE(kernel::user::hit_test_locked(*root, 30, menuTop + row15 - 1) ==
            UserHit::MenuBar);
    REQUIRE(kernel::user::hit_test_locked(*root, 30, menuTop + row15) == UserHit::Client);
    const URect origin = client_to_screen_locked(*root, {});
    REQUIRE(origin.top == root->rect.top + 3 + captionHeight + row15);
    const URect client = kernel::user::client_rect_locked(*root);
    REQUIRE(client.bottom == root->rect.bottom - root->rect.top - 6 - captionHeight - row15);
    const URect sample{2, 3, 10, 15};
    REQUIRE(screen_to_client_locked(*root, client_to_screen_locked(*root, sample)) == sample);
    REQUIRE_FALSE(kernel::user::menu_key_locked(27, false));
  }
  const auto ui15 = kernel::user::UiText::of(font15);
  const auto captionUi15 = kernel::user::UiText::current();
  REQUIRE(captionUi15.uses_default_font(root->title.data(), root->title.size(), false));
  const int32_t captionClientWidth = root->rect.right - root->rect.left - 6;
  const int32_t captionTextWidth15 =
      captionUi15.width(root->title.data(), root->title.size(), false) + 1;
  int32_t captionTextX15 = root->rect.left + 3 +
      (captionClientWidth - captionTextWidth15) / 2;
  const int32_t captionTextLeft = root->rect.left + 3 + 18 + 2;
  const int32_t captionTextRight = root->rect.left + 3 + captionClientWidth - 2 * 18 - 2;
  if (captionTextX15 + captionTextWidth15 > captionTextRight)
    captionTextX15 = captionTextRight - captionTextWidth15;
  if (captionTextX15 < captionTextLeft) captionTextX15 = captionTextLeft;
  const int32_t captionTextY15 = root->rect.top + 3 +
      (captionHeight - captionUi15.text_height()) / 2;
  REQUIRE(pixels_match_mask_text(scene.pixels, captionUi15, font15, root->title.data(),
                                 captionTextX15, captionTextY15,
                                 kernel::user::kCaptionTextColor,
                                 kernel::user::kActiveCaptionColor));
  REQUIRE(pixels_match_mask_text(scene.pixels, ui15, font15, "File",
                                 root->rect.left + 3 + 8, menuTop + 2, 0, 7));

  {
    kernel::user::Guard guard;
    REQUIRE(kernel::user::menu_mouse_locked(30, menuTop + row15 / 2, true));
    REQUIRE(kernel::user::menu_mouse_locked(30, menuTop + row15 / 2, false));
    const int32_t popupTop = menuTop + row15;
    REQUIRE(pixels_match_mask_text(scene.pixels, ui15, font15, "Run", 23 + 8,
                                   popupTop + 4, 8, 15));
    scene.queue.ring = {};
    REQUIRE(kernel::user::menu_mouse_locked(30, popupTop, true));
    REQUIRE(kernel::user::menu_mouse_locked(30, popupTop, false));
    REQUIRE(kernel::user::menu_mouse_locked(30, popupTop + 2 * row15 + 2, true));
    REQUIRE(kernel::user::menu_mouse_locked(30, popupTop + 2 * row15 + 2, false));
    REQUIRE(scene.queue.ring.count == 0);
    REQUIRE(kernel::user::menu_key_locked(40, false));
    REQUIRE(kernel::user::menu_key_locked(39, false));
  }
  const int32_t runWidth = ui15.width("Run", 3, false);
  const int32_t moreWidth = ui15.width("More", 4, false);
  const int32_t parentPopupWidth = (runWidth > moreWidth ? runWidth : moreWidth) + 24 > 48
                                       ? (runWidth > moreWidth ? runWidth : moreWidth) + 24
                                       : 48;
  const int32_t nestedPopupX = 23 + parentPopupWidth - 2;
  const int32_t nestedPopupY = menuTop + row15 + 2 + row15;
  REQUIRE(pixels_match_mask_text(scene.pixels, ui15, font15, "Quit", nestedPopupX + 8,
                                 nestedPopupY + 4, 8, 15));

  uint32_t childDc = 0;
  URect paint{};
  kernel::KDeviceContextSlot *childDcSlot = nullptr;
  int32_t oldChildDcY = 0;
  const URect childBefore = child->rect;
  const URect childRestoredBefore = child->restoredRect;
  const URect grandchildBefore = grandchild->rect;
  const URect grandchildRestoredBefore = grandchild->restoredRect;
  {
    kernel::user::Guard guard;
    REQUIRE(BeginPaintInto(*scene.process, *child, childDc, paint));
    childDcSlot = scene.process->dcTable.Resolve(childDc);
    REQUIRE(childDcSlot);
    oldChildDcY = childDcSlot->originY;
  }
  REQUIRE_FALSE(kernel::user::set_default_font(defaults, *regular, *bold, 24));
  auto font24 = defaults.snapshot();
  const int32_t row24 = kernel::user::UiText::of(font24).row_height();
  {
    kernel::user::Guard guard;
    captionHeight = kernel::user::caption_height_locked();
  }
  menuTop = root->rect.top + 3 + captionHeight;
  const int32_t rowDelta = row24 - row15;
  const int32_t captionDelta = captionHeight - captionHeight15;
  const int32_t delta = rowDelta + captionDelta;
  const int32_t descendantDelta = captionDelta + 2 * rowDelta;
  REQUIRE(row24 >= 36);
  REQUIRE(delta > 0);
  REQUIRE(child->rect.top == childBefore.top + delta);
  REQUIRE(child->restoredRect.top == childRestoredBefore.top + delta);
  REQUIRE(grandchild->rect.top == grandchildBefore.top + descendantDelta);
  REQUIRE(grandchild->restoredRect.top == grandchildRestoredBefore.top + descendantDelta);
  {
    kernel::user::Guard guard;
    REQUIRE((child->sizeParam >> 16) ==
            static_cast<uint32_t>(kernel::user::client_rect_locked(*child).bottom));
  }
  REQUIRE((child->pendingGeometry & 2) != 0);
  {
    kernel::user::Guard guard;
    REQUIRE(childDcSlot->originY == oldChildDcY + descendantDelta);
    REQUIRE(kernel::user::menu_row_height_locked() == row24);
    REQUIRE(kernel::user::hit_test_locked(*root, 30, menuTop + row24 - 1) ==
            UserHit::MenuBar);
    REQUIRE(kernel::user::hit_test_locked(*root, 30, menuTop + row24) ==
            UserHit::Client);
    REQUIRE(client_to_screen_locked(*root, {}).top ==
            root->rect.top + 3 + captionHeight + row24);
    REQUIRE(kernel::user::client_rect_locked(*root).bottom ==
            root->rect.bottom - root->rect.top - 6 - captionHeight - row24);
    REQUIRE_FALSE(kernel::user::menu_key_locked(27, false));
    REQUIRE(kernel::user::menu_mouse_locked(30, menuTop + row24 / 2, true));
    REQUIRE(kernel::user::menu_mouse_locked(30, menuTop + row24 / 2, false));
  }
  const auto ui24 = kernel::user::UiText::of(font24);
  const auto captionUi24 = kernel::user::UiText::current();
  const int32_t captionTextWidth24 =
      captionUi24.width(root->title.data(), root->title.size(), false) + 1;
  int32_t captionTextX24 = root->rect.left + 3 +
      (captionClientWidth - captionTextWidth24) / 2;
  if (captionTextX24 + captionTextWidth24 > captionTextRight)
    captionTextX24 = captionTextRight - captionTextWidth24;
  if (captionTextX24 < captionTextLeft) captionTextX24 = captionTextLeft;
  const int32_t captionTextY24 = root->rect.top + 3 +
      (captionHeight - captionUi24.text_height()) / 2;
  REQUIRE(pixels_match_mask_text(scene.pixels, captionUi24, font24, root->title.data(),
                                 captionTextX24, captionTextY24,
                                 kernel::user::kCaptionTextColor,
                                 kernel::user::kActiveCaptionColor));
  {
    kernel::user::Guard guard;
    kernel::user::expose_locked(root->rect);
  }
  REQUIRE(pixels_match_mask_text(scene.pixels, ui24, font24, "File",
                                 root->rect.left + 3 + 8, menuTop + 2, 0, 7));
  REQUIRE(pixels_match_mask_text(scene.pixels, ui24, font24, "Run", 23 + 8,
                                 menuTop + row24 + 4, 8, 15));
  {
    kernel::user::Guard guard;
    scene.queue.ring = {};
    const int32_t popupTop = menuTop + row24;
    REQUIRE(kernel::user::menu_mouse_locked(30, popupTop, true));
    REQUIRE(kernel::user::menu_mouse_locked(30, popupTop, false));
    REQUIRE(kernel::user::menu_mouse_locked(30, popupTop + 2 * row24 + 2, true));
    REQUIRE(kernel::user::menu_mouse_locked(30, popupTop + 2 * row24 + 2, false));
    REQUIRE(scene.queue.ring.count == 0);
    REQUIRE_FALSE(kernel::user::end_paint_locked(*scene.process, *child, childDc));
  }

  const URect savedGrandchildRect = grandchild->rect;
  const URect savedGrandchildRestored = grandchild->restoredRect;
  {
    kernel::user::Guard guard;
    grandchild->rect = {100, 32750, 150, 32760};
    grandchild->restoredRect = {110, 32751, 160, 32761};
    REQUIRE(kernel::user::menu_key_locked(27, false));
    REQUIRE(kernel::user::menu_key_locked(0x79, false));
  }
  const uint32_t oldGeneration = defaults.snapshot().generation;
  const URect overflowRect = grandchild->rect;
  const URect overflowRestored = grandchild->restoredRect;
  REQUIRE(kernel::user::set_default_font(defaults, *regular, *bold, 256) ==
          kernel::KernelError::OutOfRange);
  REQUIRE(defaults.snapshot().generation == oldGeneration);
  REQUIRE(defaults.snapshot().ppem == 24);
  REQUIRE(grandchild->rect == overflowRect);
  REQUIRE(grandchild->restoredRect == overflowRestored);
  {
    kernel::user::Guard guard;
    REQUIRE(kernel::user::menu_row_height_locked() == row24);
    REQUIRE(kernel::user::menu_key_locked(27, false));
    grandchild->rect = savedGrandchildRect;
    grandchild->restoredRect = savedGrandchildRestored;
  }

  const uint32_t largePopup = TakeExpected(kernel::user::create_menu(*scene.process, true));
  for (uint32_t i = 0; i < kernel::user::kMaxMenuItems; ++i)
    REQUIRE_FALSE(kernel::user::append_menu(*scene.process, largePopup, 0, 100 + i, "X"));
  REQUIRE_FALSE(kernel::user::append_menu(*scene.process, rootMenu, kernel::user::kMenuItemPopup,
                                           largePopup, "Large"));
  scene.surface->height = 120;
  REQUIRE_FALSE(kernel::user::set_default_font(defaults, *regular, *bold, 256));
  const auto font256 = defaults.snapshot();
  REQUIRE(kernel::user::UiText::of(font256).row_height() > 120);
  {
    kernel::user::Guard guard;
    REQUIRE_FALSE(kernel::user::set_focus_locked(root->hwnd));
    REQUIRE(kernel::user::menu_key_locked(0x79, false));
    REQUIRE(kernel::user::menu_key_locked(39, false));
    REQUIRE(kernel::user::menu_key_locked(40, false));
    bool selectedRowVisible = false;
    for (int32_t x = 0; x < 640; ++x)
      selectedRowVisible |= scene.pixels[4 * 640 + x] == 1;
    REQUIRE(selectedRowVisible);
    REQUIRE(kernel::user::menu_key_locked(27, false));
  }
}

TEST_CASE("USER font publication reflows minimized icons to measured label widths",
          "[user][font][scene]") {
  Scene scene;
  RestoreMenuFont restore;
  clear_menu_font();
  auto &system = *kernel::font::font_system();
  RestoreDefaultFont restoreDefault{system};
  uint32_t validation = 0;
  const auto regular = system.faces.publish(
      decode_fixture(font_fixture::SourceSans3_Regular, sizeof(font_fixture::SourceSans3_Regular)),
      validation);
  const auto bold = system.faces.publish(
      decode_fixture(font_fixture::SourceSans3_Bold, sizeof(font_fixture::SourceSans3_Bold)),
      validation);
  PublishedFaces published{system.faces, regular ? *regular : 0, bold ? *bold : 0};
  REQUIRE(regular);
  REQUIRE(bold);

  constexpr uint32_t framed = kernel::user::kStyleVisible | kernel::user::kStyleCaption |
                              kernel::user::kStyleBorder;
  KWindowObject *root = nullptr;
  KWindowObject *icons[2]{};
  const char *titles[2] = {"Accessories", "Development"};
  {
    kernel::user::Guard guard;
    root = &scene.add({10, 10, 600, 460}, 0, framed);
    for (uint32_t i = 0; i < 2; ++i) {
      icons[i] = &scene.add({30, 40, 250, 240}, root->hwnd, framed);
      icons[i]->title = titles[i];
      REQUIRE_FALSE(kernel::user::show_command_locked(*icons[i], 2));
    }
    REQUIRE_FALSE(kernel::user::set_focus_locked(root->hwnd));
  }
  REQUIRE_FALSE(kernel::user::set_default_font(system.defaults, *regular, *bold, 24));
  const auto font24 = system.defaults.snapshot();
  const auto ui24 = kernel::user::UiText::of(font24);

  const auto check_icons = [&] {
    kernel::user::Guard guard;
    const URect parent = client_to_screen_locked(*root, kernel::user::client_rect_locked(*root));
    REQUIRE(icons[0]->rect.Intersect(icons[1]->rect).Empty());
    REQUIRE(icons[0]->rect.left < icons[1]->rect.left);
    for (uint32_t i = 0; i < 2; ++i) {
      const URect r = icons[i]->rect;
      const int32_t labelWidth = ui24.width(titles[i], strlen(titles[i]), false);
      REQUIRE(icons[i]->state == KWindowObject::State::Minimized);
      REQUIRE(r.right - r.left >= labelWidth + 8);
      REQUIRE(r.bottom - r.top == kernel::user::minimized_child_height_locked());
      REQUIRE(r.Intersect(parent) == r);
      REQUIRE(kernel::user::hit_test_locked(*icons[i], r.left + 1, r.bottom - 2) == UserHit::Caption);
    }
  };
  check_icons();
  for (uint32_t i = 0; i < 2; ++i) {
    const URect r = icons[i]->rect;
    const int32_t labelWidth = ui24.width(titles[i], strlen(titles[i]), false);
    REQUIRE(pixels_match_mask_text(scene.pixels, ui24, font24, titles[i],
                                   r.left + (r.right - r.left - labelWidth) / 2, r.top + 38, 0,
                                   kernel::user::kWindowFaceColor));
  }
  {
    kernel::user::Guard guard;
    REQUIRE_FALSE(kernel::user::move_window_locked(*root, {10, 10, 560, 400}));
  }
  check_icons();
}

TEST_CASE("USER minimized children re-flow when a sibling icon is hidden, shown, or destroyed",
          "[user][scene]") {
  Scene scene;
  constexpr uint32_t framed = kernel::user::kStyleVisible | kernel::user::kStyleCaption |
                              kernel::user::kStyleBorder;
  kernel::user::Guard guard;
  KWindowObject &root = scene.add({10, 10, 600, 460}, 0, framed);
  KWindowObject *icons[3]{};
  const char *titles[3] = {"One", "Two", "Three"};
  for (uint32_t i = 0; i < 3; ++i) {
    icons[i] = &scene.add({30, 40, 250, 240}, root.hwnd, framed);
    icons[i]->title = titles[i];
    REQUIRE_FALSE(kernel::user::show_command_locked(*icons[i], 2));
  }
  const URect slot0 = icons[0]->rect, slot1 = icons[1]->rect, slot2 = icons[2]->rect;
  REQUIRE(slot0.left < slot1.left);
  REQUIRE(slot1.left < slot2.left);

  // Hiding the middle icon closes its gap.
  kernel::user::show_window_locked(*icons[1], false);
  REQUIRE(icons[0]->rect == slot0);
  REQUIRE(icons[2]->rect.left == slot1.left);
  REQUIRE(icons[2]->rect.bottom == slot1.bottom);

  // Showing it again restores the minimize order.
  kernel::user::show_window_locked(*icons[1], true);
  REQUIRE(icons[0]->rect == slot0);
  REQUIRE(icons[1]->rect == slot1);
  REQUIRE(icons[2]->rect == slot2);
  REQUIRE(kernel::user::hit_test_locked(*icons[1], slot1.left + 1, slot1.bottom - 2) == UserHit::Caption);

  // Destroying the first icon shifts the survivors left without gaps.
  const URect width1 = icons[1]->rect;
  kernel::user::destroy_window_locked(*icons[0]);
  REQUIRE(icons[1]->rect.left == slot0.left);
  REQUIRE(icons[1]->rect.right - icons[1]->rect.left == width1.right - width1.left);
  REQUIRE(icons[2]->rect.left == icons[1]->rect.right);
  REQUIRE(icons[1]->rect.Intersect(icons[2]->rect).Empty());
}

TEST_CASE("USER popup overlays clip window paints and partial redraws", "[user][menu][paint]") {
  Scene scene;
  KWindowObject *root = nullptr;
  {
    kernel::user::Guard guard;
    root = &scene.add({20, 20, 300, 240}, 0,
                      kernel::user::kStyleVisible | kernel::user::kStyleBorder |
                          kernel::user::kStyleCaption);
  }
  const uint32_t menu = TakeExpected(kernel::user::create_menu(*scene.process, false));
  const uint32_t popup = TakeExpected(kernel::user::create_menu(*scene.process, true));
  const uint32_t nested = TakeExpected(kernel::user::create_menu(*scene.process, true));
  REQUIRE_FALSE(kernel::user::append_menu(*scene.process, popup, 0, 17, "Run"));
  REQUIRE_FALSE(kernel::user::append_menu(*scene.process, popup, kernel::user::kMenuItemPopup,
                                         nested, "More"));
  REQUIRE_FALSE(kernel::user::append_menu(*scene.process, popup, kernel::user::kMenuItemSeparator,
                                         0, ""));
  REQUIRE_FALSE(kernel::user::append_menu(*scene.process, nested, 0, 18, "Quit"));
  REQUIRE_FALSE(kernel::user::append_menu(*scene.process, menu, kernel::user::kMenuItemPopup,
                                         popup, "File"));
  REQUIRE_FALSE(kernel::user::set_menu(*scene.process, root->hwnd, menu));
  {
    kernel::user::Guard guard;
    REQUIRE_FALSE(kernel::user::set_focus_locked(root->hwnd));
    const int32_t popupTop =
        root->rect.top + 3 + kernel::user::caption_height_locked() +
        kernel::user::menu_row_height_locked();
    const int32_t popupRowHeight = kernel::user::menu_row_height_locked();
    const URect popupBounds{root->rect.left + 3, popupTop, root->rect.left + 3 + 56,
                            popupTop + 3 * popupRowHeight + 4};
    uint32_t dc = 0;
    URect paint{};
    REQUIRE(BeginPaintInto(*scene.process, *root, dc, paint));
    REQUIRE(kernel::user::menu_key_locked(0x79, false));
    for (uint32_t i = 0; i < root->paintClip.count; ++i) {
      REQUIRE(root->paintClip.rects[i].Intersect(popupBounds).Empty());
    }
    REQUIRE_FALSE(kernel::user::end_paint_locked(*scene.process, *root, dc));

    uint8_t expected[640 * 480]{};
    kstd::memcpy(expected, scene.pixels, sizeof(expected));
    SECTION("Client background erasure preserves the popup") {
      REQUIRE(BeginPaintInto(*scene.process, *root, dc, paint));
      for (int y = popupBounds.top; y < popupBounds.bottom; ++y)
        for (int x = popupBounds.left; x < popupBounds.right; ++x)
          REQUIRE(scene.pixels[y * 640 + x] == expected[y * 640 + x]);
      REQUIRE_FALSE(kernel::user::end_paint_locked(*scene.process, *root, dc));
      REQUIRE(kernel::user::menu_key_locked(27, false));
      REQUIRE(BeginPaintInto(*scene.process, *root, dc, paint));
      REQUIRE(scene.pixels[70 * 640 + 30] == root->background);
      REQUIRE_FALSE(kernel::user::end_paint_locked(*scene.process, *root, dc));
    }
    SECTION("Partial popup repaint changes no pixels outside its limit") {
      const URect limit{35, 88, 40, 93};
      for (int y = limit.top; y < limit.bottom; ++y)
        for (int x = limit.left; x < limit.right; ++x)
          scene.pixels[y * 640 + x] = 0xEE;
      kernel::user::draw_open_menu_locked(limit);
      for (unsigned i = 0; i < sizeof(expected); ++i)
        REQUIRE(scene.pixels[i] == expected[i]);
    }
    SECTION("Nested popup redraw retains its parent overlay") {
      REQUIRE(kernel::user::menu_key_locked(40, false));
      REQUIRE(kernel::user::menu_key_locked(39, false));
      kstd::memcpy(expected, scene.pixels, sizeof(expected));
      kernel::user::fill_screen_locked({23, 63, 133, 129}, 0xEE);
      kernel::user::draw_open_menu_locked({0, 0, 640, 480});
      for (int y = popupBounds.top; y < popupBounds.bottom; ++y)
        for (int x = popupBounds.left; x < popupBounds.right; ++x)
          REQUIRE(scene.pixels[y * 640 + x] == expected[y * 640 + x]);
      URegion visible;
      kernel::user::visible_region_locked(*root, visible);
      for (uint32_t i = 0; i < visible.count; ++i) {
        REQUIRE(visible.rects[i].Intersect(popupBounds).Empty());
        REQUIRE(visible.rects[i].Intersect({77, 85, 133, 109}).Empty());
      }
      REQUIRE(kernel::user::menu_key_locked(27, false));
      REQUIRE(kernel::user::menu_key_locked(27, false));
    }
  }
  REQUIRE_FALSE(kernel::user::destroy_menu(*scene.process, menu));
}

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
  REQUIRE_FALSE(kernel::user::end_paint_locked(*scene.process, root, dc));
}

TEST_CASE("USER display notifications coalesce outside a full message ring", "[user][scene][mode]") {
  Scene scene;
  kernel::user::Guard guard;
  kernel::user::SceneDrawGuard drawing;
  auto &window = scene.add({10, 10, 200, 150});
  scene.queue.ring = {};
  for (uint32_t i = 0; i < kernel::kQueueSize; ++i)
    REQUIRE_FALSE(kernel::user::post_locked(scene.queue, window.hwnd, 0x400, i, 0));
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
  REQUIRE(rect == URect{93, 113, 193, 193});
  REQUIRE_FALSE(kernel::user::move_window_locked(child, rect));
  REQUIRE(kernel::user::clipped_rect_locked(child) == URect{103, 123, 193, 193});
  auto &grandchild = scene.add({90, 110, 220, 220}, child.hwnd);
  REQUIRE(kernel::user::clipped_rect_locked(grandchild) == URect{103, 123, 193, 193});
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
  REQUIRE_FALSE(kernel::user::set_focus_locked(nested.hwnd));
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
  REQUIRE_FALSE(kernel::user::set_focus_locked(nested.hwnd));
  kernel::user::set_capture_locked(nested.hwnd);
  REQUIRE_FALSE(kernel::user::move_window_locked(root, {30, 40, 270, 280}));
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
  REQUIRE(kernel::user::client_rect_locked(window) == URect{0, 0, 194, 124});
  window.invalid = {};
  kernel::user::invalidate_locked(window, {-10, -10, 12, 14});
  REQUIRE(window.invalid == URect{0, 0, 12, 14});
  uint32_t dc = 0;
  URect paint;
  REQUIRE(BeginPaintInto(*scene.process, window, dc, paint));
  REQUIRE(paint == URect{0, 0, 12, 14});
  REQUIRE(window.invalid.Empty());
  REQUIRE(window.paintClip.Bounds() == URect{103, 123, 115, 137});
  REQUIRE(kernel::user::begin_paint_locked(*scene.process, window).error() == KernelError::InvalidState);
  auto &other = scene.add({350, 100, 450, 250});
  REQUIRE(kernel::user::end_paint_locked(*scene.process, other, dc) == KernelError::InvalidHandle);
  REQUIRE(scene.process->dcTable.Resolve(dc) != nullptr);
  REQUIRE_FALSE(kernel::user::move_window_locked(window, {110, 110, 310, 260}));
  const auto *slot = scene.process->dcTable.Resolve(dc);
  REQUIRE(slot->originX == 113);
  REQUIRE(slot->originY == 133);
  REQUIRE_FALSE(kernel::user::end_paint_locked(*scene.process, window, dc));
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
  REQUIRE_FALSE(kernel::user::show_command_locked(child, 3));
  REQUIRE(child.rect == kernel::user::client_to_screen_locked(root, kernel::user::client_rect_locked(root)));
  REQUIRE_FALSE(kernel::user::show_command_locked(child, 2));
  REQUIRE(child.state == KWindowObject::State::Minimized);
  REQUIRE(kernel::user::client_rect_locked(child).Empty());
  REQUIRE_FALSE(kernel::user::effectively_visible_locked(nested));
  REQUIRE_FALSE(kernel::user::show_command_locked(child, 4));
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
  REQUIRE_FALSE(kernel::user::set_focus_locked(first.hwnd));
  REQUIRE(scene.pixels[15 * 640 + 60] == kernel::user::kActiveCaptionColor);
  auto &second = scene.add({300, 10, 550, 150}, 0, style);
  REQUIRE_FALSE(kernel::user::set_focus_locked(second.hwnd));
  REQUIRE(scene.pixels[15 * 640 + 60] == kernel::user::kInactiveCaptionColor);
  scene.pixels[15 * 640 + 60] = 0xEE;
  kernel::user::expose_locked({55, 14, 65, 17});
  REQUIRE(scene.pixels[15 * 640 + 60] == kernel::user::kInactiveCaptionColor);
  first.invalid = {};
  scene.pixels[42 * 640 + 60] = 0xEE;
  kernel::user::expose_locked({55, 40, 65, 45});
  REQUIRE(scene.pixels[42 * 640 + 60] == first.background);
  REQUIRE_FALSE(first.invalid.Empty());
}

TEST_CASE("USER NT-style chrome has beveled frames and centered bold captions", "[user][scene][chrome]") {
  Scene scene;
  kernel::user::Guard guard;
  using namespace kernel::user;
  auto &window = scene.add({10, 10, 250, 150}, 0, kStyleVisible | kStyleCaption | kStyleBorder);
  window.title = "I";
  REQUIRE_FALSE(set_focus_locked(window.hwnd));
  draw_nonclient_locked(window);
  REQUIRE(scene.pixels[10 * 640 + 80] == kWindowFrameColor);
  REQUIRE(scene.pixels[11 * 640 + 80] == 15);
  REQUIRE(scene.pixels[148 * 640 + 80] == 8);
  REQUIRE(scene.pixels[14 * 640 + 60] == 1);
  REQUIRE(scene.pixels[(window.rect.top + kBorderWidth + caption_height_locked() - 1) * 640 + 60] ==
          kWindowFrameColor);
  const auto captionUi = UiText::current();
  const int32_t captionClientWidth = window.rect.right - window.rect.left - 2 * kBorderWidth;
  const int32_t titleWidth = captionUi.width(window.title.data(), window.title.size(), false) + 1;
  const int32_t titleLeft = window.rect.left + kBorderWidth + kCaptionButtonSize + 2;
  const int32_t titleRight = window.rect.left + kBorderWidth + captionClientWidth -
                             2 * kCaptionButtonSize - 2;
  int32_t titleX = window.rect.left + kBorderWidth + (captionClientWidth - titleWidth) / 2;
  if (titleX + titleWidth > titleRight) titleX = titleRight - titleWidth;
  if (titleX < titleLeft) titleX = titleLeft;
  const int32_t titleY = window.rect.top + kBorderWidth +
                         (caption_height_locked() - captionUi.text_height()) / 2;
  if (auto *system = kernel::font::font_system_if_ready()) {
    auto captionFont = system->defaults.snapshot();
    if (captionFont.ascii) {
      REQUIRE(pixels_match_mask_text(scene.pixels, captionUi, captionFont, window.title.data(),
                                     titleX, titleY, kCaptionTextColor, kActiveCaptionColor));
    } else {
      const int32_t bitmapY =
          titleY + (captionUi.text_height() - static_cast<int32_t>(kernel::gfx::kFontCellHeight)) / 2;
      REQUIRE(pixels_match_bitmap_text(scene.pixels, window.title.data(), titleX, bitmapY,
                                       kCaptionTextColor, kActiveCaptionColor));
    }
  } else {
    const int32_t bitmapY =
        titleY + (captionUi.text_height() - static_cast<int32_t>(kernel::gfx::kFontCellHeight)) / 2;
    REQUIRE(pixels_match_bitmap_text(scene.pixels, window.title.data(), titleX, bitmapY,
                                     kCaptionTextColor, kActiveCaptionColor));
  }
  REQUIRE(hit_test_locked(window, 20, 20) == UserHit::SysMenu);
  REQUIRE(hit_test_locked(window, 220, 20) == UserHit::MinButton);
  REQUIRE(hit_test_locked(window, 240, 20) == UserHit::MaxButton);
  const int32_t captionTop = window.rect.top + kBorderWidth;
  const int32_t buttonTop = captionTop + (caption_height_locked() - kCaptionButtonSize) / 2;
  const int32_t captionLeft = window.rect.left + kBorderWidth;
  const int32_t captionWidth = window.rect.right - window.rect.left - 2 * kBorderWidth;
  const int32_t minX = captionLeft + captionWidth - 2 * kCaptionButtonSize;
  const int32_t maxX = captionLeft + captionWidth - kCaptionButtonSize;
  REQUIRE(scene.pixels[(buttonTop + 7) * 640 + captionLeft + 5] == 15);
  REQUIRE(scene.pixels[(buttonTop + 7) * 640 + minX + 8] == 0);
  REQUIRE(scene.pixels[(buttonTop + 10) * 640 + maxX + 8] == 0);
  REQUIRE_FALSE(show_command_locked(window, 3));
  const int32_t maximizedButtonTop =
      window.rect.top + kBorderWidth + (caption_height_locked() - kCaptionButtonSize) / 2;
  const int32_t maximizedMaxX = window.rect.right - kBorderWidth - kCaptionButtonSize;
  REQUIRE(scene.pixels[(maximizedButtonTop + 10) * 640 + maximizedMaxX + 8] == 0);
  REQUIRE(scene.pixels[(maximizedButtonTop + 13) * 640 + maximizedMaxX + 8] == 0);
}

TEST_CASE("USER identical class names are scoped to each process", "[user][classes]") {
  EnsureObjectManagerInitialized();
  auto first = TakeExpected(kernel::CreateKObject<kernel::KProcessObject>("user-class-first"));
  auto second = TakeExpected(kernel::CreateKObject<kernel::KProcessObject>("user-class-second"));
  {
    kernel::user::Guard guard;
    REQUIRE_FALSE(kernel::user::register_class_locked(*first, "GuiDialog", 10, 7));
    REQUIRE_FALSE(kernel::user::register_class_locked(*second, "GuiDialog", 20, 8));
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
  REQUIRE_FALSE(kernel::user::invalidate_rect(caller, root->hwnd, nullptr));

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
  REQUIRE_FALSE(kernel::user::set_window_text(caller, root->hwnd, "Title"));
  REQUIRE(root->title == kstd::string_view("Title"));

  REQUIRE(kernel::user::set_capture(*stranger, root->hwnd) == KernelError::AccessDenied);
  REQUIRE_FALSE(kernel::user::set_capture(caller, root->hwnd));
  REQUIRE_FALSE(kernel::user::release_capture(*stranger));
  {
    kernel::user::Guard guard;
    REQUIRE(kernel::user::capture_locked() == root->hwnd);
  }
  REQUIRE_FALSE(kernel::user::release_capture(caller));

  REQUIRE_FALSE(kernel::user::set_focus(caller, root->hwnd));
  REQUIRE(kernel::user::set_focus(*stranger, 0) == KernelError::AccessDenied);
  REQUIRE_FALSE(kernel::user::set_focus(caller, 0));

  REQUIRE_FALSE(kernel::user::destroy_window(caller, child->hwnd));
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

  REQUIRE_FALSE(kernel::user::register_class(caller, validClass, 1, 0));
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
  REQUIRE_FALSE(kernel::user::set_window_text(caller, window->hwnd, validTitle));
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
    REQUIRE_FALSE(kernel::user::post_message(hwnd, 0x400 + i, i, 0));
  REQUIRE(kernel::user::post_message(hwnd, 0x500, 0, 0) == KernelError::QueueFull);
  REQUIRE(scene.queue.ring.count == kernel::kQueueSize);
  QueuedMessage first{};
  REQUIRE(scene.queue.ring.Pop(first));
  REQUIRE(first.message == 0x400);
  REQUIRE_FALSE(kernel::user::post_message(hwnd, 0x501, 0, 0));
  REQUIRE(kernel::user::post_message(0xDEAD0001, 0x400, 0, 0) == KernelError::InvalidHandle);
}

TEST_CASE("USER services invalid paint before a sustained input backlog", "[user][queue][paint]") {
  Scene scene;
  KWindowObject *window = nullptr;
  {
    kernel::user::Guard guard;
    window = &scene.add({10, 10, 110, 110});
    kernel::user::invalidate_locked(*window, kernel::user::client_rect_locked(*window));
    for (uint32_t i = 0; i + 1 < kernel::kPaintFairnessBacklog; ++i)
      REQUIRE_FALSE(scene.queue.ring.Push({window->hwnd, 0x400 + i}));
    REQUIRE_FALSE(kernel::user::should_serve_paint_before_input_locked(scene.queue));
    REQUIRE_FALSE(scene.queue.ring.Push(
        {window->hwnd, 0x400 + kernel::kPaintFairnessBacklog - 1}));
    REQUIRE(kernel::user::should_serve_paint_before_input_locked(scene.queue));
    window->invalid = {};
    REQUIRE_FALSE(kernel::user::should_serve_paint_before_input_locked(scene.queue));
  }
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
  REQUIRE_FALSE(scene.process->dcTable.Close(dc));
  child.invalid = {};
  uint32_t fresh = 0;
  REQUIRE(BeginPaintInto(*scene.process, child, fresh, paint));
  REQUIRE(fresh != dc);
  REQUIRE(paint == URect{0, 0, 140, 140});
  REQUIRE_FALSE(kernel::user::end_paint_locked(*scene.process, child, fresh));
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
  REQUIRE_FALSE(kernel::user::set_focus_locked(root.hwnd));
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
  REQUIRE_FALSE(kernel::user::set_focus_locked(one.hwnd));
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
    REQUIRE_FALSE(scene.queue.ring.Push({window.hwnd, 0x400 + i}));
  REQUIRE_FALSE(kernel::user::move_window_locked(window, {30, 40, 230, 240}));
  REQUIRE_FALSE(kernel::user::move_window_locked(window, {50, 60, 350, 360}));
  QueuedMessage message;
  REQUIRE(kernel::user::geometry_message_locked(scene.queue, message, false));
  REQUIRE(message.message == kernel::user::kMsgMove);
  REQUIRE(message.lParam == ((60u << 16) | 50u));
  REQUIRE(kernel::user::geometry_message_locked(scene.queue, message, true));
  REQUIRE(message.message == kernel::user::kMsgMove);
  REQUIRE(kernel::user::geometry_message_locked(scene.queue, message, true));
  REQUIRE(message.message == kernel::user::kMsgSize);
  REQUIRE(message.lParam == ((274u << 16) | 294u));
  REQUIRE_FALSE(kernel::user::geometry_message_locked(scene.queue, message, true));
}

TEST_CASE("USER shrinking a dirty window never erases its new nonclient border", "[user][scene]") {
  Scene scene;
  kernel::user::Guard guard;
  auto &window = scene.add({10, 10, 400, 300}, 0,
                           kernel::user::kStyleVisible | kernel::user::kStyleCaption | kernel::user::kStyleBorder);
  REQUIRE_FALSE(kernel::user::move_window_locked(window, {10, 10, 200, 150}));
  uint32_t dc = 0;
  URect paint;
  REQUIRE(BeginPaintInto(*scene.process, window, dc, paint));
  REQUIRE(paint == kernel::user::client_rect_locked(window));
  REQUIRE(window.paintClip.Bounds() == kernel::user::client_to_screen_locked(window, paint));
  REQUIRE(scene.pixels[100 * 640 + 199] == kernel::user::kWindowFrameColor);
  REQUIRE(scene.pixels[149 * 640 + 100] == kernel::user::kWindowFrameColor);
  REQUIRE_FALSE(kernel::user::end_paint_locked(*scene.process, window, dc));
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
  URect expectedDamage;
  SECTION("Background erasure remains damage after the clip shrinks") {
    REQUIRE(window.paintErase == window.rect);
    expectedDamage = window.rect;
  }
  SECTION("Raster damage is presented even without a pending background erase") {
    window.paintErase = {};
    expectedDamage = {40, 40, 45, 47};
  }
  // Software glyphs live in the shadow until EndPaint, unlike accelerated
  // rectangle fills. GFX records their actual clipped damage in the DC.
  REQUIRE_FALSE(KE_GFX_SoftwareFillRect(scene.surface, 40, 40, 5, 7, 15));
  slot->presentationDirty = true;
  slot->presentationBounds = {40, 40, 45, 47};
  // A concurrent visibility update is allowed to narrow the remaining clip.
  window.paintClip.Set({200, 100, 220, 120});
  slot->clipCount = window.paintClip.count;
  uint8_t presented[640 * 480]{};
  g_presentedPixels = presented;
  g_presentedBounds = {};
  REQUIRE_FALSE(kernel::user::end_paint_locked(*scene.process, window, dc));
  g_presentedPixels = nullptr;
  REQUIRE(presented[40 * 640 + 40] == 15);
  REQUIRE(presented[46 * 640 + 44] == 15);
  REQUIRE(g_presentedBounds == expectedDamage);
  // Reusing a slot must not republish damage from the prior paint.
  REQUIRE(BeginPaintInto(*scene.process, window, dc, paint));
  slot = scene.process->dcTable.Resolve(dc);
  REQUIRE(slot);
  REQUIRE_FALSE(slot->presentationDirty);
  REQUIRE_FALSE(kernel::user::end_paint_locked(*scene.process, window, dc));
}

TEST_CASE("USER minimized children paint selectable icons with labels and preserve restore bounds", "[user][scene]") {
  Scene scene;
  kernel::user::Guard guard;
  auto &root = scene.add({10, 10, 500, 400});
  auto &first = scene.add({30, 40, 250, 240}, root.hwnd,
                          kernel::user::kStyleVisible | kernel::user::kStyleCaption | kernel::user::kStyleBorder);
  auto &second = scene.add({40, 50, 260, 250}, root.hwnd,
                           kernel::user::kStyleVisible | kernel::user::kStyleCaption | kernel::user::kStyleBorder);
  first.title = "Main";
  const auto restored = first.rect;
  REQUIRE_FALSE(kernel::user::show_command_locked(first, 2));
  REQUIRE(first.rect.right - first.rect.left == 88);
  REQUIRE(first.rect.bottom - first.rect.top == 56);
  REQUIRE(kernel::user::client_rect_locked(first).Empty());
  const auto r = first.rect;
  REQUIRE(kernel::user::hit_test_locked(first, r.left + 44, r.top + 10) == UserHit::Caption);
  REQUIRE(scene.pixels[(r.top + 3) * 640 + r.left + 29] == 15);
  REQUIRE_FALSE(kernel::user::set_focus_locked(first.hwnd));
  REQUIRE(kernel::user::focused_window_locked() == &first);
  REQUIRE(kernel::user::active_window_locked() == &root);
  bool highlight = false;
  for (int32_t y = r.top + 36; y < r.bottom; ++y)
    for (int32_t x = r.left; x < r.right; ++x)
      highlight = highlight || scene.pixels[y * 640 + x] == kernel::user::kActiveCaptionColor;
  REQUIRE(highlight);
  REQUIRE_FALSE(kernel::user::show_command_locked(second, 2));
  REQUIRE(first.rect.Intersect(second.rect).Empty());
  REQUIRE_FALSE(kernel::user::set_focus_locked(root.hwnd));
  kernel::user::cycle_window_locked(true, false);
  REQUIRE(kernel::user::focused_window_locked() == &first);
  REQUIRE_FALSE(kernel::user::show_command_locked(first, 4));
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
  REQUIRE_FALSE(kernel::user::set_focus_locked(other.hwnd));
  kernel::user::destroy_window_locked(other);
  REQUIRE_FALSE(kernel::user::set_focus_locked(root.hwnd));
  REQUIRE(kernel::user::topmost_locked() == &second);
  REQUIRE(second.below == &first);
  REQUIRE(first.below == &root);
  for (uint32_t i = 0; i < 4; ++i) {
    REQUIRE_FALSE(kernel::user::set_focus_locked(first.hwnd));
    REQUIRE_FALSE(kernel::user::set_focus_locked(second.hwnd));
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
  REQUIRE(screen.top == 100 + 3 + kernel::user::caption_height_locked());

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

namespace {
// Makes `process` the caller of syscall handlers for the scope.
struct CallerProcess {
  KObjectPtr<kernel::KThreadObject> thread;
  explicit CallerProcess(KProcessObject &process) {
    thread = TakeExpected(CreateKObject<kernel::KThreadObject>("user-test-caller"));
    thread->owner_process = &process;
    // Real processes always hold their startup event as the first handle, so
    // 0 never names a font; reproduce that before acquiring fonts.
    if (process.handles.next_raw_handle == 0)
      REQUIRE(process.handles.Allocate(KObjectPtr<kernel::KObject>(thread.get()),
                                       kernel::ProcessHandleTable::HandleAccess::Write,
                                       kernel::ProcessHandleTable::HandleFlags::None));
    test_sched::SetCurrentThread(thread);
  }
  ~CallerProcess() {
    test_sched::SetCurrentThread({});
    thread->owner_process = nullptr;
  }
};

KObjectPtr<kernel::font::FontObject> font_object(KProcessObject &process, uint32_t handle) {
  const auto *entry = process.handles.Lookup(static_cast<kernel::KHandle>(handle));
  return entry ? entry->object.As<kernel::font::FontObject>() : KObjectPtr<kernel::font::FontObject>{};
}

uint32_t acquire_font(FontSource source) {
  __KRN_GFX_ACQUIREFONT_V1 acquire{.source = source,
                                   .fallback = GlyphFallbackPolicy_FallbackFaceNotdef,
                                   .font = 0, .generation = 0};
  REQUIRE_FALSE(handle_AcquireFont_V1(kstd::move(acquire)));
  REQUIRE(acquire.font);
  return acquire.font;
}

kstd::error_code font_metrics(uint32_t font, TextMetricsV2 &metrics) {
  __KRN_GFX_GETFONTMETRICS_V1 query{.font = font, .style = TextStyle_StyleRegular,
                                    .repertoire = TextRepertoire_RepertoireBasic, .metrics = {}};
  const auto error = handle_GetFontMetrics_V1(kstd::move(query));
  metrics = query.metrics;
  return error;
}

kstd::error_code close_font(uint32_t font) {
  __KRN_GFX_CLOSEFONT_V1 close{.font = font};
  return handle_CloseFont_V1(kstd::move(close));
}

kstd::error_code select_font(uint32_t dc, uint32_t font, uint32_t &previous) {
  __KRN_GFX_SELECTFONT_V1 select{.dc = dc, .font = font, .previousFont = 0};
  const auto error = handle_SelectFont_V1(kstd::move(select));
  previous = select.previousFont;
  return error;
}

kstd::error_code draw_text(uint32_t dc, int32_t x, int32_t y, const char *text) {
  __KRN_GFX_DRAWTEXT_V1 draw{.dc = dc, .x = x, .y = y, .style = TextStyle_StyleRegular,
                             .layout = TextLayoutMode_LayoutProportional,
                             .repertoire = TextRepertoire_RepertoireBasic, .text = {}};
  draw.text = text;
  return handle_DrawText_V1(kstd::move(draw));
}

uint32_t create_dc() {
  __KRN_GFX_CREATEDC_V1 create{};
  create.initFlags = DCInitFlags_Primary;
  REQUIRE_FALSE(handle_CreateDC_V1(kstd::move(create)));
  REQUIRE(create.handle);
  return create.handle;
}

kstd::error_code delete_dc(uint32_t dc) {
  __KRN_GFX_DELETEDC_V1 remove{.dc = dc};
  return handle_DeleteDC_V1(kstd::move(remove));
}

uint32_t ink_in(const uint8_t *pixels, URect area, uint8_t background) {
  uint32_t ink = 0;
  for (int32_t y = area.top; y < area.bottom; ++y)
    for (int32_t x = area.left; x < area.right; ++x) ink += pixels[y * 640 + x] != background;
  return ink;
}

PublishedFaces publish_source_sans(kernel::font::FontSystem &system) {
  uint32_t validation = 0;
  const auto regular = system.faces.publish(
      decode_fixture(font_fixture::SourceSans3_Regular, sizeof(font_fixture::SourceSans3_Regular)),
      validation);
  const auto bold = system.faces.publish(
      decode_fixture(font_fixture::SourceSans3_Bold, sizeof(font_fixture::SourceSans3_Bold)),
      validation);
  REQUIRE(regular);
  REQUIRE(bold);
  return {system.faces, *regular, *bold};
}
} // namespace

TEST_CASE("Font objects have identical handle and DC lifetimes on bitmap and TrueType backends",
          "[user][font][lifetime]") {
  Scene scene;
  RestoreMenuFont restoreMenu;
  clear_menu_font();
  auto &system = *kernel::font::font_system();
  RestoreDefaultFont restoreDefault{system};
  const auto faces = publish_source_sans(system);

  for (const bool trueType : {false, true}) {
    CAPTURE(trueType);
    if (trueType) REQUIRE_FALSE(kernel::user::set_default_font(system.defaults, faces.regular, faces.bold, 15));
    else REQUIRE_FALSE(kernel::user::set_builtin_default_font(system.defaults));
    memset(scene.pixels, 7, sizeof(scene.pixels));
    CallerProcess caller(*scene.process);

    const uint32_t font = acquire_font(FontSource_AcquireSystemDefault);
    const auto observed = font_object(*scene.process, font);
    REQUIRE(observed);
    REQUIRE(observed->bitmap == !trueType);
    const uint32_t generation = observed->generation;
    REQUIRE(observed->reference_count.load() == 2);
    TextMetricsV2 metrics{};
    REQUIRE_FALSE(font_metrics(font, metrics));
    REQUIRE(metrics.generation == generation);
    REQUIRE(metrics.lineHeight > 0);
    const int32_t lineHeight = static_cast<int32_t>(metrics.lineHeight);

    // Failed operations leave references balanced.
    uint32_t previous = 0;
    REQUIRE(select_font(0xdead, font, previous) == kernel::KernelError::InvalidHandle);
    REQUIRE(previous == 0);
    REQUIRE(observed->reference_count.load() == 2);

    const uint32_t dc = create_dc();
    REQUIRE(draw_text(dc, 10, 10, "Hg") == kernel::KernelError::InvalidState);
    REQUIRE_FALSE(select_font(dc, font, previous));
    REQUIRE(previous == 0);
    REQUIRE(observed->reference_count.load() == 3);

    // Reselection returns the replaced font as a new caller-owned handle.
    const uint32_t second = acquire_font(FontSource_AcquireBuiltinBitmap);
    REQUIRE_FALSE(select_font(dc, second, previous));
    REQUIRE(previous);
    REQUIRE(previous != font);
    REQUIRE(font_object(*scene.process, previous) == observed);
    REQUIRE(observed->reference_count.load() == 3);
    REQUIRE_FALSE(close_font(previous));
    REQUIRE(observed->reference_count.load() == 2);
    uint32_t returned = 0;
    REQUIRE_FALSE(select_font(dc, font, returned));
    REQUIRE(font_object(*scene.process, returned)->bitmap);
    REQUIRE_FALSE(close_font(returned));
    REQUIRE_FALSE(close_font(second));
    REQUIRE(observed->reference_count.load() == 3);

    // Closing the caller handle while selected keeps the DC's snapshot drawable.
    REQUIRE_FALSE(close_font(font));
    REQUIRE(observed->reference_count.load() == 2);
    REQUIRE(font_metrics(font, metrics) == kernel::KernelError::InvalidHandle);
    REQUIRE(close_font(font) == kernel::KernelError::InvalidHandle);
    REQUIRE(select_font(dc, font, previous) == kernel::KernelError::InvalidHandle);
    REQUIRE(previous == 0);

    // A later publication does not alter the selected snapshot.
    if (trueType) REQUIRE_FALSE(kernel::user::set_default_font(system.defaults, faces.regular, faces.bold, 24));
    else REQUIRE_FALSE(kernel::user::set_builtin_default_font(system.defaults));
    REQUIRE(system.defaults.snapshot().generation != generation);
    REQUIRE(observed->generation == generation);
    memset(scene.pixels, 7, sizeof(scene.pixels));
    {
      kernel::KDeviceContextSlot *slot = scene.process->dcTable.Resolve(dc);
      REQUIRE(slot);
      slot->penColor = 0;
      slot->backgroundColor = 7;
      slot->opaqueText = false;
    }
    REQUIRE_FALSE(draw_text(dc, 10, 10, "Hg"));
    REQUIRE(ink_in(scene.pixels, {10, 10, 10 + 40, 10 + lineHeight}, 7) ==
            ink_in(scene.pixels, {0, 0, 640, 480}, 7));
    REQUIRE(ink_in(scene.pixels, {0, 0, 640, 480}, 7) > 0);
    const uint32_t current = acquire_font(FontSource_AcquireSystemDefault);
    REQUIRE(font_object(*scene.process, current)->generation == system.defaults.snapshot().generation);
    REQUIRE_FALSE(close_font(current));

    // Deleting the DC releases its reference.
    REQUIRE_FALSE(delete_dc(dc));
    REQUIRE(observed->reference_count.load() == 1);
    REQUIRE(draw_text(dc, 10, 10, "Hg") == kernel::KernelError::InvalidHandle);
  }
}

TEST_CASE("SelectFont changes nothing when the previous-font handle cannot be reserved",
          "[user][font][lifetime]") {
  Scene scene;
  RestoreMenuFont restoreMenu;
  clear_menu_font();
  auto &system = *kernel::font::font_system();
  RestoreDefaultFont restoreDefault{system};
  REQUIRE_FALSE(kernel::user::set_builtin_default_font(system.defaults));
  CallerProcess caller(*scene.process);

  const uint32_t first = acquire_font(FontSource_AcquireSystemDefault);
  const uint32_t second = acquire_font(FontSource_AcquireBuiltinBitmap);
  const auto firstObject = font_object(*scene.process, first);
  const auto secondObject = font_object(*scene.process, second);
  const uint32_t dc = create_dc();
  uint32_t previous = 0;
  REQUIRE_FALSE(select_font(dc, first, previous));
  REQUIRE(previous == 0);
  REQUIRE(firstObject->reference_count.load() == 3);
  REQUIRE(secondObject->reference_count.load() == 2);

  // Exhaust the handle table so the up-front reservation fails.
  auto &handles = scene.process->handles;
  const size_t entries = handles.entries.size();
  auto savedFree = kstd::move(handles.free_list);
  handles.free_list = {};
  const uint32_t savedNext = handles.next_raw_handle;
  handles.next_raw_handle = UINT32_MAX;
  previous = 0xffff;
  const auto failed = select_font(dc, second, previous);
  handles.next_raw_handle = savedNext;
  handles.free_list = kstd::move(savedFree);
  REQUIRE(failed == kernel::KernelError::ResourceLimitExceeded);
  REQUIRE(previous == 0);
  REQUIRE(handles.entries.size() == entries);
  {
    kernel::KDeviceContextSlot *slot = scene.process->dcTable.Resolve(dc);
    REQUIRE(slot);
    REQUIRE(slot->selectedFont.get() == firstObject.get());
  }
  REQUIRE(firstObject->reference_count.load() == 3);
  REQUIRE(secondObject->reference_count.load() == 2);

  // With handles available again, the same call succeeds and returns the original font.
  REQUIRE_FALSE(select_font(dc, second, previous));
  REQUIRE(previous);
  REQUIRE(font_object(*scene.process, previous) == firstObject);
  REQUIRE(handles.entries.size() == entries + 1);
  REQUIRE_FALSE(close_font(previous));
  REQUIRE_FALSE(delete_dc(dc));
  REQUIRE_FALSE(close_font(first));
  REQUIRE_FALSE(close_font(second));
  REQUIRE(handles.entries.size() == entries - 2);
  REQUIRE(firstObject->reference_count.load() == 1);
  REQUIRE(secondObject->reference_count.load() == 1);
}

TEST_CASE("SelectFont and DrawText reject a deleted DC whose slot was reused",
          "[user][font][lifetime]") {
  Scene scene;
  RestoreMenuFont restoreMenu;
  clear_menu_font();
  auto &system = *kernel::font::font_system();
  RestoreDefaultFont restoreDefault{system};
  REQUIRE_FALSE(kernel::user::set_builtin_default_font(system.defaults));
  CallerProcess caller(*scene.process);

  const uint32_t first = acquire_font(FontSource_AcquireSystemDefault);
  const uint32_t second = acquire_font(FontSource_AcquireBuiltinBitmap);
  const auto firstObject = font_object(*scene.process, first);
  const auto secondObject = font_object(*scene.process, second);
  const uint32_t stale = create_dc();
  uint32_t previous = 0;
  REQUIRE_FALSE(select_font(stale, first, previous));
  REQUIRE(firstObject->reference_count.load() == 3);
  REQUIRE_FALSE(delete_dc(stale));
  REQUIRE(firstObject->reference_count.load() == 2);

  const uint32_t reused = create_dc();
  REQUIRE((reused & 0xFFFF) == (stale & 0xFFFF));
  REQUIRE(reused != stale);
  REQUIRE_FALSE(select_font(reused, second, previous));
  REQUIRE(previous == 0);
  REQUIRE(secondObject->reference_count.load() == 3);

  auto &handles = scene.process->handles;
  const size_t entries = handles.entries.size();
  previous = 0xffff;
  REQUIRE(select_font(stale, first, previous) == kernel::KernelError::InvalidHandle);
  REQUIRE(previous == 0);
  REQUIRE(handles.entries.size() == entries);
  REQUIRE(draw_text(stale, 10, 10, "Hg") == kernel::KernelError::InvalidHandle);
  REQUIRE(delete_dc(stale) == kernel::KernelError::InvalidHandle);
  {
    kernel::KDeviceContextSlot *slot = scene.process->dcTable.Resolve(reused);
    REQUIRE(slot);
    REQUIRE(slot->selectedFont.get() == secondObject.get());
  }
  REQUIRE(firstObject->reference_count.load() == 2);
  REQUIRE(secondObject->reference_count.load() == 3);

  REQUIRE_FALSE(delete_dc(reused));
  REQUIRE(secondObject->reference_count.load() == 2);
  REQUIRE_FALSE(close_font(first));
  REQUIRE_FALSE(close_font(second));
  REQUIRE(firstObject->reference_count.load() == 1);
  REQUIRE(secondObject->reference_count.load() == 1);
}

TEST_CASE("Font handles and DC selections are released by process exit on both backends",
          "[user][font][lifetime]") {
  Scene scene;
  RestoreMenuFont restoreMenu;
  clear_menu_font();
  auto &system = *kernel::font::font_system();
  RestoreDefaultFont restoreDefault{system};
  const auto faces = publish_source_sans(system);

  for (const bool trueType : {false, true}) {
    CAPTURE(trueType);
    if (trueType) REQUIRE_FALSE(kernel::user::set_default_font(system.defaults, faces.regular, faces.bold, 15));
    else REQUIRE_FALSE(kernel::user::set_builtin_default_font(system.defaults));
    auto exiting = TakeExpected(CreateKObject<KProcessObject>("user-test-exiting"));
    KObjectPtr<kernel::font::FontObject> observed;
    uint32_t font = 0;
    {
      CallerProcess caller(*exiting);
      font = acquire_font(FontSource_AcquireSystemDefault);
      observed = font_object(*exiting, font);
      REQUIRE(observed);
      REQUIRE(observed->bitmap == !trueType);
      const uint32_t dc = create_dc();
      uint32_t previous = 0;
      REQUIRE_FALSE(select_font(dc, font, previous));
      REQUIRE(observed->reference_count.load() == 3);
    }
    // The exit sequence of process::terminate_current, minus the scheduler.
    kernel::user::on_process_exit(*exiting);
    exiting->fontResources.clear();
    exiting = nullptr;
    REQUIRE(observed->reference_count.load() == 1);

    CallerProcess other(*scene.process);
    TextMetricsV2 metrics{};
    REQUIRE(font_metrics(font, metrics) == kernel::KernelError::InvalidHandle);
    REQUIRE(close_font(font) == kernel::KernelError::InvalidHandle);
  }
}

TEST_CASE("WM_FONTCHANGED is nonblocking, outranks a full queue, and coalesces per window",
          "[user][font][message]") {
  Scene scene;
  RestoreMenuFont restoreMenu;
  clear_menu_font();
  auto &system = *kernel::font::font_system();
  RestoreDefaultFont restoreDefault{system};
  const auto faces = publish_source_sans(system);
  CallerProcess caller(*scene.process);

  kernel::MessageQueue *queue = nullptr;
  KWindowObject *root = nullptr;
  KWindowObject *child = nullptr;
  {
    kernel::user::Guard guard;
    root = &scene.add({20, 20, 320, 240}, 0,
                      kernel::user::kStyleVisible | kernel::user::kStyleBorder | kernel::user::kStyleCaption);
    child = &scene.add({40, 60, 140, 120}, root->hwnd);
    queue = TakeExpected(kernel::user::queue_for_current_locked(true));
    root->queue = queue;
    child->queue = queue;
    // Saturate the ring; a notification that needed a slot would be lost.
    while (!kernel::user::post_locked(*queue, root->hwnd, 0x0100, 0x41, 0)) {}
  }
  struct RestoreQueues {
    Scene &scene;
    KWindowObject *windows[2];
    ~RestoreQueues() {
      kernel::user::Guard guard;
      for (auto *w : windows) w->queue = &scene.queue;
      kernel::user::free_queues_locked(*scene.process);
    }
  } restoreQueues{scene, {root, child}};
  const uint32_t ringCount = queue->ring.count;
  REQUIRE(ringCount > 0);

  // Single-threaded publications return with no consumer running, so none
  // waits for acknowledgement.
  REQUIRE_FALSE(kernel::user::set_default_font(system.defaults, faces.regular, faces.bold, 15));
  REQUIRE_FALSE(kernel::user::set_default_font(system.defaults, faces.regular, faces.bold, 24));
  REQUIRE_FALSE(kernel::user::set_builtin_default_font(system.defaults));
  const uint32_t latest = system.defaults.snapshot().generation;
  REQUIRE(queue->ring.count == ringCount);

  const auto peeked = TakeExpected(kernel::user::get_message(false, false));
  REQUIRE(peeked.message == kernel::user::kMsgFontChanged);
  REQUIRE(peeked.wParam == latest);
  REQUIRE(peeked.lParam == 0);
  REQUIRE(TakeExpected(kernel::user::get_message(false, false)).hwnd == peeked.hwnd);

  uint32_t seen[2]{};
  for (auto &hwnd : seen) {
    const auto message = TakeExpected(kernel::user::get_message(false, true));
    REQUIRE(message.message == kernel::user::kMsgFontChanged);
    REQUIRE(message.wParam == latest);
    hwnd = message.hwnd;
  }
  REQUIRE(seen[0] != seen[1]);
  {
    // Publication repaints the windows; settle that so the ring is next.
    kernel::user::Guard guard;
    root->invalid = {};
    child->invalid = {};
  }
  REQUIRE((seen[0] == root->hwnd || seen[0] == child->hwnd));
  REQUIRE((seen[1] == root->hwnd || seen[1] == child->hwnd));
  const auto next = TakeExpected(kernel::user::get_message(false, true));
  REQUIRE(next.message == 0x0100);
  REQUIRE(queue->ring.count == ringCount - 1);

  // A publication after draining notifies again, once per window.
  REQUIRE_FALSE(kernel::user::set_default_font(system.defaults, faces.regular, faces.bold, 15));
  {
    kernel::user::Guard guard;
    root->invalid = {};
    child->invalid = {};
  }
  uint32_t notifications = 0;
  for (;;) {
    const auto message = TakeExpected(kernel::user::get_message(false, true));
    if (message.message != kernel::user::kMsgFontChanged) {
      REQUIRE(message.message == 0x0100);
      break;
    }
    REQUIRE(message.wParam == system.defaults.snapshot().generation);
    ++notifications;
  }
  REQUIRE(notifications == 2);
}

TEST_CASE("Font publication that would overflow descendant coordinates leaves the old font and geometry",
          "[user][font][overflow]") {
  Scene scene;
  RestoreMenuFont restoreMenu;
  clear_menu_font();
  auto &system = *kernel::font::font_system();
  RestoreDefaultFont restoreDefault{system};
  const auto faces = publish_source_sans(system);

  KWindowObject *root = nullptr;
  KWindowObject *child = nullptr;
  KWindowObject *grandchild = nullptr;
  const uint32_t framed = kernel::user::kStyleVisible | kernel::user::kStyleBorder | kernel::user::kStyleCaption;
  {
    kernel::user::Guard guard;
    root = &scene.add({20, 20, 320, 240}, 0, framed);
    child = &scene.add({40, 60, 300, 220}, root->hwnd, framed);
    grandchild = &scene.add({50, 100, 100, 130}, child->hwnd);
  }
  REQUIRE_FALSE(kernel::user::set_default_font(system.defaults, faces.regular, faces.bold, 15));
  {
    kernel::user::Guard guard;
    for (uint32_t i = 0; i < scene.count; ++i) {
      scene.windows[i]->pendingFontGeneration = 0;
      scene.windows[i]->pendingGeometry = 0;
    }
    kernel::user::expose_locked(root->rect);
  }

  const auto attempt = [&](URect rect, URect restored) {
    {
      kernel::user::Guard guard;
      grandchild->rect = rect;
      grandchild->restoredRect = restored;
    }
    const auto before = system.defaults.snapshot();
    URect rects[3]{};
    URect restoredRects[3]{};
    for (uint32_t i = 0; i < 3; ++i) {
      rects[i] = scene.windows[i]->rect;
      restoredRects[i] = scene.windows[i]->restoredRect;
    }
    int32_t caption = 0;
    URect client{};
    {
      kernel::user::Guard guard;
      caption = kernel::user::caption_height_locked();
      client = kernel::user::client_rect_locked(*child);
    }
    static uint8_t pixels[640 * 480];
    memcpy(pixels, scene.pixels, sizeof(pixels));

    REQUIRE(kernel::user::set_default_font(system.defaults, faces.regular, faces.bold, 256) ==
            kernel::KernelError::OutOfRange);

    const auto after = system.defaults.snapshot();
    REQUIRE(after.generation == before.generation);
    REQUIRE(after.ppem == 15);
    kernel::user::Guard guard;
    REQUIRE(kernel::user::caption_height_locked() == caption);
    REQUIRE(kernel::user::client_rect_locked(*child) == client);
    for (uint32_t i = 0; i < 3; ++i) {
      REQUIRE(scene.windows[i]->rect == rects[i]);
      REQUIRE(scene.windows[i]->restoredRect == restoredRects[i]);
      REQUIRE(scene.windows[i]->pendingFontGeneration == 0);
      REQUIRE(scene.windows[i]->pendingGeometry == 0);
    }
    REQUIRE(memcmp(pixels, scene.pixels, sizeof(pixels)) == 0);
  };
  attempt({100, 32740, 150, 32760}, {});
  attempt({50, 100, 100, 130}, {110, 32745, 160, 32765});

  {
    kernel::user::Guard guard;
    grandchild->rect = {50, 100, 100, 130};
    grandchild->restoredRect = {};
  }
  REQUIRE_FALSE(kernel::user::set_default_font(system.defaults, faces.regular, faces.bold, 24));
  REQUIRE(system.defaults.snapshot().ppem == 24);
  REQUIRE(grandchild->pendingFontGeneration == system.defaults.snapshot().generation);
}

TEST_CASE("Handle reservations are invisible to user handle operations and bind once",
          "[user][font][lifetime]") {
  Scene scene;
  auto &handles = scene.process->handles;
  const auto access = kernel::ProcessHandleTable::HandleAccess::Read;
  const auto flags = kernel::ProcessHandleTable::HandleFlags::None;
  const KObjectPtr<kernel::KObject> object(scene.process.get());
  const size_t entries = handles.entries.size();

  const auto token = handles.Reserve(access, flags);
  REQUIRE(token);
  REQUIRE(handles.entries.size() == entries + 1);
  const KHandle number = handles.entries.back().handle;
  REQUIRE_FALSE(handles.Lookup(number));
  REQUIRE_FALSE(handles.HasAccess(number, access));
  REQUIRE(handles.Close(number) == kernel::KernelError::InvalidHandle);
  REQUIRE(handles.entries.size() == entries + 1);

  REQUIRE(handles.Bind(*token, nullptr).error() == kernel::KernelError::InvalidArgument);
  const auto bound = handles.Bind(*token, object);
  REQUIRE(bound);
  REQUIRE(*bound == number);
  REQUIRE(handles.Lookup(number));
  REQUIRE(handles.Lookup(number)->object.get() == object.get());
  REQUIRE(handles.Bind(*token, object).error() == kernel::KernelError::InvalidHandle);
  REQUIRE(handles.Cancel(*token) == kernel::KernelError::InvalidHandle);
  REQUIRE(handles.Lookup(number));
  REQUIRE_FALSE(handles.Close(number));
  REQUIRE(handles.entries.size() == entries);
}

TEST_CASE("A stale handle reservation cannot bind or cancel the entry that reuses its slot",
          "[user][font][lifetime]") {
  Scene scene;
  auto &handles = scene.process->handles;
  const auto access = kernel::ProcessHandleTable::HandleAccess::Read;
  const auto flags = kernel::ProcessHandleTable::HandleFlags::None;
  const KObjectPtr<kernel::KObject> object(scene.process.get());
  const size_t entries = handles.entries.size();

  const auto stale = handles.Reserve(access, flags);
  REQUIRE(stale);
  const KHandle number = handles.entries.back().handle;
  REQUIRE_FALSE(handles.Cancel(*stale));
  REQUIRE(handles.entries.size() == entries);

  // A new reservation reuses the freed number.
  const auto fresh = handles.Reserve(access, flags);
  REQUIRE(fresh);
  REQUIRE(handles.entries.back().handle == number);
  REQUIRE(handles.Bind(*stale, object).error() == kernel::KernelError::InvalidHandle);
  REQUIRE(handles.Cancel(*stale) == kernel::KernelError::InvalidHandle);
  REQUIRE(handles.entries.size() == entries + 1);
  REQUIRE_FALSE(handles.Lookup(number));
  const auto bound = handles.Bind(*fresh, object);
  REQUIRE(bound);
  REQUIRE(*bound == number);

  // A bound entry that reuses the number is equally out of reach.
  REQUIRE_FALSE(handles.Close(number));
  const auto reused = handles.Allocate(object, access, flags);
  REQUIRE(reused);
  REQUIRE(*reused == number);
  REQUIRE(handles.Bind(*fresh, object).error() == kernel::KernelError::InvalidHandle);
  REQUIRE(handles.Cancel(*fresh) == kernel::KernelError::InvalidHandle);
  REQUIRE(handles.Lookup(number));
  REQUIRE_FALSE(handles.Close(number));
  REQUIRE(handles.entries.size() == entries);
}

namespace kernel::gfx::test_hooks {
extern void (*drawTextAfterRetain)();
}

TEST_CASE("DrawText keeps its retained font alive and rejects a DC deleted during preparation",
          "[user][font][lifetime]") {
  Scene scene;
  RestoreMenuFont restoreMenu;
  clear_menu_font();
  auto &system = *kernel::font::font_system();
  RestoreDefaultFont restoreDefault{system};
  REQUIRE_FALSE(kernel::user::set_builtin_default_font(system.defaults));
  CallerProcess caller(*scene.process);

  const uint32_t font = acquire_font(FontSource_AcquireSystemDefault);
  const auto object = font_object(*scene.process, font);
  static uint32_t dc;
  dc = create_dc();
  uint32_t previous = 0;
  REQUIRE_FALSE(select_font(dc, font, previous));
  REQUIRE_FALSE(close_font(font));
  REQUIRE(object->reference_count.load() == 2);

  static KObjectPtr<kernel::font::FontObject> *observed;
  static uint32_t countAfterDelete;
  observed = const_cast<KObjectPtr<kernel::font::FontObject> *>(&object);
  countAfterDelete = 0;
  kernel::gfx::test_hooks::drawTextAfterRetain = [] {
    REQUIRE_FALSE(delete_dc(dc));
    countAfterDelete = (*observed)->reference_count.load();
  };
  memset(scene.pixels, 7, sizeof(scene.pixels));
  const auto result = draw_text(dc, 10, 10, "Hg");
  kernel::gfx::test_hooks::drawTextAfterRetain = nullptr;

  // DeleteDC dropped the slot reference; DrawText's own reference kept it alive.
  REQUIRE(countAfterDelete == 2);
  REQUIRE(result == kernel::KernelError::InvalidHandle);
  REQUIRE(ink_in(scene.pixels, {0, 0, 640, 480}, 7) == 0);
  REQUIRE(object->reference_count.load() == 1);

  // Slot reuse during preparation is also rejected and leaves the new DC untouched.
  dc = create_dc();
  const uint32_t again = acquire_font(FontSource_AcquireBuiltinBitmap);
  REQUIRE_FALSE(select_font(dc, again, previous));
  static uint32_t replacement;
  replacement = 0;
  kernel::gfx::test_hooks::drawTextAfterRetain = [] {
    REQUIRE_FALSE(delete_dc(dc));
    replacement = create_dc();
  };
  const uint32_t first = dc;
  const auto reusedResult = draw_text(first, 10, 10, "Hg");
  kernel::gfx::test_hooks::drawTextAfterRetain = nullptr;
  REQUIRE(reusedResult == kernel::KernelError::InvalidHandle);
  REQUIRE((replacement & 0xFFFF) == (first & 0xFFFF));
  REQUIRE(replacement != first);
  REQUIRE(ink_in(scene.pixels, {0, 0, 640, 480}, 7) == 0);
  {
    kernel::KDeviceContextSlot *slot = scene.process->dcTable.Resolve(replacement);
    REQUIRE(slot);
    REQUIRE_FALSE(slot->selectedFont);
  }
  REQUIRE_FALSE(delete_dc(replacement));
  REQUIRE_FALSE(close_font(again));
}
