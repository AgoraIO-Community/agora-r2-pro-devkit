# BotStation Device Service for RV1126B

A standalone Linux device service integrating the mybot SDK with RV1126B audio,
camera, display, networking, and persistent storage. The SDK manages device
pairing, authentication, conversation lifecycle, and Agora RTC/RTM communication.
Board-specific adapters supply the hardware interfaces.

The target platform is AArch64 Linux with glibc, ALSA, OpenSSL, and LVGL 8.
The service targets the Agora RTSA 1.10.1 API with 20 ms audio packets. The
vendor package is not included; builds require an authorized package supplied
through `AGORA_SDK_DIR` and `AGORA_RTC_LIBRARY`. Dependency provenance is
recorded in `third_party/manifests/SDK_SOURCE.txt` and
`third_party/manifests/RTSA_SOURCE.txt`.

## Architecture

```text
main.c
  Configuration and CPU-serial identity
  Single-instance lock and signal handling
  Network and clock readiness gates
    |
    v
mybot SDK
  Device pairing and credential lifecycle
  HTTPS device-service client
  Conversation control and RTC/RTM
  Audio pipeline and workflow presentation
    |
    v
RV1126B platform descriptor
  ALSA capture/playback     Linux input and stdin
  IPv4 network monitoring  File-backed KV storage
  OpenSSL HTTPS           PCM announcements
  LVGL/DRM display        V4L2 + MPP video
```

The application uses separate capture, playback, and RTC/control workers.
The camera has a dedicated hardware-owning worker. Network provisioning and
system time synchronization belong to the product firmware, not this service.

### Source layout

| File or directory | Responsibility |
|---|---|
| `src/botstation-device/` | Lifecycle, configuration, version, and process entry |
| `src/platform/rv1126b/` | Audio, video, display, input, network, and platform registration |
| `third_party/agent-studio-device-firmware/` | Pinned SDK and Linux support adapters |
| `third_party/agora-rtsa/` | External Agora RTSA package location (not committed) |
| `tests/unit/` | Configuration and control logic |
| `tests/platform/rv1126b/` | Adapter, audio I/O, lifecycle, and video tests |

The Buildroot package is provided as the repository's `BR2_EXTERNAL` tree at
`integrations/buildroot-external`.

## Startup and Device Identity

Startup follows these stages:

1. Load configuration, resolve the device ID, and acquire
   `/run/botstation-device.lock`.
2. Register platform adapters and start the display.
3. Wait for a usable IPv4 address and a default route.
4. Wait until the system clock reaches the configured minimum date.
5. Start the SDK, restore matching credentials or enter pairing, and initialize services.
6. Request one conversation on the first transition to READY.

The automatic conversation request occurs once per process. A failed request,
hangup, or network recovery does not trigger another automatic attempt.

The clock minimum is the UTC configuration-day boundary, with a floor of
2026-01-01 UTC. This is a stale-clock check, not proof of NTP synchronization;
it does not reject future dates. TLS certificate verification remains enabled.

### CPU Serial identity

When `BOTSTATION_DEVICE_ID` is empty, the program reads the `Serial` field from
`/proc/cpuinfo`. It requires one non-placeholder, 16-digit hexadecimal value,
converts it to uppercase, and prefixes it with `RV1126B-`.

```text
Serial:    72bedba03e672ad9
Device ID: RV1126B-72BEDBA03E672AD9
```

Missing, duplicate, malformed, all-zero, or all-`F` serial values cause startup
to fail. There is no shared fallback identity. An explicit device ID can
override CPU-derived identity and must not exceed 32 bytes.

Stored credentials are matched against both the server address and device ID.
A different identity requires pairing; remote device records are not migrated.
Firmware images must not include credentials copied from another device.

## Audio

### Format and routing

| Parameter | Value |
|---|---|
| SDK capture and playback format | 16 kHz, mono, signed 16-bit little-endian PCM |
| Audio codec | G.722 |
| Packet duration | 20 ms; must match the supplied RTSA package |
| Samples per packet, per signal | 320 |
| PCM bytes per packet, per signal | 640 |
| Capture PCM | `botstation_capture` |
| Playback PCM | `botstation_playback` |
| ALSA slave device | `hw:0,0`, 48 kHz, stereo, S16_LE |
| Volume control | SDK software media volume |
| Local wake-word detection | Disabled |

`/etc/alsa/conf.d/90-botstation.conf` defines ALSA `plug` devices that convert
between the SDK format and the hardware format. The hardware stereo layout
does not change the SDK-facing mono format or provide acoustic echo cancellation.
Capture and playback PCM names are independently configurable.

### Cloud AEC

The product has no device-side AEC. Its CMake configuration forces
`MYBOT_CLOUD_AEC=ON`, and RTC enables `enable_audio_downlink_aec`.

The media pipeline interleaves mono microphone samples with the RTC playback
reference samples accepted by the playback adapter. Missing reference data is
represented by silence. For a 20 ms packet, the combined PCM payload is
1,280 bytes. RTC codec metadata remains 16 kHz and one channel, as required by
the SDK's cloud-AEC input convention.

Capture continues during playback to support full-duplex conversation and
user interruption. Reference-queue backpressure is reported rather than
silently discarding reference samples.

### Buffering and I/O

| ALSA setting | Requested value at 16 kHz / 20 ms packets |
|---|---|
| Period | 160 frames / 10 ms |
| Capture buffer | 1,920 frames / 120 ms |
| Playback buffer | 960 frames / 60 ms |
| Playback start threshold | 640 frames / 40 ms, capped by the negotiated buffer |
| Capture start threshold | One frame |

ALSA may negotiate different buffer and period sizes. The adapter reads back
the applied parameters and logs their durations. Playback buffers outside
50-100 ms generate a warning.

PCM access is nonblocking. Short reads are accumulated; short writes return
the accepted frame count and retain the pending suffix in the media pipeline.
I/O retries are bounded by one packet duration. EAGAIN waits release the
adapter mutex so shutdown can stop the stream. XRUN recovery retries an
unaccepted playback suffix at most once within the same deadline.

Capture, playback, and AEC-reference ring buffers each have a two-second
capacity. Capacity is not a fixed prebuffering delay. Playback consumes
available complete packets on its packet-duration timer.

The SysV service configures these controls on ALSA card 0 before launch:

| Mixer control | Value |
|---|---|
| `ACodec_LP Digital Gain Volume` | `0` |
| `ACodec_LP PGA Gain Volume` | `23` |

Mixer configuration failures prevent service startup. Direct execution of the
binary does not apply these mixer settings.

## Camera Uplink

```text
Sensor / ISP
  rkaiq_3A_server: ISP and IQ control
    -> V4L2 rkisp_mainpath: NV12
    -> MPP: hardware JPEG encoding
    -> mybot video callback
    -> Agora RTC uplink
```

The adapter does not own RKAIQ, use Rockit or external KMPP, or provide a local
video preview. It starts capture after RTC connects and releases hardware
after in-flight frame callbacks complete during stop.

| Parameter | Value |
|---|---|
| Resolution | 640 x 480 |
| Encoding and submission rate limit | 3 fps |
| Encoding format | Independent JPEG frames |
| Rotation | 180 degrees |
| Mirroring | Disabled |
| JPEG quality | Initial 60, range 5-80 |
| RTC BWE range | 100 kbit/s-1 Mbit/s |
| Initial BWE target | 550 kbit/s |
| JPEG payload budget | 80% of the current feedback target |

The encoder applies bitrate feedback on the camera worker, not in the RTC
callback. Its configured target has a 100 kbit/s minimum; submission pacing
still respects lower or zero feedback values. Pacing uses the previous JPEG
size and the current budget to determine the next eligible submission time.
It skips frames rather than accumulating encoded frames or idle-time burst credit.
Actual frame rate and bitrate depend on scene content and bandwidth.

The driver retains its capture rate. Software selects frames for copying and
encoding at intervals of at least 334 ms. The adapter supports single-planar
NV12 and multi-planar capture with one contiguous NV12 plane, including row
stride and data offset handling. Unsupported layouts or incomplete frames
produce explicit errors.

An empty `BOTSTATION_VIDEO_DEVICE` selects the unique `rkisp_mainpath` node.
Multiple matching nodes require an explicit device path. Sensor configuration,
media links, and ISP35 IQ files must match the physical camera.

The first valid JPEG after each video start is atomically saved to
`/userdata/botstation-camera.jpg` with mode `0600`. It is the rotated uplink
image, not a separate preview. Snapshot failure does not stop uplink and is
not retried during that start. An existing snapshot may remain if no new image
is saved.

A five-second capture stall or hardware error stops frame delivery. Cleanup
failures retain resources for a subsequent cleanup attempt.

### Camera ownership

With video enabled, the package installs `/etc/botstation-video-capture`.
Product camera launch scripts use this marker to exclude rkipc and external
media-module startup while allowing `rkaiq_3A_server`.

The SysV service requires `rkaiq_3A_server` to be running and rejects startup
while rkipc is running. It does not terminate competing processes. Process
presence alone does not establish ISP readiness.

## Display

The Buildroot product uses LVGL 8 and the DRM driver on `/dev/dri/card0`.
The physical 480 x 640 display is rotated 270 degrees into a 640 x 480 logical
layout. The display requires exclusive DRM access and does not use Weston.
Source Han Sans CN enables Chinese labels; unavailable fonts select English.
Pairing codes use the built-in Latin font.

Before SDK startup, the display distinguishes network readiness, clock
correction, and SDK initialization. SDK workflow screens cover pairing,
pairing codes, readiness, conversation, failure, and shutdown.

Conversation indicators distinguish listening, thinking, speaking, and
voiceprint registration. CoreS3-style eye expressions use arcs for neutral
and speaking states, dots for listening, and a dash/dot pair for thinking.
Thinking lasting at least three seconds changes to opposing orbiting dots.

Without `BOTSTATION_ENABLE_LVGL_UI`, the application uses the SDK's console LCD
adapter instead.

## Configuration

The executable loads `/etc/botstation-device.conf`. Precedence is command-line
arguments, environment variables, configuration file, then built-in defaults.
The file parser accepts known literal `KEY=VALUE` assignments, quotes,
comments, and CRLF. It does not evaluate commands, variable expansion, or
escape expressions. The SysV script also sources this file, so it must remain
a trusted, literal-only configuration.

| Key | Packaged default | Meaning |
|---|---|---|
| `BOTSTATION_ENABLED` | `1` | SysV startup switch; does not gate direct execution |
| `BOTSTATION_SERVER` | `https://botstation.shengwang.cn/api` | Device-service API base URL |
| `BOTSTATION_DEVICE_ID` | Empty | CPU-derived identity unless explicitly overridden |
| `BOTSTATION_FIRMWARE_VERSION` | `0.1.0` | Reported firmware version |
| `BOTSTATION_HARDWARE_MODEL` | `rv1126b-r2pro` | Reported hardware model |
| `BOTSTATION_CAPTURE_DEVICE` | `botstation_capture` | ALSA capture PCM |
| `BOTSTATION_PLAYBACK_DEVICE` | `botstation_playback` | ALSA playback PCM |
| `BOTSTATION_INTERFACE` | Empty | Network interface filter; empty permits eligible routed interfaces |
| `BOTSTATION_INPUT_DEVICE` | Empty | Linux input event device; SysV selects disabled hardware controls when empty |
| `BOTSTATION_TALK_KEY` | `164` | Talk key code (`KEY_PLAYPAUSE`) |
| `BOTSTATION_VIDEO_DEVICE` | Empty | V4L2 path or automatic mainpath discovery |
| `MYBOT_KV_STORE_DIR` | `/userdata/botstation` | Persistent credential storage |
| `MYBOT_ASSETS_DIR` | `/usr/share/botstation` | Announcement asset root |
| `MYBOT_LOCALE` | `zh-CN` | Announcement locale |

The parser additionally accepts `MYBOT_LCD_COLOR` for the console LCD adapter.
Command-line options include `--server`, `--device-id`, `--fw-ver`,
`--hw-model`, `--capture-device`, `--playback-device`, `--interface`,
`--input-device`, and `--talk-key`; `--help` lists their usage.

Linux input handles key presses, excluding releases and autorepeat. The talk
key starts a conversation in READY and stops it in IN_CONVERSATION. Volume keys
adjust SDK software volume. Without a configured input device, foreground
execution accepts line-based stdin controls:

| Input | Action |
|---|---|
| `s` | Start conversation |
| `q` | Stop conversation |
| `p` | Re-pair |
| `u` / `d` | Increase / decrease volume |
| `e` | Exit |

## Networking, HTTPS, and Persistence

Network readiness requires an UP/RUNNING, non-loopback interface with a
non-link-local IPv4 address and a default route. The monitor polls every
second and reports runtime loss and recovery. It does not provision networks,
test Internet reachability, or support IPv6-only operation.

The HTTPS adapter uses OpenSSL with peer and hostname verification and a
minimum of TLS 1.2. HTTP requests share a 15-second deadline across connection,
send, and receive stages. Blocking system DNS and address-by-address connection
handling mean this is not a strict wall-clock bound for every failure path.
The client supports content-length, chunked, and connection-close response
framing with a 32 KiB raw receive-buffer limit.

The KV adapter uses atomic file replacement and file synchronization.
Directories use mode `0700` and files use `0600`. The SysV service requires
the persistent `/userdata` partition to be mounted. SDK teardown runs on
SIGINT/SIGTERM; the service waits up to 60 seconds for graceful shutdown and
does not force-kill the process. It is not a crash-restart supervisor.

## Build and Product Integration

CMake builds C99 code for Linux, with thread support, ALSA, and OpenSSL.
The product links an externally supplied AArch64 RTSA shared library
`libagora-rtc-sdk.so`. Configuration checks ELF architecture and library type;
the SDK checks packet duration against RTSA metadata and video compatibility.
The supplied library requires glibc and has observed GLIBC_2.17 symbol requirements.

| CMake setting | Default or product policy |
|---|---|
| `AGORA_SDK_DIR` | `third_party/agora-rtsa/agora_rtsa_sdk/agora_sdk` |
| `AGORA_RTC_LIBRARY` | `${AGORA_SDK_DIR}/lib/aarch64/libagora-rtc-sdk.so` |
| `MYBOT_AUDIO_PTIME_MS` | `20`; must match the selected RTSA package |
| `MYBOT_CLOUD_AEC` | Forced `ON` |
| `MYBOT_ENABLE_VIDEO` | `ON` |
| `BOTSTATION_ENABLE_LVGL_UI` | `OFF` in standalone CMake; `ON` in Buildroot |
| `BOTSTATION_BUILD_TESTS` | `OFF` |

For a standalone cross-build, provide the authorized RTSA SDK root and library:

```sh
cmake -S . -B build-aarch64 \
  -DAGORA_SDK_DIR=/opt/agora-rtsa/agora_sdk \
  -DAGORA_RTC_LIBRARY=/opt/agora-rtsa/agora_sdk/lib/aarch64/libagora-rtc-sdk.so
```

The Buildroot external tree exposes `BR2_PACKAGE_BOTSTATION_DEVICE`, RTSA
directory/library overrides, packet duration, and
`BR2_PACKAGE_BOTSTATION_DEVICE_VIDEO`.
It requires AArch64, glibc, threads, and dynamic linking. The package selects
ALSA utilities, OpenSSL, CA certificates, LVGL 8, and DRM support. Video adds
Rockchip MPP with the DRM allocator and requires RKAIQ without
`BR2_PACKAGE_CAMERA_ENGINE_RKAIQ_FASTBOOT_CUT`. The camera path uses the
built-in Rockchip MPP service and compatible sensor/IQ configuration.

Runtime installation includes:

```text
/usr/bin/botstation-device
/usr/lib/libagora-rtc-sdk.so
/usr/share/botstation/locales/<locale>/*.pcm
/usr/share/licenses/botstation-device/
/etc/botstation-device.conf
/etc/alsa/conf.d/90-botstation.conf
/etc/init.d/S99botstation-device
```

LVGL, DRM, MPP, and other dependency libraries are supplied by their respective
Buildroot packages. The mybot SDK is built with the application; development
headers and static SDK archives are not installed into the root filesystem.
No systemd unit is provided.

## Diagnostics and Tests

The SysV service redirects output to `/var/log/botstation-device.log` and
records its PID in `/run/botstation-device.pid`. Foreground execution writes
to the terminal. Default application logging is NOTICE.

| Log group | Information |
|---|---|
| `[HTTPS]` | DNS/TCP/TLS stages, certificate results, transfer counts, and transport errors |
| `[HTTP]` | Request metadata, status, elapsed time, and complete text request/response bodies |
| `ALSA capture/playback` | Applied format, period, buffer size, and start threshold |
| `[AUDIO] playback queue_ms` | Current and maximum software queue duration and AEC-reference stalls |
| `[AUDIO] playback accepted` | Accepted frames, peak level, maximum write gap, recoveries, state, and ALSA delay |
| `camera:` | Capture layout, snapshots, encoding/submission rates, and budget drops |
| `[RTC] stats` | Video input/transmit rate, audio transmit rate, and last-mile delay |
| `[LCD]` | SDK workflow screen updates |

Playback diagnostics are emitted at five-second intervals; adapter write
statistics require playback calls. Write gaps include speech pauses.
`alsa_delay_ms` reports ALSA's pending delay, not complete acoustic latency,
and is `-1` when the query fails. Camera statistics are emitted approximately
every two seconds while encoding.

**HTTP body logs can contain device credentials, pairing data, and RTC tokens.**
Bodies are not redacted; Authorization headers are not printed. Logs require
restricted access and redaction before sharing. Production logging must account
for this sensitive-data exposure. App Certificates and other server-side
secrets must not be provisioned into device configuration.

`BOTSTATION_BUILD_TESTS=ON` enables CTest targets for configuration and CPU Serial
parsing, platform adapters, automatic start, key mapping, audio short I/O and
XRUN handling, CLI behavior, and video when enabled. Video tests use mocked
hardware operations. Linux host tests require a matching host RTSA library
and packet duration; they do not validate physical audio, ISP behavior, or
cloud delivery.
