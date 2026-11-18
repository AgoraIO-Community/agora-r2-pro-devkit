# 当前验证状态

更新日期：2026-11-18。源码版本由根 `CMakeLists.txt` 管理，执行 `python3 scripts/project-version.py` 查看。当前候选为 **0.6.0**，本页区分主机、交叉构建和硬件证据；历史轮次见[完整归档](../history/validation-before-0.5.3.zh-CN.md)。

## 治理专项

- Wi-Fi：完整永久 MAC 摘要保持 `mybot-xxxx` 长度；真实自有进程组清理；AP 服务失效后撤销指引并允许设置重试。中文、英文、UBSan 的相关主机回归通过。
- 交付：profile/加载/权限/板级前提在切换前检查；保留真实监督状态并核对实际应用；0.1.1 单独报告 `legacy_running` / `unsupported`；归档强制通过的 ABI 结果与 RTSA 库身份一致。
- 架构：产品生命周期集中到 controller，时钟及应用状态使用类型；公共 C 接口声明所有权和失败契约；SDK 启动失败也执行停止清理。视频线程区分 join、运行及失败，有限重试后上报。
- 依赖与条款：当前快照与历史分离，完整 RTSA 99 文件保留；目录链接明确拒绝，内部文件链接绑定文本与内容；cJSON 完整 MIT、依赖授权与商务联系随源码/适用运行包保留。
- 手册：中文和英文各 24 页、15 张 UI 图；固定字体及 manifest，30 张图片像素和两份 PDF 字节重现通过。48 页视觉和文字边界检查通过，正常 UI 各 38 帧基线一致。
- 提交规则与安全：hook 和 CI 使用完整消息校验；私密报告渠道及支持政策已明确。原始历史提交不重写，CI 按当前事件范围检查。

## 发布前统一验证

2026-11-17 在干净源码上实际执行，均保留完整日志；下表按实际验证的提交区分，不把后续修改自动计入旧结果。

| 环境 / 源码 | 实际结果 |
| --- | --- |
| Intel macOS，`7007e72` | 中文、英文、UBSan 构建及 CTest 各 **27/27**，每套包含 **201** 项 Python tooling 测试 |
| macOS，后续修复 | `db0b9a9` 的 `network-lifecycle` 在上述三套配置各 **1/1**；`8114a45` 中文 `wifi-process` / `wifi-backend` / `wifi-service` **3/3** |
| Ubuntu 24.04.4 x86_64，`8114a45` | GCC 13.3、CMake 3.28.3、Python 3.12.3；中文、英文、UBSan 全新配置、构建及 CTest 各 **27/27**，独立 tooling **201/201** |
| 同一 Ubuntu 源码，ASan + UBSan | **16/16**，覆盖输入、配置、运行保护、服务状态、监督、Wi-Fi 进程/后端/门户/控制、网络启动、UI、视频生命周期及产品控制器 |
| 工程及手册，`8114a45` | doctor、依赖、质量、diff 检查通过；278 个维护文件、107 个格式源；双语手册在 macOS / Ubuntu 重现，30 张 PNG 像素和两份 PDF 字节一致 |
| 最终 AArch64，`9eb72bb` | 中英文 × 语音/视频四套构建通过，源码摘要一致、Git 干净；应用依赖分别 9/11 项、监督器 2 项，ABI `errors=[]` |
| 最终双语归档，`9eb72bb` | 两种语言均含 voice/video，归档语义校验及各 **65** 项内部 SHA256 通过；中文重复打包字节一致；语言、cJSON 完整条款、公开 manifest 无主机绝对路径检查通过 |

Ubuntu 使用 1331 个 Git 跟踪文件构造独立 checkout，安装项目固定开发和手册依赖后执行；前后核对文件内容、0644/0755 权限与 Git 清单，原远端工作区未变、无残留验证进程。它没有目标 SDK，AArch64 检查在另一套已配置 SDK 的构建环境执行。

前两次 Ubuntu 尝试分别在 `7007e72`、`db0b9a9` 遇到 `network_route.c`、`wifi_process.c` 重复定义 `_DEFAULT_SOURCE`。修复为条件定义后，`8114a45` 完整通过；失败轮次和第二次仅用于诊断的部分测试均未计为全量成功。ASan 命令使用 `-fno-pie` / `-no-pie`，未关闭 sanitizer；完整参数与所选测试如下：

```sh
cmake -S . -B build/host-asan \
  -DRV1126B_HOST_ONLY=ON -DRV1126B_BUILD_TESTS=ON \
  -DRV1126B_LANGUAGE=zh-CN -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie' \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined -no-pie'
cmake --build build/host-asan --parallel 4
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir build/host-asan --output-on-failure \
  -R '^(input-semantics|input-worker|power-control|supervisor|renderer|ui-runtime|app-config|runtime-guard|service-status|wifi-process|wifi-backend|wifi-service|wifi-portal|network-startup|video-lifecycle|product-controller)$'
```

最终交付从干净 `9eb72bb` 重建四套目标，源码 SHA256 均为 `f65fe78eed8b8080170d1e681a271c91445536ac2ba77009ef677aa8d373c505`。先前 `db0b9a9` 的目标/归档结果仅作中间证据，不作为最终包。归档对应源码提交由 manifest 记录；本页随后记录验证结果的文档提交不改变该归属。重现命令见[发布指南](../development/releasing.zh-CN.md)。

| 最终包 | SHA256 |
| --- | --- |
| `mybot-rv1126b-0.5.3-aarch64.tar.gz` | `45aa519235e0f28ae4ca1e0d8f6b8176be8f05945f20e0b23084ec970f2ace6c` |
| `mybot-rv1126b-0.5.3-en-US-aarch64.tar.gz` | `53a33858cb89ab5fbff498e5e86b46c6386881c18b99257a81358b54d51cc2e9` |

## 设备部署与简单回归

已记录板型为 **RV1126B TEST2 RK805B V10**，AArch64 Buildroot 2024.02、Linux 6.1.172、glibc 2.41，物理屏幕 480×640。

此前 **0.5.2 中文语音**：设置入口、长按不触发配网、设备本机 HTTP 提交并保存当前网络、热点关闭及 SDK 恢复 ready，原绑定和网络配置保留。触摸由 evdev 注入，HTTP 客户端在设备内；这不等同于手机无线、DHCP 或 captive browser 验收。用户另已确认此前候选可由 UI 关机，并可长按唯一物理键开关机，未测定固定秒数和 PMIC 断电范围。

2026-11-17，经用户授权，将上表中文归档部署为 **0.5.3 中文语音**，tag 为 `20261117-governance-053`。设备内备份留在 `/userdata/mybot/backups/20261117-governance-053/`，当前链接为 `releases/mybot-rv1126b-0.5.3-aarch64`，回退链接为 `releases/mybot-rv1126b-0.5.2-aarch64`。未重启设备。

已实际检查：

- 部署前 platform/capabilities 均 ready、现有身份匹配；部署后运行版本与实际可执行路径匹配，服务为 running，应用为 ready。
- 65 项包内摘要匹配；voice / video `--version` 均加载成功。运行的是语音 profile，视频只做动态加载检查。
- 设备内比较备份与当前数据，认证记录、全部两个私有 KV 文件、server/identity 及系统 Wi-Fi 配置一致；内容和摘要均未导出。没有已保存的音量记录，不能据此声称音量恢复通过。
- 单应用与单监督器，Wi-Fi 已关联且有默认路由；无临时 AP 接口、hostapd、unclean 标记或部署锁。启动日志按错误类别只输出计数，未导出原文；未发现 fatal/error/清理或监督器失败标记。
- 显式执行一次 `/etc/init.d/S99mybot restart`，正常退出并换为新 PID，重新核对 `application_verified=yes`、`application_health=ready`。
- 通过已有 evdev fixture 注入设置齿轮、返回触摸及普通区域 6 秒长按，应用 PID 不变并保持 ready，未触发 AP；没有设备读屏证据，因此不将注入成功等同于 UI 视觉验收。
- 重启后再次核对私有数据与 Wi-Fi 配置均一致，最新启动日志的上述失败计数均为 0；测试 helper 和本轮临时目录已清理，无残留验证进程。

执行命令为部署工具 `deploy --execute --start`、`status`、release 内 `sha256sum -c SHA256SUMS` / `run.sh --version`（语音和视频）及上述服务 restart；包 SHA256 和 tag 见本页。触摸 fixture 仅用于本轮输入注入，不是常驻服务。脱敏回执与逐项结果保存在忽略目录 `local/deployment/20261117-governance-053/`，本页提供可公开核对的摘要。

这是简单回归。手机错误密码/保存/重启、AP 服务中断与重试、真实语音/音量/AEC、视频首帧/方向/启停/并发、系统授时、关机及再次开机、长期稳定性仍需专项验收。

## 2026-11-18：默认视频部署

按用户要求，受监督部署省略 `video` 时默认选择视频，显式 `false` 仍可选择语音；普通 `run.sh` 和 CMake 的默认值保持原含义。 默认值变更的部署工具测试 **58/58**、打包工具测试 **41/41** 通过，服务模板的受保护摘要已按依赖流程更新；目标 SDK 重新配置及 `doctor.py --target --video` 通过。 双语手册已按 2026-11-18 刷新并完成字节重现，各 24 页、22 个书签，48 页文字边界检查通过。设备复用上表 `9eb72bb` 的完整中文包，归档 SHA256 不变，私有部署设置显式写入 `video: true`，以 tag `20261118-video-default-053` 执行部署。

实际结果：有效配置 `RV1126B_VIDEO=1`，所选 release 内的 `bin/mybot-rv1126b-video` 路径及 PID 核对通过，应用 `ready`、服务 `running`。再次受控 restart 后换为新 PID，视频路径和 ready 状态仍匹配；65 项内部摘要通过，单应用、单监督器，无 AP、unclean 标记或部署锁。设备内与本次备份核对身份、绑定、两个私有 KV 文件及 Wi-Fi 配置均一致，最新启动日志的错误/崩溃/清理失败计数为 0。

相机环境只读检查发现唯一 `rkisp_mainpath` 为 `/dev/video12`，MPP 设备和 `rkaiq_3A_server` 存在。本轮未开始云端会话，因此未初始化采集、未提交 JPEG，**这次通过的是视频程序启用与待机恢复，不是视频出帧或云端接收验收**。短触开始会话后仍需检查首帧、方向、音视频并发及结束停流。

设备内备份为 `/userdata/mybot/backups/20261118-video-default-053/`；回退链接继续保留 0.5.2。主机回执曾因本轮设置备份预占同名目录而写入失败，确认设备切换完成后，将设置备份移到独立的忽略目录并补齐回执，未重复部署。脱敏结果在 `local/deployment/20261118-video-default-053/regression.json`。本轮未重启设备，未导出私有内容或摘要。

## 2026-11-18：新设备首次部署

本轮目标是另一台同板型的新设备，部署前没有 MyBot 发布、配置、私有数据或绑定。为本机 CPU Serial 按现有算法派生独立身份，并确认与前一台设备不同；未导入旧设备身份、绑定或 Wi-Fi 凭据。私有设置与目标选择留在忽略目录，硬件身份原值不公开。

从干净提交 `24758f9` 重建中文 voice/video，应用和监督器 ABI 均 `errors=[]`，打包语义及 **65** 项内部摘要通过。新包 `mybot-rv1126b-0.5.3-aarch64.tar.gz` 位于 `dist/new-device-20261118/`，SHA256 为 `03cf89177dc44d0588590cc4dead933c45fa2a343a52e7d5b81eb46ccba62796`，源码摘要为 `d2f3b5fe44a54bc760b28d1a594972ac3dba39d8c48e43a4407258e0b2291f13`；该候选与先前提交的同版本包有各自的 manifest 和归档身份。

以 tag `20261118-new-device-video-053` 完成设备内备份、安装、原厂业务停用和自启动服务安装；逐项重载 dnsmasq / dhcpcd 策略后启动。设备最初无联网路由，正常进入 AP；现场配网后 Wi-Fi 配置从 1 项变为 2 项，该新配置保留，未按部署前字节覆盖。首轮联网时系统仍为 2021 年，时钟守卫超时并在有限重试后停止服务；未将这一轮计为业务通过。之后确认系统 NTP 已自行同步至正确 UTC，写入 RTC，再受控重启服务恢复启动，未手动替换网络凭据。

最终观察到 `service=running`、`application_verified=yes`、`application_health=ready`，有效 `video=1`，实际所选视频程序路径匹配，已完成新设备绑定。一个应用、一个监督器、原厂业务零实例，65 项文件校验通过；无 unclean 标记或部署锁，最新启动日志无崩溃、资源清理失败、监督器失败或 ALSA 打开错误。音频别名实际定义于 `/etc/alsa/conf.d/90-botstation.conf`，不能因 PCM 枚举未显示就认定缺失。

本机没有 `previous` 发布，原始系统备份留在 `/userdata/mybot/backups/20261118-new-device-video-053/`。脱敏回执、启动、校时恢复和最终回归记录保存在 `local/deployment/20261118-new-device-video-053/`。本轮未执行整机重启、实际通话或视频出帧验收；RTC 写入不等同于断电保持验收。

## 2026-11-18：计划关机关闭背光

用户反馈 UI 关机后屏幕仍有亮度。只读检查确认唯一 PWM 背光为 `/sys/class/backlight/backlight`，实际设备路径 `/sys/devices/platform/backlight/backlight/backlight`，初始 `brightness=200`、`actual_brightness=200`、`max_brightness=255`、`bl_power=0`；普通 DRM 停止会恢复原 CRTC，现有应用没有显式关闭背光。设备的 Linux 输入配置没有 POWER 关机映射，RK805 的 `pwrkey` 节点 disabled，不能据此声称已确认实体长按的 PMIC/硬件触发链路。

0.5.4 的计划关机在监督器确认应用正常清理退出后，调用固定板级 `poweroff.sh`：保存原值，设置亮度 0、背光电源 4，核对实际亮度 0 后再请求正常 `/sbin/poweroff`。属性异常、写入/读回失败、命令失败或中断时尝试恢复原亮度和背光电源。普通应用 stop/restart 仍恢复原显示输出，未引入 DRM 停止模式或更改实体按键电路。

已在当前设备执行一次可逆背光测试。隔离副本仅把真实 poweroff 替换为固定失败探针，观察到 `brightness=0`、`actual_brightness=0`、`bl_power=4`；探针返回失败后，helper 自动恢复 200/200/0，外层恢复保护再次复核，临时文件已清理，应用继续 ready。原始 helper SHA256 为 `6cb4bbfd968ec1958ccd0b0193eeea9b86fe6971d549bca3ef7c8596a5a284da`，脱敏结果留在 `build/backlight-poweroff-054/device-backlight-results.json`。

工具验证：macOS 新 helper **10/10**、部署 **58/58**、打包 **41/41**；Ubuntu 24.04 的独立临时快照对最终 helper 测试源执行 **10/10**，使用假 sysfs/假关机并清理快照。macOS 新隔离主机构建中 `ui-runtime`、`drm-lifecycle`、`supervisor`、`power-control`、`product-controller` **5/5**。最初 host fixture 因 macOS `/var` 与 `/private/var` 规范路径差异提前拒绝，修正 fixture 后通过；生产 helper 未为此修改。 0.5.4 双语手册刷新及字节重现通过，各 24 页、22 个书签，48 页文字边界检查通过；固定手册字体按原输入正规补充“背”“光”两个字形。

最终交付从干净提交 `fc715ac` 构建中文 voice/video，应用与监督器 ABI 均 `errors=[]`；新包包含 **66** 项内部摘要并通过归档语义验证。`mybot-rv1126b-0.5.4-aarch64.tar.gz` 位于 `dist/backlight-poweroff-0.5.4/`，SHA256 为 `e2fc1f3d97a96340155a4a9fb1e6e0c97eb024624f4d3eb32d3a6246a3067cb8`，源码 SHA256 为 `f503baf1aa6ad3713ac95d60bf7ec62bd1e865efb4e499312301e8d7e5e56809`。本页后续文档结果提交不改变该制品归属。

以 tag `20261118-backlight-054` 部署至当前设备，保留 0.5.3 为 previous，默认视频开启。实际运行路径、版本及 PID 核对通过，服务 running、应用 ready；安装后的 helper 与上面已测试字节一致，监督器实际 `--poweroff-command` 指向该 helper。66 项内部摘要、身份/绑定、两个私有 KV 与 Wi-Fi 配置均核对通过，最新启动日志无错误、清理或监督失败；普通升级后的背光仍为 200/200/0。脱敏回执和结果位于 `local/deployment/20261118-backlight-054/`。

这证明 PWM 背光控制、失败恢复及新关机入口安装在本机可执行；未执行真实关机，也未验证电池/USB 供电下的所有电源轨断电、关机后屏幕目视效果或再次按键开机，不把可逆测试写成实际断电验收。

## 2026-11-18：实际关机清理路径修复

用户实测 0.5.4 UI 关机仍有亮度。重新读取既有固定格式日志得到完整顺序：`Settings page=3` → `supervisor: poweroff_pending` → `DRM: cannot restore CRTC: No such file or directory` → `supervisor: failed`，没有 `supervisor: poweroff`。这次 UI 请求因显示清理返回非零而被保护逻辑拦截，尚未执行背光脚本；此前独立的可逆背光测试没有覆盖这一清理失败。

0.5.5 仅在监督器已接受关机请求时使用专门的显示停止接口：显示 worker 持有 DRM 所有权时关闭当前 CRTC，释放资源，不恢复可能已经失效的旧 framebuffer。普通停止仍尝试恢复旧输出；真实关闭失败仍返回错误并阻止关机。监督器在释放控制 socket 后让 helper 的 stderr 继承现有私有日志，避免最后阶段失败不可见。

另用安全 host 夹具复现了独立的信号交接问题：旧 helper 的假 poweroff 即使返回成功，同时收到 TERM 也会恢复背光；它不是上述实际失败的根因。新版只在系统关机交接阶段忽略可处理的迟到信号，准备阶段中断和命令非零仍回滚，回滚自身不再被重复信号截断。

源码检查：中文、英文、UBSan 的 `ui-runtime` / `drm-lifecycle` / `product-controller` / `supervisor` 各 **4/4**，包含失效旧 FB、真实 disable 失败、生命周期重置、请求拒绝、helper 私有诊断和日志复制失败。helper 安全模拟测试 **14/14**，不执行实际关机。0.5.5 双语手册刷新和重现通过，各 24 页、22 个书签，48 页文字边界检查通过，字体和 UI PNG 未改。新设备只读检查 `CONFIG_FB` 未开启，且无 framebuffer 设备节点，本轮真机测试另核对了关闭后的 DSI 状态。

从干净提交 `8886780` 构建中文语音及视频程序，应用与监督器 ABI 检查均通过。部署包 `mybot-rv1126b-0.5.5-aarch64.tar.gz` 的 SHA256 为 `fe2510d9dbaa20ae570d9f654aaf2cf09830357e5e9901f29ef42df08d2f195e`。设备按 `20261118-display-poweroff-055` 安装视频版，并保留 0.5.4 供回退。

设备上的隔离监督器启动实际视频程序。经触摸输入打开设置并确认关机，日志依次记录 `poweroff_pending`、`stopped`、`poweroff`，应用正常退出。仅将最终 `/sbin/poweroff` 换成测试程序。该程序读到 DSI 为 `disabled`，请求亮度与实际亮度均为 0，`bl_power=4`，随后退出码为 0。已恢复正常服务并清理测试文件，确认生产视频程序 `ready`、66 项包内校验通过，身份、绑定和 Wi-Fi 配置与备份一致。完整结果在 `build/poweroff-followup-20261118/device-ui-chain-results.json`。整机实际关机后有无可见余光仍需现场验收。

Ubuntu 24.04 的 5 项 C 测试首轮 4/5，监督器用例等待超时。同一冻结源码、同一 CTest 命令复核 5/5。helper 模拟测试 14/14。原超时原因尚未确定，保留两次记录。

## 0.6.0 恢复出厂与播放音量

实现基线为 `dev@4fafa95`（0.5.5）。中文主机 11 项专项 CTest 通过，覆盖确认和返回、输入重复、重置授权、清理与恢复、重新配网、UI 和音量。使用临时目录和模拟 Wi-Fi 控制端分别验证系统已存配置、运行网络列表和断联状态，备份、日志与其他接口租约保留。部署工具 58 项、打包工具 41 项通过。

两种语言各核对 41 帧 UI 基线。双语手册各 25 页、23 个书签，包含恢复确认页，图片及 PDF 已重现。受监督重置的现场验收应使用专用设备，依次确认取消保留原值、重置后显示热点、连接后重新配对、音量为 70，以及普通升级仍沿用已保存音量。

从 `1ede145` 的干净源码构建中文和英文的语音与视频程序。应用、监督器和恢复出厂工具均通过 AArch64 ABI 检查，两份完整归档各通过 69 项 SHA256 和部署工具的归档校验。Ubuntu 24.04 隔离夹具通过真实监督器 IPC 和服务启动脚本完成重置，新应用读取音量 70，普通启动读取原保存值 37。原 Wi-Fi 配置从文件和模拟控制端清除，USB 租约及维护备份保留。Ubuntu 专项 CTest 首轮 10/11（监督器等待超时），同一冻结源码和命令复核 11/11。上述重置使用合成数据。

## 0.5.5 设备启动时短暂显示配网失败

在运行 0.5.5 的设备上，系统与 supplicant 均有两个已保存网络。应用历史日志中五次带错误的 `Wi-Fi state=5` 均来自 `No nl80211 Wi-Fi interface; configure the Wi-Fi interface explicitly`。最近这次 0.5.5 启动从等待直接转到该错误，之后通过时钟检查并进入 SDK ready。配置已指定 wlan0，检查时 wlan0 已可见且提供默认路由。接口检查位于加载配置和尝试已保存网络之前。普通启动允许在 Wi-Fi 服务报告错误后随有效路由继续，因此能解释先显示失败再 ready。系统关联日志不足以逐次重建 supplicant 选择的 SSID。现场检查只读取日志和状态。

## 如何复核

标准命令见[测试指南](../development/testing.zh-CN.md)，目标输入及交付入口见[构建](../user/build.zh-CN.md)和[发布](../development/releasing.zh-CN.md)。手册命令见[维护指南](../development/manual.zh-CN.md)。公共 CI 配置存在不代表本轮已在 GitHub 执行；本页只记录实际观察到的结果。

审查基线与问题分类见[历史审查](../history/review-0.5.2.zh-CN.md)。本地忽略日志只作补充，公开结论由这里的命令、结果摘要及维护的回归测试支撑。旧轮次的版本、哈希和硬件限制保持原归属。
