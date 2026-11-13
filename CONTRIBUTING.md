# Contributing

[English](CONTRIBUTING.md) | [简体中文](CONTRIBUTING.zh-CN.md)

Keep contributions focused on an observable problem. Explain compatibility effects and how the result was verified. For large platform or dependency changes, describe the intended boundary in the issue or PR before combining unrelated changes.

## Code boundaries

Application configuration and lifecycle belong in `app/`. Board defaults, Linux services and drivers belong in `components/mybot_platform/`. The renderer and views in `components/mybot_ui/` remain independent of DRM, ALSA, MPP and platform worker threads. Platform code consumes public MyBot headers, not SDK private interfaces.

Treat `third_party/` as pinned source snapshots. Dependency updates must name an exact source/revision, retain notices, register local patches and update the lock/provenance records through the documented [dependency workflow](docs/development/dependencies.zh-CN.md). Do not bulk-format imported or generated files. Regenerate fonts from recorded inputs instead of editing their bitmap arrays.

Never include tokens, private device identities, credentials, runtime data, personal SDK paths or unapproved binaries in a contribution. Device operations and changes to other repositories require authorization from their owner.

## Checks

Use Python 3.10+, CMake 3.25+, OpenSSL and libdrm development files. See [build setup](docs/user/build.zh-CN.md) for macOS header configuration.

```sh
python3 scripts/doctor.py --host
python3 scripts/check-dependencies.py
cmake --preset host-zh
cmake --build --preset host-zh
ctest --preset host-zh
cmake --preset host-en
cmake --build --preset host-en
ctest --preset host-en
cmake --preset host-ubsan
cmake --build --preset host-ubsan
ctest --preset host-ubsan
git diff --check
```

Run the repository quality check used by [CI](.github/workflows/ci.yml), and apply `.clang-format` only to maintained C/C++ files. Exercise target voice/video and packaging when the change affects their inputs; a host-only documentation change does not need hardware tests. Document unexecuted checks and their reason. The [testing guide](docs/development/testing.zh-CN.md) defines the distinction between compilation, host tests and hardware evidence.

Keep English and Chinese README behavior and commands aligned. Update configuration docs, `CHANGELOG.md` and [validation status](docs/validation/README.md) when applicable. Do not mark a build as hardware-tested based on an earlier package.

Use Conventional Commits, such as `refactor(platform): isolate display ownership`. Include the problem, resulting behavior, compatibility, executed checks and remaining hardware checks in each PR. Contributions follow [LICENSE](LICENSE) unless a file explicitly carries another compatible license.
