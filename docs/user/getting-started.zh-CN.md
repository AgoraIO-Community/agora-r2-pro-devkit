# 上手、升级与回退

目标为 RV1126B TEST2 RK805B V10 的现有 AArch64 Buildroot 系统。当前源码版本以根 `CMakeLists.txt` 为准，执行 `python3 scripts/project-version.py` 查看；已验收版本和候选版的检查范围见[验证状态](../validation/README.md)。源码构建见[构建指南](build.zh-CN.md)。

## 设备条件

设备需具备 glibc 2.41 基线兼容环境、可执行的 `/userdata`、既有网络/时间/CA 配置、ALSA `botstation_capture` / `botstation_playback`、DRM `/dev/dri/card0` 及 evdev 输入。应用复用已有 Linux 驱动和系统服务，不负责重建板级音频或 ISP。需要 AP 配网时，另需具备[配网指南](wifi-provisioning.zh-CN.md)列出的无线驱动、系统工具和网络控制条件。

已有 `botstation-device` 和本应用使用相同硬件，不能同时运行。升级时退出旧版本应用（Ctrl-C 或输入 `e` 后 Enter）。保留原程序目录及 `/userdata/mybot-rv1126b`；服务地址、设备身份来源和 KV 路径保持一致时复用绑定。

## 手动部署

以下命令由设备操作者执行，会向 `/userdata` 写入候选包，直接运行 `run.sh` 不安装开机服务。需要版本链接、受监督服务与自启动时，使用[设备部署工具](deployment.zh-CN.md)，不要混用两套启停步骤。主机构建和打包本身不执行这些步骤。若连接多台设备，给所有 `adb` 命令加 `-s <serial>`。

在主机仓库根目录执行：

```sh
version=$(python3 scripts/project-version.py)
package="mybot-rv1126b-${version}-aarch64"
adb push "dist/${package}.tar.gz" /userdata/
adb shell
```

在设备 shell 执行，将 `<version>` 替换为刚才上传包的实际版本；主机变量不会自动带入设备 shell：

```sh
package='mybot-rv1126b-<version>-aarch64'
cd /userdata
tar -xzf "${package}.tar.gz"
cd "$package"
sha256sum -c SHA256SUMS
./run.sh --version
```

校验应全部为 `OK`，版本与所上传包的 manifest 一致，语言和视频标志与所选程序一致。英文包、单配置包的名称不同，以实际交付文件名为准。包内包括程序、RTSA、提示音、可选授时 helper、来源/许可、构建 manifest、ABI 报告和校验清单。手动解包运行无需覆盖 `/usr/bin`、`/usr/lib`、ALSA 配置文件或开机脚本。

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

完整启动保留已有可用网络；离线时先尝试已保存网络，失败则显示 `mybot-xxxx` 热点与 `192.168.4.1` 配网入口。手机连接热点后在浏览器选择网络、输入密码并等待连接完成，详见[AP 配网](wifi-provisioning.zh-CN.md)。联网与系统时间就绪后，未绑定时显示并播报配对码。在服务控制台认领后进入 ready；短触（50–999 ms）、Menu 短按或输入 `s` 后 Enter 开始会话，再次短触/Menu 短按或 `q` 挂断。不会自动开聊。`u/d` 调音量、`p` 重新配对、`e` 退出。手动配网仅通过右上角齿轮中的“设备配网”进入，当前会话先结束；等待网络或绑定时也可操作设置。长按触屏或 Menu 键不会启动会话或配网。Ctrl-C/SIGTERM 结束程序。

网络就绪并退出 AP 后才开始等待系统时间，默认最多 30 秒。时间早于 2026-01-01 UTC 会阻止 SDK 启动，超时在 UI/标准错误说明原因并非零退出；该检查不证明精确授时。默认使用系统已有校时，只有显式选择 `--time-sync-helper "$PWD/assets/time/sync-busybox.sh"` 才允许可选的一次性校时，既有系统服务仍优先。工具、权限、网络前提及隔离测试步骤见[系统时间指南](time-sync.zh-CN.md)。

本候选版还需按[配网设备验收步骤](wifi-provisioning.zh-CN.md#设备验证)检查首次配网、错误密码重试、保存后重启以及会话中切换配网。已有网络的正常启动应保持原连接，不能因启动应用而无故开启热点。

热点与门户就绪后会播放一次配网语音，使用已保存音量；静音时不发声。请在不操作页面的情况下确认播报完整，再检查错误密码重试不重复播报、成功联网后恢复 SDK，以及播报期间退出后声卡可重新使用。提示音语言由 `MYBOT_LOCALE=zh-CN` 或 `MYBOT_LOCALE=en-US` 选择；切换手机页面语言不会改变设备语音。提示音失败不应阻止手机继续配网。

如已有绑定，确认升级无需重新配对。重复开始/挂断，检查双向语音、状态与表情、退出后再次启动。需要隔离显示问题时用 `./run.sh --no-ui`。其余选项和身份规则见[配置](configuration.zh-CN.md)。

输出音量首次使用默认为 100（此前的输出响度），调整后会保存在私有 KV 目录。请通过左右键、终端 `u/d` 和云端指令检查音量变化与静音，再退出重启及进入/退出 AP 配网，确认恢复保存音量。当前使用平台播放增益，不更改系统 mixer 或采集增益；实际响度和云端 AEC 效果仍需设备验证。

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

以上手动运行步骤不安装自启动服务。通过[部署工具](deployment.zh-CN.md)安装的受监督服务会设置 `S99mybot`、禁用原厂业务自启动，并应用板级网络策略；请按部署指南启停或回退，不能直接沿用上述原厂服务恢复命令。每次受监督服务启动还会通过 `amixer` 设置采集 mixer，不改 ALSA 配置文件。恢复原厂服务不会将其身份/凭据与本应用合并。
