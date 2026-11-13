# Third-party notices

Project-maintained application and Linux integration code uses [Apache-2.0](LICENSE) unless a file carries a different notice. Imported code and assets retain their own terms. **The combined source tree or executable must not be described as uniformly Apache-2.0.** Source identities and import scope are recorded in [VENDORED_SOURCES.md](VENDORED_SOURCES.md), [dependencies.lock.json](dependencies.lock.json) and the [original import manifest](docs/dependencies/imported-sources.json).

| Component | Source / terms | Retained notices |
| --- | --- | --- |
| MyBot SDK 1.3.0 | `junlon2006/mybot@4c36cbee4fe8908b37ccee12dcebc0b17bc436f1`; Apache-2.0 except file-level notices | [LICENSE](third_party/mybot/LICENSE), [upstream notices](third_party/mybot/THIRD_PARTY_NOTICES.md) |
| cJSON-derived code | Namespaced implementation retains Dave Gamble copyright and MIT terms | `third_party/mybot/src/support/mybot_json.c` and `src/internal/mybot_json.h` |
| AOSL | Recorded `84e086084ebcd0ae2455a0ce5721950c5fe2e656`; Apache-2.0-based license **with additional restrictive conditions** | [complete AOSL license](third_party/mybot/third_party/aosl/LICENSE) |
| LVGL 9.5.0 production subset | MIT and source-specific notices | [LICENCE.txt](third_party/lvgl/LICENCE.txt), [COPYRIGHTS.md](third_party/lvgl/COPYRIGHTS.md) |
| CoreS3 view / shapes / masks | Imported from mybot-esp32 `2782cc88f52c8a46419654b72a918a8273cc23bf`; MIT / Apache-2.0 at file level | [MIT notice](components/mybot_ui/views/pixel_glasses/LICENSE.MIT.txt), [provenance](components/mybot_ui/views/pixel_glasses/PIXEL_GLASSES_SOURCES.md) |
| DejaVu terminal and signature fonts | Renamed 18/20 px subsets; DejaVu/Bitstream Vera terms and notices | [terminal font license](components/mybot_ui/fonts/licenses/TERMINAL_FONT_LICENSE.txt), [inputs](components/mybot_ui/fonts/sources.json) |
| Source Han Sans Chinese subset | SIL OFL 1.1 | [Chinese font license](components/mybot_ui/fonts/licenses/LVGL_VIEW_FONT_LICENSE.txt) |
| Montserrat / Font Awesome glyphs | SIL OFL and respective retained notices | [Montserrat](third_party/lvgl/scripts/built_in_font/font_license/Montserrat/OFL.txt), [Font Awesome](third_party/lvgl/scripts/built_in_font/font_license/FontAwesome5/LICENSE.txt) |
| RV1126B audio/network/video, DRM and ELF-tooling reference | agora-r2-pro-devkit inspected worktree, HEAD `d56bda5a24c4bae00179a92a9e67d4e8fee02cfa`; Apache-2.0 | Imported worktree hashes in the original manifest, including inputs not identified by HEAD |
| Agora RTSA | External AArch64 Linux GNU 1.10.1; separate vendor use/distribution terms | [RTSA_SOURCE.txt](docs/dependencies/RTSA_SOURCE.txt) |

AOSL's additional conditions remain in force; its license must be read before use, modification, deployment or redistribution. This project's root license does not remove those conditions.

RTSA binaries, target system libraries and build caches are excluded from source control. The AArch64 package used here is distinct from the x86_64 development package described in the retained upstream MyBot notice; that x86_64 binary is absent. Local test bundles may include the selected external RTSA library and provenance. Possession or local testing of the archive does not establish public redistribution or production-use rights. Obtain and retain the applicable vendor terms/authorization before distributing an artifact containing it.

Target ALSA, OpenSSL, libdrm, MPP and libc remain system dependencies. They are not copied over device system libraries by this application. Their own notices and the board firmware's distribution terms apply to the respective system contents.

The LVGL snapshot carries the [mask callback signature patch](docs/dependencies/patches/lvgl-mask-callbacks.patch). Linux allocation/configuration lives outside the snapshot in `components/mybot_ui/`. Font generation derives from MIT-licensed mybot-esp32 generators; `scripts/generate-ui-fonts.py` retains that notice. Generated inputs, tool versions and hashes are recorded in the font manifest; derived font names retain required license restrictions.

The simulator referenced by upstream pixel-glasses provenance is not included, and that source record does not grant a separate license to the simulator. Do not infer a redistribution grant for external references from their being inspected.
