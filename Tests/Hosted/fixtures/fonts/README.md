# TrueType baseline fixtures

These unmodified static TrueType fonts form the F00 redistributable reference
corpus. They provide proportional regular and bold faces and a monospaced face.
The image also stages the unmodified Source Sans 3 faces as
`/System/Fonts/SSR.TTF` (regular) and `SSB.TTF` (bold), with their license as
`OFLSS.TXT`. These short filenames support the current filesystem. The fonts
are also the startup default: init waits for Font Loader to publish the pair
at 12 ppem before starting Program Manager.

Both families use the SIL Open Font License 1.1. The corresponding upstream
license files are included in this directory. Adobe reserves the font name
`Source`; keep these font files unmodified. Any modified font must follow the
license terms, including the reserved-name restriction.

| File | Use | Glyphs | Font program | Prep program | Nonempty glyph programs | Glyph-program bytes | SHA-256 |
|------|-----|-------:|-------------:|-------------:|-----------------------:|-------------------:|---------|
| `SourceSans3-Regular.ttf` | Proportional regular | 2,478 | 3,596 B | 261 B | 1,471 | 87,172 B | `4644c81b86ec9caaa76b634889968ed3c4f4f52f054855933acc7c2b21e53b0f` |
| `SourceSans3-Bold.ttf` | Proportional bold | 2,478 | 3,596 B | 261 B | 1,465 | 86,058 B | `9214b9d95e4231c609802815c2646c98174e2102d0d37f88978a7f8e71006e6a` |
| `SourceCodePro-Regular.ttf` | Monospaced regular | 1,568 | 3,596 B | 239 B | 980 | 61,585 B | `74bd80d3e42a08517cd7e1108ba3d86f2da29ac0f3065be95e0357956ab9db37` |

Each file has 19 SFNT tables, including `cmap`, `cvt `, `fpgm`, `gasp`,
`glyf`, `head`, `hhea`, `hmtx`, `loca`, `maxp`, `name`, `OS/2`, `post`, and
`prep`; the files also contain layout tables that the current loader does not
export. The table and instruction figures describe the source files.
Hosted corpus tests decode all three fonts and independently validate their
UFT1 output. At 9, 11, 12, 16, 24, 48 and 256 ppem they probe normalized CVT
entries, execute the original `fpgm` and `prep` programs, and execute simple
glyph programs. Known out-of-range CVT reads are bounded by the corpus test's
failure allowance. Separate glyph and raster tests cover composite assembly,
unhinted fallback and mask generation.

Pinned upstream sources:

- Source Sans 3, `adobe-fonts/source-sans` revision
  `87b37a2daaed80fcb8e8ccb0085c4d72ddade12e`:
  https://github.com/adobe-fonts/source-sans/tree/87b37a2daaed80fcb8e8ccb0085c4d72ddade12e/TTF
- Source Code Pro, `adobe-fonts/source-code-pro` revision
  `803b7e23ec97ae58b6232ea76519a76d428ba268`:
  https://github.com/adobe-fonts/source-code-pro/tree/803b7e23ec97ae58b6232ea76519a76d428ba268/TTF

Verify the stored font files with:

```sh
shasum -a 256 Tests/Hosted/fixtures/fonts/*.ttf
```

`Tests/Hosted/tests/font_corpus_test.cpp` checks the glyph counts, weights,
units per em, and normalized program lengths against this inventory. CMake
embeds the original bytes in the hosted test target; the test does not need
host file APIs or a specific working directory.
Run the corpus alone with the `[corpus]` selector, or include it
with the existing `[font],[fontloader]` selectors.
