# 配置参考

`./run.sh --help` 为当前程序的选项入口。UI 语言、视频和音频时长属于构建配置；server、设备节点、旋转和运行时长属于运行配置。

## CLI 与环境

同一字段按 **CLI > 环境变量 > 默认值** 取值；只列出真实可用的环境项。

| CLI | 环境变量 | 默认值 / 说明 |
| --- | --- | --- |
| `--server HTTPS_URL` | `RV1126B_SERVER` | 按 UI 构建语言选择：`zh-CN` 为 `https://botstation.shengwang.cn/api`，`en-US` 为 `https://botstation.agora.io/api`；要求 HTTPS |
| `--device-id ID` | `RV1126B_DEVICE_ID` | 空，默认从 CPU Serial 派生；最多 32 字符 |
| `--identity-interface IF` | 无 | 以永久出厂 MAC 派生；显式 device-id 优先 |
| `--capture-device PCM` | `RV1126B_CAPTURE_DEVICE` | `botstation_capture` |
| `--playback-device PCM` | `RV1126B_PLAYBACK_DEVICE` | `botstation_playback` |
| `--interface IF` | `RV1126B_INTERFACE` | 空；SDK 网络监测/路由过滤，不指定 Wi-Fi 配网接口 |
| `--wifi-interface IF` | `RV1126B_WIFI_INTERFACE` | 空，自动选择 nl80211 无线接口 |
| `--no-wifi-provisioning` | 无 | 仅监测既有网络，禁用自动/手动 AP 配网 |
| `--time-sync-timeout SECONDS` | `RV1126B_TIME_SYNC_TIMEOUT` | 30；联网且退出 AP 后等待时间合理性的秒数，范围 1–300 |
| `--time-sync-helper PATH` | `RV1126B_TIME_SYNC_HELPER` | 空；默认只等待 OS 校时，显式指定绝对可执行路径才允许启动前台 helper |
| `--input-device PATH` | `RV1126B_INPUT_DEVICE` | `auto`；可选 `none`、`stdin` 或 evdev 路径 |
| `--touch-device PATH` | `RV1126B_TOUCH_DEVICE` | `auto`；可选 `none` 或 evdev 路径 |
| `--talk-key CODE` | 无 | 139（KEY_MENU），音量/左右键不能用作会话键 |
| `--drm-device PATH` | 无 | `/dev/dri/card0` |
| `--rotation DEGREES` | 无 | 270，只接受 0/90/180/270 |
| `--run-seconds N` | 无 | 0，持续到退出；最大 86400 秒 |
| `--no-ui` | 无 | 关闭显示，保留语音/网络 |
| `--ui-demo` | 无 | 23 表情演示，不启动 SDK/音频/网络；不能与 `--no-ui` 同用 |
| `--help` / `--version` | 无 | 仅打印信息，不创建文件或访问硬件 |

| 仅环境变量 | 作用 |
| --- | --- |
| `MYBOT_KV_STORE_DIR` | 默认 `/userdata/mybot-rv1126b`，必须为当前用户拥有的私有绝对路径（0700） |
| `MYBOT_ASSETS_DIR` | 默认包内 `assets/`；绝对路径 |
| `MYBOT_LOCALE` | 提示音 locale，包括 AP 配网语音；默认与 UI 构建语言相同，包内提供 `zh-CN`、`en-US`；不会替换编译字库/UI文案或切换默认服务 |
| `RV1126B_VIDEO` | 由 `run.sh` 读取，0 选语音程序，1 选视频程序；双 profile 包直接运行默认 0，受监督部署默认写入 1 |
| `RV1126B_VIDEO_DEVICE` | 视频程序指定相机节点，默认自动寻找唯一 `rkisp_mainpath` |
| `RV1126B_NTP_SERVER` | 仅内置可选 BusyBox helper 读取，默认 `pool.ntp.org`；接受 DNS 主机名或 IPv4 地址，不自动启用 helper |

启动脚本不自动 source `example.env`；按需先在 shell 中 export。服务地址按 `--server` > `RV1126B_SERVER` > UI 构建语言默认值选择。运行时修改 `MYBOT_LOCALE` 只切换提示音语言，不改变默认服务。受监督部署中已显式配置的服务地址继续优先，切换构建语言不会覆盖该配置。

## 系统时间

网络就绪且 AP 配网结束后，应用才开始等待系统时间达到 2026-01-01 00:00:00 UTC。该门槛仅检查合理性，不证明准确授时；默认只等待 OS 现有校时，30 秒超时后在 UI/标准错误说明原因并非零退出，不跳过 TLS 验证。网络断开会取消当前等待与自有 helper，恢复后重新计算完整窗口。

可通过 `--time-sync-helper` 显式选择包内 `"$PWD/assets/time/sync-busybox.sh"`，不自动启用。只有时钟无效且 `/proc` 检查确认没有 `ntpd`、`chronyd`、`systemd-timesyncd` 或 `openntpd` 时才运行；既有服务优先，无法检查时明确失败。helper 无参数调用，退出时钟仍无效也会失败，取消只清理本应用启动的进程组。工具、权限、UDP 123 和设备测试要求见[系统时间指南](time-sync.zh-CN.md)。

## 身份与持久化

CPU Serial 经规范化和 HMAC-SHA256 派生为 `RV1126B-` 加 24 位大写十六进制字符。`--identity-interface` 只接受稳定的永久 MAC；不能证明永久地址时失败。显式 `--device-id` 优先于身份派生。

身份算法、KV 目录及服务配置是升级兼容边界。当前候选版保持 0.1.0、0.1.1 的身份策略；不要为了升级而删除目录、改身份来源或重新创建 ID。目录保存绑定凭据、输出音量、Wi-Fi 派生密钥及 RTSA 日志，不与原厂服务共享。Wi-Fi 最多保存 10 个网络。私有模式保存于 `wifi/wifi.credentials`（0600），私有上级目录为 0700；受监督部署使用下述系统协作模式的唯一主记录。开放网络不保存密码；加密网络保存派生 PSK，仍应按凭据保护。只有连接并获得可用 IPv4 和路由后才保存候选网络；失败不覆盖已有配置。备份和反馈日志时避免公开凭据。

## 输入及音视频契约

短触有效时长为 50–999 ms，Menu 短按切换会话，左右键减/增音量。长按触屏或 Menu 键不会启动会话或配网。手动配网仅通过右上角设置中的“设备配网”进入，关闭 UI 时没有手动配网入口。前台终端输入 `s/q/p/u/d/e` 后 Enter，分别开始、挂断、重新配对、音量增/减及退出。到达 ready 后需显式开始会话。

默认启动保留已有可用网络；没有网络时在总计 30 秒内尝试已保存网络，失败或没有配置时进入 AP。手动配网会先停止 SDK/会话再开启热点，完成后恢复 SDK。`--wifi-interface` 与 SDK 的 `--interface` 职责不同，双网卡环境可分别设置；后者若显式过滤到另一无网络接口，仍会阻止 SDK 就绪。完整操作及运行依赖见[配网指南](wifi-provisioning.zh-CN.md)。

AP 热点与 HTTP/DNS 门户真正就绪后，每次配网流程播放一次 `MYBOT_ASSETS_DIR/locales/<MYBOT_LOCALE>/wificonfig.pcm`，使用配置的播放设备和已保存输出音量，0 音量时不发声。同一流程内刷新页面或错误密码重试不重复播报；再次主动进入配网会重新播放。离开 AP 或退出应用会取消提示音并等待声卡释放，之后才可恢复 SDK 音频。资源、声卡或播放失败会报告错误，但不阻止手机继续配网。

SDK 音频固定为 16 kHz、单声道 S16、20 ms，开启云端 AEC。当前 ALSA plug 负责与 48 kHz 双声道硬件转换。改变时长必须同时取得匹配 RTSA 包并更新校验记录。

输出音量为 0–100：0 静音，首次使用默认 100（单位增益，保持此前输出响度）。左右键、终端 `u/d` 和云端音量指令通过同一平台适配器更新音量，写入 ALSA 前只缩放一次，不改变原始 PCM、采集增益、系统 mixer 或 ALSA 配置。云端 AEC 参考音频取自设备音量之前，实际响度和回声消除效果仍需真机验证。

音量保存在 `MYBOT_KV_STORE_DIR/volume.settings`，默认是 `/userdata/mybot-rv1126b/volume.settings`，文件权限为 0600，采用原子写入。应用重启、SDK 重新初始化及手动配网后恢复保存值；只有保存成功才应用新音量。保存失败会报告错误并保留原音量；内容损坏时告警并回退默认 100，后续成功调节可重新保存。属主、权限或符号链接检查失败会阻止音量服务初始化，不会覆盖不安全的文件。升级无需删除或手动编辑此文件。

UI 逻辑画布 640×480，默认旋转 270°到物理 480×640；终端/签名/中文/配对码分别采用 18/20/40/64 px 编译字库。视频为可选编译能力：640×480 NV12 → MPP JPEG，最高 3 fps，默认编码旋转 180°。板级默认值集中在[板型目录](../../src/platform/boards/rv1126b-test2-rk805b-v10/)。

## 设备服务与系统网络协作

受监督部署使用 `/etc/mybot-rv1126b.conf`（0600），资源固定到本次启动选择的物理版本目录。部署 settings 省略 `video` 时选择视频程序，显式 `"video": false` 选择语音；所选程序缺失时拒绝部署。`MYBOT_KV_STORE_DIR` 保持 `/userdata/mybot-rv1126b`，该目录必须是真实目录且权限为 0700。`MYBOT_STATUS_PATH` 指定私有运行目录内的匿名状态文件，只有版本、PID 和状态，不含身份或凭据。

设置 `MYBOT_SYSTEM_WPA_CONFIG=/etc/wpa_supplicant.conf` 启用系统协作模式。系统 Wi-Fi 文件为唯一持久主记录，应用读取并管理其网络列表，成功连接后原子提交并要求现有 supplicant 重配置；退出不删除已提交的网络。`MYBOT_DHCPCD_LEASE_DIR=/run/mybot-network/leases` 配合部署的 dhcpcd hook，以租约代次、BSSID 和实际接口地址确认新连接。保存后另有最多 30 秒等待目标网络的关联、租约与路由稳定，确认前手机保持“正在连接”；超时保留已提交的网络并允许重试。系统模式不另外写私有 `wifi.credentials`，也不启动竞争的 STA/DHCP 客户端。

没有设置系统模式时仍使用应用私有 Wi-Fi 存储。两种模式切换应明确导入记录，不将两份配置作为并行主记录。部署与恢复步骤见[部署指南](deployment.zh-CN.md)。

## 设置和产品输入

右上角设置提供设备配网和关机确认。产品输入由应用独立启动、跨 SDK 停启保留，SDK 只注册和注销语义事件回调；触屏轴范围在设备内读取，归一化到显示模式后按配置的旋转转换到 640×480 逻辑坐标。菜单消费其区域内手势，避免同时触发会话。手动配网请求只来自设置中的“设备配网”，等待网络和绑定时也可操作。

关机只接受设置页二次确认后的产品请求。服务启动器通过 `MYBOT_SUPERVISOR_RUNTIME` 暴露私有控制入口，并给监督器提供固定的 `/sbin/poweroff`；只有受监督的直接应用子进程可请求计划关机。应用正常清理退出后，监督器先关闭自身控制资源，再同步并执行系统关机，避免与系统停止服务互相等待。手动运行不会获得此权限。见[设置指南](settings.zh-CN.md)。
