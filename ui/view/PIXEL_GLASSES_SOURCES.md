# CoreS3 pixel-glasses renderer

The following records the original CoreS3 import. RV1126B 0.1.1 uses native
640x480 rendering, 18/20/40/64 px generated fonts and a two-pixel outline grid.
Current font inputs and hashes are in `../fonts/sources.json`; generation is
documented in `../../docs/ui-native-fonts.zh-CN.md`.

This renderer adapts the MyBot BK7259 pixel-glasses view for the existing
CoreS3 320x240 LVGL display. The source's MIT copyright notices remain in
`pixel_glasses_view.cc` and `pixel_expression_shapes.h`. Other board profiles
retain the preceding shared `lvgl_view.cc`; this renderer does not initialize
panels, change GPIO assignments, rotate pixels, own DMA buffers, or create a
FreeRTOS task.

The visual reference is the supplied simulator's `process` skin (工艺灰) and
`geek` face (像素眼镜). Reference provenance:

| Input | SHA256 |
| --- | --- |
| `src/expressions.js` | `1d51ca444e69a39da5b2feedc38d543aea362433184e89a76a825e757b684e31` |
| `src/reference-style.js` | `2b0cf05ffceb91f079d1cd542a982622e9c44c882ff84ac98a84b9a90bfa0646` |
| `src/main.js` | `675c47ed36249a89f3791c9b12ffe4300b6eec86598c88d3e62a6048e55eec5a` |

The simulator is a read-only reference and is not included in this repository.
Its supplied files do not establish a separate open-source redistribution
grant; this record does not assign a license to the simulator.

## Layout and data ownership

The 1024x896 reference scales uniformly to a 288x252 artwork area at `(16,-6)`.
Only empty source margins are cropped. The black-glass background is fixed
`#050708`; neutral artwork uses `#f1f1ee`. Four terminal labels begin at
`(45,24)`, `(45,37)`, `(45,50)`, and `(45,63)`. Each is 194x12 pixels; the
longest normal state line `> state: in_conversation | vp:pending` measures
185 pixels. The first line identifies the target as `> MyBot / CoreS3`.
The 10-pixel `</>` signature starts at `(240,26)`.

The source rectangle for terminal width is widened from 650 to 688 units so
CoreS3 keeps all four rows single-line. Row spacing respects actual font line
height. The blinking cursor uses the same font's measured advance and the
actual fourth-row position.

Guide labels use x=36..283. Their title begins at y=92, pair code or SSID at
y=128, and notice at y=180. SSID and notice each reserve two 26-pixel lines,
leaving separate boxes through y=179 and y=231. Pair codes remain six-character
strings and always use Montserrat32; leading zeroes such as `000042` are
preserved. No length-based font substitution or numeric conversion occurs.

`create`/`create_sized`/`update`/`destroy` retain the existing view entry points.
The CoreS3 owner calls the additional `mybot_pixel_glasses_update` and
show/replay functions while holding its LVGL lock. Runtime state and SSID are
borrowed only for a call and copied into bounded view storage. Shape points
and both banks remain valid for the view's lifetime. The view owns one LVGL
timer, stopped and deleted before its widgets on destruction.

All 23 simulator expressions are retained. The platform's independent
expression policy maps SDK phases and the opening expression; the renderer owns
only their drawing. State/VP changes update the third terminal row without
restarting the opener hold. No reset, motor-control or backend-protocol
implementation is imported into the view.

## Masks and fonts

Five immutable A8 masks occupy 13,631 bitmap bytes: heart 51x48, star 77x74,
ellipse 49x59, cheek 67x29 and tear 21x31. `generate-expression-masks.py`
reconstructs their geometry at the original 9/16 sampling scale, using Pillow
10.2.0 and 4x coverage before downsampling. Public LVGL image drawing scales
them to the smaller artwork; there is no full-face canvas or image sequence.
Regenerate or verify the committed bytes with:

```sh
python3 generate-expression-masks.py --check
python3 generate-expression-masks.py --reference /path/to/threejs-project/src/expressions.js
```

`pixel_fonts.c` contains renamed MyBot Terminal subsets of external
`DejaVuSansMono-Bold.ttf`, pinned to SHA256
`3a3c502eeff669a231549e80df9f7c49de109bafe303170409e905d0b31a38fe`.
The 9-pixel ASCII subset uses 1,697 bitmap bytes; the 10-pixel `</>` subset
uses 60. Their total 1,757 bytes exclude glyph/descriptor metadata. The input
font stays external. Bitstream copyright, trademark notices, DejaVu notice
and the complete Bitstream Vera license are retained in
`TERMINAL_FONT_LICENSE.txt`; the derived font name contains neither reserved
word. Regenerate with:

```sh
python3 generate-pixel-font.py --font /path/to/DejaVuSansMono-Bold.ttf
```

Operational Chinese/English text reuses the existing 20-pixel MyBot UI Sans
font and its existing OFL provenance; this change does not regenerate that
shared font. All fixed visible glyphs are checked in the native rendering
test. Arbitrary Chinese SSIDs require broader font coverage and are not
established by checking the fixed vocabulary.

## Validation scope

Native LVGL 9.5 rendering validates both languages and disabled animations at
320x240, all 23 expressions and sampled transitions, four-line widths and
spacing, fixed codes `135790` and `000042`, 32-byte ASCII SSIDs, phase/VP text,
glyph availability, visible label bounds and repeated view destruction. These
checks use the ESP32 repository's LVGL sources with a host allocator.
They do not establish the physical panel's colors, byte order, touch behavior,
DMA timing, CPU load or concurrent RTC/video performance; those require a
CoreS3 firmware build and device validation.
