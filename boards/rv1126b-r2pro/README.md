# RV1126B R2 Pro reference port

This directory contains the runtime configuration for the exploratory RV1126B
service. It is not a confirmed R2 Pro board profile.

## Known contract

- AArch64 Linux with glibc
- ALSA card 0 exposing the `ACodec_LP` controls
- Capture and playback converted to 16 kHz, mono, S16_LE by
  `90-botstation.conf`
- `/userdata` mounted before service startup
- IPv4 address and default route provided by the product image
- CPU Serial available through `/proc/cpuinfo`, or an explicit device ID
- Optional V4L2/ISP/MPP video path with `/etc/botstation-video-capture`

Pin map, partition sizes, camera sensor, display timing and recovery procedure
remain unverified. Do not convert these assumptions into release claims until
the physical board is tested.

The service configuration is installed as `/etc/botstation-device.conf`. It
contains no credentials or private endpoints.
