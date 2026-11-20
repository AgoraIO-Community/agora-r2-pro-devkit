# Changelog

## 1.0.0 — 2026-11-20

First stable release for the Rockchip RV1126B TEST2 RK805B V10.

- Chinese and English runtime bundles each include voice and video executables, the supervised service, and an illustrated offline manual. Supervised deployment selects video by default.
- Devices reconnect to saved Wi-Fi or open the setup hotspot. The service waits for the wireless driver and control socket at boot and can continue when system time synchronizes late.
- Settings supports Wi-Fi setup, confirmed shutdown and local factory reset. A reset clears active Wi-Fi profiles and local pairing credentials and restores playback volume to 70.
- The supervisor quarantines incomplete resource cleanup. Deployment verifies firmware compatibility and package integrity, creates an on-device backup and records each completed stage in a private host receipt.

When upgrading an installed device, use its existing server and device ID to retain pairing, Wi-Fi and saved volume. The previous release remains available for rollback. See the [operations manual](docs/device-operations.en.pdf) and [validation record](docs/validation/README.md).
