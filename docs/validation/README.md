# 当前验证状态

更新日期：2026-11-17。源码版本由根 `CMakeLists.txt` 管理，执行 `python3 scripts/project-version.py` 查看。当前治理候选为 **0.5.3**，本页区分主机、交叉构建和硬件证据；历史轮次见[完整归档](../history/validation-before-0.5.3.zh-CN.md)。

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

## 如何复核

标准命令见[测试指南](../development/testing.zh-CN.md)，目标输入及交付入口见[构建](../user/build.zh-CN.md)和[发布](../development/releasing.zh-CN.md)。手册命令见[维护指南](../development/manual.zh-CN.md)。公共 CI 配置存在不代表本轮已在 GitHub 执行；本页只记录实际观察到的结果。

审查基线与问题分类见[历史审查](../history/review-0.5.2.zh-CN.md)。本地忽略日志只作补充，公开结论由这里的命令、结果摘要及维护的回归测试支撑。旧轮次的版本、哈希和硬件限制保持原归属。
