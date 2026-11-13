# Third-party sources

The application and Linux integration use Apache-2.0 unless a source file carries a different notice.
Imported sources retain their upstream notices. See `docs/imported-sources.json` for the original
file hashes and `docs/rv1126b-feasibility.zh-CN.md` for the inspected reference revisions.

| Component | Source / version | Notices |
| --- | --- | --- |
| MyBot SDK | `junlon2006/mybot`, `4c36cbee4fe8908b37ccee12dcebc0b17bc436f1`, 1.3.0 | `third_party/mybot/LICENSE`, `THIRD_PARTY_NOTICES.md`; cJSON portions retain MIT |
| AOSL | Reference snapshot records `84e086084ebcd0ae2455a0ce5721950c5fe2e656` | `third_party/mybot/third_party/aosl/LICENSE`; source hashes recorded |
| LVGL | Production subset from `mybot-esp32`, upstream 9.5.0 `85aa60d18b3d5e5588d7b247abf90198f07c8a63` | `third_party/lvgl/LICENCE.txt`, `COPYRIGHTS.md`, source-specific notices |
| CoreS3 view, shapes and masks | `mybot-esp32` `2782cc88f52c8a46419654b72a918a8273cc23bf`, with Linux allocation/configuration and target-label adaptations | MIT / Apache-2.0 at file level; upstream provenance in `ui/view/PIXEL_GLASSES_SOURCES.md` |
| Terminal font | Renamed DejaVu Sans Mono Bold 18/20 px subsets; input from Matplotlib 3.8.2 | `ui/view/TERMINAL_FONT_LICENSE.txt`, `ui/fonts/sources.json` |
| Chinese UI font | Source Han Sans static subset | `ui/view/LVGL_VIEW_FONT_LICENSE.txt`, SIL OFL 1.1 |
| Montserrat / Font Awesome glyphs | LVGL built-in fonts; native 64 px pair-code subset generated from Montserrat Medium | `third_party/lvgl/scripts/built_in_font/font_license/` |
| RV1126B audio/network/video, DRM resource management and ELF tooling | `agora-r2-pro-devkit` inspected worktree, HEAD `d56bda5a24c4bae00179a92a9e67d4e8fee02cfa` | Apache-2.0; imported hashes include uncommitted reference files where applicable |
| Agora RTSA | External AArch64 Linux GNU 1.10.1 package | `docs/RTSA_SOURCE.txt`; external SDK terms apply |

RTSA binaries, target system libraries and build caches are excluded from source control. Local
test bundles include the selected RTSA library and its provenance. Target ALSA, OpenSSL, libdrm,
MPP and libc remain system dependencies; they are not copied over device system libraries.

The LVGL copy carries the mask-callback signature fix documented in
`docs/patches/lvgl-mask-callbacks.patch`. The Linux libc allocation backend and
XRGB8888 configuration live outside the imported runtime under `ui/`.
Native font generation derives from the MIT-licensed mybot-esp32 generators;
`scripts/generate-ui-fonts.py` retains that license. Generated font inputs,
tool versions and exact source hashes are recorded in `ui/fonts/sources.json`.
