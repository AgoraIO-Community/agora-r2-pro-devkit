# 构建指南

所有命令在仓库根目录执行。构建、检查和打包只操作主机，不通过 ADB 连接设备。当前实际环境与检查结果见[验证矩阵](../validation/README.md)；未实测的宿主系统组合不视为已支持。

## 主机开发

需要 Python **3.10+**、CMake **3.25+**、C/C++ 编译器、构建工具、OpenSSL 开发文件、libdrm 开发头文件和 `pkg-config`。普通构建使用已提交的字库，不要求 Pillow/FreeType。确保 `python3 --version` 满足要求；系统默认版本较旧时，使用合适的解释器执行 Python 命令，并给脚本入口设置 `PYTHON=/path/to/python3.10`。

Debian/Ubuntu 可安装 `build-essential pkg-config libssl-dev libdrm-dev cmake python3`，同时检查发行版提供的实际版本。主机测试不需要 RTSA、AArch64 工具链或设备。

```sh
python3 scripts/doctor.py --host
python3 scripts/check-dependencies.py
cmake --preset host-zh
cmake --build --preset host-zh
ctest --preset host-zh
```

`host-en` 与 `host-ubsan` 使用独立目录；configure/build/test 的 preset 名保持一致。默认输出到 `build/<preset>/`。

macOS 中 OpenSSL 可通过 `CMAKE_PREFIX_PATH` 指向已安装的开发包；libdrm 头文件未进入默认搜索路径时显式配置。例如：

```sh
python3 scripts/doctor.py --host --drm-include /path/to/development/staging/usr/include/libdrm
cmake --preset host-zh -DRV1126B_TEST_DRM_INCLUDE=/path/to/development/staging/usr/include
cmake --build --preset host-zh
ctest --preset host-zh
```

doctor 的目录直接包含 `drm.h`；CMake 的目录直接包含 `xf86drm.h` / `xf86drmMode.h`，并具备匹配的 `libdrm/` UAPI 头。仅复用可在宿主编译的开发头，不链接目标平台的 libdrm 库。完整主机门禁需要 DRM 测试依赖，缺失时应失败。`sh scripts/test-host.sh` 为兼容入口，不自动读取目标 SDK 环境；需要时通过 `RV1126B_HOST_BUILD_DIR` 及显式 CMake 参数指定目录和头文件。

## 独立目标依赖

目标构建需要：

- 产生 AArch64 Linux GNU ELF 的 C/C++、ar/ranlib 工具链。
- 与设备兼容的开发 staging（ALSA、OpenSSL、libdrm，视频另需 MPP 头文件/库），必要时提供完整 libc sysroot。
- [固定 RTSA 包](../dependencies/RTSA_SOURCE.txt)的公共头文件与 AArch64 动态库；20 ms 配置及库哈希必须匹配。
- 用于 ABI 闭包检查的目标运行库快照及 manifest。

运行库快照仅用于验证目标依赖，不能代替完整开发 sysroot。设备不含编译环境，不应从设备运行库推断具有完整 SDK。依赖获取和许可见[依赖指南](../development/dependencies.zh-CN.md)。

显式配置本地路径，实际选项以 `--help` 为准：

```sh
python3 scripts/configure-local-sdk.py --help
python3 scripts/configure-local-sdk.py \
  --cross-compile /path/to/toolchain/bin/aarch64-linux-gnu- \
  --staging /path/to/development/staging \
  --rtsa-sdk /path/to/agora_sdk \
  --runtime-manifest /path/to/device-runtime/manifest.json
```

完整 libc sysroot 使用 `--sysroot PATH`；也可用 `--zig PATH` 代替 `--cross-compile`。配置生成忽略的 `local/sdk/config.json` 和 `local/sdk/environment.sh`；不要提交个人路径。执行 `python3 scripts/doctor.py --target` 检查配置，视频另加 `--video`。

## 目标构建与打包

配置完成后可使用兼容入口：

```sh
sh scripts/build-rv1126b.sh
RV1126B_BUILD_DIR="$PWD/build/rv1126b-video-zh" sh scripts/build-rv1126b.sh -DMYBOT_ENABLE_VIDEO=ON
python3 scripts/package-rv1126b.py --profile all --language zh-CN
```

默认语音目录为 `build/rv1126b-voice-zh`，视频目录为 `build/rv1126b-video-zh`。目标 preset 包括 `rv1126b-voice-zh`、`rv1126b-video-zh`、`rv1126b-voice-en`、`rv1126b-video-en`。直接使用时先在当前 shell 加载配置生成的环境，再 configure/build 同名 preset；CMake 默认构建目标会自动完成 ABI 与制品 manifest 检查。例如：

```sh
. local/sdk/environment.sh
cmake --preset rv1126b-voice-zh
cmake --build --preset rv1126b-voice-zh
```

默认目标按顺序生成编译输入快照、编译程序、检查 ABI 并生成制品 manifest；检查失败时整个构建失败。

英文使用独立目录并设置 `-DRV1126B_LANGUAGE=en-US`，打包设置 `--language en-US`。`--profile voice` 或 `--profile video` 只选择一个变体；`all` 必须两个均存在且配置一致，不能静默省略缺少的程序。可用 `--voice-build` / `--video-build` 指向其他构建目录。

中文全配置候选包为 `dist/mybot-rv1126b-0.2.0-aarch64.tar.gz`；英文名附加 `-en-US`，单变体附加 `-voice` 或 `-video`。已有同名输出会被拒绝，先保留或移动旧候选结果。包内校验和、ABI 和 manifest 校验通过后，按[上手指南](getting-started.zh-CN.md)手动上板。

## UI 预览

```sh
mkdir -p dist/preview
build/host-zh/mybot-ui-preview dist/preview
```

预览程序使用真实 LVGL renderer 生成流程页和表情，不启动目标平台。它验证布局和渲染结果，不能证明物理屏幕、音质或视频表现。
