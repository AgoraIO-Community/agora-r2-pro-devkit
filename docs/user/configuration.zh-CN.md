# 配置参考

`./run.sh --help` 为当前程序的选项入口。UI 语言、视频和音频时长属于构建配置；server、设备节点、旋转和运行时长属于运行配置。

## CLI 与环境

同一字段按 **CLI > 环境变量 > 默认值** 取值；只列出真实可用的环境项。

| CLI | 环境变量 | 默认值 / 说明 |
| --- | --- | --- |
| `--server HTTPS_URL` | `RV1126B_SERVER` | `https://botstation.shengwang.cn/api`，要求 HTTPS |
| `--device-id ID` | `RV1126B_DEVICE_ID` | 空，默认从 CPU Serial 派生；最多 32 字符 |
| `--identity-interface IF` | 无 | 以永久出厂 MAC 派生；显式 device-id 优先 |
| `--capture-device PCM` | `RV1126B_CAPTURE_DEVICE` | `botstation_capture` |
| `--playback-device PCM` | `RV1126B_PLAYBACK_DEVICE` | `botstation_playback` |
| `--interface IF` | `RV1126B_INTERFACE` | 空，按可用 IPv4/默认路由监测 |
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
| `MYBOT_LOCALE` | 提示音 locale，默认与 UI 构建语言相同；不会替换编译字库/UI文案 |
| `RV1126B_VIDEO` | 由 `run.sh` 读取，0 选语音程序，1 选视频程序 |
| `RV1126B_VIDEO_DEVICE` | 视频程序指定相机节点，默认自动寻找唯一 `rkisp_mainpath` |

启动脚本不自动 source `example.env`；按需先在 shell 中 export。默认中英文构建均保留同一 server 地址，切换 UI 语言不会自动切换服务。可使用 `--server` 显式覆盖。

## 身份与持久化

CPU Serial 经规范化和 HMAC-SHA256 派生为 `RV1126B-` 加 24 位大写十六进制字符。`--identity-interface` 只接受稳定的永久 MAC；不能证明永久地址时失败。显式 `--device-id` 优先于身份派生。

身份算法、KV 目录及服务配置是升级兼容边界。0.1.0、0.1.1 与 0.2.0 候选版保持相同策略；不要为了升级而删除目录、改身份来源或重新创建 ID。目录保存绑定凭据及 RTSA 日志，不与原厂服务共享。备份和反馈日志时避免公开凭据。

## 输入及音视频契约

短触有效时长为 50–999 ms；长按不触发配网。Menu 切换会话，左右键减/增音量。前台终端输入 `s/q/p/u/d/e` 后 Enter，分别开始、挂断、重新配对、音量增/减及退出。到达 ready 后需显式开始会话。

SDK 音频固定为 16 kHz、单声道 S16、20 ms，开启云端 AEC。当前 ALSA plug 负责与 48 kHz 双声道硬件转换。不能套用 ESP32 工程的 60 ms；改变时长必须同时取得匹配 RTSA 包并更新校验记录。

UI 逻辑画布 640×480，默认旋转 270°到物理 480×640；终端/签名/中文/配对码分别采用 18/20/40/64 px 编译字库。视频为可选编译能力：640×480 NV12 → MPP JPEG，最高 3 fps，默认编码旋转 180°。板级默认值集中在[板型目录](../../components/mybot_platform/boards/rv1126b-test2-rk805b-v10/)。
