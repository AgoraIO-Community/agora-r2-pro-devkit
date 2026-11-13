# RV1126B TEST2 RK805B V10

This profile records the Linux interfaces observed on the original test device. It does not initialize GPIO, change system audio configuration, provision networking, or install firmware services.

| Interface | Baseline |
| --- | --- |
| CPU / OS | AArch64, RV1126B, Buildroot 2024.02, Linux 6.1.172, glibc 2.41 |
| Display | 480×640 DSI, `/dev/dri/card0`; 640×480 logical canvas rotated 270° |
| Audio | Existing `botstation_capture` / `botstation_playback` ALSA plugs; SDK 16 kHz mono S16, 20 ms |
| Input | Discover `axs_ts` touch and `adc-keys`; conversation key 139; left/right volume |
| Camera | Optional `rkisp_mainpath` V4L2 capture + MPP JPEG, 640×480, at most 3 fps, 180° |
| Persistence | Independent `/userdata/mybot-rv1126b` directory |

`board_config.h` defines defaults. Existing CLI/environment overrides remain available. Logical UI dimensions and font/layout choices belong to the UI component.

The original product service shares the hardware and must be stopped by the operator before testing. Leave the camera 3A and network services running. Current verified behavior and remaining checks are tracked in the [validation matrix](../../../../docs/validation/README.md); build or mock-test success does not establish physical device behavior.
