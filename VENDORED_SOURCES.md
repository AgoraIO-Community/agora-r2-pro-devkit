# Vendored sources and provenance

This document describes the inputs actually used by this project. The machine-readable [dependency lock](dependencies.lock.json) records current identities; [third-party notices](THIRD_PARTY_NOTICES.md) describe applicable terms. Dependency snapshots are committed directly and are not Git submodules.

| Input | Fixed source | Current location and adaptation |
| --- | --- | --- |
| MyBot SDK 1.3.0 | [junlon2006/mybot](https://github.com/junlon2006/mybot), `4c36cbee4fe8908b37ccee12dcebc0b17bc436f1` | `vendor/mybot/`; fixed source snapshot, external integration in `cmake/dependencies/` |
| AOSL | [AgoraIO-Community/aosl](https://github.com/AgoraIO-Community/aosl), recorded commit `84e086084ebcd0ae2455a0ce5721950c5fe2e656` | `vendor/aosl/`; independent fixed snapshot of common implementation and Linux HAL |
| LVGL 9.5.0 production subset | [lvgl/lvgl](https://github.com/lvgl/lvgl), `85aa60d18b3d5e5588d7b247abf90198f07c8a63` | `vendor/lvgl/`; correctly typed mask callback wrappers; Linux allocator/config outside snapshot |
| Terminal font | DejaVu Sans Mono Bold from Matplotlib 3.8.2 | `src/ui/fonts/generated/pixel_fonts.c`; 18/20 px renamed subsets; fixed source hash in fonts manifest |
| Chinese UI font | Source Han Sans SC Normal, pinned input hash | `src/ui/fonts/generated/lvgl_fonts.c`; 40 px subset |
| Pair-code font | Montserrat Medium, pinned input hash | `src/ui/fonts/generated/pair_font.c`; 64 px subset |
| Agora RTSA 1.10.1 | Official AArch64 Linux GNU release archive, pinned by SHA256 | `vendor/rtsa/`; complete unmodified package, [provenance and configuration](docs/dependencies/RTSA_SOURCE.txt) |

## Current identities and historical provenance

The schema 2 lock checks current snapshot files, local patches, generated fonts, retained licenses and required build inputs. MyBot, AOSL, LVGL and RTSA have separate, non-overlapping snapshot entries. Maintained application, platform and UI view source is governed by Git and source tests, without historical per-file path mappings or a parallel frozen-source inventory in the active lock. Their applicable license notices remain pinned separately.

The exact schema 1 lock is retained in [dependencies-lock-v1.json](docs/history/dependencies-lock-v1.json). The [original import manifest](docs/dependencies/imported-sources.json) retains its original bytes. The active lock binds both archives by SHA256, without resolving their old paths against today's source tree. These records preserve earlier migrations, omissions and adaptation reasons; they are historical evidence rather than current build inputs.

The LVGL fix remains [lvgl-mask-callbacks.patch](docs/dependencies/patches/lvgl-mask-callbacks.patch), with original input, patch and current result hashes. Font source and tool identities remain in [fonts/sources.json](src/ui/fonts/sources.json). The pixel-glasses [source record](src/ui/views/pixel_glasses/PIXEL_GLASSES_SOURCES.md) explains file-level terms. Current integration and ownership are described in the [architecture](docs/development/architecture.zh-CN.md) and [dependency guide](docs/development/dependencies.zh-CN.md).

RTSA preserves all 99 files (38,268,142 bytes) from the official package, including AArch64 and x86_64 binaries, examples and documentation. The package-relative `archive_files` inventory remains unchanged; ordinary lock refresh refuses changed, added or missing release members. The application selects only `vendor/rtsa/agora_sdk/lib/aarch64/libagora-rtc-sdk.so`. Original package metadata is retained.

## Verification and updates

```sh
python3 scripts/check-dependencies.py
```

An update must preserve upstream notices, record the exact upstream input and scope, maintain patch/result hashes and regenerate assets with pinned tools. Vendor changes require explicit snapshot selection; changed license text and patch results require separate acceptance. Run the affected host, sanitizer, target and artifact checks. See the [dependency workflow](docs/development/dependencies.zh-CN.md) for detailed steps. The complete pinned RTSA package is an essential committed dependency. Toolchains, target system libraries, personal SDK paths and caches stay out of source control.
