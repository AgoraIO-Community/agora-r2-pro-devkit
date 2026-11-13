# Pixel-glasses view

The RV1126B UI renders a 640×480 logical canvas with a dark industrial-gray palette, pixel-glasses geometry, four terminal rows and 23 expressions. Physical rotation and framebuffer ownership belong to the Linux display service.

## Rendering and ownership

The view uses LVGL 9.5.0. The display worker serializes all LVGL calls; state and pairing text are copied into bounded view storage. Shape and mask arrays remain valid for the view's lifetime. The view owns its LVGL timer and deletes it before destroying widgets.

Rectangular outlines use a two-pixel geometry grid. The background is `#050708`, with neutral geometry `#f1f1ee`. The renderer maps SDK workflow/state indicators to presentation without owning pairing or conversation lifecycle. Pairing codes remain strings, preserving leading zeroes.

## Fonts and assets

Fonts compile into the executable as 4 bpp glyph data:

- 18 px DejaVu Sans Mono Bold terminal text and 20 px signature.
- 40 px Source Han Sans SC Normal Chinese workflow text.
- 64 px Montserrat Medium pairing codes.

The [font manifest](../../fonts/sources.json) records inputs, character coverage and tool versions. Generate or verify them using `scripts/generate-ui-fonts.py` from the repository root; the [dependency guide](../../../../docs/development/dependencies.zh-CN.md) describes the required environment. Shape and mask data are compiled constants.

## Licenses and validation

Keep the [MIT notice](LICENSE.MIT.txt), file-level notices and [font licenses](../../fonts/licenses/) when redistributing the view or assets. Font families retain their required derived names and respective terms.

Host checks exercise workflow pages, all expressions, visible text bounds, glyph coverage, leading-zero pairing codes and repeated view destruction. The preview baseline compares 34 frames per language with the accepted native layout. Physical display and concurrent audio/video behavior are tracked separately in the [validation matrix](../../../../docs/validation/README.md).
