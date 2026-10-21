#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

struct TextMetrics {
    uint32_t cellWidth;
    uint32_t cellHeight;
    uint32_t ascent;
    uint32_t descent;
    uint32_t firstChar;
    uint32_t lastChar;
    uint32_t defaultChar;
};
