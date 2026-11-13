# LVGL integration

This is a production subset of LVGL 9.5.0, pinned to upstream commit
`85aa60d18b3d5e5588d7b247abf90198f07c8a63` from
<https://github.com/lvgl/lvgl>. The application builds it as the static
`mybot_lvgl` target using [lvgl.cmake](../../cmake/dependencies/lvgl.cmake) and
the explicit [source list](../../cmake/dependencies/lvgl-sources.cmake).

## Linux configuration and ownership

- [lv_conf.h](../../components/mybot_ui/lv_conf.h) selects 32-bit color, libc
  memory/string/formatting functions, one software draw unit, and the widgets
  used by the UI. Built-in themes and external image/font decoders are disabled.
- [renderer.c](../../components/mybot_ui/src/renderer.c) creates a native
  640×480 `LV_COLOR_FORMAT_XRGB8888` display, a canvas and a partial draw buffer.
  Generated fonts are compiled into the UI component; rendering needs no
  runtime font files.
- [lv_mem_clib.c](../../components/mybot_ui/src/lv_mem_clib.c) implements the
  allocator hooks with libc `malloc`, `realloc` and `free`. Heap statistics are
  unavailable through these portable hooks.
- The target's [display worker](../../components/mybot_platform/src/services/display_runtime.c)
  owns LVGL initialization, updates, timer handling and destruction. Other
  threads publish copied content through the display service. LVGL uses
  `LV_OS_NONE`; callers must preserve this single-owner model.
- The [DRM backend](../../components/mybot_platform/src/drivers/display/drm.c)
  owns the physical framebuffer and output restoration. It rotates the native
  canvas for the panel; rotation and framebuffer pitch handling remain outside
  the renderer.

## Local patch and licenses

[`src/draw/sw/lv_draw_sw_mask.c`](src/draw/sw/lv_draw_sw_mask.c) uses correctly
typed callback wrappers to remove function-pointer undefined behavior found by
Clang UBSan. The [patch](../../docs/dependencies/patches/lvgl-mask-callbacks.patch)
and [dependency lock](../../dependencies.lock.json) record the change and its
result hashes.

The snapshot retains headers and production sources for the selected core,
display, drawing, text, widget and layout features. The bundled Montserrat
fonts include upstream Font Awesome 5 symbol glyphs; their licenses remain
under [scripts/built_in_font/font_license/](scripts/built_in_font/font_license/).
[LICENCE.txt](LICENCE.txt) and [COPYRIGHTS.md](COPYRIGHTS.md) retain the library
notices. Application font inputs and licenses are recorded in the
[font manifest](../../components/mybot_ui/fonts/sources.json).
