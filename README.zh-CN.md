# mybot-rv1126b

[English](README.md) | [简体中文](README.zh-CN.md)

面向 **RV1126B TEST2 RK805B V10** 的 MyBot 1.3.0 Linux 应用，提供 HTTPS 配对、双向语音、稳定设备身份与持久化、像素眼镜 UI，以及可选的 V4L2/MPP JPEG 上行。

当前源码为 **0.2.0 工程治理候选版**。0.1.0 的语音/UI业务流程和 0.1.1 的文字渲染优化已由用户真机验收；目录重组后的候选版需要重新进行设备回归。可选视频和长时间稳定性专项尚未完成。[验证状态](docs/validation/README.md)是结果的唯一现行入口。

## 平台与行为

已记录的设备基线为 AArch64 Buildroot 2024.02、Linux 6.1、glibc 2.41、ALSA、DRM/KMS 和物理 480×640 DSI 屏幕。UI 使用静态 LVGL 9.5、原生 640×480 画布，默认旋转 270°。音频为 16 kHz 单声道 signed-16 PCM、20 ms，开启云端 AEC。

应用复用系统已有网络与 ALSA 配置。当前不包含固件安装、配网、自启动、OTA、本地唤醒/NPU、视频下行或本地摄像头预览。短触或 Menu 键开始/结束会话。运行数据默认保存在 `/userdata/mybot-rv1126b`；升级时保持服务地址与身份配置即可复用绑定。

## 构建与测试

需要 **Python 3.10+**、**CMake 3.25+**、C/C++ 编译器、OpenSSL 开发文件和 libdrm 开发头文件。主机 UI/测试不需要 RTSA 或目标 SDK。Debian/Ubuntu 的开发包包括 `build-essential`、`pkg-config`、`libssl-dev` 和 `libdrm-dev`。

```sh
python3 scripts/doctor.py --host
python3 scripts/check-dependencies.py
cmake --preset host-zh
cmake --build --preset host-zh
ctest --preset host-zh
```

英文使用 `host-en`，未定义行为检查使用 `host-ubsan`。macOS 未安装原生 libdrm 开发包时，需显式提供头文件目录，见[构建指南](docs/user/build.zh-CN.md)。主机 CI 面向 Linux；实际已运行的环境以验证矩阵为准。

目标构建需提供匹配的 AArch64 编译器、开发 staging/sysroot、固定 Agora RTSA 包及运行库清单。所有依赖路径均显式配置，步骤见[独立构建指南](docs/user/build.zh-CN.md)。

```sh
sh scripts/build-rv1126b.sh
RV1126B_BUILD_DIR="$PWD/build/rv1126b-video-zh" sh scripts/build-rv1126b.sh -DMYBOT_ENABLE_VIDEO=ON
python3 scripts/package-rv1126b.py --profile all --language zh-CN
```

构建与打包命令只在主机执行，不部署设备。[上手指南](docs/user/getting-started.zh-CN.md)提供手动安装、服务占用、升级和回退步骤。

## 工程结构

| 目录 | 职责 |
| --- | --- |
| `app/` | 配置、身份、进程保护及应用生命周期 |
| `components/mybot_platform/` | 板级默认值、Linux 服务与硬件驱动 |
| `components/mybot_ui/` | 纯渲染、像素眼镜视图、表情和编译字库 |
| `cmake/` | 依赖集成与工具链配置 |
| `third_party/` | 固定的上游源码快照 |
| `tests/`、`tools/ui_preview/` | 主机检查与独立 UI 预览 |
| `scripts/`、`packaging/` | 构建检查工具及交付模板 |
| `docs/` | 用户/开发指南、验证状态与历史证据 |

## 文档与贡献

- [文档索引](docs/README.md)、[配置](docs/user/configuration.zh-CN.md)、[故障排查](docs/user/troubleshooting.zh-CN.md)
- [架构](docs/development/architecture.zh-CN.md)、[测试](docs/development/testing.zh-CN.md)、[发布流程](docs/development/releasing.zh-CN.md)
- [贡献指南](CONTRIBUTING.zh-CN.md)、[支持](SUPPORT.zh-CN.md)、[变更记录](CHANGELOG.md)

自维护代码默认采用 [Apache-2.0](LICENSE)，具体文件声明优先。MyBot/AOSL、LVGL 和字体分别保留原许可，像素眼镜视图遵循文件级许可声明；外部获取的 RTSA 不受本项目许可证覆盖。再分发源码或交付包前，请阅读[第三方声明](THIRD_PARTY_NOTICES.md)和[依赖来源](VENDORED_SOURCES.md)。
