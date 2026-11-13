# Changelog

All notable changes are recorded here.

## Unreleased

### Changed

- Split service source, RV1126B platform adapters, board runtime configuration,
  third-party sources, tests and build-system integration into explicit
  repository boundaries.
- Add a root CMake entry point, single-source product version, `--version`
  output, host and AArch64 build scripts, and a Buildroot external tree.
- Install the runtime configuration, ALSA compatibility layer and SysV service
  through the standalone install target.
- Add explicit RTSA codec/ABI/ptime checks and dependency documentation.
