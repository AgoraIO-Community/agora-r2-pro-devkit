# MyBot RV1126B 运行包

适用于 RV1126B TEST2 RK805B V10 的既有 AArch64 Buildroot 系统。版本、语言及语音/视频变体以包内 manifest 和 `./run.sh --version` 为准。

解包后可离线阅读[中文操作手册 PDF](../docs/device-operations.zh-CN.pdf)。源码仓库提供[构建指南](../docs/user/build.zh-CN.md)与[部署指南](../docs/user/deployment.zh-CN.md)。硬件验收范围以[验证状态](../docs/validation/README.md)为准。

## 校验与直接运行

将归档解包到 `/userdata` 下的独立目录。在包目录执行：

```sh
sha256sum -c SHA256SUMS
./run.sh --version
./run.sh --help
```

全部校验成功，并由操作者完整停止已有应用/原厂业务后再运行；同一设备不能同时运行两个实例。保留系统网络、授时及相机 3A 服务。

```sh
./run.sh
# 包含视频程序时显式选择：
RV1126B_VIDEO=1 ./run.sh
```

`example.env` 不会自动加载。服务地址优先级为 `--server` > `RV1126B_SERVER` > 构建语言默认值；中文默认 `https://botstation.shengwang.cn/api`，英文默认 `https://botstation.agora.io/api`。控制台地址为服务器地址去掉末尾 `/api`。升级时保持服务地址、身份及数据目录一致以复用绑定。

## 基本操作

- 离线且保存网络不可用时，连接屏幕显示的 `mybot-xxxx` 热点，手机打开 `http://192.168.4.1/`，提交 2.4 GHz Wi-Fi 并等待成功。
- 按屏幕配对码在对应服务区的控制台认领设备；绑定后短触屏幕开始/结束对话。
- 右上角设置可选择设备配网、关机；返回位于左上角。长按屏幕不触发配网。
- 手动运行时，终端 `s/q/p/u/d/e` 加 Enter 分别开始、挂断、重新配对、音量增/减、退出；Ctrl-C 也可退出。

## 系统依赖与持久化

镜像需提供匹配的 glibc 2.41/运行库、ALSA `botstation_capture`/`botstation_playback`、DRM/evdev、CA 与系统授时。音频为 16 kHz 单声道 S16、20 ms。AP 配网需 `ip`、`iw`、`hostapd`、`dnsmasq`、`wpa_supplicant`、`udhcpc` 及兼容 AP 驱动；可选视频需 V4L2/MPP。RTSA 运行库、提示音、helper 及依赖声明随包提供，具体条款见 `licenses/`，授权摘要见 `licenses/DEPENDENCY_LICENSING.md`。

默认私有目录为 `/userdata/mybot-rv1126b`：`device_auth` 保存绑定，`volume.settings` 保存音量，`wifi/wifi.credentials` 保存私有 Wi-Fi 记录。受监督系统协作模式的 Wi-Fi 主记录另在 `/etc/wpa_supplicant.conf`。保留这些配置和数据；仅改变 KV 目录不构成系统网络隔离。反馈问题时隐藏 token、密码、配对码和设备身份，不上传私有目录。

## 直接运行与受监督服务

直接执行 `run.sh` 不安装开机服务、不写 ALSA 配置文件。受监督部署由源码仓库的显式部署工具完成，会安装 `S99mybot`、禁用原厂业务启动并应用板级网络策略；每次启动通过 `amixer` 设置采集控制。服务配置位于 `/etc/mybot-rv1126b.conf`，版本位于 `/userdata/mybot/releases`，运行数据独立保留。受监督设备的升级、启停及回退按部署指南操作，不直接恢复原厂服务。

受监督的计划关机关闭应用显示输出；普通停止仍恢复原显示。清理后由 `poweroff.sh` 关闭 pwm-backlight（`brightness=0`、`bl_power=4`），读回确认后调用 `/sbin/poweroff`。准备中断或命令失败尝试恢复原值；系统已接收关机请求后不因关闭信号复亮。应用退出后 helper 失败只尝试恢复背光值，维护者需查私有日志并显式重启服务。软件关机不代表所有供电切断，实体长按的硬件链路尚未确认。

在设置中确认恢复出厂会清除本机绑定、活动 Wi-Fi 配置和保存的音量。服务重新启动后显示配网热点，默认播放音量为 70。设备 ID 和当前软件保留，云端归属及维护备份由管理员另行处理。
