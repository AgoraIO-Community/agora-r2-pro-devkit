# Architecture

The repository separates product code, board adapters, board runtime data,
third-party sources and external build-system integration.

```text
src/botstation-device/     lifecycle, configuration and process entry
src/platform/rv1126b/      RV1126B audio, video, display, input and network
boards/rv1126b-r2pro/      runtime configuration and hardware assumptions
third_party/               read-only SDK snapshot and dependency manifests
tests/unit/                pure configuration and control logic
tests/platform/rv1126b/    adapter and mocked-device tests
integrations/buildroot-external/  BR2_EXTERNAL package and options
docs/                      architecture, build and validation contracts
```

`botstation-device` owns process startup, configuration precedence, network
and clock gates, and SDK start/stop ordering. The platform library owns Linux
adapter operations and is linked into the service. The SDK snapshot remains a
read-only dependency and must expose only its documented public headers to
product code.

The board directory is not a complete board-support package. It records only
runtime assumptions that are verified by the current source. Pin maps,
partition sizes and hardware validation remain product-SDK responsibilities.
