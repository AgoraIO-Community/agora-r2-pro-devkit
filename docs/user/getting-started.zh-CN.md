# 上手与直接运行

完整逐步操作见[中文 PDF 手册](../device-operations.zh-CN.pdf)或[英文 PDF 手册](../device-operations.en.pdf)。
本页提供直接运行入口；安装自启动、升级及回退统一使用[部署指南](deployment.zh-CN.md)。
当前源码版本由 `python3 scripts/project-version.py` 读取，实际验收范围见[验证状态](../validation/README.md)。

## 先选择运行方式

- **受监督部署**：由主机部署工具预检、备份、安装启动服务并切换独立版本目录。适合设备日常运行，设置页关机需要此方式。按部署指南操作，不混用原厂服务启停命令。
- **直接运行**：在设备独立目录执行 `run.sh`，适合 UI、语音和故障定位。它不安装启动服务，运行前须由操作者停止占用相同硬件的原业务服务。

目标板型和系统、ALSA/DRM/输入、网络/授时/CA、AP 工具条件见[构建](build.zh-CN.md)、[配网](wifi-provisioning.zh-CN.md)和[时间](time-sync.zh-CN.md)指南。
保留已有身份、服务地址及 `/userdata/mybot-rv1126b`；不要通过删除凭据目录制造首次启动。

## 直接运行步骤

1. 在主机完成构建及打包，读取实际版本并上传。多个 ADB 设备时所有命令加 `-s <serial>`。

```sh
version=$(python3 scripts/project-version.py)
package="mybot-rv1126b-${version}-aarch64"
adb push "dist/${package}.tar.gz" /userdata/
adb shell
```

2. 在设备 shell 使用实际包名解压和校验。英文或单 profile 包以交付文件名为准；主机变量不会自动传入设备。

```sh
package='mybot-rv1126b-<version>-aarch64'
cd /userdata
tar -xzf "${package}.tar.gz"
cd "$package"
sha256sum -c SHA256SUMS
./run.sh --version
```

3. 确认当前没有受监督 MyBot 实例。原厂业务仍运行时，由操作者停止 `/etc/init.d/S99botstation-device`；保留 ISP、网络和系统授时服务。不要并行运行两个应用实例。
4. 先预览 UI，再运行语音业务：

```sh
./run.sh --ui-demo --run-seconds 50
./run.sh
```

检查文字、方向和动画；UI demo 不启动 SDK/音视频。进入业务后，按[配网指南](wifi-provisioning.zh-CN.md)联网，在对应服务控制台用配对码认领，再短触或 Menu 短按开始对话。设置、音量和终端操作分别见[设置](settings.zh-CN.md)及[配置](configuration.zh-CN.md)。视频只用于含视频程序的包，检查步骤和限制见手册及验证状态。

5. 输入 `e` 后 Enter 或 Ctrl-C 退出，确认完整清理再启动其他版本。手动回退到保留的兼容目录，并继续使用原身份、服务地址及 KV 目录；受监督设备使用部署工具回退。

收集包名、SHA256、版本、命令和问题附近日志，按[支持说明](../../SUPPORT.zh-CN.md)脱敏。用户实际设备业务通过、主机模拟和可选视频验收分别记录。
