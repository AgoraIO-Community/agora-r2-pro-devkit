# Changelog

Changes to project-maintained code follow Semantic Versioning. Validation evidence is maintained in [one matrix](docs/validation/README.md); a release entry does not imply hardware acceptance.

## 0.2.0 — Unreleased

- Organize application configuration, process guards, board defaults, Linux drivers/services and pure UI into explicit components.
- Add isolated CMake presets, explicit SDK preparation, dependency provenance checks and host CI entry points.
- Bind build configuration and binary identity to manifests, verify selected package profiles and keep generated artifacts outside source control.
- Add user/developer guides, contribution/support templates and release guidance; archive original migration records.
- Preserve the 0.1.1 CLI, identity/KV contract, 20 ms audio configuration and native pixel-glasses rendering as the compatibility baseline.

This governance candidate requires a new hardware regression. Optional video, audio-quality measurements and extended concurrent operation remain pending.

## 0.1.1 — 2026-11-13

- Render at native 640×480 and rotate for the physical screen.
- Generate target-size 18/20/40/64 px fonts and adjust layout while retaining pixel-glasses geometry.
- Add text bounds, glyph coverage and pixel-repetition regression checks.
- User confirmed the improved display meets requirements on the target device.

## 0.1.0 — 2026-11-13

- Initial MyBot 1.3.0 RV1126B Linux application: HTTPS pairing, bidirectional voice, persistent identity, evdev input and CoreS3 UI.
- Optional V4L2/MPP JPEG uplink, AArch64 ABI checks and self-contained test bundles.
- User verified checksums and the default voice/UI business flow on hardware. Optional video and extended stability were not accepted by that result.
