# 设备替换部署与回退

`scripts/deploy-rv1126b.py` 是显式 ADB 部署入口，面向已授权修改的 RV1126B TEST2 RK805B V10。构建与打包不会调用它。`deploy`、`rollback` 默认只生成计划，不访问设备；只有 `--execute` 才写设备。`preflight` 和 `status` 只读访问选定设备，不重启或启动业务。

直接解包执行 `run.sh` 不安装系统服务，也不修改 ALSA 配置文件；本指南的受监督部署会安装 `S99mybot`、禁用原厂业务自启动，并写入下述板级网络策略。每次服务启动另通过 `amixer` 应用采集 mixer 设置，保留 ALSA 配置文件。安装策略与重载网络服务是两个独立步骤，后者由操作者确认连接条件后执行。

## 布局与保护边界

| 路径 | 用途 |
| --- | --- |
| `/userdata/mybot/releases/<包目录名>/` | 已验证的独立版本目录；同名不同归档不覆盖 |
| `/userdata/mybot/current`、`previous` | 当前与回退版本的相对链接，使用同一文件系统内的原子替换 |
| `/userdata/mybot/backups/<tag>/original.tar.gz` | 设备内私有备份，目录 0700、归档 0600 |
| `/userdata/mybot-rv1126b/` | 保留的应用身份、绑定及音量等私有数据，不随程序切换覆盖 |
| `/etc/init.d/S99mybot`、`/etc/mybot-rv1126b/` | 稳定服务入口、监督程序和板级音频准备 |
| `/etc/mybot-rv1126b.conf` | root 私有的实际服务配置，0600 |
| `local/deployment/<tag>/receipt.json` | 主机上的路径/摘要回执，目录 0700、文件 0600，受 Git 忽略 |

备份只留在设备。工具不会把绑定 token、Wi-Fi 密钥、`device_auth` 或整个私有数据包导出到主机；回执只记录公开包摘要和设备备份路径/摘要。不要公开设备备份或私有服务配置，也不要用旧预装应用的认证文件替换当前应用数据。

系统 Wi-Fi 模式使用 `/etc/wpa_supplicant.conf` 作为主记录，由应用网络后端在设备内读取、原子保存并重载；列表从该记录生成，不另建一份私有 Wi-Fi 主记录。部署仅设置该模式，不导出或重写已有 Wi-Fi 凭据。

## 制品与私有设置

目标包必须来自本项目打包入口，提供外部 SHA256。工具先验证归档摘要、安全条目、包内所有文件的 `SHA256SUMS`、AArch64 ELF、ABI 报告及构建 manifest/二进制来源标记。监督程序使用独立的 `role=supervisor` manifest/ABI 摘要，并与所选程序配置和来源标记一致，兼容纯视频包。部署包还必须包含：

```text
bin/mybot-supervisor
supervisor-manifest.json
abi-report-supervisor.json
assets/service/S99mybot
assets/service/run-service.sh
assets/service/mybot-rv1126b.conf
assets/service/audio-prepare.sh
assets/service/poweroff.sh
assets/network/99-mybot-lease
```

首次替换时建议同时导入已验收的 0.1.1 完整包作为回退版本。仅这个显式旧格式允许没有构建 manifest，仍检查外部/内部摘要、ELF 与 ABI；其文件保持原样。监督程序安装到稳定的 `/etc/mybot-rv1126b/mybot-supervisor`，因此旧版本不需要补入或改写监督程序。

归档校验还要求目录具有所属用户的读取/遍历权限，`run.sh`、程序和必要 helper 具有读取/执行权限。摘要正确但启动文件为 0644 的包会在访问设备前拒绝；不要手工改变归档权限来绕过检查，应重新打包。

在忽略目录建立属于当前用户、权限 0600 的 JSON 设置。以下身份只是示例，必须替换为当前应用实际使用的稳定身份；不要填入 token、Wi-Fi 密码或任何旧预装认证数据。

```json
{
  "server": "https://botstation.shengwang.cn/api",
  "device_id": "RV1126B-0123456789ABCDEF01234567",
  "video": true,
  "wifi_interface": "wlan0"
}
```

例如保存到 `local/deployment-settings.json` 后执行 `chmod 600 local/deployment-settings.json`。设置只允许这四个字段；`server` 与 `device_id` 必填，省略 `video` 时默认为 true，接口空值表示自动选择。受监督部署默认选择视频程序；只部署语音时显式设置 `"video": false`。包中必须包含所选 profile，缺少视频程序时会在访问设备前报错，不会自动切换为语音。预览计划会显示所选 `profile`。

该默认值仅用于受监督部署。直接运行包含两个 profile 的包时，`run.sh` 仍默认选择语音程序，视频通过 `RV1126B_VIDEO=1` 显式选择。

部署前在设备内核对现有 `device_auth` 的版本、长度以及 server/ID。只读取相应的固定长度头部字段，完全不读取 token 区域，也不把身份打印或写回执。已存在记录但格式不符或 server/ID 不匹配时阻止部署；记录不存在时允许首次绑定。该核对避免新 SDK 因配置变化丢弃已有绑定。

## 检查、预览和部署

主机需要 Python 3.10+ 和可用的 `adb`。设备需为 AArch64、以 root 连接、已挂载 `/userdata` 并提供 tar/sha256sum/stat/dd/od/awk 等工具。连接多台设备时必须明确指定 `--serial`；只连接一台已授权设备时可省略。以下 `SERIAL` 和 `ARCHIVE_SHA256` 应替换为实际值。

只读预检还会核对准确板型、依赖锁中的 20 项运行库摘要、动态加载器、电源命令、Wi-Fi 工具、dhcpcd hook 目录，以及两项采集 mixer 控制的可写整数范围和板级 PWM 背光路径、权限与数值。预检不设置 mixer、不创建热点，也不要求旧应用释放端口。其他固件必须先按依赖流程登记匹配基线；不能仅凭 AArch64 架构认定兼容。

```sh
version=$(python3 scripts/project-version.py)

python3 scripts/deploy-rv1126b.py preflight --serial SERIAL \
  --settings local/deployment-settings.json

python3 scripts/deploy-rv1126b.py deploy --serial SERIAL \
  --settings local/deployment-settings.json \
  --package "dist/mybot-rv1126b-${version}-aarch64.tar.gz" \
  --sha256 ARCHIVE_SHA256 --dry-run
```

预览只输出脱敏计划；不会执行 ADB。确认包和计划后，将 `--dry-run` 改为 `--execute`。首次导入回退包时再提供 `--rollback-package PATH` 与 `--rollback-sha256 SHA256`。默认安装完成后保持服务停止，由操作者检查配置与网络策略后启动；只有显式加 `--start` 才在版本切换后启动，不自动重启设备。

`--start` 最多等待 30 秒核对应用版本、PID 存活、实际可执行路径和装配后的状态，单次 ADB 健康查询最多 5 秒。`ready` 表示可对话，`started` 表示已进入 SDK 启动流程，`degraded` 表示本地应用已启动但仍处于配网、等待网络/时钟、绑定、离线或 `wifi_error` 等状态；后者会如实显示，不强制等待云端 ready。健康检查失败保留部署锁供检查，不会打印成功回执或自动回退。

显式兼容的 0.1.1 早于应用状态接口。仅对此版本，检查所选旧二进制的真实 PID、直接父进程监督器的执行路径，以及该监督器的服务 runtime 参数，报告 `application_health=legacy_running`、`application_state=unsupported`；这只确认受监督的本地进程，不能证明业务 ready。现代版本缺失应用状态文件时仍拒绝启动确认。

写入顺序为：只读平台/能力/身份检查 → 设备内完整备份 → 上传及再次校验 → 解包为独立 release → 用选定 profile 执行无硬件访问的 `--version` 加载检查 → 再核对身份 → 停止旧服务 → 安装稳定服务配置 → 原子切换链接。停止命令失败、旧进程仍占用或校验失败时不继续。保留 ALSA 配置文件，不禁用 chronyd、ADB 或电源服务。每次服务启动前，`audio-prepare.sh` 通过 `amixer cset` 设置并读回核对 `ACodec_LP Digital Gain Volume=0`、`ACodec_LP PGA Gain Volume=23` 两项采集控制；这会修改声卡的运行时 mixer 状态，不修改播放控制或写入 ALSA 配置文件。

部署同时安装固定的 `/etc/mybot-rv1126b/poweroff.sh`。只有已确认的受监督关机在清理完成后调用它，将板级背光 `brightness` 设为 0、`bl_power` 设为 4 并读回，再请求 `/sbin/poweroff`；失败或信号中断尝试恢复原值。普通服务停止、重启及版本切换继续恢复原显示，不主动关闭背光。软件关机不等于所有电源掉电，实体长按的实际硬件链路尚未确认。

原 `botstation-device` 配置改为 `BOTSTATION_ENABLED=0`，其 `S99botstation-device` 移入 `/etc/mybot-rv1126b/disabled/`，避免开机重复占用。系统 dnsmasq 配置限制为 `interface=usb0`、`bind-dynamic`，保留现有 USB 监听地址与 DHCP 范围，USB 接口尚无 IPv4 时也不会抢占 AP 地址。dhcpcd 增加 `denyinterfaces mbap*`，防止其接管临时 AP；网络租约 hook 安装到 `/lib/dhcpcd/dhcpcd-hooks/99-mybot-lease`（0644），运行记录目录 `/run/mybot-network/leases` 为 0700。操作者在确认连接条件后分别受控重载：

```sh
adb -s SERIAL shell /etc/init.d/S80dnsmasq restart
adb -s SERIAL shell /etc/init.d/S41dhcpcd restart
```

该系统的 `S41dhcpcd restart` 可能仅重新配置已有进程，不产生新的租约事件。确认 `wlan0` 由 dhcpcd 管理后，可显式触发租约处理和 hook，再启动业务：

```sh
adb -s SERIAL shell dhcpcd -n wlan0
adb -s SERIAL shell /etc/init.d/S99mybot start
```

hook 原子生成 `/run/mybot-network/leases/wlan0.lease`（0600），记录租约世代、关联与 IPv4 状态，不包含密码、PSK 或 token。每条命令都应成功后再继续；不要全局 kill 网络、授时或系统服务。部署工具的 `--start` 不代替策略重载和租约检查。

## 状态、回退与故障处理

`status` 分别输出实际 `service` 状态和应用的 version/pid/state、`application_verified`、`application_health`。监督器处于 `backoff` 时不会显示为 running；陈旧状态、错误版本/profile、PID 不存活或执行路径不属于当前 release 时不会通过健康确认。此查询独立于安装能力预检，便于在运行库或板级依赖损坏时诊断。

```sh
python3 scripts/deploy-rv1126b.py status --serial SERIAL
python3 scripts/deploy-rv1126b.py rollback --serial SERIAL \
  --settings local/deployment-settings.json --dry-run
python3 scripts/deploy-rv1126b.py rollback --serial SERIAL \
  --settings local/deployment-settings.json --execute --start
```

回退默认切到 `previous`，也可用 `--release mybot-rv1126b-0.1.1-aarch64` 选择已安装版本。备份与停止前，先检查身份、实际服务配置的语音/视频选项与 settings 一致，以及目标文件摘要、执行权限和 `--version` 加载结果；停止前再核对一次。仅语音目标不能承接视频配置，回退不会隐式修改 profile。使用相同私有 KV 和稳定监督程序，不恢复旧数据、不重新绑定，不自动重新启用预装业务。再次执行回退可切回刚才的版本。

监督程序遇到信号崩溃或强制停止、无法证明资源已经清理时，会保留 `/run/mybot-rv1126b-service/unclean` 标记，`status`/`stop` 返回失败。此状态阻止自动重启、服务 restart 以及部署/回退切换；不要删除标记或运行目录来绕过保护。先检查私有服务日志、遗留进程组，以及网络接口、门户端口、DHCP 和音视频资源的归属，再安排恢复。当前没有自动恢复清理入口；需要重启设备时由维护者在确认现场状态后执行，使 `/run` 与内核资源一起复位，部署工具不会自动重启设备。

部署期间持有 `/run/mybot-deploy.lock`。失败会保留锁、备份和已验证 release 供检查，不会未经检查恢复网络或覆盖应用数据。此时先保持业务停止，检查设备备份及失败步骤，修复后由设备维护者移除确认失效的部署锁，再用新的 tag 重试或选择已验证版本。不要直接解包备份覆盖整个系统或当前 KV；恢复系统配置须逐项检查，凭据继续留在设备。

完成后分别检查版本/包摘要、服务 start/stop/restart、绑定复用、音量恢复、重启自启动、既有 Wi-Fi 复用、AP 配网及失败重试、系统时间、ADB/USB 网络、音频和可选视频。主机测试只验证工具的保护逻辑，不能替代这些设备结果；实际记录见[验证状态](../validation/README.md)。
