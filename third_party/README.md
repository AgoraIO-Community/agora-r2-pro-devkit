# Pinned source snapshots

`mybot/` and `lvgl/` contain the committed source inputs used by this Linux application. Preserve upstream file notices and directory layout. Integration wrappers live in `../cmake/dependencies/`; platform and UI adaptations belong in `../components/`.

- [Vendored source versions and import scope](../VENDORED_SOURCES.md)
- [Applicable terms](../THIRD_PARTY_NOTICES.md), including AOSL's additional conditions
- [Current machine-readable lock](../dependencies.lock.json)
- [Original import baseline](../docs/dependencies/imported-sources.json)
- [Update/patch workflow](../docs/development/dependencies.zh-CN.md)

Nested README/AGENTS and integration records describe upstream projects. Their commands and external-package claims are not the current RV1126B application guide. This copy has no bundled RTSA binary; target builds use an explicitly supplied, pinned AArch64 package. Use the root build guide and do not run dependency repository release/install scripts as project setup.
