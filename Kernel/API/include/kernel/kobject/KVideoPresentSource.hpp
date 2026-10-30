#pragma once

#include <kernel/kobject/KDisplay.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/kobject/KSurface.hpp>
#include <kernel/kobject/ObjectType.hpp>

namespace kernel::gfx {
constexpr uint32_t VideoPresentRecordLimit = 64;
constexpr uint32_t DesktopSourceId = 1;

struct KVideoPresentSource final
    : KObjectT<KVideoPresentSource, 1, TYPE_VIDEO_PRESENT_SOURCE> {
  using KObjectT::KObjectT;
  uint32_t sourceId{DesktopSourceId};
  KObjectPtr<KSurface> surface{nullptr};
  KObjectPtr<KDisplay> target{nullptr};
  KDisplayModeInfo currentMode{};
  bool active{false};
};
}// namespace kernel::gfx

namespace kernel {
template<>
struct ObjectTypeOf<TYPE_VIDEO_PRESENT_SOURCE> {
  using type = gfx::KVideoPresentSource;
};
}// namespace kernel
