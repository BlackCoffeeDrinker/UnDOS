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

  cfunc<size_t()> GetModeCount;
  cfunc<bool(size_t modeIndex, KDisplayModeInfo &outInfo)> GetModeInfo;

  // Lifecycle hooks
  cfunc<bool()> Enable;// program hardware mode and map framebuffer
  cfunc<void()> Disable;
  cfunc<bool()> Validate;// verify mode still present

  // Optional: power management, hotplug
  cfunc<void()> OnSuspend;
  cfunc<void()> OnResume;
};

template<>
struct ObjectTypeOf<TYPE_DISPLAY> {
  using type = KDisplay;
};
}// namespace kernel
