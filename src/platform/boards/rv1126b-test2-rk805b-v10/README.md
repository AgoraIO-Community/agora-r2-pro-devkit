# RV1126B TEST2 RK805B V10

This profile records the Linux interfaces observed on the original test device. It does not initialize GPIO, change system audio configuration or install firmware services. AP Wi-Fi setup is implemented by the platform service and requires target-side tools and compatible nl80211 driver support.

| Interface | Baseline |
| --- | --- |
| CPU / OS | AArch64, RV1126B, Buildroot 2024.02, Linux 6.1.172, glibc 2.41 |
| Display | 480×640 DSI, `/dev/dri/card0`; 640×480 logical canvas rotated 270° |
| Audio | Existing `botstation_capture` / `botstation_playback` ALSA plugs; SDK 16 kHz mono S16, 20 ms |
| Input | Discover `axs_ts` touch and `adc-keys`; conversation key 139; left/right volume |
| Camera | Optional `rkisp_mainpath` V4L2 capture + MPP JPEG, 640×480, at most 3 fps, 180° |
| Persistence | Independent `/userdata/mybot-rv1126b` directory; private Wi-Fi profiles under `wifi/` |
| Wi-Fi setup | Open `mybot-xxxx` 2.4 GHz AP, `192.168.4.1`; runtime tools/driver and AP+STA or safe station ownership required |

`board_config.h` defines defaults. Existing CLI/environment overrides remain available. Logical UI dimensions and font/layout choices belong to the UI component.

The original product service shares the hardware and must be stopped by the operator before testing. Leave the camera 3A and existing network services running. AP setup never globally stops external network managers; if the driver cannot provide AP+STA and an external manager owns the station, explicitly hand off that interface before testing or use `--no-wifi-provisioning`. See [Wi-Fi setup](../../../../docs/user/wifi-provisioning.zh-CN.md). Current verified behavior and remaining checks are tracked in the [validation matrix](../../../../docs/validation/README.md); build or mock-test success does not establish physical device behavior.
