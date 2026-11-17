# 当前验证状态

本页是项目验证状态的唯一现行入口。更新日期：2026-11-17。当前源码版本以根 `CMakeLists.txt` 为准，执行 `python3 scripts/project-version.py` 查看。历史检查保持其原始版本归属，不自动移用于新二进制。

目标基线：**RV1126B TEST2 RK805B V10**、AArch64 Buildroot 2024.02、Linux 6.1.172、glibc 2.41，物理屏幕 480×640。已记录开发主机为 **Intel macOS** 和 **Ubuntu 24.04.4 / GCC 13.3**，交叉编译使用 Zig 0.15.2；其他宿主及远端 CI 以实际运行证据为准。

| 版本 / 变体 | 类别 | 证据与结果 | 限制 |
| --- | --- | --- | --- |
| 0.1.0，默认语音/UI | 用户真机 | 用户提供 `sha256sum -c SHA256SUMS` 全部 OK、`--version`；随后确认 `./run.sh` 全部流程打通，业务符合预期 | 不是视频或长期专项验收 |
| 0.1.1，默认语音/UI | 用户真机 | 用户升级后确认“测试通过，效果满足需求”，文字清晰度优化满足要求 | 不扩展为可选视频/音质/AEC/长期并发专项 |
| 0.1.1，中英文 UI | 本地主机 | 中文完整 7 项测试及 UBSan 通过，英文真实 renderer 回归通过；10 流程页、23 表情、文字边界及像素粒度检查；字体 `--check` 通过 | 原始 Intel macOS 环境，详情见历史记录 |
| 0.1.1，语音/视频 | 交叉构建与 ABI | 两个 AArch64 程序构建；9 / 11 项依赖检查 `errors=[]`，加载器和 RPATH 符合预期 | ABI/构建不证明相机和物理音视频质量 |
| 0.2.0，中文/英文/UBSan | 本地主机 | Intel macOS，三套 preset 均 13/13；每套包含 67 项 Python 工具用例、ALSA/网络故障清理与线程退出测试 | 公共 Linux CI 尚未在远端执行 |
| 0.2.0，中英文 UI | 基线回归 | 每种语言 34 帧 SHA256 与 0.1.1 逐字节一致；固定字库再生成 `--check` 通过 | 物理显示仍需候选包真机回归 |
| 0.2.0，zh-CN/en-US × voice/video | 交叉构建与 ABI | 四套 AArch64 构建及 manifest 生成通过；语音 9、视频 11 项依赖，全部 `errors=[]` | 未在设备运行 |
| 0.2.0，依赖/工程/制品 | 本地检查 | host/target doctor、锁文件与更新幂等、格式/SPDX/语法/文档链接通过；中英文预检包生成；两独立目录的中文归档 SHA256 相同 | 归档可复现不等于编译字节可复现；开发包标明 dirty 源码 |
| 0.2.0，治理候选 | 真机 | 尚未执行 | 不能继承 0.1.1 硬件结论 |
| 0.2.1，第三方目录 | 迁移与裁剪 | AOSL 的 330 文件先原样平移并校验；MyBot/AOSL/LVGL 分别保留 110/124/719 文件；262 项显式省略；运行源码/头文件/构建模板与迁移前一致 | 文档有布局适配；原始导入清单未改写 |
| 0.2.1，中文/英文/UBSan | 全新主机目录 | 三套配置均 13/13；每套含 93 项工具用例；每种语言 34 帧与 0.1.1 UI 基线一致 | Intel macOS，本轮未执行远端 CI |
| 0.2.1，zh-CN/en-US × voice/video | 四套 AArch64 净构建 | 构建和 manifest 全部通过；语音 9 / 视频 11 项依赖，均 `errors=[]`；每套唯一 AOSL target 的 49 源文件来自平级目录 | 未在设备执行 |
| 0.2.1，依赖与交付 | 本地检查 | 独立快照/资源归属、623 项必需输入、迁移/省略校验及更新幂等通过；旧缓存改用新路径，旧 manifest 被拒绝；中英文包 50 项校验及中文重复归档通过 | 新候选包仍需真机回归 |
| 0.2.1，完整 RTSA 内置 | 原包与依赖检查 | 官方归档 SHA256 匹配；99 文件逐字节及执行权限比对通过；四项独立快照、627 项必需输入及锁刷新零差异通过 | 保留原始厂商元数据与全部示例；未更换库版本 |
| 0.2.1，RTSA 内置后回归 | 主机与交叉构建 | 中文/英文/UBSan 各 13/13，每套含 118 项工具测试；四套 AArch64 构建和 manifest 通过，ABI 依赖语音 9 / 视频 11，均 `errors=[]` | 仅本地 Intel macOS，未访问设备或运行远端 CI |
| 0.2.1，RTSA 包与提交规范 | 本地检查 | 中英文包各 53 项校验，RTSA 原始说明/包身份随包保留，中文重复归档一致；提交 hook 正反例及 CI 四种事件范围检查通过 | 开发制品，不代表真机验收或正式发布 |
| 0.2.2，`src/` 与 `vendor/` | 目录与依赖迁移 | 1104 个文件完成移动；四项依赖共 1052 文件内容和执行权限保持一致，完整 RTSA 仍为 99 文件；原始导入清单、262 项省略记录及字体输入/输出不变 | 构建规则、工具和当前路径记录已同步 |
| 0.2.2，中文/英文/UBSan | 全新主机目录 | 三套各 13/13，每套含 130 项工具测试；双语 UI 基线及固定字体生成 `--check` 通过 | Intel macOS，未执行远端 CI |
| 0.2.2，zh-CN/en-US × voice/video | 四套 AArch64 净构建 | 构建与 manifest 通过；ABI 语音 9 / 视频 11 项依赖，均 `errors=[]`；旧 AOSL/RTSA 路径被覆盖，每套唯一 AOSL target 的 49 项源码来自 `vendor/aosl/` | 未在设备执行 |
| 0.2.2，迁移工具与交付 | 本地检查 | 目录迁移负向测试、锁刷新零差异、host/target doctor、质量检查通过；双语包各 53 项校验，中文两次归档字节一致 | 候选包仍需真机回归 |
| 0.3.0，中文/英文/UBSan | 主机逻辑与协议 | 三套各 16/16，包含 130 项工具测试；新增 Wi-Fi 后端、真实 loopback HTTP/DNS 和控制器测试；原双语 34 帧 UI 基线保持一致，新增 AP 指引文字/字形/布局检查通过 | Intel macOS；未操作目标设备或验证射频 |
| 0.3.0，zh-CN/en-US × voice/video | 四套 AArch64 构建 | 构建与 manifest 通过；语音 9 / 视频 11 项 ABI 依赖，均 `errors=[]`；固定 SDK、RTSA、字体未变 | 构建不证明目标系统具备 AP 工具和驱动能力 |
| 0.3.0，AP 运行包 | 本地制品检查 | 双语包各 54 项校验通过；DHCP hook 内容一致且归档权限为 0755；两个独立目录的中文归档字节一致 | `iw`、hostapd、dnsmasq、supplicant、udhcpc 仍由目标系统提供 |
| 0.3.1，中文/英文/UBSan | 主机逻辑 | 三套各 17/17，包含 130 项工具测试；新增音量恢复、私有存储边界、并发读取和实际 ALSA 增益/部分写入重试检查；双语 UI 基线通过 | Intel macOS；未测实际响度或声学效果 |
| 0.3.1，zh-CN/en-US × voice/video | 四套 AArch64 构建 | 构建与 manifest 通过；语音 9 / 视频 11 项 ABI 依赖，均 `errors=[]`；依赖与质量检查通过 | 不代表真机播放、音量或 AEC 验收 |
| 0.3.2，中文/英文/UBSan | 主机逻辑 | 三套各 18/18，包含 130 项工具测试；新增双语提示、短写/无进度/取消/失败清理及 ALSA 排空边界；双语 UI 基线通过 | Intel macOS；未测实际播报完整性和声卡交接 |
| 0.3.2，zh-CN/en-US × voice/video | 四套 AArch64 构建 | 构建与 manifest 通过；语音 9 / 视频 11 项 ABI 依赖，均 `errors=[]`；依赖与质量检查通过 | AP 广播、提示音和业务恢复仍待设备验证 |
| 0.3.3，中文/英文/UBSan | 主机逻辑 | 三套各 19/19，包含 133 项工具测试；新增网络/时钟双门槛、超时/断网重置、外部服务优先、假 helper 进程组回收与 stdin 隔离；新增等待/失败 UI 检查，旧双语基线通过 | Intel macOS；没有修改宿主时钟或运行真实授时 |
| 0.3.3，zh-CN/en-US × voice/video | 四套 AArch64 构建 | 构建与 manifest 通过；语音 9 / 视频 11 项 ABI 依赖，均 `errors=[]`；资源锁、质量和打包工具检查通过 | 设备时间服务、BusyBox applet、权限、DNS 和 UDP 123 需实际验证 |
| 0.4.1，中文/英文/UBSan | 主机逻辑与工具 | 三套各 21/21，包含 163 项工具用例；覆盖系统 Wi-Fi 事务、租约事件、恢复前 SDK 停止、监督与私有健康状态、AP 频段/并发选择；双语 UI 基线通过 | Intel macOS；不代替物理音视频和手机验证 |
| 0.4.1，zh-CN/en-US × voice/video | 四套 AArch64 构建与制品 | 全部构建和 manifest 通过；语音 9、视频 11、独立监督程序 2 项 ABI 依赖，均 `errors=[]`；双语归档及部署校验通过 | 设备仅运行中文语音配置，未执行英文/视频业务 |
| 0.4.1，中文语音 | 授权真机部署与冒烟 | 版本化部署、身份匹配、绑定与 Wi-Fi 配置字节保留、服务启停/重启、单一业务进程及 `state=ready` 通过；5 GHz STA 与 2.4 GHz AP 共存，HTTP 200、DNS 应答、DHCP 监听共存及退出清理通过 | 未重启设备；手机 DHCP/提交配网、实际对话/声学、断网恢复与长期运行待用户验收 |
| 0.4.1，手机配网后恢复 | 用户真机与故障定位 | 手机报告完成后，设备卡在等待网络；现场 WLAN 已恢复关联、地址与默认路由，Wi-Fi 状态却停在 `RECOVERY_REQUIRED` | 已由 0.4.3 修复并执行设备 HTTP 完整流程回归；手机复测待用户执行 |
| 0.4.3，中文/英文/UBSan | 主机与交叉构建 | 三套各 22/22，每套含 163 项工具测试；四套目标构建通过，ABI 依赖语音 9、视频 11、监督程序 2，均 `errors=[]` | 物理手机、音视频和重启不由构建结果证明 |
| 0.4.3，中文语音 | 设备配网 HTTP 完整流程 | 实际提交已保存网络、持久化重载、目标关联/租约确认、成功响应、热点关闭、SDK 自动恢复 `ready`；确认成功耗时约 5 秒；退出清理、绑定与网络记录保留通过 | 客户端在设备本地执行，未模拟手机射频、DHCP 或触摸输入；仍需用户手机复测 |
| 0.4.4，Wi-Fi 纯 C | macOS 与 Ubuntu 24.04 | 两种宿主的中文、英文、UBSan 各 23/23，每套含 163 项工具测试；Ubuntu ASan + UBSan 的四项 Wi-Fi 与网络启动测试 5/5 | Ubuntu 使用 GCC 13.3；不代表目标设备启用 sanitizer |
| 0.4.4，zh-CN/en-US × voice/video | 交叉构建与语言检查 | 四套构建通过，Wi-Fi 的 9 个源文件均以 `-std=c99` 编译；ABI 语音 9、视频 11、监督程序 2 项，均无错误；7 个头文件的 C99/C++17 独立包含及 C/C++ 调用链接检查通过 | Wi-Fi 模块为纯 C；项目其他模块可以继续使用 C++ |
| 0.4.4，中文语音 | 设备部署与配网回归 | 本地 HTTP 客户端约 5.5 秒确认配网成功，热点关闭后 SDK 恢复 `ready`；绑定和 Wi-Fi 配置字节保留，临时 AP 清理通过 | 手机实际无线接入、完整语音与重启仍待用户验收 |
| 0.5.1，设置与产品输入 | macOS 与 Ubuntu 24.04 | 两种宿主的中文、英文、UBSan 各 24/24，每套含 163 项工具测试；Ubuntu ASan + UBSan 与泄漏检测专项 6/6，覆盖输入、UI、监督器和关机客户端；双语 38 帧基线通过 | 主机测试不证明物理触摸或真实断电 |
| 0.5.1，zh-CN/en-US × voice/video | 四套 AArch64 构建与制品 | 四套构建、manifest 和 ABI 均通过，依赖语音 9、视频 11、监督器 2 项且无错误；双语包各 63 项 SHA256 校验通过 | 设备运行中文语音配置，英文/视频业务仍待验收 |
| 0.5.1，中文语音 | 设备菜单与配网回归 | 实际 evdev 注入验证首次同点点击、连续同轴点击左上角返回、关机取消及菜单不触发会话；设置进入 AP、本地 HTTP 完整配网约 11.8 秒确认成功、恢复 ready | 触摸由事件注入，HTTP 客户端在设备本地；仍需用户物理点按和手机无线验证 |
| 0.5.1，计划关机 | 设备隔离监督器 | 使用正式二进制及只写标记的假关机命令，SDK READY 和 SDK 启动前 AP 两种场景均正常清理并退出；无 unclean 标记、测试监督器或残留 AP | 未执行真实 poweroff、重启或验证重新上电 |
| 0.5.1，文档与工具修正 | macOS 与 Ubuntu 24.04 | 中文、英文、UBSan 各 24/24，每套含 181 项工具测试；Ubuntu 按文档安装固定格式工具后自动选择 11，显式使用系统 18 在格式化前被拒绝；版本同步与示例环境回归通过 | Ubuntu 使用隔离源码副本，本轮未连接或更新设备 |
| 0.5.1，文档与工具修正 | 交叉构建与运行包 | 四套目标构建与 ABI 通过，双语包各 63 项校验通过，语言注释和无版本 helper 路径正确；依赖、质量与锁更新幂等检查通过 | 新包与此前设备部署包的哈希不同，不继承新包真机验收 |
| 0.5.1，设置页独占手动配网 | macOS / Ubuntu / 交叉构建 | 两种宿主中文、英文、UBSan 最终各 24/24，含 181 项工具测试；Ubuntu ASan + UBSan 输入及配置专项 3/3；四套目标构建和双语包检查通过 | 本轮未更新设备，物理长按不进入 AP 与设置页配网待新包真机验收；macOS 英文首轮一项既有计时断言失败，后续复核通过，详情见下文 |
| 0.5.2，设置页独占手动配网 | 授权设备更新与真机回归 | 安装独立版本并保留 0.5.1 回退；4.2 秒触屏/Menu 合成事件不配网、不切换会话，设置短按进入 AP；当前网络提交保存、关闭热点及 SDK 自动恢复 ready 通过；绑定和网络配置字节保留 | 触屏/按键使用 evdev 注入，HTTP 客户端位于设备本地；未代替手机无线、DHCP/captive browser 或物理手感测试 |
| 0.5.2，zh-CN/en-US × voice/video | 构建与版本元数据检查 | 四套目标构建、ABI 与双语包 63 项校验通过；版本更新后 macOS 中文、UBSan 输入/配置专项各 3/3 | 功能实现的两宿主完整回归见上一批次；本轮仅中文语音配置在设备运行 |
| 公共 CI / 其他宿主组合 | CI / 环境 | 以仓库对应提交实际 workflow 运行记录为准 | 配置存在不等于已运行通过 |

## 保留的基线制品

0.1.1 中文全配置包：`mybot-rv1126b-0.1.1-aarch64.tar.gz`，SHA256：

```text
b028d775d3e91858c54f4b1ac336da4f7eeea41cfe618acfb9ade90e69e2abf7
```

0.1.0 包 SHA256：

```text
de26c8978e939e1c41dbdbdf73bd624809e90552ae4ff71701920043f65d9716
```

这些是已记录本地交付制品的身份，不表示远端发布。0.1.1 源码提交基线为 `dev@0589cde`；治理方案以该 revision 审查。完整旧检查见[0.1 历史验证](../history/validation-0.1.zh-CN.md)，原生渲染依据见[0.1.1 字体记录](../history/ui-native-fonts.zh-CN.md)。

## 尚未完成的硬件专项

- 当前候选的 UI 方向/文字/动画、语音、输入/音量与重启自启动；音量需检查静音、应用重启与手动配网后的恢复、云端控制和 AEC。代理已验证服务启停/重启及绑定字节保留，实际业务由用户验收。
- 系统时间有效/无效、默认等待超时、已有时间服务和可选 helper、断网取消及 SDK 启动门槛；见[时间验证指南](../user/time-sync.zh-CN.md#排查与设备验证)。2026 年门槛仅排除明显无效时间，不能证明精确 NTP 同步。
- 手机发现热点、DHCP/captive browser、提交配网、错误密码重试、凭据重启复用、会话中手动配网与单接口驱动路径；步骤见 [AP 验证指南](../user/wifi-provisioning.zh-CN.md#设备验证)。代理已验证本板 AP+STA 多信道、门户/DNS 与退出清理。
- 视频首帧/云端接收、方向/颜色、V4L2/MPP 启停与语音/UI并发。
- 设置页确认后的实际系统关机、PMIC 断电范围及重新上电方式；代理仅使用模拟关机命令验证清理流程，未执行真实断电。
- 音质/增益和 AEC 测量、物理按键位置、网络恢复、显示恢复和长时间稳定性。

初始设备检查由代理只读执行；0.1.0/0.1.1 部署及真机测试由用户完成。早期工程治理仅在主机进行；2026-11-16 用户明确授权后，代理执行了下述设备替换与冒烟，没有重启设备。设备状态不能根据历史快照推断。

新增证据应记录版本/revision、包 SHA256、profile/language、板型/系统、命令、执行者、结果及未覆盖项。指南见[测试](../development/testing.zh-CN.md)与[上手](../user/getting-started.zh-CN.md)。

## 0.2.0 本轮复现与证据

本地检查使用 Python 3.10.19、CMake 4.2.0、AppleClang 17、clang-format 11 和 Zig 0.15.2。macOS 主机测试显式提供 libdrm 开发头文件与 OpenSSL；目标使用显式配置并核对的 RTSA、staging 与 runtime manifest。没有借主机测试隐式导入目标 SDK。

- 主机结果：`build/host-{zh,en,ubsan}/Testing/Temporary/LastTest.log`。
- 目标证据：四个 `build/rv1126b-{voice,video}-{zh,en}/abi-report.json` 和 `build-manifest.json`；候选包保留归一化报告。
- 工具用例：34 项依赖/锁更新、27 项制品身份/归档、6 项构建参数解析。
- 构建配置隔离额外使用 `-DMYBOT_ENABLE_VIDEO:BOOL=TRUE` 验证英文视频目录选择。
- 未修改 SDK/LVGL 快照，原 0.1.0/0.1.1 归档保留且哈希复核一致。

开发候选包位于 `dist/mybot-rv1126b-0.2.0-aarch64.tar.gz` 和 `dist/mybot-rv1126b-0.2.0-en-US-aarch64.tar.gz`；确切归档 SHA256 以相邻 `.sha256` 为准，包内文件以 `SHA256SUMS` 核对。未创建远端发布或执行设备部署。

## 0.2.1 本轮复现与证据

净构建使用 `build/thirdparty-flat/host-{zh,en,ubsan}` 和 `build/thirdparty-flat/rv1126b-{voice,video}-{zh,en}`。主机日志位于各目录的 `Testing/Temporary/LastTest.log`；目标目录保留 ABI、构建 manifest 和 CMake File API codemodel。

- 工具用例：60 项依赖/迁移/省略/归属保护，27 项制品检查，6 项构建参数解析。
- CMake codemodel 核对每套配置恰好一个 `aosl`，其全部源码使用 `third_party/aosl/`，include 不再指向旧目录。
- 原有目标构建目录重新 configure 后，缓存 `AOSL_DIR` 正确覆盖到新位置；验收产物来自全新目录。
- 必需输入保护涵盖实际源码/头文件闭包、配置模板、PCM 提示音及依赖许可。`aosl/test/aosl_test.c` 和 SDK 的 `docs/Doxyfile.in` 保留。
- 包内增加 LVGL COPYRIGHTS，AOSL 许可证字节与平铺前一致；重复打包只证明相同构建输入的归档字节一致。

候选包为 `dist/mybot-rv1126b-0.2.1-aarch64.tar.gz` 和 `dist/mybot-rv1126b-0.2.1-en-US-aarch64.tar.gz`，确切 SHA256 见相邻 `.sha256`。本轮未访问设备、未提交或发布；0.1.0、0.1.1 和 0.2.0 交付包保留。

## 0.2.1 完整 RTSA 内置复现与证据

本轮使用独立目录 `build/rtsa-vendored/host-{zh,en,ubsan}` 和 `build/rtsa-vendored/rv1126b-{voice,video}-{zh,en}`，日志位于同级 `.log` 与各主机目录的 `Testing/Temporary/LastTest.log`，目标目录保留 ABI 和构建 manifest。环境仍为 Python 3.10.19、CMake 4.2.0、AppleClang 17 与 Zig 0.15.2。

- 原始官方归档 SHA256 为 `850834b5b925ffb7fb447e97cf9ec5dc33fe0a0f68108e1a4954ee7590870fbe`，全部 99 文件与 `third_party/rtsa/` 一致。原始导入历史清单保持不变。
- 工具用例为 67 项依赖/配置、16 项完整 RTSA 快照、27 项制品、8 项构建参数/路径测试；双语 UI 基线通过。
- 本地 SDK 重新配置时未传 `--rtsa-sdk`；target doctor（含视频头检查）通过。四套目标 configure 特意传入过期 SDK/库路径，最终缓存和构建配置均指向仓库内 AArch64 库。
- 中英文预检包各 53 项 SHA256 通过，目标库及厂商双语 README、PACKAGE_INFO 与仓库原文件一致；两独立目录生成的中文归档字节相同。
- 提交 hook 的 8 个正反例通过；CI 提交范围分别模拟 PR、普通 push、新分支 push 和手动触发，检查通过。本地已设置 `core.hooksPath=githooks` 和 `commit.template=.gitmessage`，未修改历史提交。

本轮候选制品单独存放于 `dist/rtsa-vendored/`，保留此前同版本治理包；确切 SHA256 见相邻 `.sha256`。未执行设备部署、真机测试、Git 提交或远端发布。

## 0.2.2 源码目录重构复现与证据

本轮使用 `build/source-layout/host-{zh,en,ubsan}` 与 `build/source-layout/rv1126b-{voice,video}-{zh,en}` 全新目录。主机日志位于各目录的 `Testing/Temporary/LastTest.log`，同级 `.log` 保存构建过程；目标目录保存 ABI、构建 manifest 与 CMake File API codemodel。环境仍为 Intel macOS、Python 3.10.19、CMake 4.2.0、AppleClang 17、Zig 0.15.2。

- 自维护模块迁至 `src/application/`、`src/platform/`、`src/ui/`，依赖迁至 `vendor/`。运行源码、头文件和生成字库逐字节保持不变，CMake target 与公共头命名空间保留。
- 依赖锁的三条 `path_migrations` 记录受锁管理的目录迁移，627 项必需输入与当前快照路径同步。原始导入清单 SHA256 仍为 `279e45663582bc67f36711188a82acc894ec6e93fabeb765a10849ec69df5c25`；历史键、原始哈希与省略记录保持不变。
- 每套主机检查包含 77 项依赖/配置/迁移、18 项完整 RTSA 快照、27 项制品及 8 项构建参数/路径测试。迁移测试覆盖快照增删改、旧目录复现、目标缺失、重叠/循环、说明缺失及重复更新幂等。
- 目标配置特意传入迁移前 AOSL/RTSA 路径，最终缓存、构建配置和 CMake codemodel 均使用 `vendor/`；四套配置的运行依赖闭包检查全部通过。
- 重新配置本地 SDK 后，host doctor 与含视频头检查的 target doctor 均通过。固定字库再生成检查、依赖完整性、质量与文档链接检查通过；锁更新预览 `changes=[]`。
- 双语预检包的 53 项 SHA256 全部通过，目标库及厂商说明与 `vendor/rtsa/` 原文件一致；中文归档在两个独立输出目录中字节相同。

候选制品单独存放于 `dist/source-layout/`，文件名为 `mybot-rv1126b-0.2.2-aarch64.tar.gz` 与 `mybot-rv1126b-0.2.2-en-US-aarch64.tar.gz`，确切 SHA256 见相邻 `.sha256`。以上检查针对提交前工作区，制品保留开发状态身份；未执行设备部署、真机测试或远端发布，旧交付包保持原样。

## 0.3.0 AP 配网逻辑复现与证据

用户本轮明确设备已关机，仅要求逻辑实现，由用户后续执行设备测试。实现与检查均在主机进行，没有运行设备程序或修改设备文件。AP 的无线驱动、系统工具、射频并发、DHCP 与手机兼容性仍待确认，不将逻辑或交叉构建结果写成真机通过。

- 主机目录为 `build/ap-provision/host-{zh,en,ubsan}`；完整日志保存在各目录的 `Testing/Temporary/LastTest.log`，构建日志位于同级 `host-*-build*.log`。环境仍为 Intel macOS、Python 3.10.19、CMake 4.2.0、AppleClang 17 和 Zig 0.15.2。
- `wifi-backend` 验证 PBKDF2 标准向量、UTF-8/长度/控制字符、扫描解析、0600 原子存储及符号链接拒绝、shell 字符按普通参数传递、命令超时/取消/输出上限/回收，以及同一 SSID/BSSID 的已确认租约复用和更换网络时拒绝旧租约。
- `wifi-portal` 在 loopback 高端口执行 HTTP/DNS 协议检查，覆盖 captive 重定向、JSON/Unicode、Host/Origin/CSRF、错误帧、异步连接/失败/重试、保存操作 busy/SSID 身份约束、DNS 查询边界及关闭。页面 JavaScript 语法和异步保存的实际函数行为另行检查通过。
- `wifi-service` 使用模拟后端与门户，覆盖已有网络保留、禁用模式、AP 启动失败回收、保存网络时限、两次试连、实际路由门槛、单接口恢复、保存失败保持 AP、最多 10 项及默认/删除、索引过期拒绝、手动请求与 ONLINE/关闭竞争、退出响应延迟和取消。
- 输入测试覆盖 3 秒触摸请求的单次触发、释放与丢帧清理，终端 `w` 只生成产品请求。屏幕线程复制真实热点状态；只有 AP/门户准备好才显示热点与地址，原先等待网络页仍保持旧基线。
- 目标目录为 `build/ap-provision/rv1126b-{voice,video}-{zh,en}`，保留 ABI 与构建 manifest；全部固定依赖、原始导入历史与生成字库身份不变。依赖、质量、锁更新幂等和 target doctor 检查通过。
- 双语预检包各 54 项 SHA256 通过，`assets/wifi/udhcpc.script` 与源码一致；直接目录和 tar 归档均为可执行权限，中文两次归档相同。

最终候选包位于 `dist/ap-provision/`，名称为 `mybot-rv1126b-0.3.0-aarch64.tar.gz` 与 `mybot-rv1126b-0.3.0-en-US-aarch64.tar.gz`。SHA256 见相邻 `.sha256`；制品绑定提交前开发工作区。未创建远端发布或自动部署，之前交付包保留。

## 0.3.1 音量持久化复现与证据

主机测试使用 `build/batch1-volume/host-{zh,en,ubsan}`，日志为各目录的 `Testing/Temporary/LastTest.log`。四套目标配置位于 `build/batch1-volume/rv1126b-{voice,video}-{zh,en}`，各自保留 ABI 与构建 manifest。环境仍为 Intel macOS、Python 3.10.19、CMake 4.2.0、AppleClang 17 与 Zig 0.15.2。

- 音量服务验证默认 100、0/50/100 PCM 边界、SDK 重建与新进程恢复、损坏文件、权限/链接/FIFO 拒绝、保存失败保留当前增益，以及持久化期间的并发读取。
- ALSA 集成验证实际 50% 输出、静音、部分写入和恢复、截止后改变音量并重试原始后缀，检查输入 PCM 不变及第二次分配失败的清理。
- SDK 音量适配器仅在产品存储恢复后注册，SDK 停启保留状态；最终输出缩放一次，音频热路径不执行持久化或等待音量锁。云 AEC 参考保留设备音量前信号，实际声学效果待测。
- 固定 SDK、RTSA 和字体没有修改；更新当前 ALSA 适配结果哈希，原始导入清单保持不变，锁刷新预览为 `changes=[]`。

本轮未访问设备；设备验证由用户后续执行。构建和主机测试不能证明物理音量、云端控制或 AEC 效果。

## 0.3.2 AP 语音提示复现与证据

主机目录为 `build/batch2-prompt/host-{zh,en,ubsan}`，各套 `Testing/Temporary/LastTest.log` 保存 18 项结果。目标目录为 `build/batch2-prompt/rv1126b-{voice,video}-{zh,en}`，保留 ABI、源码身份和构建 manifest。运行环境与 0.3.1 相同。

- 独立提示音测试验证双语 S16LE 数据、短写/零写入后的原后缀、EOF 尾帧、无进度超时、主动取消、重复启动拒绝与停止后重播；同时覆盖无效资源、PCM 初始化/启动/写入/排空/停止失败及资源回收。
- ALSA 排空测试覆盖 `EAGAIN` 后成功、截止超时、错误和等待期间的停止；成功后禁止继续写入，须重新 start。提示音正常 EOF 最多排空 100 ms，取消直接停止。
- 主循环按配网会话锁存首次真实 AP active；错误密码造成的单接口热点重建不重播。离开 AP、SDK 启动和退出清理前均停止并等待 worker，声卡不会由提示音与 SDK 同时占用。
- 复用原有双语 `wificonfig.pcm` 和播放增益，未修改依赖快照或字体。依赖、质量和锁更新幂等检查通过，原始导入历史保持不变。

未访问或部署设备。实际双语播报、尾音、静音、取消延迟、声卡交接与配网后业务恢复按 [AP 验证步骤](../user/wifi-provisioning.zh-CN.md#设备验证)由用户验证。

## 0.3.3 启动时间保护复现与证据

主机目录为 `build/batch3-time/host-{zh,en,ubsan}`，每套 `Testing/Temporary/LastTest.log` 保存 19 项结果，含 133 项工具用例。目标目录为 `build/batch3-time/rv1126b-{voice,video}-{zh,en}`，保留 ABI、源码身份与构建 manifest。环境与前两项相同。

- 时间服务通过注入的实时时钟、单调时钟和进程后端验证网络/时钟同时就绪、AP/离线期间不计时、断网重置窗口、系统服务优先、超时与 helper 退出后时间仍无效的失败路径。
- 使用真实的临时假 helper 验证正在运行和已退出 leader 的进程组清理、PID 保留与回收、禁止对 0/1/自身/非子进程发信号，以及标准输入重定向后不消费前台管道。没有运行真实 NTP、设置系统日期或改变网络。
- UI 检查等待/失败标签、字形与布局、表情覆盖抑制、清除后帧哈希恢复及线程状态复制；原有每语言 34 帧基线保持一致，无字体或依赖快照改动。
- BusyBox wrapper 的主机名、IPv4、参数边界用假 BusyBox 验证；打包测试检查两个可执行资源在目录及 tar 中的权限和可复现性。可选脚本随包保留，但默认不启用。
- 当前 UI 适配哈希与可执行脚本输入已登记，依赖检查与锁更新幂等通过；原始导入清单保持不变。

前三项均在主机实施，未访问设备或修改设备文件。候选包与相邻 `.sha256` 保存在各次独立 `dist/batch1-volume/`、`dist/batch2-prompt/` 和 `dist/batch3-time/` 目录；源码提交后的干净构建包另行生成，具体身份以包内 manifest 为准。尚未执行真实时间同步、无线配网、提示音、响度或 AEC 的硬件验收。

## 0.4.1 设备替换部署与冒烟证据

2026-11-16，用户授权修改设备配置并部署新版，由代理通过 USB ADB 执行。制品来自 `dev@8bbbc84` 上的未提交工作区，manifest 保留开发状态和当时的源码摘要；不作为干净提交的正式发布。主机环境仍为 Intel macOS、Python 3.10.19、CMake 4.2.0、Zig 0.15.2。没有向主机导出绑定 token、Wi-Fi 密钥或设备私有数据备份。

- 主机目录为 `build/production-rollout/host-{zh,en,ubsan}`，三套均 21/21；每套包含 163 项工具测试，日志位于各目录的 `Testing/Temporary/LastTest.log`。目标四套目录为 `build/production-rollout/rv1126b-{voice,video}-{zh,en}`，ABI 和独立监督程序报告均无错误。质量、依赖及锁更新幂等检查通过。
- 额外在本板运行 AArch64 `test-supervisor`，验证子进程身份、独立进程组/后代回收及内核缺少 `/proc/PID/task/PID/children` 时的 `/proc` 回退路径；仅使用测试创建的进程和临时文件，没有停止系统网络进程。
- 先部署 0.4.0 冒烟时发现 5 GHz STA 被旧 AP 信道保护拒绝，修复为 0.4.1。读取本无线 PHY 的有效组合确认支持 managed+AP 多信道后，保持 STA 信道 149（5745 MHz），在临时 `mbap*` 接口开启 2.4 GHz 信道 1（2412 MHz）的 AP。`hostapd` 状态为 ENABLED；匿名健康记录为 `state=ap`。
- 通过 `--wifi-provision --run-seconds 30 --input-device none --touch-device none` 执行有界 AP 冒烟；`wget -T 2` 访问 `http://192.168.4.1/` 返回 HTTP 200，DNS 查询 `mybot.invalid` 返回 `192.168.4.1`。门户 HTTP/DNS 与 AP DHCP 监听正常，同时保留系统 USB dnsmasq。未连接手机，不将 DHCP 监听解释为手机实际取得租约。
- AP 正常退出后临时接口和 hostapd 均已回收，原 WLAN 默认路由保留。部署前后的 `/etc/wpa_supplicant.conf` 与 `device_auth` 在设备内比较完全一致；仅将原 Wi-Fi 文件权限收紧为 0600。系统模式以该 Wi-Fi 配置为主记录，安装 dhcpcd hook 并触发原生 reconfigure，生成 0600 租约事件，没有另建私有 Wi-Fi 主记录。
- 使用 `scripts/deploy-rv1126b.py deploy ... --execute` 校验、备份与安装。首次完整备份保存在设备内 `/userdata/mybot/backups/20261116-112814/original.tar`（0600）；另有每次部署的私有备份和主机脱敏回执。部署工具检查平台和现有身份匹配，未重置绑定。
- 当前 `/userdata/mybot/current` 指向 `releases/mybot-rv1126b-0.4.1-aarch64`；`previous` 指向已验收的 `releases/mybot-rv1126b-0.1.1-aarch64`。0.4.0 仅保留为诊断版本，不作为默认回退。原预装服务已禁用并移出开机脚本匹配范围；旧程序文件保留为非活动恢复材料。
- `/etc/init.d/S99mybot start|stop|restart|status` 已实测；最终状态为 `running`、应用 `version=0.4.1` / `state=ready`，只有一个新版业务进程和一个独立监督程序，没有 `unclean` 标记。实际程序来自版本目录，工作目录仍为 `/userdata/mybot-rv1126b`，资源绑定物理 release，不随链接切换漂移。开机脚本已安装启用，但未执行设备重启。

本轮归档位于 `dist/production-rollout/`：

| 归档 | SHA256 |
| --- | --- |
| `mybot-rv1126b-0.4.1-aarch64.tar.gz` | `89abd4f08cf98519154e73d1e5a8c25fca86475ae557f4205adc5a5153ea6bbc` |
| `mybot-rv1126b-0.4.1-en-US-aarch64.tar.gz` | `2dd2e89f754ffe5ba6bbbbb3d83d04aea0b7695d7e57019f650b9039c5faf8f7` |

交付前的硬件记录只证明上述部署和冒烟。重启自启动、手机完整配网与错误密码重试、触摸/按键、实际语音与提示音、音量恢复、断网恢复、AEC、视频及长期稳定性仍由用户后续验证；本轮没有发起对话、运行真实授时 helper 或重启设备。部署操作与回退方法见[设备替换指南](../user/deployment.zh-CN.md)。

## 0.4.3 配网后恢复修复与证据

2026-11-16，用户报告 0.4.1 长按进入配网、手机显示完成后，设备停在等待网络。代理通过授权 ADB 检查：WLAN 为 COMPLETED 且已有地址和默认路由，应用健康为 `waiting_network`，最后的 Wi-Fi 状态为 `RECOVERY_REQUIRED`。故障是配网完成后的路由波动触发恢复请求，而 SDK 尚未启动的等待循环没有确认该请求；同时发现系统配置重载时控制套接字可能因 `/var/run` 改写为 `/run` 而失效。

- 应用新增网络启动门控，在 SDK 已停止、AP 已关闭时确认恢复请求；重复快照不重复发起恢复。新增集成回归使用真实 Wi-Fi Service 复现 AP 关闭导致路由丢失、路由已恢复但状态仍等待、应用确认后重连成功的完整状态链。
- 保留原控制接口文本，并在重配置导致 Unix datagram 端点替换时重新连接；真实 Unix socket 测试覆盖重建、回复丢失与明确拒绝。手机成功状态现在必须等待保存后的目标网络关联、有效租约和路由稳定。
- 中间候选 0.4.2 的设备 HTTP 测试发现，重载会短暂选择其他已保存网络；仅观察 15 秒不能可靠恢复目标。0.4.3 对此增加一次受控重选择，并在目标关联后缺少租约证明时，向现有 dhcpcd 请求一次有界续租；确认预算为 30 秒，期间手机保持连接中。没有启动竞争的 supplicant/DHCP 客户端。
- 0.4.3 主机目录仍为 `build/production-rollout/host-{zh,en,ubsan}`，三套各 22/22（每套含 163 项工具测试）。四套 `rv1126b-{voice,video}-{zh,en}` 构建和 ABI 校验通过；质量、依赖与锁更新幂等检查通过。日志名称为同目录下 `*-043-build.log`、`*-043-ctest.log` 及各套 `Testing/Temporary/LastTest.log`。
- 通过部署工具安装 0.4.3，保留设备内备份 `/userdata/mybot/backups/20261116-production-043/original.tar.gz`。在设备本地运行临时 HTTP 客户端，从设备配置读取首选网络，向真实门户提交、轮询成功并请求退出；凭据始终留在设备内存和本地配网请求中，没有导出、放入进程参数或打印。
- 本次客户端收到提交 202，约 5011 ms 后得到 `connected`，退出请求 202；应用状态顺序为 AP → CONNECTING → VERIFYING → ONLINE，随后系统时间就绪、SDK 启动到 READY，健康为 `version=0.4.3` / `state=ready`。未再发生重载失败或永久等待。
- 正常结束测试应用后，AP 接口与 hostapd 均已回收；设备内比较确认 `device_auth` 与首次备份一致，Wi-Fi 配置与本次部署前备份一致。已重新启动常驻服务，默认回退链接仍为已验收的 0.1.1；0.4.1/0.4.2 只保留作诊断，不作为默认回退。

本轮中文归档 `mybot-rv1126b-0.4.3-aarch64.tar.gz` SHA256 为 `34b464d93a2e0c2ce71823b22899ea5c8e70c971103b189a8215e668e38f14dd`，英文归档 `mybot-rv1126b-0.4.3-en-US-aarch64.tar.gz` SHA256 为 `f4a53e0db54c2610c7b3836b95bb6a6f53b58ed64220c7b288a2595189827558`，均位于 `dist/production-rollout/`。制品仍来自 `dev@8bbbc84` 上的未提交开发工作区。

本轮设备 HTTP 客户端验证了实际后端与应用恢复，不能替代手机无线发现、DHCP、captive browser 和长按输入的整体验证；这些由用户再次操作确认。本轮没有重启设备或发起语音对话。

## 0.4.4 Wi-Fi 纯 C 迁移与证据

Wi-Fi 运行实现及对应测试已迁移为 C99。公开 `mybot_platform/wifi.h` 接口保持兼容，内部使用上下文与操作表、容量受限的数据结构、明确的销毁顺序、pthread 同步和单调时钟。旧 `.cc` 实现已移除；源码拆分为控制器、门户、系统配置、凭据、Linux 后端、进程、无线解析、控制套接字和公共辅助共 9 个 `.c` 文件。

- 主机使用全新 `build/c-wifi-migration/host-{zh,en,ubsan}`，三套各 23/23（每套含 163 项工具测试）；独立配置/租约测试保留原用例，并增加 C 字符串边界和 UTF-8 检查。原双语 UI 基线通过。真实 Unix socket 重建、HTTP/DNS、取消、进程回收和配网后恢复回归均保留。
- Ubuntu 24.04.4 使用 GCC 13.3、CMake 3.28.3、Python 3.12.3、OpenSSL 3.0.13、libdrm 2.4.125，在隔离源码副本执行中文、英文和 UBSan，各 23/23。另一个 ASan + UBSan 配置只构建并运行 `wifi-backend`、`wifi-portal`、`wifi-service`、`wifi-profiles`、`network-startup`，5/5 通过。日志和 `results.json` 位于忽略目录 `build/c-wifi-migration/ubuntu-validation-20261116/`；没有覆盖 Ubuntu 原仓库或其 Git 历史。
- 四套目标目录为 `build/c-wifi-migration/rv1126b-{voice,video}-{zh,en}`；所有 Wi-Fi 编译记录均为 `.c` 和 `-std=c99`，构建、来源 manifest 与 ABI 检查通过。固定依赖、原始导入清单、字体和网页内容保持一致，依赖检查及锁更新预览无差异。
- 通过授权部署工具安装中文 0.4.4，设备内备份为 `/userdata/mybot/backups/20261116-production-044/original.tar.gz`。设备 C HTTP 客户端只在本机读取已保存网络并提交真实门户，不导出凭据、不放入参数或日志；约 5514 ms 返回成功，退出请求 202，随后 AP → CONNECTING → VERIFYING → ONLINE → SDK READY。实际健康为 `version=0.4.4` / `state=ready`。
- 正常终止有界验证进程后，临时 AP 和 hostapd 均已清理；在设备内比较 `device_auth` 与首次备份一致、Wi-Fi 配置与本次部署前备份一致。已启动常驻服务，默认回退保留已验收的 0.1.1，没有重启设备。
- 制作上述制品后，按用户要求为函数定义、头文件类型和声明补充空行，并为全部 7 个 Wi-Fi 头文件增加 `__cplusplus` / `extern "C"` 保护。C 静态库重新构建通过；逐头文件以 C99、C++17 严格模式独立包含，再分别编译、链接并执行调用 C 静态库的程序，均通过。格式、依赖检查及源码差异空白检查通过。Ubuntu 更新隔离副本后也重新构建 C 库，GCC 13.3 / G++ 13.3 的逐头文件检查各 7/7，两个客户端的链接运行均通过，日志为 `header-compatibility.log`；此前全套测试和 sanitizer 无需因排版重复。设备常驻服务重启后仍恢复 `ready`。这些排版与调用兼容性调整不改变已部署程序的 C 业务行为；制品源码身份以包内 manifest 为准。

制品位于 `dist/c-wifi-migration/`：

| 归档 | SHA256 |
| --- | --- |
| `mybot-rv1126b-0.4.4-aarch64.tar.gz` | `1a784631b6138648aa9c16aaee0539aa11ff96aba0a4619b8e01a8296369bfb9` |
| `mybot-rv1126b-0.4.4-en-US-aarch64.tar.gz` | `57087b30a3c996500c9cd6d44b80bce42388e0b0f69e8a3ca84d87b5acd23a6d` |

上述制品仍来自 `dev@8bbbc84` 上的未提交工作区。本轮未发起语音对话，设备本地 HTTP 检查不能替代用户手机的发现热点、DHCP、浏览器和长按操作回归；英文/视频业务与长期稳定性未在设备验收。

## 0.5.1 设置菜单、触摸与计划关机证据

右上角齿轮打开设置，左上角返回；设备配网沿用现有 AP 流程，关机增加二次确认。产品输入独立于 SDK 生命周期，等待网络和 AP 期间继续工作。新输入与关机实现为 C99，UI 工作线程独占 LVGL。

- 主机使用 `build/settings-menu/host-{zh,en,ubsan}`，三套各 24/24，每套含 163 项工具测试。每种语言 38 帧基线通过；固定字体生成检查、工程质量、依赖与锁更新幂等检查通过。GCC 发现的测试 mock 宏括号告警已修正，原生与 UBSan 输入专项重新通过。
- Ubuntu 24.04.4 / GCC 13.3 / CMake 3.28.3 / Python 3.12.3 在全新隔离副本运行中文、英文、UBSan，各 24/24。ASan + UBSan 与泄漏检测的输入语义、输入 worker、renderer、显示 worker、监督器、关机控制共 6/6。1261 个文件与打包源码逐文件一致，命令和结果位于 `build/settings-menu/ubuntu-validation-20261116-051/`；没有覆盖远端原仓库。
- Linux 会省略未变化的 ABS 坐标事件。回归覆盖同一点重复点击、仅一个坐标轴变化、启动时读取内核 slot 状态、丢帧后的重同步和按住时必须先释放。真机在应用启动前预置齿轮坐标，启动后首次点击仍正确；随后三次齿轮与左上角返回切换、关机确认与取消共 10 次页面变更全部符合预期，没有产生会话事件。
- 四套目标目录为 `build/settings-menu/rv1126b-{voice,video}-{zh,en}`，构建与 ABI 均通过，来源摘要一致：`356361e09c56ffd6e014343cb902f5ce4392c774d4ea19a7f23dbd67b5409c6c`。本节验证记录在打包后补充，制品的源码身份以包内 manifest 为准。
- 授权部署中文 0.5.1，备份保留在设备 `/userdata/mybot/backups/20261116-settings-051/original.tar.gz`。设置进入 AP 后仍可打开菜单并通过左上角返回；设备本地 C HTTP 客户端提交已有网络，11777 ms 得到成功，再退出 AP，SDK 自动恢复 READY。凭据未导出或放入参数、日志。
- 隔离监督器使用正式程序和 root 私有目录中的假命令，该命令只写标记。分别在 SDK READY 与 SDK 尚未启动的 AP 状态通过 UI 确认关机，应用状态均正常进入 stopped，监督器清理 IPC 后执行假命令，无 unclean 标记或残留 AP。未调用真实系统关机或重启。
- 最终在设备内逐字节比较绑定、系统 Wi-Fi、dhcpcd 与 dnsmasq 配置，均与部署前一致。本板沿用系统 Wi-Fi 配置，私有 Wi-Fi 目录保持为空。常驻服务已恢复 `version=0.5.1` / `state=ready`，恰好一个应用和监督器，无 AP 或隔离测试进程；`current` 指向 0.5.1，`previous` 保留已验证的 0.4.4，0.1.1 基线包仍可用。

制品位于 `dist/settings-menu/`，均来自 `dev@108dad0` 上的未提交工作区：

| 归档 | SHA256 |
| --- | --- |
| `mybot-rv1126b-0.5.1-aarch64.tar.gz` | `d686151f344c035b051b08b64eee071faad5d0488ab5b36405dc32a61cc733c9` |
| `mybot-rv1126b-0.5.1-en-US-aarch64.tar.gz` | `0907efecaa12abb5b104aded6cb0563a5222925b509e61b5d23e4d4a9c9e97e5` |

手机实际无线接入、物理触摸手感、完整语音和真实关机/重新上电由用户验收，未创建 Git 提交或远端发布。

## 0.5.1 文档与构建工具修正证据

当前版本由根 CMake 读取；`scripts/project-version.py --set X.Y.Z` 同步 CMake 与中英文 README 的当前版本标记，`--check` 接入质量检查。操作指南使用版本变量或包内 manifest，不自动改写历史记录。格式工具优先使用 `local/dev-tools` 中的固定版本，并在执行格式化前校验主版本。运行包的示例环境按所选 UI 语言渲染，helper 路径从解包目录解析；直接运行和受监督部署的系统配置边界已明确。

- 新增版本工具 5 项、格式工具与质量联动 11 项回归，打包测试增加中英文环境、实际 helper 路径、模板损坏拒绝与归档一致性检查，完整工具套件共 181 项通过。
- macOS 使用已有 `build/settings-menu/host-{zh,en,ubsan}`，三套各 24/24，每套含全部工具测试。目标使用已有四套 `build/settings-menu/rv1126b-{voice,video}-{zh,en}` 更新构建和 manifest，ABI 语音 9、视频 11、监督器 2 项依赖，均无错误。构建日志位于 `build/docs-quality-audit/target-builds.log`。
- Ubuntu 24.04.4 / GCC 13.3 在全新隔离副本按文档执行 venv 与固定依赖安装，不激活环境；PATH 中系统 clang-format 仍为 18.1.3，质量脚本自动选择项目 11.0.1 后通过。显式选择系统 18 并加 `--fix-format`，脚本在扫描或改写源码前拒绝。全新中文、英文、UBSan 目录各 24/24；单独工具测试 181 项、doctor、依赖检查通过。测试结束逐文件复核全部 1264 个快照文件字节一致，远端原始仓库源码未修改。日志位于 `build/docs-quality-audit/ubuntu-validation-20261116/ubuntu-logs/`。
- 双语实际包各 63 项 SHA256 校验通过，环境注释分别为 zh-CN / en-US，模板标记没有残留，helper 不再含固定版本路径。依赖锁只更新来源说明 38 帧对应的两个文档摘要，vendor 与字库未变；刷新本地 SDK 锁身份后 target doctor（含视频）通过，锁更新预览无差异。
- 本轮仅更正退出诊断，不改变监督器的失败重试和隔离策略。没有执行设备操作或部署，不将此前设备的验证结果用于本次新包。

制品位于 `dist/docs-quality-audit/`，来自 `dev@24f02d0` 上的未提交工作区。打包源码摘要为 `eabe879fd804403ca271b7557f14ea8df8ca478e0ca33b4b71bd0435caff9228`；本节在检查及制品生成后补充，来源身份以包内 manifest 为准。

| 归档 | SHA256 |
| --- | --- |
| `mybot-rv1126b-0.5.1-aarch64.tar.gz` | `5dfb31eaf91221d3502a2fe77b521ad84b6dd6db31e842846d6d85aa8edb1737` |
| `mybot-rv1126b-0.5.1-en-US-aarch64.tar.gz` | `9e4288297bd71b3f0b7f9bc68191e7e61df0a677c1287a905453443bd8403415` |

## 0.5.1 手动配网入口收敛与证据

2026-11-17，手动配网只通过设置页的“设备配网”进入。删除触屏/Menu 长按、终端 `w` 和 `--wifi-provision` 强制配网参数；长按释放不触发会话切换。短触及 Menu 短按会话、离线自动尝试已保存网络后进入 AP、断网恢复及禁用配网选项保留。物理电源键和系统关机脚本未改动。

- 输入请求只由设置点击产生并消费一次，删除旧长按状态和周期轮询。回归覆盖 1/3/10 秒按住、释放、重复按下/自动重复、移动/丢帧、终端 `w` 无效、短按边界，以及 SDK 启动前和解绑后同一 reader 的设置配网；配置回归确认旧强制配网参数被拒绝。
- macOS 使用 `build/settings-menu/host-{zh,en,ubsan}`，最终三套各 24/24，每套含 181 项工具测试。首次并行运行的英文 `wifi-service` 在未修改的 250 ms 时限断言处失败；保存首轮日志为 `build/settings-only-wifi/host-en-initial-ctest.log`。单项复测通过，其他本地构建结束后整套英文再跑 24/24，通过时未修改测试阈值或 Wi-Fi 服务代码。主机构建日志位于 `build/settings-only-wifi/host-*-build.log`。
- Ubuntu 24.04.4 / GCC 13.3 在新隔离副本运行中文、英文、UBSan，各 24/24；固定工具、依赖、doctor 与单独 181 项工具测试通过。GCC ASan + UBSan 与泄漏检测的输入语义、输入 worker、配置共 3/3；测试后 1264 个快照文件逐字节复核一致。结果与日志位于 `build/settings-only-wifi/ubuntu-validation-20261117/`，远端原始仓库源码未修改。
- 四套目标构建使用已有 `build/settings-menu/rv1126b-{voice,video}-{zh,en}` 更新，日志为 `build/settings-only-wifi/target-builds.log`；manifest 来源摘要一致为 `6d54da8f4d6ae8c7f9787f797c248096cc575c2ef12164a22a2572513d9eb5e5`，ABI 语音 9、视频 11、监督器 2 项依赖，均无错误。目标二进制的 CLI/输入提示不再包含原快捷配网说明。
- 双语包各 63 项 SHA256 校验通过，包内指南只说明设置页手动配网。质量、版本一致性、依赖与锁更新幂等检查通过；没有修改字库或 UI 图像基线。本轮未连接或部署设备，不将主机模拟事件当成真机验证。

制品位于 `dist/settings-only-wifi/`，来自 `dev@44f3085` 上的未提交工作区。本节在打包后补充，制品身份以包内 manifest 为准。

| 归档 | SHA256 |
| --- | --- |
| `mybot-rv1126b-0.5.1-aarch64.tar.gz` | `ac54bf6b1f1b127918e48ca984cc203e812649afaceb09dfd229476606f5fe68` |
| `mybot-rv1126b-0.5.1-en-US-aarch64.tar.gz` | `1f143cb41ae9c16cb10b835527615e447ca67013d43cedc47982642a48eb6439` |

## 0.5.2 设备更新与设置配网验收

2026-11-17，为避免覆盖设备既有 0.5.1，使用版本同步工具将候选更新为 0.5.2，并重新构建四套目标及双语运行包。输入实现与上一批设置页独占配网的完整主机回归一致；版本元数据更新后，macOS 中文与 UBSan 的输入语义、输入 worker、应用配置专项各 3/3 通过。构建、ABI、打包证据保存在 `build/settings-only-wifi/rollout-052/`，四套来源摘要一致为 `145dd5699bb118955f22bf0ea07b316afc55c5b68973cff220b7b3c664489240`。

- 使用部署工具安装并启动中文语音 0.5.2。设备内备份为 `/userdata/mybot/backups/20261117-settings-only-052/original.tar.gz`，`current` 指向 0.5.2，`previous` 保留更新前的 0.5.1。部署身份预检匹配，安装目录全部 SHA256 校验通过。
- 实际 evdev 注入只使用触屏 `BTN_TOUCH` 与 Menu 合成键，未注入 `KEY_POWER`。分别在普通区域、齿轮、设置页配网按钮按住 4200 ms，并验证 Menu 长按；按住超过原 3 秒门槛时和松手后均不进入 AP、不触发会话。增量日志没有瞬时 SDK/AP/设置切换，进程 PID 保持一致。旧 `--wifi-provision` 参数被明确拒绝。
- 齿轮短按打开设置，配网项长按保持当前菜单，短按才进入 AP；本地访问门户返回 HTTP 200，AP 中的设置返回操作正常。首次仅检查页面后，通过正常服务重启清理 AP，恢复 ready，并确认配置字节保留。
- 在用户明确授权后，设备 C 客户端从已有系统配置读取凭据，并通过本机 supplicant 状态匹配当前同一个网络；凭据只在设备内存及本机配网接口使用，未导出或放入 argv/日志。再次经设置进入 AP，AP 期间长按仍不产生额外请求，随后提交返回 202，轮询得到 connected，退出请求返回 202；日志观察到 AP → CONNECTING → VERIFYING → ONLINE → SDK READY。客户端记录的 33903 ms 包含等待 UI 和测试触发信号，不能作为纯联网耗时。
- 完整配网后，在同一应用进程内打开设置并返回，再次长按屏幕，随后连续观察 ready、同一 PID 和无 AP，均通过。最终恰好一个应用、一个监督器，测试客户端已退出，无 unclean 标记；清理测试二进制、触发文件和含临时门户 token 的页面，保留脱敏状态证据。
- 在设备内逐字节比较 `device_auth`、`wpa_supplicant.conf`、`dhcpcd.conf` 和 `dnsmasq.conf`，均与本次部署前备份一致。系统电源键脚本及事件配置的 SHA256 与先前只读副本完全一致。本轮没有执行系统关机或重启，设备最终保持 `version=0.5.2` / `state=ready`。

脱敏检查结果和枚举状态分别为 `build/settings-only-wifi/rollout-052/device-verification.json`、`device-state-transitions.log`。本节在构建和部署后补充；制品来自当时的未提交工作区，身份以包内 manifest 为准。

制品位于 `dist/settings-only-wifi-052/`：

| 归档 | SHA256 |
| --- | --- |
| `mybot-rv1126b-0.5.2-aarch64.tar.gz` | `4850254ae3fec97a3bca34d6b878623c359373f454161dc1d29a0610f55aad5d` |
| `mybot-rv1126b-0.5.2-en-US-aarch64.tar.gz` | `2dcceccc6d6f5303e301642041424ae936afdeeaf11a08dc52379cb473e958f8` |
