# Build

## Host validation

Host tests require Linux x86_64, CMake 3.16 or newer, a C99 compiler, ALSA and
OpenSSL development packages, and an authorized RTSA package with a matching
host library. Host packages commonly use a different packet cadence, so pass
the cadence declared by that package:

```sh
export AGORA_SDK_DIR=/path/to/agora_rtsa_sdk/agora_sdk
export AGORA_RTC_LIBRARY="$AGORA_SDK_DIR/lib/x86_64/libagora-rtc-sdk.so"
MYBOT_AUDIO_PTIME_MS=60 ./scripts/build-host.sh
```

The host configuration disables camera and LVGL. It validates configuration
parsing, lifecycle gates, audio short-I/O handling, key mapping and CLI
behavior; it does not validate hardware.

## AArch64 standalone build

The product build requires:

- an AArch64 Linux sysroot and CMake toolchain file from the RV1126B SDK
- an authorized RTSA 1.10.1 package built for AArch64
- ALSA and OpenSSL in the target sysroot
- Rockchip MPP and RKAIQ for video
- LVGL 8, lv_drivers DRM and libdrm for the display

```sh
export CMAKE_TOOLCHAIN_FILE=/path/to/buildroot/toolchainfile.cmake
export AGORA_SDK_DIR=/path/to/agora_rtsa_sdk/agora_sdk
export AGORA_RTC_LIBRARY="$AGORA_SDK_DIR/lib/aarch64/libagora-rtc-sdk.so"
export BOTSTATION_INSTALL_ROOT="$PWD/out-r2pro"
./scripts/build-aarch64.sh
```

For a minimum compile check without camera or display:

```sh
MYBOT_ENABLE_VIDEO=OFF BOTSTATION_ENABLE_LVGL_UI=OFF \
  ./scripts/build-aarch64.sh
```

CMake verifies the RTSA ELF architecture and packet cadence. It also rejects a
package metadata file that does not enable G722 because the pinned SDK requests
G722.

## Buildroot

Use `integrations/buildroot-external` as a `BR2_EXTERNAL` tree with the vendor
RV1126B Buildroot SDK:

```sh
make BR2_EXTERNAL="$PWD/integrations/buildroot-external" menuconfig
```

Enable `Agora R2 Pro -> botstation-device`, set the authorized RTSA SDK
directory, and select the matching packet cadence. Merge
`integrations/buildroot-external/configs/rv1126b_r2pro.fragment` into the vendor
defconfig only after the required Rockchip and LVGL packages are available.

The package installs:

```text
/usr/bin/botstation-device
/usr/lib/libagora-rtc-sdk.so
/usr/share/botstation/locales/
/etc/botstation-device.conf
/etc/alsa/conf.d/90-botstation.conf
/etc/init.d/S99botstation-device
/etc/botstation-video-capture    # when video is enabled
```

The repository does not provide a bootloader, kernel, root filesystem or
partition image.
