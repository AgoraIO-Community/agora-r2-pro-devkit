> 历史审查：下文保留基线 57f335d 的发现与当时状态；修正结果及当前验证范围见[验证状态](../validation/README.md)。授权待核实项已由维护方确认，现行说明见[依赖授权](../dependencies/licensing.md)。

# 工程全面审查：57f335d / 0.5.2

日期：2026-11-17。目标：专业、可维护的公开工程，架构简洁，功能与交付行为可解释、可验证。

审查覆盖项目维护的 C/C++ 源码、公开/私有头文件、Linux 服务及部署脚本、CMake/构建身份/打包/CI、测试、注释、双语文档与 PDF 生成、依赖来源和许可边界。固定依赖审查关注接入方式、完整性及许可，不声称审计闭源 RTSA 内部实现。本次没有访问设备、修改受维护文件或创建提交。

## 结论

现有 `src/application`、`src/platform`、`src/ui`、`vendor` 的顶层划分可以保留。进一步提升质量应优先修复失败路径、交付前校验和文档准确性，再收敛重复策略与历史治理负担。

工程已经有明确的线程/资源归属、私有持久化、依赖固定、归档校验及较完整的主机测试。剩余问题集中于“正常流程通过，异常流程状态未同步”和“验证了文件字节，尚未完整验证文件可执行及版本可运行”。本报告的 P2 为有明确代码触发路径的缺陷或交付/指南错误；P3 为维护与专业性改进。许可项单列为发布前核实事项，不断言存在未授权使用。

## 验证及范围

- `scripts/check-quality.py`：通过，248 个维护文件、98 个格式检查源码。
- `scripts/check-dependencies.py`：通过，快照、来源、补丁及生成字体检查成功。
- 中文主机构建成功；24 个 CTest 条目均已验证通过。首次运行有 4 项被沙箱禁止绑定本机 socket，允许本地 socket 后四项复测通过；不是把第一次失败掩盖为源码通过。
- 部署工具 25 项、打包工具 32 项测试通过；它们已经包含在主机工具门禁或由审查者独立执行。
- 额外隔离复现：AP 状态在后端退出后不撤销；进程组组长退出后有后代残留；回退未校验所需 profile；视频进程名漏检；0644 的启动文件可通过归档验证；主机绝对路径进入嵌入式构建身份；`!` 提交没有 footer 仍被 hook 接受。
- 视频失效恢复问题通过驱动与 SDK 调用路径交叉复核，未声称已在真机观察到。
- 未在本轮运行目标构建、手机无线/声学/电源/视频专项，也未声称远端 CI 已执行。本次不需要修改设备。

审查工作目录保留了格式/依赖/主机测试日志及两份 Wi-Fi 隔离复现程序；这些本地补充文件没有作为公开附件归档。本报告保留当时的命令、结果和复现结论，修正后的可执行回归保存在维护的 `tests/` 中，现行结果见[验证状态](../validation/README.md)。部分复现使用主机 mock，不能替代硬件验收。

## 应优先修复的问题

### R01 / P2：Wi-Fi 子进程组停止只等组长，可能留下后代

位置：`src/platform/src/services/wifi/wifi_process.c:106`、`:122`。

`wifi_child_stop()` 给组发 TERM，随后 `wifi_child_running()` 仅通过 waitpid 检查并回收组长。组长先退出时，pid 被改为 -1，后续 SIGKILL 分支被跳过，忽略 TERM 的后代仍存活。独立复现得到 `leader_pid_after_stop=-1 descendant_alive_after_stop=1`，复现后已清理所有测试自有进程。

影响：关闭或重建 AP 时可能遗留 DHCP hook/辅助进程，资源停止保证不完整。监督器最终退出时的后代收养不能替代应用继续运行期间每次 Wi-Fi 停启的正确清理。

建议：保留可证明归属的进程组身份，组长在清理完成前不提前丢失；参考已有 helper 的未回收 PID 保护处理 TERM→KILL→reap。不能只缓存一个可能复用的 PGID 后任意发信号。补组长先退、后代忽略 TERM 的回归。

### R02 / P2：AP 辅助服务退出后仍向用户展示“可配网”

位置：`src/platform/src/services/wifi/wifi_service.c:585`、`:623`；`wifi_backend.c:492`。

AP_READY 循环会刷新扫描，但不再检查 backend 的 AP 健康。hostapd 或 dnsmasq 死亡后，public status 仍可保持 `ap_active=true`。隔离复现将后端 ap 置为 0，200 ms、4 次扫描后服务仍显示 AP_READY，未重建。

建议：在 AP 等待阶段检查实际 AP/辅助进程健康，失败时撤销“可用”状态并清理 owned 资源；提供明确错误及有上限的恢复。不要让 SDK 长期停止而 UI 继续给出不可用热点。

### R03 / P2：热点名使用 MAC 厂商前缀，同批设备大量重名

位置：`src/platform/src/services/wifi/wifi_backend.c:294`。

`mybot-xxxx` 取 MAC 前两个字节。`AA:BB:CC:00:00:01` 与 `AA:BB:CC:00:00:02` 都成为 `mybot-aabb`。同一批次同时配网时，手机和用户难以区分设备。

建议：使用验证过的完整 MAC 尾部，例如末三个字节，或稳定身份的短摘要；同步测试、设备图示及热点命名说明。这是可用性问题，不宣称短名称可保证数学上的全局唯一。

### R04 / P2：回退不校验所选语音/视频程序是否存在

位置：`scripts/deploy-rv1126b.py:459`、`:486`、`:534`；`packaging/run.sh:7`。

新部署会验证 settings 选择的 profile；rollback 仅校验目标 release 的摘要，保持当前全局服务配置。当前为 video=true、previous 为合法 voice-only 包时，回退会停止可用业务、切换 current，再尝试执行不存在的视频程序。主机 mock 已确认该路径可返回成功且未检查视频程序。

建议：停止前读取实际生效的 profile，验证回退目标含对应可执行文件；需要改变 profile 时显式处理配置。settings 参数与设备实际生效配置也应一致校验。

### R05 / P2：部署检查漏掉仍在运行的视频实例

位置：`scripts/deploy-rv1126b.py:350`。

旧实例检查只匹配 comm 为 `mybot-rv1126b`。视频文件为 `mybot-rv1126b-video`，Linux 15 字符 comm 截断为 `mybot-rv1126b-v`，因此直接运行的视频进程可逃过检查。主机 shell-case 复现证实语音拒绝、视频通过。

影响：部署可能在旧视频实例占用硬件时修改服务/网络策略并切换版本；新程序随后因运行锁启动失败，旧业务仍留在运行。

建议：通过已打开的运行锁、`/proc/PID/exe` 与服务归属识别实例，至少补齐截断名称。匹配过程保持只读，不能以名称全局杀进程。

### R06 / P2：预检遗漏可提前发现的依赖，失败时系统已经改变

位置：`scripts/deploy-rv1126b.py:236`、`:512`、`:519`；`packaging/service/audio-prepare.sh:6`。

preflight 主要检查 root/aarch64、挂载、通用工具和路径归属。dhcpcd hook 目录到旧服务停止、系统策略写入后才检查；amixer 和采集控制到启动时才检查。也没有把连接设备的板型/运行库/加载器与构建基线对应起来。仅“另一个 AArch64 系统”就可能通过 platform=ready。

建议：将预测得到的必需目录、工具、音频控制、板型/固件能力前提移到修改系统之前；先在 staging release 运行纯信息 `--version` 等加载检查，并对运行库身份或能力执行明确核对。ABI 报告验证的是构建输入，不能自动证明当前连接设备兼容。

### R07 / P2：归档校验不验证启动文件的执行权限

位置：`scripts/deploy-rv1126b.py:59`、`:314`；`tests/tooling/test_deployment.py:69`。

校验拒绝危险权限位，但允许 run.sh 和应用二进制为 0644。解包后没有修复这些文件的执行权限。现有“合法归档”测试夹具甚至统一将文件设置为 0644，掩盖了该问题；主机验证确实接受此类包。

建议：在修改设备之前，校验必要目录及 run.sh、所选程序、监督器和 executable assets 的语义权限；测试夹具应模拟真实可运行交付包，并有错误权限的负例。摘要正确不等于可运行。

### R08 / P2：部署状态把 backoff 等监督状态压成 running

位置：`src/platform/src/services/supervisor.c:868`；`scripts/deploy-rv1126b.py:414`；`packaging/service/S99mybot:21`。

监督器对 backoff 等状态返回查询成功，部署 status 丢弃实际字符串并打印 service=running。应用尚未重新启动时，状态仍可能报 running；`--start` 也没有选定版本已成功执行的健康确认。

建议：保留实际 supervisor state，另展示 application.status 的 version/pid/state，并核对 PID 存活/选定 release。部署健康条件允许 waiting_network、AP、pairing 等合法离线状态，不应强制依赖云端 ready。

### R09 / P2：视频工作线程失败缺少状态回传及受控恢复

位置：`src/platform/src/drivers/video/v4l2_mpp_video.c:529`、`:586`、`:617`、`:650`；`vendor/mybot/src/media/mybot_video.c:69`。

初始化、STREAMON、采集超时或编码失败后，工作线程记录日志、释放 pipeline 后退出；没有失败/已退出状态回传。thread_started 仍表示“需要 join”，SDK active 也维持为真，后续 start 被短路。本次会话可持续只有语音没有视频，通常需要结束再开启会话才恢复。

建议：独立表示线程可 join、采集运行、失败及退出，保留 join 所有权；增加有界初始化结果及运行失败上报/有限重试。不要简单在线程退出时清零 thread_started 而泄漏 join。公开契约要求 start 及时返回，不能要求同步调用帧回调或无限等待第一帧。

目前没有覆盖 V4L2/MPP 生命周期的 host 失败注入测试。建议优先验证初始化失败、运行中失败、重复启停、回调在途与停止失败保留资源，真机另测方向、时序和音频并发。

### R10 / P2：相同 0.5.2 名称对应不同来源，新包不能覆盖旧安装

位置：`CMakeLists.txt:3`；`docs/validation/README.md:316`、`:331`；`docs/user/deployment.zh-CN.md:11`。

已部署 0.5.2 与之后服务区默认值变更后的 0.5.2 包摘要不同；部署器正确拒绝同名不同归档。当前源码直接打包不能作为新版本替换已有同名安装。

建议：下一次交付先升级补丁版本，从干净 revision 构建；一个可安装版本对应一个已登记制品来源。开发包可以另有构建标识，但不要削弱不可覆盖的 release 保护。

### R11 / P2：配网测试文档把独立 KV 目录误当作系统 Wi-Fi 隔离

位置：`docs/user/wifi-provisioning.zh-CN.md:77`；`packaging/service/mybot-rv1126b.conf:9`；`wifi_backend.c:1126`。

更换 MYBOT_KV_STORE_DIR 不会取消 MYBOT_SYSTEM_WPA_CONFIG=/etc/wpa_supplicant.conf。读者沿用系统模式环境时，测试仍可更新/删除正式 Wi-Fi 主记录。

建议：清楚区分私有与系统协作模式。系统模式使用受控测试镜像或设备内备份；私有模式还需要明确网络管理服务的归属，不能以换数据目录承诺所有网络副作用都被隔离。

### R12 / P2：当前文档的验证状态互相矛盾

位置：`README.md:7`、`README.zh-CN.md:7`；`packaging/README.zh-CN.md:50`；`docs/user/settings.zh-CN.md:13`；`docs/validation/README.md:82`。

AP 被笼统写为“尚未真机验证”，但已有设备 AP/本机 HTTP 提交保存/SDK 恢复证据。设置指南仍说按键开机待验证，而用户已经确认 UI 实际关机与长按唯一物理键开关机，双语手册也采用了该事实。

建议：按测试范围记录“已通过设备后端/本机 HTTP”和“手机 DHCP/captive browser/声学待专项”两个事实；补录用户电源确认，保留精确按住时长、PMIC 断电范围未知。历史模拟关机记录继续保留原归属，不改写为真实关机。

### R13 / P2：手册图示与 PDF 缺少完整的受维护更新链

位置：`docs/development/manual.zh-CN.md:34`；`scripts/generate-device-manual.py:164`；`.github/workflows/ci.yml:15`。

PDF 生成器消费现有 PNG；补充 AP/时间/会话截图的专用 fixture 只在忽略的 build 目录。UI 或版本更新可通过现有门禁，但提交的图示/PDF 继续旧版。版本检查只覆盖双语 README。

建议：把截图场景/导出程序纳入维护源码，登记场景和来源版本；手册相关路径变化时，用固定字体执行图片/PDF/文本的同步检查。用户要求 PDF 留在 docs，应保留，而不是以删除 PDF 回避同步。

## 公开发布前需核实的授权边界

### R14 / P1（授权待核实，不断言违规）

位置：`vendor/aosl/LICENSE:10`；`THIRD_PARTY_NOTICES.md:9`、`:16`；`docs/dependencies/RTSA_SOURCE.txt`。

AOSL 文件中的 Apache-based 条款还有终端设备部署与竞争用途限制。项目已正确说明整树/最终程序不统一适用 Apache-2.0，但仓库没有可定位到此产品用途的补充授权记录。RTSA 同样依赖外部使用/分发条款，仓库没有独立许可文件可直接完成判定。

建议：由项目维护方确认实际适用的产品授权，登记可公开的条款来源、版本、允许的使用/分发范围和所需获取方式。已有单独授权可能完全覆盖这些用途，本审查不能据文件缺失推定未授权。完整 RTSA 包及其清单继续保留；这是明确的项目依赖要求。

## 影响专业性与简洁性的优化

| 编号 | 问题及证据 | 建议 |
| --- | --- | --- |
| R15 / P3 | `scripts/build_metadata.py:148` 的输入摘要含 SDK/compiler 绝对路径，并在 `build-manifest.py:27` 嵌入二进制。同字节输入放到不同目录会改变产物身份。 | 区分可移植内容身份与本机诊断 provenance。规范化路径后增加一次独立编译比较；现有证据应继续称“归档可重复”，不扩大为完整构建可复现。 |
| R16 / P3 | `scripts/check-dependencies.py:203`、`update-dependencies-lock.py:76` 的活动门禁同时维护初次导入、历史路径迁移和自有代码适配历史。 | 活动 lock 只维护当前 vendor、许可证/补丁及生成资源；历史导入记录保留在不可变档案和 Git 中。保留完整 RTSA 清单及篡改检测。 |
| R17 / P3 | `src/application/CMakeLists.txt:48` 的 install 规则与 `scripts/package-rv1126b.py:15` 的运行包布局分别维护；普通 cmake install 不含完整运行入口、manifest 和许可布局。 | 明确一个受支持的制品 staging 契约。无意支持独立安装时，移除或明确标注部分 install 语义；避免新增一套包装层。 |
| R18 / P3 | `scripts/package-rv1126b.py:188` 给英文包也只复制中文 README。 | 提供对应语言的精简运行说明，并链接完整英文手册；保留一套内容来源/语言选择逻辑。 |
| R19 / P3 | `src/ui/include/mybot_ui/display_status.h:21` 残留无生产宏/调用方的条件空实现；`wifi.h:41` 等公开声明仍挤在一起，部分私有 C 头没有统一 C++ linkage；`lvgl_view.h:12` 仍描述 LVGL lock/FreeRTOS 的移植背景，而当前由单一 UI worker 串行调用。 | 删除无使用方的兼容 stub；公开头补充线程、所有权、阻塞和失败契约，统一声明空行及需要的 extern C。注释描述当前实现和设计原因，不保留过时平台背景。配置宏/仅 static inline 头不机械添加 extern C。 |
| R20 / P3 | `portal_page.h:1` 将人工维护的 HTML/CSS/JS 放进大量 C 转义字符串；`wifi_portal.c:616` 不输出已存在的错误分类，页面 `portal_page.h:132` 把存储/重载故障也提示为检查密码。 | 维护独立 HTML，构建时生成私有 header；无需前端框架。返回不含凭据的错误分类，按连接/保存/提交阶段给出可操作提示。 |
| R21 / P3 | `main.c:293` 同时组织网络、授时、提示音、SDK、输入和 UI；`main.c:365`、`display_runtime.c:182` 用魔法整数传时钟状态；状态字符串/映射分散。网络 ready 检查在 Wi-Fi/controller/network monitor 等处多份实现。 | 将产品启动/配网/退出策略收敛到一个 controller，main 保留装配和退出。用小型 enum/status 快照统一产品状态及 UI 映射；只合并语义相同的网络检查，保留 SDK 事件监测与产品恢复策略各自职责。 |
| R22 / P3 | `githooks/commit-msg:72` 未要求 `!` 对应 BREAKING CHANGE footer；CI 仅验 subject。独立复现不带 footer 的 breaking commit 可通过。 | 完整消息校验要求的 footer；CI 对受检完整 message 使用相同规则。 |
| R23 / P3 | README、getting-started、packaging README、专题指南、双语手册重复维护流程；validation 首页同时含当前状态和大量历史 dirty 构建日志路径。 | 每主题一个权威参考，入口精简；手册保存完整操作步骤但复用受维护片段/配置。验证首页只留最新状态与可定位制品，旧轮次归入 history。公开证据不要只有外部读者无法访问的本地 ignored 日志路径。 |
| R24 / P3 | `SUPPORT.md:16` 没有明确的私密漏洞报告渠道及支持版本政策。 | 为 root 网络应用提供真实可用的私密报告入口与支持范围；不需要复杂合规流程或默认公开敏感细节。 |

## 推荐的简洁架构

保留现有三层和单板目标，不为了目录看起来复杂而增加 framework/core/manager 等层。

```text
application
  main                  配置、资源装配、信号和退出码
  controller            网络/授时/配网/SDK 启停的产品策略
  identity/config       稳定身份与配置解析
       |
       +--> MyBot 公共 API：配对/会话/音频流水线
       +--> platform services：类型化状态与明确生命周期
                  +--> Linux drivers / helpers / persistent storage
                  +--> UI worker --> pure UI renderer

delivery tools          制品身份、设备预检、安装切换和健康确认
vendor                  固定的外部依赖与许可/补丁
```

- 产品策略有一个 owner；SDK 的业务状态继续由 SDK 管，不复制 SDK 状态机。
- 网络/进程/文件副作用有明确 owner，清理结果必须可观察；已有 backend/portal 注入接口可以继续使用。
- 状态分清 supervisor、application、Wi-Fi 子系统，UI 是状态的展示者，不把“进程存在”叫“业务 ready”。
- 交付工具的顺序是验证输入 → 预检目标 → 暂存并验证可执行性 → 备份/停止 → 安装/切换 → 有限健康确认。
- 同一制品只维护一套布局定义和当前依赖身份。用户数据和程序版本继续分开，绑定算法保持兼容。

## 建议实施批次与验收

1. **先修确认的失败路径（R01–R09）。** 每项独立提交，对应最小故障回归。接受标准：不残留 owned 后代，不发布失效 AP，错误 profile/权限/目标前提在停旧业务前拒绝，状态反映真实监督/应用状态，视频失败可识别且能受控恢复。
2. **修正交付版本和操作文档（R10–R13）。** 分配下一补丁版本，统一系统/私有 Wi-Fi 测试说明、用户电源确认与 AP 验证范围，维护截图场景和双语 PDF 一致性。
3. **完成发布授权说明（R14）。** 在公开发布前确认适用条款范围，不改变用户要求的完整 RTSA 依赖。
4. **再做有限结构整理（R15–R24）。** 收敛 controller、状态类型、重复网络判断、活动 lock、制品布局和文档权威来源；保留当前业务/公共 SDK 边界。维护性整理不要与网络行为修复混成一个大提交。

每批完成主机回归和相应制品检查后，再按实际改动做真机验证：AP+STA/单接口、错误密码与辅助进程中断、音频/AEC、视频启停、关机/再次开机、长期稳定性。依赖快照完整、主机门禁通过与真实设备验收分别记录。

## 不应当作为缺陷的问题

- 当前顶层目录名称和 `application/platform/ui/vendor` 划分本身合理。
- 用户要求的完整 RTSA 包、双语 PDF 放在 docs、测试中的模拟后端，本身不是“开源不专业”。
- Ubuntu 24.04 主机依赖和目标 AArch64 SDK 是两类输入；目标需匹配 staging/sysroot 是正常的交叉构建前提。
- 将必需硬件失败保留为失败、保留 unclean 标记、不直接抢占外部网络管理器，是资源安全策略；应改善可操作性，不盲目删除保护。
- 单板设备的部分进程级单例是有意资源约束；不建议为假想多实例需求重写为复杂对象框架。
