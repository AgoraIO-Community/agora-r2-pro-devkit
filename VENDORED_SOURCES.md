# Vendored sources and provenance

This document describes the inputs actually used by this project. The machine-readable [dependency lock](dependencies.lock.json) records current identities; [third-party notices](THIRD_PARTY_NOTICES.md) describe applicable terms. Existing source snapshots are committed directly and are not Git submodules.

| Input | Fixed source | Current location and adaptation |
| --- | --- | --- |
| MyBot SDK 1.3.0 | [junlon2006/mybot](https://github.com/junlon2006/mybot), `4c36cbee4fe8908b37ccee12dcebc0b17bc436f1` | `third_party/mybot/`; fixed source snapshot, external integration in `cmake/dependencies/` |
| AOSL | [AgoraIO-Community/aosl](https://github.com/AgoraIO-Community/aosl), recorded commit `84e086084ebcd0ae2455a0ce5721950c5fe2e656` | `third_party/aosl/`; independent fixed snapshot of common implementation and Linux HAL |
| LVGL 9.5.0 production subset | [lvgl/lvgl](https://github.com/lvgl/lvgl), `85aa60d18b3d5e5588d7b247abf90198f07c8a63` | `third_party/lvgl/`; correctly typed mask callback wrappers; Linux allocator/config outside snapshot |
| Terminal font | DejaVu Sans Mono Bold from Matplotlib 3.8.2 | `components/mybot_ui/fonts/generated/pixel_fonts.c`; 18/20 px renamed subsets; fixed source hash in fonts manifest |
| Chinese UI font | Source Han Sans SC Normal, pinned input hash | `components/mybot_ui/fonts/generated/lvgl_fonts.c`; 40 px subset |
| Pair-code font | Montserrat Medium, pinned input hash | `components/mybot_ui/fonts/generated/pair_font.c`; 64 px subset |
| Agora RTSA 1.10.1 | External AArch64 Linux GNU archive/library pinned by SHA256 | Not vendored; [provenance and configuration](docs/dependencies/RTSA_SOURCE.txt), local authorized input |

## Historical baseline and current state

[imported-sources.json](docs/dependencies/imported-sources.json) preserves the **original import** paths and hashes. Those paths predate the component migration and some native font changes. The current lock records mappings and expected results rather than rewriting history or pretending that adapted files still equal their original hash.

MyBot, AOSL and LVGL have separate snapshot entries and non-overlapping directory ownership. AOSL was moved out of MyBot's nested dependency directory without changing the moved file contents; intentional omissions are recorded separately with the original path/hash, prior target and reason. The AOSL revision is the already recorded fixed identity, not a claim that upstream was independently reverified during this layout change. The retained snapshot scope and pruning rules are described in the [layout guide](docs/development/third-party-layout.zh-CN.md).

The LVGL fix is retained as [lvgl-mask-callbacks.patch](docs/dependencies/patches/lvgl-mask-callbacks.patch). Generated-font sources and tool identity live in [fonts/sources.json](components/mybot_ui/fonts/sources.json); original font binaries remain external under ignored local storage. The pixel-glasses [source record](components/mybot_ui/views/pixel_glasses/PIXEL_GLASSES_SOURCES.md) describes the project-maintained view and its file-level terms. For current Linux layout and resources, use the [architecture](docs/development/architecture.zh-CN.md) and [dependency guide](docs/development/dependencies.zh-CN.md).

Project-managed view files belong to the lock's `managed_assets` group; generated fonts and their inputs use `generated_fonts`. Both are checked independently of the three third-party source snapshots. Their file-level licenses still apply. Project-specific [LVGL integration](docs/dependencies/lvgl.md) lives outside the snapshot. Retained MyBot documentation describes the SDK; its upstream RTSA notice refers to an x86_64 development package absent from this tree. This application uses only the explicitly configured AArch64 external package.

## Verification and updates

```sh
python3 scripts/check-dependencies.py
```

An update must preserve upstream notices, record the exact upstream input and import scope, maintain ordered patches/result hashes and regenerate assets with pinned tools. Run the affected host, sanitizer, target and artifact checks. See the [dependency workflow](docs/development/dependencies.zh-CN.md) for detailed steps. Toolchains, target system libraries, RTSA binaries and caches stay out of source control.
