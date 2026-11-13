# 当前验证状态

本页是项目验证状态的唯一现行入口。更新日期：2026-11-13。当前源码版本：**0.2.2 源码目录重构候选版**。历史检查保持其原始版本归属，不自动移用于新二进制。

目标基线：**RV1126B TEST2 RK805B V10**、AArch64 Buildroot 2024.02、Linux 6.1.172、glibc 2.41，物理屏幕 480×640。已记录的本地主机为 **Intel macOS**，交叉编译使用 Zig 0.15.2；Linux/Apple Silicon 的新 CI/开发入口需以实际运行证据为准。

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
| 0.2.1，第三方目录 | 迁移与裁剪 | AOSL 的 330 文件先原样平移并校验；agent-studio-device-firmware/AOSL/LVGL 分别保留 110/124/719 文件；262 项显式省略；运行源码/头文件/构建模板与迁移前一致 | 文档有布局适配；原始导入清单未改写 |
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

- 当前 0.2.2 候选的绑定复用、UI方向/文字/动画、语音、输入/音量、重复启停及退出恢复。
- 视频首帧/云端接收、方向/颜色、V4L2/MPP 启停与语音/UI并发。
- 音质/增益和 AEC 测量、物理按键位置、网络恢复、显示恢复和长时间稳定性。

初始设备检查由代理只读执行；0.1.0/0.1.1 部署及真机测试由用户完成。本轮工程治理在主机进行，不隐式上传或运行设备程序。设备状态不能根据历史快照推断。

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
