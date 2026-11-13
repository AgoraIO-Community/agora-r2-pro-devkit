# Buildroot package

Use this tree as a Buildroot external tree:

```sh
make BR2_EXTERNAL=/path/to/agora-r2-pro-devkit/integrations/buildroot-external \
  menuconfig
```

Enable `Agora R2 Pro -> botstation-device` and set the authorized RTSA SDK
directory. The vendor Buildroot tree must provide the Rockchip MPP, RKAIQ,
LVGL 8, lv_drivers and DRM packages selected by the board configuration.

The package installs the executable, RTSA shared library, locale prompts,
runtime configuration, ALSA plug definitions and SysV service. The RTSA
directory is outside this repository and is not downloaded automatically.
