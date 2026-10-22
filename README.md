# agora-r2-pro-devkit

[![License](https://img.shields.io/github/license/AgoraIO-Community/agora-r2-pro-devkit)](LICENSE)
[![Upstream](https://img.shields.io/badge/upstream-agent--studio--device--firmware-0969da)](https://github.com/AgoraIO-Community/agent-studio-device-firmware)
[![Status](https://img.shields.io/badge/status-bootstrap-yellow)](README.md)

**agora-r2-pro-devkit** is the firmware, hardware-integration, and documentation workspace for
the R2 Pro AI development kit. The project is intended to run real-time AI voice conversations
with [agent-studio-device-firmware](https://github.com/AgoraIO-Community/agent-studio-device-firmware)
and Agora RTSA while keeping board-specific hardware behind a clear platform adapter boundary.

> **Project status:** initial scaffold. Firmware sources, the SoC selection, pin map, partition
> layout, and build scripts have not been added yet. Capabilities described below are integration
> requirements, not claims that a board port has been implemented or hardware-validated.

## Goals

- Provide a reproducible reference firmware baseline for R2 Pro hardware.
- Support low-latency, full-duplex AI conversations over Agora RTSA.
- Keep Wi-Fi provisioning, network recovery, credentials, and device settings persistent.
- Isolate board initialization, audio, input, display, storage, and networking behind platform
  interfaces so SDK code remains portable.
- Ship Chinese (`zh-CN`) and English (`en-US`) user flows from one maintained codebase.
- Validate builds separately from real-device behavior and publish clear release artifacts.

## Reference specifications

The following table captures the initial software contract. Hardware-dependent values must be
replaced with values verified against the R2 Pro schematic and bring-up tests.

| Area | Initial target |
| --- | --- |
| Product | Agora R2 Pro development kit |
| AI runtime | `agent-studio-device-firmware` |
| Real-time transport | Agora RTSA full-duplex audio |
| SoC and board SDK | To be selected during board bring-up |
| Network | Wi-Fi station mode, reconnect, and first-boot provisioning |
| Audio boundary | 16 kHz, mono, signed 16-bit PCM, frame-based APIs |
| RTSA packet cadence | 60 ms baseline; change only with a matching RTSA build |
| Persistence | Wi-Fi credentials, volume, and required device identity |
| Input and display | Board dependent; exposed through the platform adapter |
| UI languages | Simplified Chinese and English |
| Repository code license | Apache-2.0 unless a file states otherwise |

## Intended capabilities

- First-boot access-point provisioning followed by station-mode operation.
- HTTPS communication with hostname verification.
- Independent audio capture and playback paths with bounded shutdown and retry behavior.
- Physical-button and optional touchscreen conversation controls.
- Optional display status for provisioning, pairing, network, and conversation state.
- A board profile that owns clocks, pins, power sequencing, storage layout, and peripheral
  initialization.
- Bilingual provisioning prompts, pairing-code announcements, and device-service selection.
- Scripted multi-language builds and documented recovery/flashing procedures.

## Suggested repository layout

The tree below is the intended structure as implementation is added:

```text
agora-r2-pro-devkit/
├── components/       portable SDK sources and platform adapters
├── docs/             hardware, provisioning, build, and validation notes
├── firmware/         application entry point and board profiles
├── partitions/       reviewed storage and firmware partition tables
├── scripts/          dependency checks and reproducible build helpers
└── tests/            host tests and hardware-test procedures
```

Keep generated output, downloaded dependencies, local SDK configuration, signing material, and
built firmware outside version control. Pin external dependencies to exact revisions and record
their licenses and provenance.

## Getting started

The first usable milestone requires:

- Git and Python 3 for repository tooling.
- The toolchain and board SDK selected for the R2 Pro SoC.
- R2 Pro hardware, a data-capable USB connection, and recovery/flashing instructions.
- Access to a compatible device service and valid Agora service configuration.

Clone the repository and its submodules after the build structure is published:

```sh
git clone --branch main --recurse-submodules https://github.com/AgoraIO-Community/agora-r2-pro-devkit.git
cd agora-r2-pro-devkit
```

Build and flash commands will be added together with the first working board profile. Until then,
the repository is documentation-only.

## Bring-up checklist

1. Record the SoC, Flash, RAM/PSRAM, operating voltages, reset and recovery inputs, and debug
   console from the schematic.
2. Record the audio codec or I2S wiring, microphone channels, speaker path, sample formats, and
   gain controls.
3. Record the display controller, touch controller, buttons, LEDs, and any camera interface.
4. Choose and document the partition table, OTA layout, factory-data region, and recovery method.
5. Implement a minimal boot, console, storage, and network smoke test before integrating the SDK.
6. Implement the platform adapters and pass sustained bidirectional-audio, reconnect, repeated
   session start/stop, and failure-cleanup tests on physical hardware.
7. Add reproducible build commands, artifacts, CI coverage, and hardware validation results.

Do not mark a hardware feature as supported based only on a successful compile or host test.

## License

Repository-owned code is licensed under the [Apache License 2.0](LICENSE). The license file is an
unmodified template and its copyright placeholder should be completed before release. Bundled
SDKs, codec binaries, model files, fonts, audio prompts, and other third-party assets may use
different terms; review their notices before redistributing source or firmware images.
