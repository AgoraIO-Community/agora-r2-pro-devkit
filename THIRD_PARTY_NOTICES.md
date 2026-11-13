# Third-party notices

Project-maintained application and Linux integration code uses [Apache-2.0](LICENSE) unless a file carries a different notice. Imported code and assets retain their own terms. **The combined source tree or executable must not be described as uniformly Apache-2.0.** Source identities and import scope are recorded in [VENDORED_SOURCES.md](VENDORED_SOURCES.md), [dependencies.lock.json](dependencies.lock.json) and the [original import manifest](docs/dependencies/imported-sources.json).

| Component | Source / terms | Retained notices |
| --- | --- | --- |
| MyBot SDK 1.3.0 | `junlon2006/mybot@4c36cbee4fe8908b37ccee12dcebc0b17bc436f1`; Apache-2.0 except file-level notices | [LICENSE](vendor/mybot/LICENSE), [upstream notices](vendor/mybot/THIRD_PARTY_NOTICES.md) |
| cJSON-derived code | Namespaced implementation retains Dave Gamble copyright and MIT terms | `vendor/mybot/src/support/mybot_json.c` and `src/internal/mybot_json.h` |
| AOSL | Recorded `84e086084ebcd0ae2455a0ce5721950c5fe2e656`; Apache-2.0-based license **with additional restrictive conditions** | [complete AOSL license](vendor/aosl/LICENSE) |
| LVGL 9.5.0 production subset | MIT and source-specific notices | [LICENCE.txt](vendor/lvgl/LICENCE.txt), [COPYRIGHTS.md](vendor/lvgl/COPYRIGHTS.md) |
| DejaVu terminal and signature fonts | Renamed 18/20 px subsets; DejaVu/Bitstream Vera terms and notices | [terminal font license](src/ui/fonts/licenses/TERMINAL_FONT_LICENSE.txt), [inputs](src/ui/fonts/sources.json) |
| Source Han Sans Chinese subset | SIL OFL 1.1 | [Chinese font license](src/ui/fonts/licenses/LVGL_VIEW_FONT_LICENSE.txt) |
| Montserrat / Font Awesome glyphs | SIL OFL and respective retained notices | [Montserrat](vendor/lvgl/scripts/built_in_font/font_license/Montserrat/OFL.txt), [Font Awesome](vendor/lvgl/scripts/built_in_font/font_license/FontAwesome5/LICENSE.txt) |
| Agora RTSA 1.10.1 | Complete official package; separate vendor use/distribution terms | [Bundled package](vendor/rtsa/README.en.md), [RTSA_SOURCE.txt](docs/dependencies/RTSA_SOURCE.txt) |

AOSL's additional conditions remain in force; its license must be read before use, modification, deployment or redistribution. This project's root license does not remove those conditions.

The complete RTSA package is retained in `vendor/rtsa/` with its original notices and documentation, including AArch64 and x86_64 binaries. Application bundles include the selected AArch64 library and its provenance. The official archive supplies no standalone license file; the vendor's applicable use and distribution terms remain in force. Inclusion in this repository does not place RTSA under Apache-2.0 or grant additional rights. Target system libraries and build caches remain outside source control.

Target ALSA, OpenSSL, libdrm, MPP and libc remain system dependencies. They are not copied over device system libraries by this application. Their own notices and the board firmware's distribution terms apply to the respective system contents.

The LVGL snapshot carries the [mask callback signature patch](docs/dependencies/patches/lvgl-mask-callbacks.patch). Linux allocation/configuration lives outside the snapshot in `src/ui/`. The pixel-glasses view and font generator retain their file-level MIT / Apache-2.0 notices; see the [view license](src/ui/views/pixel_glasses/LICENSE.MIT.txt). Generated inputs, tool versions and hashes are recorded in the font manifest; derived font names retain required license restrictions.
