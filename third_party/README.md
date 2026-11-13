# Pinned source snapshots

Each direct dependency owns one source snapshot and an independent entry in the dependency lock:

| Directory | Role | Scope |
| --- | --- | --- |
| `mybot/` | MyBot SDK 1.3.0 | SDK, required Linux adapters, assets and build inputs |
| `aosl/` | AOSL runtime | Fixed recorded revision, common implementation and Linux HAL |
| `lvgl/` | LVGL 9.5.0 | Production subset and required headers/licenses |

MyBot links AOSL through `AOSL_DIR`; neither dependency is copied into the other. MyBot also requires the separately supplied Agora RTSA package for target builds. Project-owned integration stays in `../cmake/dependencies/`, and application UI/resources stay in `../components/`.

- [Vendored source versions and import scope](../VENDORED_SOURCES.md)
- [Applicable terms](../THIRD_PARTY_NOTICES.md), including AOSL's additional conditions
- [Current machine-readable lock](../dependencies.lock.json)
- [Original import baseline](../docs/dependencies/imported-sources.json)
- [Update/patch workflow](../docs/development/dependencies.zh-CN.md)
- [Layout and pruning rules](../docs/development/third-party-layout.zh-CN.md)
- [LVGL integration](../docs/dependencies/lvgl.md)

The lock records retained files, path mappings, intentional omissions and local patches. Preserve upstream copyright, license and required notice files. Do not bulk-format snapshots or remove headers solely because a matching source file is not compiled. `mybot/docs/Doxyfile.in` and `aosl/test/aosl_test.c` are required by the retained build logic.

Retained upstream documentation describes the dependencies, not this application's setup. This tree has no bundled RTSA binary; target builds use an explicitly supplied, pinned AArch64 package. Use the root build guide for application builds.
