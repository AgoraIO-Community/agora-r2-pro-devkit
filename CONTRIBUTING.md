# Contributing

## Repository boundaries

- Put process lifecycle and product configuration in `src/botstation-device`.
- Put RV1126B-specific adapters in `src/platform/rv1126b`.
- Keep board assumptions and runtime files in `boards/<board-id>`.
- Do not modify SDK source under `third_party/agent-studio-device-firmware`
  without an explicit dependency update and updated manifests.
- Never commit the Agora RTSA package, credentials, generated configuration,
  build directories or firmware images.

Tests for pure logic belong in `tests/unit`; Linux and RV1126B adapters belong
in `tests/platform/rv1126b`. A hardware result must identify the board revision
and must not be inferred from a host test.

## Validation

Run:

```sh
./scripts/check-layout.sh
for script in scripts/*.sh boards/rv1126b-r2pro/config/S99botstation-device; do
  sh -n "$script"
done
find src tests -type f \( -name '*.c' -o -name '*.h' \) \
  -exec clang-format --dry-run --Werror {} +
git diff --check
```

When an authorized matching RTSA package is available, also run
`scripts/build-host.sh`. Changes to CMake, platform source or runtime
configuration must retain a successful host build.

Use Conventional Commits with an imperative subject of at most 72 characters.
Describe product behavior or the build-system change, not the mechanical file
moves.
