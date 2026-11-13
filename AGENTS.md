# Repository guide

Follow the user's task and preserve unrelated changes. This is a Linux/CMake application for RV1126B; do not apply ESP-IDF, FreeRTOS or flash-partition conventions.

Project documentation and tool output describe this application and its required dependencies. Omit background about other application repositories. Preserve applicable copyright and license notices.

- `src/application/` owns configuration, identity, runtime guards and lifecycle. `src/platform/` owns board defaults, Linux services and drivers. `src/ui/` owns a pure 640×480 renderer and views. Use MyBot public headers only.
- Keep public include namespaces (`mybot_platform/`, `mybot_ui/`) and CMake targets stable during directory refactors. `tools/` contains developer programs; `scripts/` contains build/check/generation/packaging automation.
- `vendor/` is a pinned snapshot boundary. Keep the required RTSA package complete, including vendor binaries and documentation. Make dependency changes deliberately, retain licenses, record patches and synchronize `dependencies.lock.json` and provenance. Do not bulk-format snapshots or generated fonts. See `VENDORED_SOURCES.md`.
- Follow the Conventional Commits format and allowed types in `CONTRIBUTING.md`; keep the subject imperative and within 72 characters, mark incompatible changes with `!` and a `BREAKING CHANGE:` footer, and install `./scripts/setup-githooks.sh`.
- Keep credentials, runtime data, private device identifiers, local SDK paths and generated packages out of commits. Generated outputs belong in ignored `build/`, `local/` and `dist/` directories.
- Device access, deployment, external repositories and Git history changes follow the current user's authorization. Build/check/package scripts must not implicitly deploy or connect to a device.
- Preserve the current CLI/identity/KV, 16 kHz mono S16/20 ms audio and native UI contracts during structural refactors. Keep public/private include boundaries explicit.
- Use Python 3.10+ and CMake 3.25+. Run checks appropriate to the change as documented in `docs/development/testing.zh-CN.md`; keep missing dependencies visible. Record actual results in `docs/validation/README.md`, separating host, cross-build and hardware evidence.

Do not claim unexecuted checks passed. Keep English/Chinese entry points aligned and update affected docs with code. This guide does not require an extra approval step for work already authorized by the user.
