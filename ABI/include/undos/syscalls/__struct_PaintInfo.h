#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

struct PaintInfo {
    /** Invalid area in client coordinates (half-open). */
    struct Rect rcPaint;
    /** Non-zero when the background was erased by the kernel. */
    uint32_t erased;
};
