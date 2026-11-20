# agora-r2-pro-devkit RV1126B runtime package

For an existing AArch64 Buildroot system on RV1126B TEST2 RK805B V10. The bundled manifests and `./run.sh --version` identify the version, language and voice/video profiles.

The archive includes the [English operations manual PDF](../docs/device-operations.en.pdf). See the [build reference](../docs/user/build.zh-CN.md) and [deployment reference](../docs/user/deployment.zh-CN.md). The source references are in Chinese; the English PDF includes the complete procedure. The [validation record](../docs/validation/README.md) defines the hardware acceptance scope.

## Verify and run directly

Extract the archive into its own directory under `/userdata`. From that directory, run:

```sh
sha256sum -c SHA256SUMS
./run.sh --version
./run.sh --help
```

All checks must pass. Have the operator fully stop any existing application or stock business service before starting; only one instance can run on a device. Keep system networking, time synchronization and camera 3A services available.

```sh
./run.sh
# Select video explicitly when the video executable is included:
RV1126B_VIDEO=1 ./run.sh
```

`example.env` is not loaded automatically. Server precedence is `--server` > `RV1126B_SERVER` > the build language default: `https://botstation.agora.io/api` for English and `https://botstation.shengwang.cn/api` for Chinese. Remove the trailing `/api` for the console address. Preserve the server, identity and data directory across upgrades to reuse pairing.

## Basic operation

- When offline and saved networks are unavailable, connect your phone to the displayed `mybot-xxxx` hotspot, open `http://192.168.4.1/`, submit a 2.4 GHz Wi-Fi network and wait for success.
- Claim the displayed pairing code in the console for the matching service region. After pairing, tap the screen briefly to start/end a conversation.
- Use the top-right settings menu for Wi-Fi setup and poweroff. Back is at the top left. Holding the screen does not start Wi-Fi setup.
- For direct runs, enter `s/q/p/u/d/e` followed by Enter to start, hang up, pair again, increase/decrease volume or exit. Ctrl-C also exits.

## System dependencies and persistent data

The image must provide the matching glibc 2.41/runtime libraries, ALSA `botstation_capture`/`botstation_playback`, DRM/evdev, CA certificates and system time synchronization. Audio is 16 kHz mono S16 with 20 ms frames. AP setup requires `ip`, `iw`, `hostapd`, `dnsmasq`, `wpa_supplicant`, `udhcpc` and compatible AP drivers; optional video requires V4L2/MPP. The package includes the RTSA runtime, prompts, helpers and dependency notices. Applicable terms are in `licenses/`; the authorization summary is `licenses/DEPENDENCY_LICENSING.md`.

The private default directory is `/userdata/mybot-rv1126b`: `device_auth` stores pairing, `volume.settings` stores volume and `wifi/wifi.credentials` stores private Wi-Fi records. Supervised system cooperation also uses `/etc/wpa_supplicant.conf` as its primary Wi-Fi record. Preserve these settings and data; changing the KV directory alone does not isolate system networking. Redact tokens, passwords, pairing codes and device identity when reporting problems. Do not upload the private directory.

## Direct runs and supervised services

Running `run.sh` directly does not install startup services or write ALSA configuration files. The repository's explicit deployment tool installs `S99mybot`, disables stock business startup and applies board network policies. Each supervised launch sets capture controls through `amixer`. Service settings are in `/etc/mybot-rv1126b.conf`, releases in `/userdata/mybot/releases`, and application data remains separate. Follow the deployment guide for supervised upgrades, service control and rollback; do not directly restore the stock service.

Only supervisor-accepted shutdown closes the application display output; ordinary stops restore the prior display. After cleanup, `poweroff.sh` sets and verifies pwm-backlight `brightness=0` and `bl_power=4`, then calls `/sbin/poweroff`. Preparation interruption or command failure triggers a restore attempt; late shutdown signals during system handoff do not relight it. After app exit, helper failure only attempts backlight recovery; inspect the private log and restart the service explicitly. Software poweroff does not guarantee all board power is off; the physical long-press hardware path remains unconfirmed.

Factory Reset in Settings clears local pairing credentials, active Wi-Fi profiles and saved volume. The service restarts in Wi-Fi setup with volume 70. It retains the device ID and installed software. Administrators manage cloud ownership and maintenance backups separately.
