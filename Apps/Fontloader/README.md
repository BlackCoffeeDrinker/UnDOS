# Font Loader (F02)

`/System/fontload.elf /absolute/font.ttf` reads a file and uploads a normalized
UFT1 resource in chunks of at most 4096 bytes. It performs shared structural
validation before upload. A successful run reports **registered (rendering
unavailable)** and closes its private face. This form does not publish a
system font.

`/System/fontload.elf /absolute/regular.ttf /absolute/bold.ttf ppem` uploads both
faces and calls `SetSystemFont` with the TrueType backend (ppem 1 to 256), then
closes its handles. The kernel retains the published faces. A failed
publication leaves the active font unchanged. Init can instead select the
built-in bitmap backend directly through the same publication syscall.
Acquired font objects retain a generation snapshot for metrics, measurement,
and drawing. USER chrome, SDK controls, and migrated applications derive
text-dependent geometry from those metrics and measured runs.

Font replacement prepares metrics and chrome geometry before USER commits a
new generation. USER rejects a candidate with `OutOfRange` if cumulative
coordinate shifts would exceed its signed 16-bit window-coordinate range;
the previous font and geometry remain active. Oversized menus and other
scrollable content retain their measured dimensions and use the actual
viewport for clipping and keyboard visibility.

Normal init startup reads `/System/FONTS.INI`. `Backend=truetype` selects
regular and bold paths and an integer point size. Init converts the point size
at fixed 96 DPI and invokes Fontloader; the shipped 11-point setting maps to
15 ppem. `Backend=bitmap` selects the built-in face without loading files.
Missing, unreadable, or invalid configuration and failed publication stop
desktop startup with a serial diagnostic.

`FONTS.INI` uses one `[Fonts]` section and a required `Backend` key. The value
is `bitmap` or `truetype`. The bitmap backend accepts no face paths or point
size. The TrueType backend requires one `Regular`, `Bold`, and `SizePt` key.
Paths must be absolute and 1–255 ASCII bytes. Quote a path when it contains
spaces; escapes and inline comments are not supported. The file must be ASCII,
have no byte-order mark or NUL, and contain at most 1023 bytes. The parser
rejects duplicate or unknown sections and keys. `SizePt` must be a base-10
integer from 1 through 192. Init uses integer rounding:
`ppem = (SizePt * 96 + 36) / 72`.

The image includes unmodified Source Sans 3 regular and bold faces and their
SIL Open Font License under `/System/Fonts`. To load them and show the SDK
text preview, run:

```text
/System/fontload.elf /System/Fonts/SSR.TTF /System/Fonts/SSB.TTF 12 --preview
```

`/System/fontload.elf --preview` is a shorthand for this packaged font pair
at 12 ppem. Preview is an independent manual command; it does not read
`/System/FONTS.INI`.

`/System/fontload.elf --set-default ppem` publishes the packaged Source Sans 3
regular and bold faces at the requested ppem, then exits without opening the
preview. Use the explicit-path form when you need other faces.
`/System/fontload.elf ppem` is a short form for the same packaged-font
operation.

The optional preview draws regular, bold, and transparent text through the
unified GUI font object. Blue rules show measured text widths. Escape or
closing the preview exits the loader. Omit `--preview` to publish the font and
exit without opening a window.

`fontdecode` is the reusable freestanding decoder. `decode(Bytes, vector&)`
returns an explicit `DecodeError` and clears output on failure. Input must not
alias output. It accepts standalone static TrueType SFNT version 1 or `true`,
with required `head`, `hhea`, `hmtx`, `maxp` 1.0, `loca`, `glyf`, and Unicode
`cmap` format 4 or 12. It chooses one Unicode subtable, preferring format 12,
then Windows Unicode over platform 0; it does not merge different subtables.
Coordinates, contour numbering, point attachment, signed transforms, metrics,
CVT and original program bytes are retained. Composite graphs are checked for
cycles, depth, work and expanded point/contour budgets.
Zero-contour glyphs may have only a glyph header, or an instruction-length
field and optional instructions. Both forms retain their advance and bearing;
an incomplete instruction-length field or truncated program is rejected.

Collections, CFF/CFF2, variable, bitmap and color outline tables are explicitly
rejected. Unsupported-only cmaps are rejected. Strict table overlap, length,
alignment and zero glyph-padding checks can reject noncanonical font files.
SFNT checksums and name/layout tables are not identity or execution trust.
`gasp`, kerning, shaping and other rendering policy are not exported in UFT1;
F02 only registers outlines and programs and makes no rendering-policy claim.
Style comes from `head.macStyle` and `post.isFixedPitch`; weight comes from
`OS/2.usWeightClass` or defaults to 400 when OS/2 is absent.

All declared wire-format limits apply, including 4 MiB input, 8 MiB output and
4096 glyphs. Intermediate section storage is bounded by the output budget.
No exceptions or hosted runtime are required. Like existing `kstd::vector`,
the decoder requires a working allocation provider; the application reports
allocation failure and terminates rather than throwing or continuing with a
null allocation. Bytecode is preserved, not semantically validated or executed.

Hosted tests use synthetic SFNT binaries and the three pinned OFL-licensed
fonts in `Tests/Hosted/fixtures/fonts/`. The corpus tests check decoding,
independent UFT1 validation, glyph counts, weights, and program lengths.
No third-party font engine is used. Decoder validation alone does not execute
instructions or render glyphs; separate hosted font tests cover those paths.
