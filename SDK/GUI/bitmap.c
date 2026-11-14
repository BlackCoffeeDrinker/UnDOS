#include <windows.h>
#include <undosgui/extensions.h>
#include <undoscrt/string.h>

#include "private/backend.h"
#include "private/ui_internal.h"
#include "private/win_internal.h"

#define UI_BITMAP_LIMIT 32
#define UI_BITMAP_HANDLE_TAG 0x00200000u
/* Largest bitmap converted in one piece: 16384 BGRA pixels (WINFILE's
   biggest strip, wbbitmap.bmp, is 288x55). */
#define UI_BITMAP_SCRATCH_BYTES 65536u

typedef struct BitmapRecord {
  BOOL used;
  BOOL internal;
  uint8_t generation;
  uint32_t handle;
  LONG width;
  LONG height;
  uint32_t dc;
} BitmapRecord;

static BitmapRecord bitmaps[UI_BITMAP_LIMIT];
static uint8_t scratch[UI_BITMAP_SCRATCH_BYTES];

void ui_bitmap_reset(void) {
  memset(bitmaps, 0, sizeof(bitmaps));
}

BOOL ui_bitmap_is_handle(uintptr_t handle) {
  return (handle & 0xFFF00000u) == UI_BITMAP_HANDLE_TAG;
}

static BitmapRecord *bitmap_by_handle(uintptr_t handle) {
  if (!ui_bitmap_is_handle(handle)) return NULL;
  const uint32_t index = (uint32_t)(handle & 0xFFu);
  const uint8_t generation = (uint8_t)((handle >> 8) & 0xFFu);
  if (index >= UI_BITMAP_LIMIT || !bitmaps[index].used ||
      bitmaps[index].generation != generation)
    return NULL;
  return &bitmaps[index];
}

static uintptr_t bitmap_value(const BitmapRecord *record) {
  return UI_BITMAP_HANDLE_TAG | ((uintptr_t)record->generation << 8) |
         (uintptr_t)(record - bitmaps);
}

static BOOL fits_scratch(LONG width, LONG height) {
  return width > 0 && height > 0 &&
         (uint64_t)width * (uint64_t)height * 4u <= UI_BITMAP_SCRATCH_BYTES;
}

/* Creates a kernel ARGB32 bitmap; pixels may be NULL for a zeroed bitmap. */
static BitmapRecord *create_bitmap(LONG width, LONG height,
                                   const uint8_t *pixels, BOOL internal) {
  if (width <= 0 || height <= 0) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return NULL;
  }
  BitmapRecord *record = NULL;
  for (uint32_t i = 0; i < UI_BITMAP_LIMIT && !record; ++i)
    if (!bitmaps[i].used) record = &bitmaps[i];
  if (!record) {
    ui_core_failure(UI_STATUS_OUT_OF_MEMORY, ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }
  uint32_t handle = 0;
  const uint64_t bytes = (uint64_t)width * (uint64_t)height * 4u;
  if (bytes > UINT32_MAX) {
    ui_core_failure(UI_STATUS_OUT_OF_MEMORY, ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }
  if (!backend_ok(ui_backend_create_bitmap((uint32_t)width, (uint32_t)height,
                                           pixels, (uint32_t)bytes, &handle)))
    return NULL;
  record->used = TRUE;
  record->internal = internal;
  record->generation = (uint8_t)(record->generation + 1u);
  record->handle = handle;
  record->width = width;
  record->height = height;
  record->dc = 0;
  return record;
}

static void release_bitmap(BitmapRecord *record) {
  (void)ui_backend_delete_bitmap(record->handle);
  record->used = FALSE;
}

static DCRecord *bitmap_dc(HDC dc) {
  DCRecord *record = dc_by_handle(hdc_value(dc));
  if (!record) ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
  return record;
}

HDC CreateCompatibleDC(HDC dc) {
  if (dc && !bitmap_dc(dc)) return NULL;
  DCRecord *record = dc_slot();
  if (!record) {
    ui_core_failure(UI_STATUS_OUT_OF_MEMORY, ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }
  uint32_t handle = 0;
  if (!backend_ok(ui_backend_create_memory_dc(&handle))) return NULL;
  /* Win32 memory DCs start with a 1x1 bitmap selected. */
  BitmapRecord *initial = create_bitmap(1, 1, NULL, TRUE);
  if (!initial || !backend_ok(ui_backend_select_bitmap(handle, initial->handle))) {
    DWORD error = GetLastError();
    DWORD raw = UndosGetLastStatus();
    if (initial) release_bitmap(initial);
    (void)ui_backend_delete_dc(handle);
    ui_core_failure(raw, error);
    return NULL;
  }
  initial->dc = handle;
  memset(record, 0, sizeof(*record));
  record->used = TRUE;
  record->ended = TRUE;
  record->handle = handle;
  record->textColor = RGB(0, 0, 0);
  record->backgroundColor = RGB(255, 255, 255);
  record->opaque = TRUE;
  record->memory = TRUE;
  record->bitmap = bitmap_value(initial);
  record->defaultBitmap = record->bitmap;

  UNDOS_FONT font;
  uint32_t generation, previous;
  if (UndosAcquireFont(UNDOS_FONT_SYSTEM_DEFAULT, UNDOS_GLYPH_FALLBACK_NOTDEF,
                       &font, &generation)) {
    if (ui_backend_select_font(handle, font, &previous) == UI_STATUS_OK) {
      record->currentFont = font;
      record->hasFont = TRUE;
      record->defaultFont = font;
      record->hasDefaultFont = TRUE;
    } else {
      (void)ui_backend_close_font(font);
    }
  }
  return hdc_from_value(handle);
}

BOOL DeleteDC(HDC dc) {
  DCRecord *record = bitmap_dc(dc);
  if (!record) return FALSE;
  if (!record->memory) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if (!backend_ok(ui_backend_delete_dc(record->handle))) return FALSE;
  BitmapRecord *selected = bitmap_by_handle(record->bitmap);
  if (selected) selected->dc = 0;
  BitmapRecord *initial = bitmap_by_handle(record->defaultBitmap);
  if (initial) release_bitmap(initial);
  if (record->hasDefaultFont) (void)ui_backend_close_font(record->defaultFont);
  record->used = FALSE;
  return TRUE;
}

static HGDIOBJ select_bitmap(DCRecord *record, uintptr_t handle) {
  BitmapRecord *bitmap = bitmap_by_handle(handle);
  if (!bitmap || bitmap->internal || !record->memory ||
      (bitmap->dc && bitmap->dc != record->handle)) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return NULL;
  }
  const uintptr_t previous = record->bitmap;
  if (previous == handle) return (HGDIOBJ)previous;
  if (!backend_ok(ui_backend_select_bitmap(record->handle, bitmap->handle)))
    return NULL;
  BitmapRecord *old = bitmap_by_handle(previous);
  if (old) old->dc = 0;
  bitmap->dc = record->handle;
  record->bitmap = handle;
  return (HGDIOBJ)previous;
}

/* Selecting the 1x1 default bitmap back is the usual way to deselect. */
static HGDIOBJ restore_default(DCRecord *record, uintptr_t handle) {
  BitmapRecord *initial = bitmap_by_handle(handle);
  if (!initial) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return NULL;
  }
  const uintptr_t previous = record->bitmap;
  if (previous == handle) return (HGDIOBJ)previous;
  if (!backend_ok(ui_backend_select_bitmap(record->handle, initial->handle)))
    return NULL;
  BitmapRecord *old = bitmap_by_handle(previous);
  if (old) old->dc = 0;
  initial->dc = record->handle;
  record->bitmap = handle;
  return (HGDIOBJ)previous;
}

HGDIOBJ ui_bitmap_select(DCRecord *record, uintptr_t handle) {
  if (record->memory && handle == record->defaultBitmap)
    return restore_default(record, handle);
  return select_bitmap(record, handle);
}

BOOL ui_bitmap_delete(uintptr_t handle) {
  BitmapRecord *bitmap = bitmap_by_handle(handle);
  if (!bitmap || bitmap->internal) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if (bitmap->dc) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  release_bitmap(bitmap);
  return TRUE;
}

int GetObject(HGDIOBJ object, int size, LPVOID buffer) {
  BitmapRecord *bitmap = bitmap_by_handle((uintptr_t)object);
  if (!bitmap) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
    return 0;
  }
  if (!buffer) return (int)sizeof(BITMAP);
  if (size < (int)sizeof(BITMAP)) {
    ui_core_failure(UI_STATUS_INVALID_SIZE, ERROR_INSUFFICIENT_BUFFER);
    return 0;
  }
  BITMAP result = {0, bitmap->width, bitmap->height, bitmap->width * 4, 1, 32,
                   NULL};
  memcpy(buffer, &result, sizeof(result));
  return (int)sizeof(BITMAP);
}

HBITMAP CreateCompatibleBitmap(HDC dc, int width, int height) {
  if (!bitmap_dc(dc)) return NULL;
  if (width < 0 || height < 0) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return NULL;
  }
  if (!width || !height) width = height = 1;
  BitmapRecord *record = create_bitmap(width, height, NULL, FALSE);
  return record ? (HBITMAP)bitmap_value(record) : NULL;
}

HBITMAP CreateBitmap(int width, int height, UINT planes, UINT bitsPerPixel,
                     const void *bits) {
  if (width <= 0 || height <= 0 || planes != 1) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return NULL;
  }
  if (bitsPerPixel != 1 && bitsPerPixel != 32) {
    ui_core_failure(UI_STATUS_UNSUPPORTED, ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
  }
  if (!bits) {
    BitmapRecord *record = create_bitmap(width, height, NULL, FALSE);
    return record ? (HBITMAP)bitmap_value(record) : NULL;
  }
  if (!fits_scratch(width, height)) {
    ui_core_failure(UI_STATUS_OUT_OF_MEMORY, ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }
  const uint8_t *source = (const uint8_t *)bits;
  if (bitsPerPixel == 32) {
    memcpy(scratch, source, (size_t)width * (size_t)height * 4u);
  } else {
    /* Monochrome rows are WORD aligned; set bits are white. */
    const size_t stride = (((size_t)width + 15u) / 16u) * 2u;
    for (LONG y = 0; y < height; ++y) {
      for (LONG x = 0; x < width; ++x) {
        const BOOL set = (source[(size_t)y * stride + (size_t)x / 8u] >>
                          (7u - (unsigned)x % 8u)) & 1u;
        uint8_t *pixel = scratch + ((size_t)y * (size_t)width + (size_t)x) * 4u;
        pixel[0] = pixel[1] = pixel[2] = set ? 0xFFu : 0u;
        pixel[3] = 0xFFu;
      }
    }
  }
  BitmapRecord *record = create_bitmap(width, height, scratch, FALSE);
  return record ? (HBITMAP)bitmap_value(record) : NULL;
}

typedef struct DibLayout {
  LONG width;
  LONG height;
  BOOL topDown;
  UINT bitCount;
  size_t stride;
  const RGBQUAD *palette;
  UINT colors;
} DibLayout;

static BOOL dib_layout(const BITMAPINFO *info, DibLayout *layout) {
  if (!info) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  const BITMAPINFOHEADER *header = &info->bmiHeader;
  const UINT bits = header->biBitCount;
  if (header->biSize < sizeof(BITMAPINFOHEADER) || header->biWidth <= 0 ||
      header->biHeight == 0 || header->biPlanes != 1) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (header->biCompression != BI_RGB ||
      (bits != 1 && bits != 4 && bits != 8 && bits != 24 && bits != 32)) {
    ui_core_failure(UI_STATUS_UNSUPPORTED, ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
  }
  layout->width = header->biWidth;
  layout->topDown = header->biHeight < 0;
  layout->height = layout->topDown ? -header->biHeight : header->biHeight;
  layout->bitCount = bits;
  layout->stride = (((size_t)layout->width * bits + 31u) / 32u) * 4u;
  layout->palette = (const RGBQUAD *)((const uint8_t *)info + header->biSize);
  layout->colors = header->biClrUsed ? header->biClrUsed
                   : bits <= 8 ? (1u << bits) : 0u;
  if (bits <= 8 && layout->colors > (1u << bits)) layout->colors = 1u << bits;
  return TRUE;
}

static void convert_row(const DibLayout *layout, const uint8_t *row,
                        LONG width, uint8_t *out) {
  for (LONG x = 0; x < width; ++x, out += 4) {
    uint8_t b = 0, g = 0, r = 0;
    if (layout->bitCount <= 8) {
      const size_t bit = (size_t)x * layout->bitCount;
      const UINT shift = 8u - layout->bitCount - (UINT)(bit % 8u);
      const UINT index = (row[bit / 8u] >> shift) & ((1u << layout->bitCount) - 1u);
      if (index < layout->colors) {
        b = layout->palette[index].rgbBlue;
        g = layout->palette[index].rgbGreen;
        r = layout->palette[index].rgbRed;
      }
    } else {
      const uint8_t *pixel = row + (size_t)x * (layout->bitCount / 8u);
      b = pixel[0];
      g = pixel[1];
      r = pixel[2];
    }
    out[0] = b;
    out[1] = g;
    out[2] = r;
    out[3] = 0xFFu;
  }
}

/* Converts lines [start, start + lines) of a DIB into the scratch image of a
   width x height bitmap. Returns the number of lines converted. */
static int convert_dib(const DibLayout *layout, const uint8_t *bits, UINT start,
                       UINT lines, LONG width, LONG height) {
  const LONG copyWidth = layout->width < width ? layout->width : width;
  int converted = 0;
  for (UINT i = 0; i < lines; ++i) {
    const UINT line = start + i;
    if (line >= (UINT)layout->height) break;
    const LONG row = layout->topDown ? (LONG)line : layout->height - 1 - (LONG)line;
    if (row < height) {
      convert_row(layout, bits + (size_t)i * layout->stride, copyWidth,
                  scratch + (size_t)row * (size_t)width * 4u);
    }
    ++converted;
  }
  return converted;
}

int SetDIBits(HDC dc, HBITMAP handle, UINT start, UINT lines,
              const void *bits, const BITMAPINFO *info, UINT usage) {
  (void)dc;
  BitmapRecord *bitmap = bitmap_by_handle((uintptr_t)handle);
  if (!bitmap || bitmap->internal) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
    return 0;
  }
  if (!bits || usage != DIB_RGB_COLORS) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return 0;
  }
  DibLayout layout;
  if (!dib_layout(info, &layout)) return 0;
  if (!fits_scratch(bitmap->width, bitmap->height)) {
    ui_core_failure(UI_STATUS_OUT_OF_MEMORY, ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }
  const uint32_t bytes = (uint32_t)(bitmap->width * bitmap->height * 4);
  const BOOL whole = start == 0 && lines >= (UINT)layout.height &&
                     layout.height >= bitmap->height &&
                     layout.width >= bitmap->width;
  if (!whole &&
      !backend_ok(ui_backend_get_bitmap_bits(bitmap->handle, scratch, bytes)))
    return 0;
  const int converted = convert_dib(&layout, (const uint8_t *)bits, start, lines,
                                    bitmap->width, bitmap->height);
  if (!backend_ok(ui_backend_set_bitmap_bits(bitmap->handle, scratch, bytes)))
    return 0;
  return converted;
}

HBITMAP ui_bitmap_from_dib(const BITMAPINFO *info, const void *bits) {
  DibLayout layout;
  if (!dib_layout(info, &layout)) return NULL;
  if (!fits_scratch(layout.width, layout.height)) {
    ui_core_failure(UI_STATUS_OUT_OF_MEMORY, ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }
  (void)convert_dib(&layout, (const uint8_t *)bits, 0, (UINT)layout.height,
                    layout.width, layout.height);
  BitmapRecord *record = create_bitmap(layout.width, layout.height, scratch,
                                       FALSE);
  return record ? (HBITMAP)bitmap_value(record) : NULL;
}

BOOL BitBlt(HDC dst, int x, int y, int width, int height, HDC src, int srcX,
            int srcY, DWORD rop) {
  DCRecord *target = bitmap_dc(dst);
  if (!target) return FALSE;
  if (rop == BLACKNESS || rop == WHITENESS || rop == PATCOPY)
    return PatBlt(dst, x, y, width, height, rop);
  if (rop != SRCCOPY) {
    ui_core_failure(UI_STATUS_UNSUPPORTED, ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
  }
  DCRecord *source = bitmap_dc(src);
  if (!source) return FALSE;
  if (width < 0 || height < 0) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (!width || !height) return TRUE;
  return backend_ok(ui_backend_bit_blt(target->handle, x, y, (uint32_t)width,
                                       (uint32_t)height, source->handle, srcX,
                                       srcY));
}
