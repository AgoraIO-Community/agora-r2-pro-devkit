# MyBot RV1126B 运行包

本包用于 RV1126B TEST2 RK805B V10 的现有 AArch64 Buildroot 系统。0.2.2 为源码目录重构候选，需重新进行真机回归；此前 0.1.0 语音/UI与 0.1.1 显示优化已由用户验收。可选视频和长期专项尚未完成。

## 校验与运行

先退出旧版本程序，保留 `/userdata/mybot-rv1126b` 以复用绑定。将归档解包到 `/userdata` 的独立版本目录，在包目录中执行：

```sh
sha256sum -c SHA256SUMS
./run.sh --version
./run.sh --help
```

校验应全部 OK。确认原厂 `botstation-device` 未运行；若其仍占用硬件，由操作者执行：

```sh
/etc/init.d/S99botstation-device stop
```

等待服务完整停止，保留网络、系统时间和 `rkaiq_3A_server`。先看 UI，再检查完整语音：

```sh
./run.sh --ui-demo --run-seconds 50
./run.sh
```

UI 为 640×480，默认旋转 270°到物理 480×640 屏；调整方向可加 `--rotation 90`。语音启动等待已有网络，未绑定时显示并播报配对码。认领后短触（50–999 ms）或 Menu 键开始/挂断，左右键调音量。终端 `s/q/p/u/d/e` 加 Enter 分别为开始、挂断、重新配对、音量增/减和退出；Ctrl-C 也可退出。

默认服务为 `https://botstation.shengwang.cn/api`，`--server HTTPS_URL` 可覆盖。服务地址、身份配置及 KV 路径保持一致时沿用原绑定。默认身份由 CPU Serial 稳定派生；需要显式身份可用 `--device-id` 或首次绑定前设置 `--identity-interface`，不要在升级时随意变更。

## 音视频及内容

音频使用既有 `botstation_capture` / `botstation_playback`，SDK 为 16 kHz mono S16、20 ms、Cloud AEC。`--no-ui` 可隔离语音。包内包含视频程序时可选择：

```sh
RV1126B_VIDEO=1 ./run.sh
```

视频在 RTC 连接后以 V4L2/MPP 发送 640×480 JPEG，最高 3 fps，默认编码旋转 180°；需要时设置 `RV1126B_VIDEO_DEVICE=/dev/video12`。不自动保存相机文件。

包内 `build-manifest-*.json`、`abi-report-*.json` 和 `SHA256SUMS` 记录所选变体和身份。字体静态编入，无运行时字体依赖。`example.env` 仅提供示例，不会自动加载。`licenses/` 和字体来源保留各依赖条件；RTSA 使用及再分发仍受其外部条款约束。

## 退出与回退

程序不修改系统库、ALSA 配置或开机服务。结束候选程序后可从保留的旧目录运行旧版本；共享 KV 目录保留且两版不可同时运行。恢复原厂服务：

```sh
/etc/init.d/S99botstation-device start
```

反馈时提供包名、校验和、version、实际命令和问题附近日志；隐藏 token、配对码、设备 ID/Serial/MAC。不要上传 KV 目录。源码仓库的 `docs/validation/README.md` 是当前验证状态的唯一现行记录。
