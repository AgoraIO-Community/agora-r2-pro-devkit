# Validation

Validation levels are deliberately separate:

1. Repository layout and formatting
2. Host build and adapter tests
3. AArch64 cross-build against the exact RTSA package
4. Buildroot root filesystem and image integration
5. Physical RV1126B audio, display, camera and network testing

## Current evidence

On 2026-11-11, before the repository reorganisation, the service compiled on
Linux x86_64 with the available 60 ms RTSA package, camera and LVGL disabled,
and seven CTest entries passed. This covers configuration, platform adapter,
automatic start, audio short-I/O and XRUN handling, key mapping, help and
invalid arguments.

That result does not validate the target RTSA 1.10.1 AArch64 package or its
G722 configuration, Rockchip MPP/RKAIQ, DRM/LVGL, physical audio, cloud
delivery or R2 Pro hardware.

## Required target checks

- Build with the documented 20 ms AArch64 RTSA package and matching G722.
- Exercise provisioning prerequisites, route loss/recovery and stale clock.
- Verify CPU Serial identity and credential reuse after reboot.
- Test sustained full-duplex audio, prompt drain, playback XRUN and shutdown.
- Test display orientation, workflow transitions and exclusive DRM ownership.
- Test camera layout, first snapshot, JPEG submission pacing and repeated stop.
- Record logs, package hashes, SDK/RTSA versions and the board hardware revision.
