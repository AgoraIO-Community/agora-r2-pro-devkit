# mybot-rv1126b

RV1126B Linux application for MyBot 1.3.0: bidirectional voice, HTTPS pairing,
CoreS3 pixel-glasses UI and optional V4L2/MPP JPEG uplink.

首版面向 RV1126B TEST2 RK805B V10 的现有 AArch64 Buildroot 系统。UI 沿用 M5Stack
CoreS3 工艺灰像素眼镜，采用应用内静态 LVGL 9.5，原生 640×480 渲染，默认旋转
270° 输出到物理 480×640 DSI 屏幕。SDK 侧音频为 16 kHz mono S16、20 ms，云端 AEC 开启。

0.1.0 默认语音/UI流程已由用户在板端验证通过。0.1.1 改为原生分辨率和目标字号字库，用户已完成真机测试并确认显示效果满足需求。

- [首版验证步骤](docs/first-validation.zh-CN.md)：上传、原服务停止、UI/语音/视频测试及恢复。
- [0.1.1 原生文字渲染](docs/ui-native-fonts.zh-CN.md)：改动、字库来源和升级说明。
- [构建与验证记录](docs/validation.zh-CN.md)：实际检查、覆盖范围和限制。
- [移植可行性与实施方案](docs/rv1126b-feasibility.zh-CN.md)：硬件与接口依据。
- [依赖来源与许可](THIRD_PARTY_NOTICES.md)。

## 本地构建

当前 Intel macOS 可直接复用同级 `agora-r2-pro-devkit` 的已准备 SDK。需要 Python 3.7+、
CMake 3.18+、make、主机 C/C++ 编译器及主机 OpenSSL 开发文件。

```sh
python3 scripts/configure-local-sdk.py
sh scripts/test-host.sh
sh scripts/build-rv1126b.sh
RV1126B_BUILD_DIR="$PWD/build-rv1126b-video" sh scripts/build-rv1126b.sh -DMYBOT_ENABLE_VIDEO=ON
python3 scripts/package-rv1126b.py
```

交付包输出到 `dist/mybot-rv1126b-0.1.1-aarch64.tar.gz`，包含默认语音/UI程序和可选视频程序、
RTSA 库、提示音、许可证、ABI 报告与校验清单。打包脚本会拒绝覆盖已有同名解包目录，重打包前
可先移走旧目录。

独立工具链可以设置 `CROSS_COMPILE`、`RV1126B_STAGING`、`AGORA_SDK_DIR`、
`AGORA_RTC_LIBRARY`，完整 libc sysroot 另设 `RV1126B_SYSROOT`。
英文构建加 `-DRV1126B_LANGUAGE=en-US` 并使用单独的构建目录。

本地 UI 预览：

```sh
mkdir -p dist/preview
build-host/mybot-ui-preview dist/preview
```

所有构建、打包与主机测试脚本均不部署或启动设备程序。设备端的 `run.sh` 由验证者手动运行，
使用独立的 `/userdata/mybot-rv1126b` 保存身份绑定数据与 RTSA 日志。
