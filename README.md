# mybot-rv1126b

[English](README.md) | [简体中文](README.zh-CN.md)

A Linux application for running MyBot 1.3.0 on the **RV1126B TEST2 RK805B V10** board. It provides HTTPS pairing, bidirectional voice, persistent device identity, a pixel-glasses UI, and an optional V4L2/MPP JPEG uplink.

The current source is the **0.2.0 engineering-governance candidate**. The 0.1.0 voice/UI flow and 0.1.1 text-rendering improvement passed user testing on hardware. The reorganized candidate needs a new device regression; optional video and extended stability testing remain incomplete. [Validation status](docs/validation/README.md) is the single source for results.

## Platform and behavior

The recorded device baseline is AArch64 Buildroot 2024.02, Linux 6.1, glibc 2.41, ALSA, DRM/KMS and a physical 480×640 DSI screen. The UI renders at 640×480 with a default 270° rotation and statically linked LVGL 9.5. Audio uses 16 kHz mono signed-16 PCM, 20 ms frames and Cloud AEC.

The application uses the system's existing network and ALSA configuration. It does not install firmware, provisioning, autostart, OTA, wakeword/NPU, video downlink or a local camera preview. A short touch or Menu key starts/stops a conversation. Runtime data defaults to `/userdata/mybot-rv1126b`; upgrades preserve pairing when server and identity configuration stay the same.

## Build and test

Use **Python 3.10+**, **CMake 3.25+**, a C/C++ compiler, OpenSSL development files and libdrm development headers. Host UI/tests require neither RTSA nor a target SDK. On Debian/Ubuntu, the development packages include `build-essential`, `pkg-config`, `libssl-dev` and `libdrm-dev`.

```sh
python3 scripts/doctor.py --host
python3 scripts/check-dependencies.py
cmake --preset host-zh
cmake --build --preset host-zh
ctest --preset host-zh
```

Use `host-en` for English and `host-ubsan` for undefined-behavior checks. On macOS, explicitly provide the libdrm header directory when it is not installed as a native development package; see [build instructions](docs/user/build.zh-CN.md). Host CI runs on Linux; only the environment recorded in the validation matrix has been tested locally.

Target builds require a matching AArch64 compiler, development staging/sysroot, the pinned Agora RTSA package and a runtime-library manifest. Configure their paths explicitly as described in [independent target setup](docs/user/build.zh-CN.md).

```sh
sh scripts/build-rv1126b.sh
RV1126B_BUILD_DIR="$PWD/build/rv1126b-video-zh" sh scripts/build-rv1126b.sh -DMYBOT_ENABLE_VIDEO=ON
python3 scripts/package-rv1126b.py --profile all --language zh-CN
```

Build and packaging commands run locally and do not deploy to a device. [Getting started](docs/user/getting-started.zh-CN.md) covers manual installation, service ownership, upgrade and rollback.

## Project layout

| Directory | Responsibility |
| --- | --- |
| `app/` | Configuration, identity, process guards and application lifecycle |
| `components/mybot_platform/` | Board defaults, Linux services and hardware drivers |
| `components/mybot_ui/` | Pure renderer, pixel-glasses view, expressions and compiled fonts |
| `cmake/` | Dependency integration and toolchain configuration |
| `third_party/` | Pinned upstream source snapshots |
| `tests/`, `tools/ui_preview/` | Host verification and standalone UI preview |
| `scripts/`, `packaging/` | Build/check tools and delivery templates |
| `docs/` | User/developer guides, validation and historical evidence |

## Documentation and contribution

- [Documentation index](docs/README.md), [configuration](docs/user/configuration.zh-CN.md), [troubleshooting](docs/user/troubleshooting.zh-CN.md)
- [Architecture](docs/development/architecture.zh-CN.md), [testing](docs/development/testing.zh-CN.md), [release process](docs/development/releasing.zh-CN.md)
- [Contributing](CONTRIBUTING.md), [support](SUPPORT.md), [changelog](CHANGELOG.md)

Project-maintained code is [Apache-2.0](LICENSE) unless a file carries another notice. MyBot/AOSL, LVGL and fonts retain their own terms; file-level notices also apply to the pixel-glasses view. RTSA is externally obtained and is not covered by this project's license. Read [third-party notices](THIRD_PARTY_NOTICES.md) and [vendored sources](VENDORED_SOURCES.md) before redistributing source or packages.
