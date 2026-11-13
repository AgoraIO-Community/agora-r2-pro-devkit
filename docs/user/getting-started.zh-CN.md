# 上手、升级与回退

当前候选版为 0.2.0，目标为 RV1126B TEST2 RK805B V10 的现有 AArch64 Buildroot 系统。已验收版本和候选版的检查范围见[验证状态](../validation/README.md)。源码构建见[构建指南](build.zh-CN.md)。

## 设备条件

设备需具备 glibc 2.41 基线兼容环境、可执行的 `/userdata`、既有网络/时间/CA 配置、ALSA `botstation_capture` / `botstation_playback`、DRM `/dev/dri/card0` 及 evdev 输入。应用复用已有 Linux 驱动和系统服务，不负责重建板级音频、ISP 或网络配置。

已有 `botstation-device` 和本应用使用相同硬件，不能同时运行。升级时退出旧版本应用（Ctrl-C 或输入 `e` 后 Enter）。保留原程序目录及 `/userdata/mybot-rv1126b`；服务地址、设备身份来源和 KV 路径保持一致时复用绑定。

## 手动部署

以下命令由设备操作者执行，会向 `/userdata` 写入候选包。主机构建和打包本身不执行这些步骤。若连接多台设备，给所有 `adb` 命令加 `-s <serial>`。

在主机仓库根目录执行：

```sh
adb push dist/mybot-rv1126b-0.2.0-aarch64.tar.gz /userdata/
adb shell
```

在设备 shell 执行：

```sh
cd /userdata
tar -xzf mybot-rv1126b-0.2.0-aarch64.tar.gz
cd mybot-rv1126b-0.2.0-aarch64
sha256sum -c SHA256SUMS
./run.sh --version
```

校验应全部为 `OK`，版本为 0.2.0，语言和视频标志与所选程序一致。英文包、单配置包的名称不同，以实际交付文件名为准。包内包括程序、RTSA、提示音、来源/许可、构建 manifest、ABI 报告和校验清单。无需覆盖 `/usr/bin`、`/usr/lib`、ALSA 配置或开机脚本。

如果原厂服务仍在运行，由操作者停止它：

```sh
/etc/init.d/S99botstation-device stop
```

等待停止命令返回，保留 `rkaiq_3A_server`、网络和系统时间服务运行。应用会检查原厂进程/锁及自身单实例锁，资源仍被占用时退出。

## UI 与默认语音

```sh
./run.sh --ui-demo --run-seconds 50
./run.sh
```

UI 演示循环 23 种表情，不启动 SDK、网络、音频或摄像头。检查原生 640×480 画布经默认 270° 旋转后在物理 480×640 屏上的方向、英文边缘、颜色及动画。方向需调整时使用 `--rotation 90` 等选项。

完整启动等待现有网络与系统时间就绪，未绑定时显示并播报配对码。在服务控制台认领后进入 ready；短触（50–999 ms）、Menu 键或输入 `s` 后 Enter 开始会话，再次短触/Menu 或 `q` 挂断。不会自动开聊。`u/d` 调音量、`p` 重新配对、`e` 退出。Ctrl-C/SIGTERM 结束程序。

如已有绑定，确认升级无需重新配对。重复开始/挂断，检查双向语音、状态与表情、退出后再次启动。需要隔离显示问题时用 `./run.sh --no-ui`。其余选项和身份规则见[配置](configuration.zh-CN.md)。

## 可选视频

仅包含视频程序的包可执行：

```sh
RV1126B_VIDEO=1 ./run.sh
```

视频在 RTC 连接后通过 V4L2/MPP 发送 640×480 JPEG，最高 3 fps，默认编码旋转 180°。有多个匹配相机时显式设置 `RV1126B_VIDEO_DEVICE=/dev/video12`。该能力尚无已记录的用户真机专项验收；需确认首帧、云端接收、方向/颜色、停止后停流、再次启动以及语音/UI并发。程序不自动保存相机快照。

## 收集结果与回退

记录包名、SHA256、`--version`、实际命令、语言/视频配置、UI/语音/输入结果和失败附近日志。公开前隐藏身份、token 和配对码，参见[支持](../../SUPPORT.zh-CN.md)。

回退时先退出候选程序，进入保留的 0.1.1 目录运行 `./run.sh`。不要并行运行两个版本，也不要删除共享 KV 目录。结束本应用后，可恢复原厂服务：

```sh
/etc/init.d/S99botstation-device start
```

应用不安装自启动服务。恢复原厂服务不会将其身份/凭据与本应用合并。
