#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

struct PaintInfo {
    /** Invalid area in window coordinates. */
    struct Rect rcPaint;
    /** Non-zero when the background was erased by the kernel. */
    uint32_t erased;
};
