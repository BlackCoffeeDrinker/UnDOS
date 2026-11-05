#pragma once

#include <stdint.h>
#include <undos/syscalls.h>

typedef uint32_t (*UDOSGUIInvalidateRectV2Call)(
    uint32_t hwnd, const struct Rect *rect, uint32_t whole, uint32_t erase);

static inline uint32_t udosgui_invalidate_rect_v2(
    uint32_t hwnd, const struct Rect *rect, int32_t erase,
    UDOSGUIInvalidateRectV2Call call) {
  struct Rect native = {0, 0, 0, 0};
  if (rect) native = *rect;
  return call(hwnd, &native, rect ? 0u : 1u, erase ? 1u : 0u);
}

static inline uint32_t udosgui_invalidate_whole_v2(
    uint32_t hwnd, UDOSGUIInvalidateRectV2Call call) {
  const struct Rect empty = {0, 0, 0, 0};
  return call(hwnd, &empty, 1u, 1u);
}
