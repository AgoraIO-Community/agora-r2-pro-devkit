# RV1126B 首版验证

> 历史记录：本文保留对应阶段的判断、路径和命令，不作为当前操作指南。现行入口见[文档索引](../README.md)，结果以[验证状态](../validation/README.md)为准。

本包提供 MyBot 1.3.0 的 RV1126B Linux 应用：双向语音、HTTPS 配对、独立持久化，以及工艺灰像素眼镜 UI；另附可选摄像头 JPEG 上行程序。0.1.0 默认语音/UI流程及 0.1.1 UI 显示效果已由用户真机验证通过，以下步骤供复验使用。

## 从 0.1.0 升级

先退出正在运行的 0.1.0 测试程序（Ctrl-C 或 `e`），将 0.1.1 解包到独立的新目录。保留 `/userdata/mybot-rv1126b`；服务地址和身份选项保持一致时会复用原绑定，无需重新配对。两版使用同一个运行锁，应依次运行。

0.1.1 只调整 UI 渲染、字体和布局，默认终端字体为原生 18 px，签名 20 px，中文 40 px，配对码 64 px。原有输入、会话和媒体功能继续按以下步骤验证。

## 上传与解包

包名：`mybot-rv1126b-0.1.1-aarch64.tar.gz`。在源码仓库的 macOS 终端执行：

```sh
adb push dist/mybot-rv1126b-0.1.1-aarch64.tar.gz /userdata/
adb shell
```

以下命令在设备 shell 中执行：

```sh
cd /userdata
tar -xzf mybot-rv1126b-0.1.1-aarch64.tar.gz
cd mybot-rv1126b-0.1.1-aarch64
sha256sum -c SHA256SUMS
./run.sh --version
```

包内包括 `bin/`、所选 RTSA `lib/`、中英文 PCM `assets/`、许可证及两个 ABI 报告。LVGL 9.5 已静态链接。无需覆盖 `/usr/bin`、`/usr/lib`、ALSA 配置或开机脚本。

## 先停止已有应用

设备现有 `botstation-device` 占用相同的声卡、摄像头和显示。开始测试前手动停止：

```sh
/etc/init.d/S99botstation-device stop
```

等待命令正常返回后再运行新程序；停止脚本最多等待约 60 秒。新程序也会检查旧进程和运行锁，发现占用时退出。保留 `rkaiq_3A_server`、网络和系统时间服务运行。

## 1. 检查 UI

```sh
./run.sh --ui-demo --run-seconds 50
```

此模式循环显示全部 23 种表情，每种约两秒，50 秒后自动退出。它不启动 SDK、配对、音频或摄像头。检查像素眼镜、黑色背景、四行终端、颜色、字体和方向。

默认物理屏幕为 480×640，原生 640×480 画面旋转 270°。若方向相反，尝试：

```sh
./run.sh --ui-demo --rotation 90 --run-seconds 50
```

## 2. 检查语音与配对

```sh
./run.sh
```

默认设备服务为 `https://botstation.shengwang.cn/api`；需要指定其他兼容服务时：

```sh
./run.sh --server https://YOUR_SERVER/api
```

等待网络及系统时间就绪后，屏幕显示真实配对码，并通过扬声器播报。到对应网页控制台认领设备；出现 `ready` 后短触屏幕开始会话，再次短触挂断。

- 短触有效时长为 50–999 ms，长按不触发配网。
- ADC `KEY_MENU`（139）也可切换会话；左右键减/增音量。物理键实际位置需要确认，可用 `--talk-key NUMBER` 调整。
- 前台交互 shell 支持 `s` 开始、`q` 挂断、`u/d` 音量、`p` 重新配对、`e` 退出，输入后按 Enter。硬件输入按设备名自动发现。
- Ctrl-C 或 SIGTERM 结束整个程序。可加 `--run-seconds 120` 进行定时验证。
- 屏幕显示 `listening` / `thinking` / `speaking` 与 `vp:pending` / `vp:ok`；表情按会话状态切换。

默认设备 ID 由 CPU Serial 的 HMAC-SHA256 派生为 `RV1126B-` 加 24 位十六进制字符，与旧服务的 ID 不同，首次需要重新配对。绑定 token 和 RTSA 日志位于独立的 `/userdata/mybot-rv1126b`，不读取旧服务的凭据。

需要以出厂 MAC 生成身份时，可在首次配对前使用 `--identity-interface wlan0`；该接口必须报告永久 MAC。保持后续启动使用相同身份来源。`--device-id ID` 供明确指定稳定身份时使用，最多 32 字符。

若屏幕已正常而音频有问题，可单独验证语音：

```sh
./run.sh --no-ui
```

默认使用设备已有的 `botstation_capture` / `botstation_playback` ALSA plug，将 48 kHz 双声道硬件格式转换成 SDK 的 16 kHz 单声道 S16；20 ms、云端 AEC 开启。首版沿用当前系统已设置的 codec/mixer，不负责重建音频系统配置。

## 3. 检查摄像头

语音和显示通过后退出程序，再选择视频版本：

```sh
RV1126B_VIDEO=1 ./run.sh
```

视频在 RTC 连接后启动，采用唯一 `rkisp_mainpath`、640×480 NV12、MPP 硬件 JPEG，最高 3 fps；默认保留参考实现的摄像头 180° 编码旋转。日志应出现首个 JPEG 被 SDK 接受的消息，再到云端确认图像方向、颜色与识别效果。

有多个匹配摄像头时明确指定：

```sh
RV1126B_VIDEO=1 RV1126B_VIDEO_DEVICE=/dev/video12 ./run.sh
```

视频程序不自动保存摄像头快照。退出会话应停流；之后重复开始、挂断，并观察语音与 UI 是否连续。

## 收集结果与恢复

需要日志时，可在设备 shell 中手动运行：

```sh
./run.sh 2>&1 | tee /userdata/mybot-rv1126b-test.log
```

反馈时优先提供：语音或视频版本、使用的旋转角度、配对/开聊是否完成、声音与图像表现，以及失败附近的日志；分享前隐藏设备身份及凭据。

测试结束，确认新程序已退出后恢复原服务：

```sh
/etc/init.d/S99botstation-device start
```

本包不会安装开机服务。常见问题：

| 现象 | 检查方向 |
| --- | --- |
| existing service / lock 错误 | 原服务是否已经停止，是否仍有另一个测试程序运行 |
| DRM 初始化失败 | 显示是否被其他进程占用；比较 `--no-ui`；确认运行用户有设备权限 |
| ALSA busy / open/configure 错误 | 原服务的声卡描述符是否已关闭；已有 ALSA plug 名称是否存在 |
| private runtime directory 错误 | `/userdata` 是否挂载；KV 路径应为绝对路径，目录归当前用户且权限为 0700 |
| 一直等待网络/时间 | 是否有可用 IPv4 和默认路由；系统时间是否正确 |
| TLS 或设备服务失败 | CA bundle、系统日期、DNS、服务地址与服务端响应 |
| ready 后未自动开聊 | 首版采用短触、Menu 键或 `s` 显式开始 |
| 视频为空但语音正常 | `rkaiq_3A_server`、相机占用、MPP 兼容性，以及云端是否接收 JPEG |
