
#pragma once

#include <kernel/__core.hpp>
#include <kernel/gfx/msgring.hpp>
#include <kernel/gfx/region.hpp>
#include <kernel/gfx/window_message.hpp>
#include <kernel/kobject/KObjectT.hpp>

namespace kernel {
constexpr uint32_t kQueueSize = 64;
constexpr uint32_t kWindowClassNameLen = 32;
constexpr uint32_t kWindowTitleLen = 64;

using Ring = user::MessageRing<kQueueSize, user::kMsgMouseMove>;

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
  kstd::static_string<kWindowClassNameLen> name;
  uint32_t wndProc{0};
  uint32_t background{0};
  KProcessObject *process{nullptr};
};

struct KWindowObject : KObjectT<KWindowObject, 1, TYPE_WINDOW> {
  using KObjectT::KObjectT;

  enum class State : uint32_t {
    Restored = 0,
    Minimized = 2,
    Maximized = 3,
  };

  uint32_t hwnd{0};
  uint32_t classIndex{0};
  kstd::static_string<kWindowTitleLen> title;
  uint32_t style{0};
  user::URect rect{};// screen coordinates
  user::URect restoredRect{};
  State state{State::Restored};
  uint32_t parent{0};
  uint32_t userData{0};
  uint32_t wndProc{0};
  uint32_t background{0};

  KProcessObject *process{nullptr};
  MessageQueue *queue{nullptr};

  user::URect invalid{};  // client coordinates
  user::URegion paintClip;// screen coordinates, referenced by the BeginPaint DC
  HDC paintDC{0};
  user::URect painting{};// validated client damage, restored if painting is interrupted
  user::URect paintErase{};// screen bounds of BeginPaint erase pixels still awaiting Present
  uint32_t moveParam{0}, sizeParam{0};
  uint8_t pendingGeometry{0};// coalesced Move/Size survive a full message ring
  bool pendingDisplayChange{false};
  uint32_t displayBpp{0}, displaySize{0};

  // z-order, topmost first; the window table holds the reference.
  KWindowObject *above{nullptr};
  KWindowObject *below{nullptr};

  [[nodiscard]] bool Visible() const noexcept { return (style & user::kStyleVisible) != 0; }
};

}// namespace kernel
