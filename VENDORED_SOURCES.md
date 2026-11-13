# Vendored sources and provenance

This document describes the inputs actually used by this project. The machine-readable [dependency lock](dependencies.lock.json) records current identities; [third-party notices](THIRD_PARTY_NOTICES.md) describe applicable terms. Existing source snapshots are not Git submodules and do not require a sibling checkout to build host tests.

| Input | Fixed source | Current location and adaptation |
| --- | --- | --- |
| MyBot SDK 1.3.0 | [junlon2006/mybot](https://github.com/junlon2006/mybot), `4c36cbee4fe8908b37ccee12dcebc0b17bc436f1` | `third_party/mybot/`; fixed source snapshot, external integration in `cmake/dependencies/` |
| AOSL | [AgoraIO-Community/aosl](https://github.com/AgoraIO-Community/aosl), recorded commit `84e086084ebcd0ae2455a0ce5721950c5fe2e656` | `third_party/mybot/third_party/aosl/`; populated snapshot supplied from the inspected reference, not an empty gitlink |
| LVGL 9.5.0 production subset | Upstream `85aa60d18b3d5e5588d7b247abf90198f07c8a63`, imported through mybot-esp32 `2782cc88f52c8a46419654b72a918a8273cc23bf` | `third_party/lvgl/`; correctly typed mask callback wrappers; Linux allocator/config outside snapshot |
| CoreS3 pixel-glasses view, expressions, shapes and masks | mybot-esp32 `2782cc88f52c8a46419654b72a918a8273cc23bf` | `components/mybot_ui/views/pixel_glasses/`; Linux lifecycle/configuration, target labels and native layout adaptations |
| Terminal font | DejaVu Sans Mono Bold from Matplotlib 3.8.2 | `components/mybot_ui/fonts/generated/pixel_fonts.c`; 18/20 px renamed subsets; fixed source hash in fonts manifest |
| Chinese UI font | Source Han Sans SC Normal, pinned input hash | `components/mybot_ui/fonts/generated/lvgl_fonts.c`; 40 px subset |
| Pair-code font | Montserrat Medium, pinned input hash | `components/mybot_ui/fonts/generated/pair_font.c`; 64 px subset |
| Linux audio/network/video, DRM and ELF-tooling reference | agora-r2-pro-devkit HEAD `d56bda5a24c4bae00179a92a9e67d4e8fee02cfa`, including recorded dirty/untracked worktree inputs | Adapted under platform drivers/services and scripts; HEAD alone does not identify those source bytes |
| Agora RTSA 1.10.1 | External AArch64 Linux GNU archive/library pinned by SHA256 | Not vendored; [provenance and configuration](docs/dependencies/RTSA_SOURCE.txt), local authorized input |

## Historical baseline and current state

[imported-sources.json](docs/dependencies/imported-sources.json) preserves the **original import** paths and hashes. Those paths predate the component migration and some native font changes. The current lock records mappings and expected results rather than rewriting history or pretending that adapted files still equal their original hash.

The LVGL fix is retained as [lvgl-mask-callbacks.patch](docs/dependencies/patches/lvgl-mask-callbacks.patch). Generated-font sources and tool identity live in [fonts/sources.json](components/mybot_ui/fonts/sources.json); original font binaries remain external under ignored local storage. The pixel-glasses [source record](components/mybot_ui/views/pixel_glasses/PIXEL_GLASSES_SOURCES.md) retains upstream simulator provenance and describes the original ESP32 layout. For current Linux layout and resources, use the [architecture](docs/development/architecture.zh-CN.md) and [dependency guide](docs/development/dependencies.zh-CN.md).

`third_party/lvgl/README.integration.md` and nested MyBot documentation describe their upstream projects. In particular, MyBot's upstream RTSA notice refers to its own x86_64 reference package; that binary is absent from this snapshot. This application uses only the explicitly configured AArch64 external package.

## Verification and updates

```sh
python3 scripts/check-dependencies.py
```

An update must preserve upstream notices, record the exact upstream input and import scope, maintain ordered patches/result hashes and regenerate assets with pinned tools. Run the affected host, sanitizer, target and artifact checks. See the [dependency workflow](docs/development/dependencies.zh-CN.md) for detailed steps. Toolchains, target system libraries, RTSA binaries and caches stay out of source control.
