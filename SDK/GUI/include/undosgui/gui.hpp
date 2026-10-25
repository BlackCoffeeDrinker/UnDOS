#pragma once

#include <userspace.h>
#include <undos/syscalls.h>

namespace gui {

using Procedure = void (*)(const Msg &);
constexpr uint32_t Face = 7;
constexpr uint32_t Ink = 0;
constexpr uint32_t Paper = 15;
constexpr uint32_t Highlight = 1;

bool check(uint32_t result, const char *operation);
bool register_class(const char *name, Procedure procedure, uint32_t background = Face);
uint32_t window(const char *className, const char *title, int x, int y, int width, int height,
                uint32_t parent = 0, uint32_t data = 0);
void dispatch(const Msg &message);
bool next(Msg &message);
bool destroy(uint32_t hwnd);
bool invalidate(uint32_t hwnd);
bool show(uint32_t hwnd, ShowCommand command = ShowCommand_Show);
bool move(uint32_t hwnd, int x, int y, int width, int height);
Rect client(uint32_t hwnd);
uint32_t data(uint32_t hwnd);
int mouse_x(const Msg &message);
int mouse_y(const Msg &message);
bool inside(int x, int y, int left, int top, int right, int bottom);

enum class FontStyle : uint32_t { Regular = TextStyle_StyleRegular, Bold = TextStyle_StyleBold };
enum class FontSource : uint32_t {
  SystemDefault = FontSource_AcquireSystemDefault,
  BuiltinBitmap = FontSource_AcquireBuiltinBitmap
};
enum class TextLayoutMode : uint32_t {
  Proportional = TextLayoutMode_LayoutProportional,
  UniformGrid = TextLayoutMode_LayoutUniformGrid
};
enum class TextRepertoire : uint32_t {
  Basic = TextRepertoire_RepertoireBasic,
  Terminal = TextRepertoire_RepertoireTerminal
};
enum class GlyphFallback : uint32_t {
  FaceNotdef = GlyphFallbackPolicy_FallbackFaceNotdef,
  BitmapForUnmapped = GlyphFallbackPolicy_FallbackBitmapForUnmapped
};

struct TextExtent {
  uint32_t width{0};
  uint32_t height{0};
  uint32_t baseline{0};
  int32_t inkLeft{0};
  int32_t inkTop{0};
  int32_t inkRight{0};
  int32_t inkBottom{0};
  uint32_t generation{0};
};

struct TextMetrics {
  uint32_t generation{0};
  uint32_t ascent{0};
  uint32_t descent{0};
  uint32_t lineGap{0};
  uint32_t lineHeight{0};
  uint32_t baseline{0};
  uint32_t cellWidth{0};
  uint32_t cellHeight{0};
  int32_t inkLeft{0};
  int32_t inkTop{0};
  int32_t inkRight{0};
  int32_t inkBottom{0};
};

class Font {
public:
  Font() = default;
  ~Font();
  Font(const Font &) = delete;
  Font &operator=(const Font &) = delete;
  Font(Font &&other) noexcept;
  Font &operator=(Font &&other) noexcept;

  static Font acquire(FontSource source = FontSource::SystemDefault,
                      GlyphFallback fallback = GlyphFallback::FaceNotdef);
  explicit operator bool() const { return valid_; }
  uint32_t generation() const { return generation_; }
  bool metrics(TextMetrics &result, FontStyle style = FontStyle::Regular,
               TextRepertoire repertoire = TextRepertoire::Basic) const;
  bool measure(const char *text, TextExtent &result,
               FontStyle style = FontStyle::Regular,
               TextLayoutMode layout = TextLayoutMode::Proportional,
               TextRepertoire repertoire = TextRepertoire::Basic) const;
  bool select(uint32_t dc) const;

private:
  uint32_t handle_{0};
  uint32_t generation_{0};
  bool valid_{false};
};

Font &system_font();
void refresh_system_font();

class TextLayout {
public:
  explicit TextLayout(const Font &font, FontStyle style = FontStyle::Regular,
                      TextLayoutMode mode = TextLayoutMode::Proportional,
                      TextRepertoire repertoire = TextRepertoire::Basic)
      : font_(font), style_(style), mode_(mode), repertoire_(repertoire) {}

  bool measure(const char *text, TextExtent &extent) const;
  bool fit_prefix(const char *text, char *output, uint32_t capacity, uint32_t width,
                  TextExtent &extent) const;
  bool fit_tail(const char *text, char *output, uint32_t capacity, uint32_t width,
                TextExtent &extent) const;

private:
  const Font &font_;
  FontStyle style_;
  TextLayoutMode mode_;
  TextRepertoire repertoire_;
};

class Paint {
public:
  explicit Paint(uint32_t hwnd);
  ~Paint();
  Paint(const Paint &) = delete;
  Paint &operator=(const Paint &) = delete;
  explicit operator bool() const { return dc_ != 0; }
  void fill(int left, int top, int right, int bottom, uint32_t color);
  bool draw_text(int x, int y, const char *text, FontStyle style = FontStyle::Regular,
                 uint32_t color = Ink, uint32_t background = Face, bool opaque = true,
                 TextLayoutMode layout = TextLayoutMode::Proportional,
                 TextRepertoire repertoire = TextRepertoire::Basic);
  void button(int x, int y, int width, const char *label, bool focused = false);
  void icon(int x, int y, uint32_t kind, bool selected = false);

private:
  uint32_t hwnd_;
  uint32_t dc_{0};
};

int button_height(const char *label);

struct Field {
  const char *label;
  char *value;
  uint32_t capacity;
};

bool dialog(const char *title, const char *message, Field *fields, uint32_t count,
            bool confirm = true);
void message(const char *title, const char *text);

} // namespace gui
