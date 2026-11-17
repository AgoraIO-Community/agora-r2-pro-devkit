# mybot-rv1126b

[English](README.md) | [简体中文](README.zh-CN.md)

A Linux application for running MyBot 1.3.0 on the **RV1126B TEST2 RK805B V10** board. It provides AP Wi-Fi setup with voice guidance, HTTPS pairing, bidirectional voice, persistent device identity and output volume, a pixel-glasses UI, and an optional V4L2/MPP JPEG uplink.

The current source is the **<!-- project-version -->0.5.3<!-- /project-version --> deployment candidate**. The 0.1.0 voice/UI flow and 0.1.1 text-rendering improvement passed user testing on hardware. AP Wi-Fi setup, voice guidance, persistent volume and time handling require device testing; optional video and extended stability testing remain incomplete. [Validation status](docs/validation/README.md) is the single source for results.

## Platform and behavior

The recorded device baseline is AArch64 Buildroot 2024.02, Linux 6.1, glibc 2.41, ALSA, DRM/KMS and a physical 480×640 DSI screen. The UI renders at 640×480 with a default 270° rotation and statically linked LVGL 9.5. Audio uses 16 kHz mono signed-16 PCM, 20 ms frames and Cloud AEC.

Output volume is saved in the private runtime directory and restored across application restarts and manual Wi-Fi setup. The existing keys, terminal commands and cloud volume controls use the same 0–100 scale; 100 preserves the previous output level. Application volume controls apply playback gain once and do not adjust capture gain or system mixer settings. See [configuration](docs/user/configuration.zh-CN.md).

The application preserves an existing usable network and the system's ALSA configuration. When offline, it tries saved Wi-Fi networks for up to 30 seconds, then starts an open `mybot-xxxx` hotspot. Connect a phone and open `http://192.168.4.1/` to select a 2.4 GHz network or enter a hidden SSID. The bilingual page supports saved networks, connection progress and retries. A short touch or short Menu press starts/stops a conversation. To configure Wi-Fi manually, open the top-right settings gear and choose “设备配网” (Wi-Fi setup); this is the only manual entry point and is available before the SDK starts. Holding the screen or Menu key does not start a conversation or Wi-Fi setup. The application stops MyBot before manual Wi-Fi setup and restarts it after setup completes. [Wi-Fi setup guide](docs/user/wifi-provisioning.zh-CN.md).

Once the hotspot and portal are ready, the application plays the bundled setup guidance once per setup session, using `MYBOT_LOCALE` and the saved output volume. Muting also silences guidance. Leaving setup or exiting cancels playback and releases the audio device before MyBot resumes; a guidance failure is reported while the portal remains available.

Before starting MyBot, the application waits up to 30 seconds for plausible system time after the network is ready and AP setup has ended. It uses existing OS time services by default; an optional foreground helper must be selected explicitly. A timeout or helper failure reports the cause and exits with a nonzero status. The 2026-01-01 threshold only rejects obviously invalid dates and does not establish accurate NTP synchronization. See the [time setup guide](docs/user/time-sync.zh-CN.md).

AP setup requires target-side `iw`, `hostapd`, `dnsmasq`, `wpa_supplicant`, `udhcpc`, `ip` and a driver with nl80211 AP support. These programs are supplied by the target system; only the DHCP hook is bundled. AP+STA is preferred; a single-interface fallback requires application ownership of the station. Existing network managers are never globally stopped. `--no-wifi-provisioning` retains network monitoring only. The application does not include firmware installation, OTA, wakeword/NPU, video downlink or a local camera preview. Runtime data defaults to `/userdata/mybot-rv1126b`; upgrades preserve pairing when server and identity configuration stay the same.

Directly running `run.sh` does not install startup services or modify system ALSA configuration files. Explicit [supervised deployment](docs/user/deployment.zh-CN.md) installs `/etc/init.d/S99mybot`, disables the original application startup, and updates `dnsmasq`/`dhcpcd` policies and a lease hook. Each service launch applies the board's capture mixer settings through `amixer`; it preserves ALSA configuration files. The operator reloads network policies separately after checking the connection conditions.

## Build and test

Use **Python 3.10+**, **CMake 3.25+**, a C/C++ compiler, OpenSSL development files and libdrm development headers. Host UI/tests do not load the target RTSA library or require a target SDK. On Ubuntu 24.04, install the development packages and the pinned formatting tool in an ignored virtual environment:

```sh
sudo apt install build-essential pkg-config libssl-dev libdrm-dev cmake python3 python3-venv
python3 -m venv local/dev-tools
local/dev-tools/bin/python -m pip install -r requirements-dev.txt
```

The quality check automatically selects `local/dev-tools/bin/clang-format` and verifies its version; activating the virtual environment is not required.

```sh
python3 scripts/doctor.py --host
python3 scripts/check-dependencies.py
python3 scripts/check-quality.py
cmake --preset host-zh
cmake --build --preset host-zh
ctest --preset host-zh
```

Use `host-en` for English and `host-ubsan` for undefined-behavior checks. On macOS, explicitly provide the libdrm header directory when it is not installed as a native development package; see [build instructions](docs/user/build.zh-CN.md). Host CI runs on Linux; only the environment recorded in the validation matrix has been tested locally.

The complete pinned Agora RTSA 1.10.1 package is included in `vendor/rtsa/`; target builds automatically use its AArch64 library. Supply a matching AArch64 compiler, development staging/sysroot and a runtime-library manifest as described in [independent target setup](docs/user/build.zh-CN.md).

```sh
sh scripts/build-rv1126b.sh
RV1126B_BUILD_DIR="$PWD/build/rv1126b-video-zh" sh scripts/build-rv1126b.sh -DMYBOT_ENABLE_VIDEO=ON
python3 scripts/package-rv1126b.py --profile all --language zh-CN
```

Build and packaging commands run locally and do not deploy to a device. [Getting started](docs/user/getting-started.zh-CN.md) covers manual installation, service ownership, upgrade and rollback.

## Project layout

| Directory | Responsibility |
| --- | --- |
| `src/application/` | Configuration, identity, process guards and application lifecycle |
| `src/platform/` | Board defaults, Linux services and hardware drivers |
| `src/ui/` | Pure renderer, pixel-glasses view, expressions and compiled fonts |
| `cmake/` | Dependency integration and toolchain configuration |
| `vendor/mybot/`, `vendor/aosl/`, `vendor/lvgl/`, `vendor/rtsa/` | Independently pinned dependencies, including the complete RTSA package; build integration stays in `cmake/dependencies/` |
| `tests/` | Unit, integration and automation-tool verification |
| `tools/` | Developer programs, including the standalone `ui_preview/` |
| `scripts/` | Build, check, code-generation and packaging automation |
| `packaging/` | Delivery templates, startup script and example configuration |
| `docs/` | User/developer guides, validation and historical evidence |
| `githooks/` | Conventional Commit message validation |

Project-maintained modules live under `src/`; pinned dependency snapshots live under `vendor/`. Public include namespaces (`mybot_platform/`, `mybot_ui/`) and CMake target names remain stable when directories move. See the [architecture guide](docs/development/architecture.zh-CN.md) for the dependency boundaries.

## Documentation and contribution

- Illustrated device manual: [English PDF](docs/device-operations.en.pdf) · [简体中文 PDF](docs/device-operations.zh-CN.pdf)
- [Documentation index](docs/README.md), [configuration](docs/user/configuration.zh-CN.md), [Wi-Fi setup](docs/user/wifi-provisioning.zh-CN.md), [system time](docs/user/time-sync.zh-CN.md), [troubleshooting](docs/user/troubleshooting.zh-CN.md)
- [Architecture](docs/development/architecture.zh-CN.md), [testing](docs/development/testing.zh-CN.md), [release process](docs/development/releasing.zh-CN.md)
- [Dependency layout](docs/development/third-party-layout.zh-CN.md), [source provenance](VENDORED_SOURCES.md)
- [Contributing](CONTRIBUTING.md), [support](SUPPORT.md), [changelog](CHANGELOG.md)

Project-maintained code is [Apache-2.0](LICENSE) unless a file carries another notice. MyBot/AOSL, LVGL and fonts retain their own terms; file-level notices also apply to the pixel-glasses view. The bundled RTSA package retains its vendor terms and is not covered by this project's license. Read [third-party notices](THIRD_PARTY_NOTICES.md) and [vendored sources](VENDORED_SOURCES.md) before redistributing source or packages.

Versioned installation, system Wi-Fi integration, service controls and rollback are described in the [device deployment guide](docs/user/deployment.zh-CN.md). Build and packaging scripts never deploy to a device.

The top-right settings entry opens Wi-Fi setup and poweroff confirmation; see the [settings guide](docs/user/settings.zh-CN.md).
