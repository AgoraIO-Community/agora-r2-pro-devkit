# Pinned dependencies

Each direct dependency owns one snapshot and an independent entry in the dependency lock:

| Directory | Role | Scope |
| --- | --- | --- |
| `mybot/` | MyBot SDK 1.3.0 | SDK, required Linux adapters, assets and build inputs |
| `aosl/` | AOSL runtime | Fixed recorded revision, common implementation and Linux HAL |
| `lvgl/` | LVGL 9.5.0 | Production subset and required headers/licenses |
| `rtsa/` | Agora RTSA 1.10.1 | Complete official package: headers, binaries, examples and upstream documentation |

MyBot links AOSL through `AOSL_DIR`; neither dependency is copied into the other. Target builds use the pinned AArch64 library at `rtsa/agora_sdk/lib/aarch64/libagora-rtc-sdk.so`. Project-owned integration stays in `../cmake/dependencies/`, and application UI/resources stay in `../components/`.

- [Vendored source versions and import scope](../VENDORED_SOURCES.md)
- [Applicable terms](../THIRD_PARTY_NOTICES.md), including AOSL's additional conditions
- [Current machine-readable lock](../dependencies.lock.json)
- [Original import baseline](../docs/dependencies/imported-sources.json)
- [Update/patch workflow](../docs/development/dependencies.zh-CN.md)
- [Layout and pruning rules](../docs/development/third-party-layout.zh-CN.md)
- [LVGL integration](../docs/dependencies/lvgl.md)

The lock records retained files, path mappings, intentional omissions and local patches. Preserve upstream copyright, license and required notice files. Do not bulk-format snapshots or remove headers solely because a matching source file is not compiled. `mybot/docs/Doxyfile.in` and `aosl/test/aosl_test.c` are required by the retained build logic.

RTSA is a required complete package, retained without pruning or local changes. Its 99 files include vendor x86_64 binaries, examples, collaboration guides and skills; those files remain part of the vendor snapshot. Architecture selection uses the actual AArch64 ELF and the locked library hash. Retained upstream documentation describes each dependency; use the root build guide for this application's setup.
