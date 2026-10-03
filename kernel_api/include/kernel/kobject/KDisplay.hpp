#pragma once

#include <kernel/__core.hpp>
#include <kernel/cfunc.hpp>
#include <kernel/fwd/KObjectPtr.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/kobject/KSurface.hpp>
#include <kernel/kobject/ObjectType.hpp>

namespace kernel {
struct KGraphicsAdapter;

// Display object representing a mode/connector/framebuffer
struct KDisplay : KObjectT<KDisplay, 1, TYPE_DISPLAY> {
  using KObjectT::KObjectT;
  KObjectPtr<KGraphicsAdapter> adapter;

  // Surface backing this display
  KObjectPtr<KSurface> surface;
  KDisplayModeInfo currentMode;

  cfunc<size_t(const KObjectPtr<KDisplay> &)> GetModeCount;
  cfunc<bool(const KObjectPtr<KDisplay> &, size_t modeIndex, KDisplayModeInfo &outInfo)> GetModeInfo;

  // Lifecycle hooks
  cfunc<bool(const KObjectPtr<KDisplay> &)> Enable;// program hardware mode and map framebuffer
  cfunc<void(const KObjectPtr<KDisplay> &)> Disable;
  cfunc<bool(const KObjectPtr<KDisplay> &)> Validate;// verify mode still present

  // Optional: power management, hotplug
  cfunc<void(const KObjectPtr<KDisplay> &)> OnSuspend;
  cfunc<void(const KObjectPtr<KDisplay> &)> OnResume;
};

template<>
struct ObjectTypeOf<TYPE_DISPLAY> {
  using type = KDisplay;
};
}// namespace kernel
