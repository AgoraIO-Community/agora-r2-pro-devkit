# mybot-rv1126b

[English](README.md) | [简体中文](README.zh-CN.md)

An official Agora Linux application for RV1126B, built on MyBot 1.3.0. It provides AP Wi-Fi setup with voice guidance, HTTPS pairing, bidirectional voice, persistent device identity and volume, a pixel-glasses UI, and optional V4L2/MPP JPEG uplink.

The current source version is **<!-- project-version -->0.5.4<!-- /project-version -->**, targeting **RV1126B TEST2 RK805B V10**. [Validation status](docs/validation/README.md) distinguishes source checks, cross-builds and hardware evidence. New packages require their own hardware acceptance.

## Use

Follow the [illustrated English operations manual (PDF)](docs/device-operations.en.pdf) for build, deployment and daily use. A [Chinese edition](docs/device-operations.zh-CN.pdf) is also available.

The application keeps an existing usable network. If offline recovery fails, connect a phone to the displayed open `mybot-xxxx` hotspot and visit `http://192.168.4.1/`. For manual setup, choose Wi-Fi setup from the top-right settings menu. A short screen touch or Menu press starts/ends a conversation; holding either does not trigger conversation or setup. Settings also provides poweroff confirmation. Preserve the server, device identity and data directory to retain pairing across restarts, updates and Wi-Fi setup.

Supervised shutdown explicitly turns off the PWM backlight before requesting system poweroff; ordinary application stops restore the prior display. Software poweroff does not guarantee that all board power is off, and the physical long-press hardware path remains unconfirmed.

Chinese builds default to the domestic service; English builds default to the international service, with CLI and environment overrides. The default data directory, `/userdata/mybot-rv1126b`, contains sensitive credentials. The references below define the complete behavior and configuration.

| Task | Authoritative reference |
| --- | --- |
| Build, host tests, target SDK and packaging | [Build guide](docs/user/build.zh-CN.md) |
| Direct execution and UI/voice smoke checks | [Getting started](docs/user/getting-started.zh-CN.md) |
| Install startup service, update, inspect health and roll back | [Device deployment](docs/user/deployment.zh-CN.md) |
| Phone setup, network ownership and verification | [AP Wi-Fi setup](docs/user/wifi-provisioning.zh-CN.md) |
| Settings, shutdown and power on | [Device settings](docs/user/settings.zh-CN.md) |
| Servers, identity, persistence and media options | [Configuration](docs/user/configuration.zh-CN.md) |
| System time and fault diagnosis | [Time guide](docs/user/time-sync.zh-CN.md), [troubleshooting](docs/user/troubleshooting.zh-CN.md) |

Direct execution of `run.sh` does not install startup services or modify system ALSA configuration files. Explicit supervised deployment installs startup scripts, disables the original application's startup and updates board network policies; each service launch applies capture mixer settings through `amixer`. Deployment defaults to the video profile; set `"video": false` in deployment settings for voice only. The package must contain the selected profile. Deployment checks and backs up the device before switching releases. Build, packaging and check scripts do not connect to devices.

## Develop

Ubuntu 24.04 needs Python 3.10+, CMake 3.25+, C/C++ compilers, OpenSSL and libdrm development files:

```sh
sudo apt install build-essential pkg-config libssl-dev libdrm-dev cmake python3 python3-venv
python3 -m venv local/dev-tools
local/dev-tools/bin/python -m pip install -r requirements-dev.txt
python3 scripts/doctor.py --host
python3 scripts/check-dependencies.py
python3 scripts/check-quality.py
cmake --preset host-en
cmake --build --preset host-en
ctest --preset host-en
```

The quality check selects pinned clang-format 11 automatically. Use `host-zh` or `host-ubsan` for Chinese or undefined-behavior checks; see the build guide for explicit macOS header setup. Host tests do not load target RTSA or need a target SDK.

Target builds use the complete bundled Agora RTSA 1.10.1 package and additionally require a matching AArch64 compiler, development staging/sysroot and runtime manifest. After following target setup in the build guide:

```sh
RV1126B_BUILD_DIR="$PWD/build/rv1126b-voice-en" sh scripts/build-rv1126b.sh -DRV1126B_LANGUAGE=en-US
RV1126B_BUILD_DIR="$PWD/build/rv1126b-video-en" sh scripts/build-rv1126b.sh -DRV1126B_LANGUAGE=en-US -DMYBOT_ENABLE_VIDEO=ON
python3 scripts/package-rv1126b.py --profile all --language en-US
```

## Architecture and contribution

`src/application/` owns configuration, identity and product lifecycle; `src/platform/` owns Linux services and hardware; `src/ui/` is a pure renderer. `vendor/` holds independently pinned MyBot/AOSL/LVGL and the complete RTSA package. Build integration lives in `cmake/`, automation in `scripts/` and developer programs in `tools/`. The board baseline is AArch64 Buildroot 2024.02, Linux 6.1 and glibc 2.41, with a logical 640×480 UI and 16 kHz mono S16 audio in 20 ms frames.

See [architecture](docs/development/architecture.zh-CN.md), [testing](docs/development/testing.zh-CN.md), [contributing](CONTRIBUTING.md), [support](SUPPORT.md) and [security reporting](SECURITY.md). The [documentation index](docs/README.md) links all guides; the [changelog](CHANGELOG.md) records version changes.

Project-maintained code is [Apache-2.0](LICENSE) unless a file states otherwise. Dependencies and fonts retain their own terms, including AOSL's additional conditions and the applicable RTSA terms. Read [third-party notices](THIRD_PARTY_NOTICES.md), [provenance](VENDORED_SOURCES.md) and [authorization and commercial contacts](docs/dependencies/licensing.md).
