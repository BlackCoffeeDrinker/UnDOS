#ifndef UNDOSGUI_PRIVATE_FONT_MAPPING_H
#define UNDOSGUI_PRIVATE_FONT_MAPPING_H

#include <undosgui/extensions.h>
#include <undos/syscalls/__enum_FontSource.h>
#include <undos/syscalls/__enum_GlyphFallbackPolicy.h>
#include <undos/syscalls/__enum_TextLayoutMode.h>
#include <undos/syscalls/__enum_TextRepertoire.h>
#include <undos/syscalls/__enum_TextStyle.h>

static int ui_map_font_source(uint32_t value, enum FontSource *native) {
  switch (value) {
    case UNDOS_FONT_SYSTEM_DEFAULT:
      *native = FontSource_AcquireSystemDefault;
      return 1;
    case UNDOS_FONT_BUILTIN_BITMAP:
      *native = FontSource_AcquireBuiltinBitmap;
      return 1;
    default: return 0;
  }
}

static int ui_map_glyph_fallback(uint32_t value,
                                 enum GlyphFallbackPolicy *native) {
  switch (value) {
    case UNDOS_GLYPH_FALLBACK_NOTDEF:
      *native = GlyphFallbackPolicy_FallbackFaceNotdef;
      return 1;
    case UNDOS_GLYPH_FALLBACK_BITMAP:
      *native = GlyphFallbackPolicy_FallbackBitmapForUnmapped;
      return 1;
    default: return 0;
  }
}

static int ui_map_text_style(uint32_t value, enum TextStyle *native) {
  switch (value) {
    case UNDOS_TEXT_REGULAR: *native = TextStyle_StyleRegular; return 1;
    case UNDOS_TEXT_BOLD: *native = TextStyle_StyleBold; return 1;
    default: return 0;
  }
}

static int ui_map_text_layout(uint32_t value, enum TextLayoutMode *native) {
  switch (value) {
    case UNDOS_TEXT_PROPORTIONAL:
      *native = TextLayoutMode_LayoutProportional;
      return 1;
    case UNDOS_TEXT_UNIFORM_GRID:
      *native = TextLayoutMode_LayoutUniformGrid;
      return 1;
    default: return 0;
  }
}

static int ui_map_text_repertoire(uint32_t value,
                                 enum TextRepertoire *native) {
  switch (value) {
    case UNDOS_TEXT_BASIC:
      *native = TextRepertoire_RepertoireBasic;
      return 1;
    case UNDOS_TEXT_TERMINAL:
      *native = TextRepertoire_RepertoireTerminal;
      return 1;
    default: return 0;
  }
}

#endif
