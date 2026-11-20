# agora-r2-pro-devkit

[English](README.md) | [简体中文](README.zh-CN.md)

声网官方的 RV1126B Linux 应用，基于 agent-studio-device-firmware 1.3.0，提供 AP 配网及语音引导、HTTPS 绑定、双向语音、稳定设备身份和音量持久化、像素眼镜 UI，以及可选的 V4L2/MPP JPEG 上行。

当前应用版本为 **<!-- project-version -->1.0.0<!-- /project-version -->**，面向 **RV1126B TEST2 RK805B V10**。源码检查、交叉构建与真机验证分别记录在[验证状态](docs/validation/README.md)；新包不继承旧包的硬件验收结论。

## 使用

首次构建、部署和使用按[中文图文手册（PDF）](docs/device-operations.zh-CN.pdf)逐步操作；也提供[英文手册](docs/device-operations.en.pdf)。

已有网络时直接沿用；离线恢复失败后显示开放热点 `mybot-xxxx`，手机访问 `http://192.168.4.1/` 配网。手动配网仅通过右上角设置中的“设备配网”进入。短触屏幕或 Menu 短按开始/结束会话，长按不触发会话或配网；设置中还可确认关机或恢复出厂设置。恢复出厂会清除已存 Wi-Fi 和本机绑定凭据、将音量恢复为 70，然后重新进入配网。重启应用、升级或配网后保持原绑定，需要保留服务地址、设备身份及数据目录。

受监督关机在请求系统 poweroff 前显式关闭 PWM 背光；普通停止应用仍恢复原显示。软件关机不等同于整机所有供电切断，实体长按的实际硬件链路尚未确认。

中文构建默认使用国内服务，英文构建默认使用国际服务；CLI 和环境变量可覆盖。默认数据目录为 `/userdata/mybot-rv1126b`，包含敏感凭据。完整行为和配置见下表。

| 任务 | 权威参考 |
| --- | --- |
| 编译、主机测试、目标 SDK 和打包 | [构建指南](docs/user/build.zh-CN.md) |
| 直接运行、UI 与语音冒烟 | [上手指南](docs/user/getting-started.zh-CN.md) |
| 安装自启动、升级、健康检查和回退 | [设备部署](docs/user/deployment.zh-CN.md) |
| 手机配网、网络管理归属与验证 | [AP 配网](docs/user/wifi-provisioning.zh-CN.md) |
| 设置及关机/开机 | [设备设置](docs/user/settings.zh-CN.md) |
| 服务地址、身份、持久化及音视频参数 | [配置参考](docs/user/configuration.zh-CN.md) |
| 系统时间与故障定位 | [时间指南](docs/user/time-sync.zh-CN.md)、[故障排查](docs/user/troubleshooting.zh-CN.md) |

直接运行 `run.sh` 不安装自启动或修改系统 ALSA 配置文件。显式受监督部署会安装启动脚本、禁用原厂业务自启动、更新板级网络策略；每次服务启动通过 `amixer` 应用采集 mixer 设置。部署默认选择视频 profile，只部署语音时在 settings 中显式设置 `"video": false`；包中必须包含所选 profile。部署工具先预检和备份，再安装切换；构建、打包和检查脚本不连接设备。

## 开发

Ubuntu 24.04 使用 Python 3.10+、CMake 3.25+、C/C++ 编译器、OpenSSL 和 libdrm 开发文件：

```sh
sudo apt install build-essential pkg-config libssl-dev libdrm-dev cmake python3 python3-venv
python3 -m venv local/dev-tools
local/dev-tools/bin/python -m pip install -r requirements-dev.txt
python3 scripts/doctor.py --host
python3 scripts/check-dependencies.py
python3 scripts/check-quality.py
cmake --preset host-zh
cmake --build --preset host-zh
ctest --preset host-zh
```

质量检查自动使用固定的 clang-format 11。另有 `host-en`、`host-ubsan`；macOS 的显式头文件设置见构建指南。主机测试不加载目标 RTSA，也不要求目标 SDK。

目标构建使用仓库内完整的 Agora RTSA 1.10.1，另需匹配的 AArch64 编译器、开发 staging/sysroot 和运行库 manifest。按构建指南配置后执行：

```sh
sh scripts/build-rv1126b.sh
RV1126B_BUILD_DIR="$PWD/build/rv1126b-video-zh" sh scripts/build-rv1126b.sh -DMYBOT_ENABLE_VIDEO=ON
python3 scripts/package-rv1126b.py --profile all --language zh-CN
```

## 架构与贡献

`src/application/` 负责配置、身份及产品生命周期；`src/platform/` 负责 Linux 服务和硬件；`src/ui/` 为纯渲染器；`vendor/` 保存独立固定的 agent-studio-device-firmware/AOSL/LVGL/完整 RTSA。构建接入在 `cmake/`，自动化在 `scripts/`，开发程序在 `tools/`。板级基线为 AArch64 Buildroot 2024.02、Linux 6.1、glibc 2.41，逻辑 UI 为 640×480，音频为 16 kHz mono S16、20 ms。

见[架构](docs/development/architecture.zh-CN.md)、[测试](docs/development/testing.zh-CN.md)、[贡献指南](CONTRIBUTING.zh-CN.md)、[支持](SUPPORT.zh-CN.md)和[安全报告](SECURITY.zh-CN.md)。完整导航在[文档索引](docs/README.md)，版本变化见[更新记录](CHANGELOG.md)。

项目维护代码使用 [Apache-2.0](LICENSE)，文件另有声明的除外。依赖及字体保留各自条款；AOSL 附加条件和 RTSA 条款继续适用。见[第三方声明](THIRD_PARTY_NOTICES.md)、[来源记录](VENDORED_SOURCES.md)及[授权与商务联系](docs/dependencies/licensing.md)。
