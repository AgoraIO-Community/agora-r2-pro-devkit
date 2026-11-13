# Third-party notices

This repository contains or links against software supplied by third parties.
The root Apache-2.0 license applies only to repository-owned code.

- The pinned device SDK and AOSL sources retain the licenses and notices under
  `third_party/agent-studio-device-firmware/`.
- Agora RTSA is supplied as an external authorized package. Its archive,
  library hashes and ABI evidence are recorded under `third_party/manifests/`.
- ALSA, OpenSSL, libdrm, LVGL, lv_drivers, Rockchip MPP and RKAIQ are provided
  by the product Buildroot or target sysroot.

Redistributing a firmware image does not grant rights to redistribute the
external RTSA package or any Buildroot component. Review the corresponding
upstream terms before publishing source or binary artifacts.
