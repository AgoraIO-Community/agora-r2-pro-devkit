# MyBot RV1126B 运行包

本包用于 RV1126B TEST2 RK805B V10 的现有 AArch64 Buildroot 系统。包版本、语言与变体以 `./run.sh --version` 和包内构建 manifest 为准。此前 0.1.0 语音/UI与 0.1.1 显示优化已由用户验收；当前版本的设备回归、可选视频和长期专项范围以源码仓库的验证状态为准。

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

UI 为 640×480，默认旋转 270°到物理 480×640 屏；调整方向可加 `--rotation 90`。语音启动保留已有可用网络；离线时先尝试已保存网络，失败则进入 AP 配网。手机连接开放热点 `mybot-xxxx`，打开 `http://192.168.4.1/`，选择 2.4 GHz Wi-Fi 或手动输入隐藏 SSID，输入密码并等待连接成功。失败可重试，成功后关闭热点并恢复业务。未绑定时显示并播报配对码。认领后短触（50–999 ms）或 Menu 键开始/挂断，左右键调音量。持续按住触屏或 Menu 键 3 秒请求 Wi-Fi 配网，会先结束会话。终端 `s/q/p/u/d/w/e` 加 Enter 分别为开始、挂断、重新配对、音量增/减、配网和退出；Ctrl-C 也可退出。

默认服务为 `https://botstation.shengwang.cn/api`，`--server HTTPS_URL` 可覆盖。服务地址、身份配置及 KV 路径保持一致时沿用原绑定。默认身份由 CPU Serial 稳定派生；需要显式身份可用 `--device-id` 或首次绑定前设置 `--identity-interface`，不要在升级时随意变更。

## 系统时间

网络就绪且退出 AP 后，启动 MyBot 前默认等待 OS 校时最多 30 秒。`--time-sync-timeout 60` 可调整等待（1–300 秒）；时间仍早于 2026-01-01 UTC 时，UI 和标准错误报告原因并非零退出，不跳过 TLS 校验。此门槛只表示日期合理，不能证明精确同步。网络丢失取消等待，恢复后重新计时；保留系统现有校时服务。

可选的 `assets/time/sync-busybox.sh` 已附带但不自动运行。仅在系统没有运行现成时间服务、设备具备 `/bin/busybox ntpd`、校时与 socket 权限（通常为 root）、DNS 和 UDP 123 通路时显式选择：

```sh
./run.sh --time-sync-helper "$PWD/assets/time/sync-busybox.sh"
```

它在前台执行一次性校时，默认服务器为 `pool.ntp.org`，可通过 `RV1126B_NTP_SERVER` 指定 DNS 主机名或 IPv4 地址。已有 `ntpd`/`chronyd`/`systemd-timesyncd`/`openntpd` 优先；应用不停止它们，只回收自有 helper 进程组。helper 退出后时钟仍无效也会失败。请验证正常/无效时间、超时、缺少 helper、现成服务优先及取消后无进程残留；不要在共享生产系统随意回拨时钟。

## AP 配网前提

目标系统需提供 `iw`、`hostapd`、`dnsmasq`、`wpa_supplicant`、`udhcpc`、`ip` 和支持 nl80211 AP 的无线驱动。包内只包含 `assets/wifi/udhcpc.script`，不包含上述系统工具。需有创建接口、配置 IP、启动 DHCP 与绑定 80/53 端口的权限。优先 AP+STA；单接口回退不能抢占外部网络管理服务，无法安全接管时报告错误。热点的 `xxxx` 取自 STA MAC 的前两个字节，小写十六进制。

`./run.sh --wifi-provision` 强制配网，`--wifi-interface wlan0` 选择无线接口，`--no-wifi-provisioning` 仅监测已有网络。SSID/密码不写入 URL/日志；加密网络最多保存 10 项派生 PSK 于私有的 `/userdata/mybot-rv1126b/wifi/wifi.credentials`，不要公开该目录。手机未弹出页面时手动访问 HTTP 地址；单接口连接尝试期间热点可能短暂断开，失败后重新连接热点重试。

当前 AP 功能尚未真机验收。请检查首次配网、错误密码重试、重启复用、会话中切换配网、已有网络不变及退出清理。

热点与门户就绪后，每次配网流程播放一次语音引导，使用包内 `assets/locales/<MYBOT_LOCALE>/wificonfig.pcm`，默认语言与 UI 构建相同，也可选 `zh-CN` 或 `en-US`。语音使用已保存音量，静音时不发声；错误密码重试不重复播报。离开配网或退出时取消并释放声卡，恢复业务前不会与 SDK 音频重叠；提示音失败会报告错误，手机仍可继续配网。请检查完整播报、两种语言、静音、重试，以及播放期间退出后再次启动的音频。

## 音视频及内容

音频使用既有 `botstation_capture` / `botstation_playback`，SDK 为 16 kHz mono S16、20 ms、Cloud AEC。`--no-ui` 可隔离语音。

输出音量范围为 0–100，首次启动默认 100，保持此前的输出响度。按键、终端和云端控制共用已保存音量；重启应用及手动配网后恢复。音量设置原子保存于私有的 `/userdata/mybot-rv1126b/volume.settings`（0600），保存失败时报告错误并保留原音量，不修改系统 mixer 或采集增益。请检查静音、递增/递减、退出重启、配网恢复和云端控制后的实际响度与回声消除效果。

包内包含视频程序时可选择：

```sh
RV1126B_VIDEO=1 ./run.sh
```

视频在 RTC 连接后以 V4L2/MPP 发送 640×480 JPEG，最高 3 fps，默认编码旋转 180°；需要时设置 `RV1126B_VIDEO_DEVICE=/dev/video12`。不自动保存相机文件。

包内 `build-manifest-*.json`、`abi-report-*.json` 和 `SHA256SUMS` 记录所选变体和身份。字体静态编入，无运行时字体依赖。`example.env` 仅提供示例，不会自动加载。`licenses/` 和字体来源保留各依赖条件；RTSA 使用及再分发仍受其外部条款约束。

## 退出与回退

直接执行 `run.sh` 不修改系统库、ALSA 配置文件或开机服务。手动运行时，结束候选程序后可从保留的旧目录运行旧版本；共享 KV 目录保留且两版不可同时运行。仅在原厂服务未被部署工具禁用、且本应用已经完整停止时恢复原厂服务：

```sh
/etc/init.d/S99botstation-device start
```

反馈时提供包名、校验和、version、实际命令和问题附近日志；隐藏 token、配对码、设备 ID/Serial/MAC。不要上传 KV 目录。源码仓库的 `docs/validation/README.md` 是当前验证状态的唯一现行记录。

受监督设备部署使用包内 `assets/service/` 和 `bin/mybot-supervisor`，由源码仓库的显式部署工具安装。它会安装 `/etc/init.d/S99mybot`、禁用原厂业务自启动，并更新 `/etc/dnsmasq.conf`、`/etc/dhcpcd.conf` 和系统 dhcpcd 租约 hook；网络策略由操作者确认连接条件后另行重载。每次服务启动，`audio-prepare.sh` 通过 `amixer cset` 应用板级采集 mixer 设置，不写 ALSA 配置文件。通过部署工具安装后，按源码仓库的设备部署指南启停与回退，不直接恢复原厂服务。

`assets/network/99-mybot-lease` 是系统 dhcpcd 的租约事件 hook，与应用自有 DHCP hook 用途不同。系统模式以系统 Wi-Fi 配置为主记录，已有绑定数据目录保持不变。
