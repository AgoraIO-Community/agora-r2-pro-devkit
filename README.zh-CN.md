# mybot-rv1126b

[English](README.md) | [简体中文](README.zh-CN.md)

面向 **RV1126B TEST2 RK805B V10** 的 MyBot 1.3.0 Linux 应用，提供 AP 配网与语音引导、HTTPS 配对、双向语音、稳定设备身份与音量持久化、像素眼镜 UI，以及可选的 V4L2/MPP JPEG 上行。

当前源码为 **<!-- project-version -->0.5.3<!-- /project-version --> 设备部署候选版**。0.1.0 的语音/UI业务流程和 0.1.1 的文字渲染优化已由用户真机验收；AP 配网、语音引导、音量持久化与时间处理仍需设备验证。可选视频和长时间稳定性专项尚未完成。[验证状态](docs/validation/README.md)是结果的唯一现行入口。

## 平台与行为

已记录的设备基线为 AArch64 Buildroot 2024.02、Linux 6.1、glibc 2.41、ALSA、DRM/KMS 和物理 480×640 DSI 屏幕。UI 使用静态 LVGL 9.5、原生 640×480 画布，默认旋转 270°。音频为 16 kHz 单声道 signed-16 PCM、20 ms，开启云端 AEC。

输出音量保存在私有运行目录，重启应用及手动配网后恢复。已有按键、终端命令和云端音量控制共用 0–100 的音量范围；100 保持此前的输出响度。应用音量控制只在播放端应用一次增益，不改变采集增益或系统 mixer，详见[配置](docs/user/configuration.zh-CN.md)。

应用保留已有可用网络与系统 ALSA 配置。离线时先在 30 秒预算内尝试已保存的 Wi-Fi，再开启开放热点 `mybot-xxxx`；手机连接热点后访问 `http://192.168.4.1/`，可选择 2.4 GHz 网络或手动输入隐藏 SSID。中英文页面支持已保存网络管理、连接进度与失败重试。短触或 Menu 短按开始/结束会话；手动配网仅通过右上角齿轮中的“设备配网”进入，SDK 启动前也可操作。长按触屏或 Menu 键不会启动会话或配网。应用先停止 MyBot，再进入配网，完成后恢复启动，详见[配网指南](docs/user/wifi-provisioning.zh-CN.md)。

热点与门户均就绪后，每次配网流程播放一次包内语音引导，语言由 `MYBOT_LOCALE` 选择，使用已保存输出音量，静音时不发声。离开配网或退出应用时取消播放并释放声卡，再恢复 MyBot；语音失败会报告错误，门户继续可用。

网络就绪且 AP 配网结束后，应用在启动 MyBot 前最多等待 30 秒，检查系统时间合理性。默认沿用 OS 现有校时服务；可选的前台 helper 必须显式启用。超时或 helper 失败会说明原因并以非零状态退出。2026-01-01 门槛只能排除明显无效日期，不能证明精确 NTP 同步，详见[系统时间指南](docs/user/time-sync.zh-CN.md)。

AP 配网要求目标系统提供 `iw`、`hostapd`、`dnsmasq`、`wpa_supplicant`、`udhcpc`、`ip`，以及支持 nl80211 AP 的驱动。上述工具由目标系统提供，运行包只内置 DHCP hook。优先采用 AP+STA；单接口回退要求应用拥有 STA 的控制权，应用不会全局停止已有网络管理服务。`--no-wifi-provisioning` 可仅监测已有网络。当前不包含固件安装、OTA、本地唤醒/NPU、视频下行或本地摄像头预览。运行数据默认保存在 `/userdata/mybot-rv1126b`；升级时保持服务地址与身份配置即可复用绑定。

直接执行 `run.sh` 不安装开机服务，也不修改系统 ALSA 配置文件。显式[受监督部署](docs/user/deployment.zh-CN.md)会安装 `/etc/init.d/S99mybot`、禁用原厂业务自启动，并更新 `dnsmasq`/`dhcpcd` 策略及租约 hook。每次服务启动通过 `amixer` 应用板级采集 mixer 设置，保留 ALSA 配置文件。操作者确认连接条件后，另行受控重载网络策略。

## 构建与测试

需要 **Python 3.10+**、**CMake 3.25+**、C/C++ 编译器、OpenSSL 开发文件和 libdrm 开发头文件。主机 UI/测试不加载目标 RTSA 库，也不需要目标 SDK。Ubuntu 24.04 安装开发包，并在忽略的虚拟环境中固定格式工具：

```sh
sudo apt install build-essential pkg-config libssl-dev libdrm-dev cmake python3 python3-venv
python3 -m venv local/dev-tools
local/dev-tools/bin/python -m pip install -r requirements-dev.txt
```

质量检查自动选择 `local/dev-tools/bin/clang-format` 并校验版本，无需激活虚拟环境。

```sh
python3 scripts/doctor.py --host
python3 scripts/check-dependencies.py
python3 scripts/check-quality.py
cmake --preset host-zh
cmake --build --preset host-zh
ctest --preset host-zh
```

英文使用 `host-en`，未定义行为检查使用 `host-ubsan`。macOS 未安装原生 libdrm 开发包时，需显式提供头文件目录，见[构建指南](docs/user/build.zh-CN.md)。主机 CI 面向 Linux；实际已运行的环境以验证矩阵为准。

完整的固定 Agora RTSA 1.10.1 包已内置于 `vendor/rtsa/`，目标构建自动使用其中的 AArch64 库。另需提供匹配的 AArch64 编译器、开发 staging/sysroot 及运行库清单，步骤见[独立构建指南](docs/user/build.zh-CN.md)。

```sh
sh scripts/build-rv1126b.sh
RV1126B_BUILD_DIR="$PWD/build/rv1126b-video-zh" sh scripts/build-rv1126b.sh -DMYBOT_ENABLE_VIDEO=ON
python3 scripts/package-rv1126b.py --profile all --language zh-CN
```

构建与打包命令只在主机执行，不部署设备。[上手指南](docs/user/getting-started.zh-CN.md)提供手动安装、服务占用、升级和回退步骤。

## 工程结构

| 目录 | 职责 |
| --- | --- |
| `src/application/` | 配置、身份、进程保护及应用生命周期 |
| `src/platform/` | 板级默认值、Linux 服务与硬件驱动 |
| `src/ui/` | 纯渲染、像素眼镜视图、表情和编译字库 |
| `cmake/` | 依赖集成与工具链配置 |
| `vendor/mybot/`、`vendor/aosl/`、`vendor/lvgl/`、`vendor/rtsa/` | 分别固定的依赖快照，包含完整 RTSA 包；构建集成位于 `cmake/dependencies/` |
| `tests/` | 单元、集成和自动化工具检查 |
| `tools/` | 开发工具程序，包括独立的 `ui_preview/` |
| `scripts/` | 构建、检查、代码生成和打包自动化 |
| `packaging/` | 交付模板、启动脚本和示例配置 |
| `docs/` | 用户/开发指南、验证状态与历史证据 |
| `githooks/` | Conventional Commits 提交信息校验 |

自维护模块集中于 `src/`，固定依赖集中于 `vendor/`。目录调整保留公共头文件命名空间（`mybot_platform/`、`mybot_ui/`）和 CMake target 名称；模块依赖边界见[架构指南](docs/development/architecture.zh-CN.md)。

## 文档与贡献

- 图文设备操作手册：[简体中文 PDF](docs/device-operations.zh-CN.pdf) · [English PDF](docs/device-operations.en.pdf)
- [文档索引](docs/README.md)、[配置](docs/user/configuration.zh-CN.md)、[AP 配网](docs/user/wifi-provisioning.zh-CN.md)、[系统时间](docs/user/time-sync.zh-CN.md)、[故障排查](docs/user/troubleshooting.zh-CN.md)
- [架构](docs/development/architecture.zh-CN.md)、[测试](docs/development/testing.zh-CN.md)、[发布流程](docs/development/releasing.zh-CN.md)
- [依赖目录规则](docs/development/third-party-layout.zh-CN.md)、[源码来源](VENDORED_SOURCES.md)
- [贡献指南](CONTRIBUTING.zh-CN.md)、[支持](SUPPORT.zh-CN.md)、[变更记录](CHANGELOG.md)

自维护代码默认采用 [Apache-2.0](LICENSE)，具体文件声明优先。MyBot/AOSL、LVGL 和字体分别保留原许可，像素眼镜视图遵循文件级许可声明；内置 RTSA 保留厂商条款，不受本项目许可证覆盖。再分发源码或交付包前，请阅读[第三方声明](THIRD_PARTY_NOTICES.md)和[依赖来源](VENDORED_SOURCES.md)。

版本化安装、系统 Wi-Fi 协作、服务启停与回滚见[设备部署指南](docs/user/deployment.zh-CN.md)。构建和打包脚本不隐式部署。

主界面右上角提供设置入口，可进入设备配网或确认关机；操作与验证见[设置指南](docs/user/settings.zh-CN.md)。
