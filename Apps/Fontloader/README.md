# Font Loader (F02)

`/System/fontload.elf /absolute/font.ttf` reads files and uploads normalized UFT1
resources in chunks of at most 4096 bytes. It performs shared structural
validation before upload. A successful run reports **registered (rendering
unavailable)** and immediately closes its private face. It does not install a
system font, change GUI text, execute instructions, or rasterize glyphs.

`fontdecode` is the reusable freestanding decoder. `decode(Bytes, vector&)`
returns an explicit `DecodeError` and clears output on failure. Input must not
alias output. It accepts standalone static TrueType SFNT version 1 or `true`,
with required `head`, `hhea`, `hmtx`, `maxp` 1.0, `loca`, `glyf`, and Unicode
`cmap` format 4 or 12. It chooses one Unicode subtable, preferring format 12,
then Windows Unicode over platform 0; it does not merge different subtables.
Coordinates, contour numbering, point attachment, signed transforms, metrics,
CVT and original program bytes are retained. Composite graphs are checked for
cycles, depth, work and expanded point/contour budgets.

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

Hosted tests build synthetic SFNT binaries in memory; no external fonts or
third-party font engines are bundled.
