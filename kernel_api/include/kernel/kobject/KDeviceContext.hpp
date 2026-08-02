#pragma once

#include <kernel/__core.hpp>
#include <kernel/fwd/KObjectPtr.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/kobject/KSurface.hpp>
#include <kernel/kobject/ObjectType.hpp>

namespace kernel {
struct KDisplay;

// GDI-style device context: target of drawing primitives (e.g. Rectangle, LineTo, BitBlt).
struct KDeviceContext : KObjectT<KDeviceContext, 1, TYPE_DEVICE_CONTEXT> {
  using KObjectT::KObjectT;
  KObjectPtr<KDisplay> display;       // target display (primary DC)
  KObjectPtr<KSurface> target;        // currently selected surface (display surface or a bitmap)
  KObjectPtr<KSurface> selectedBitmap;// last SelectObject'd bitmap, if any

  uint32_t penColor = 0;

  // MoveTo/LineTo cursor
  int32_t currentX = 0;
  int32_t currentY = 0;
};

template<>
struct ObjectTypeOf<TYPE_DEVICE_CONTEXT> {
  using type = KDeviceContext;
};
}// namespace kernel
