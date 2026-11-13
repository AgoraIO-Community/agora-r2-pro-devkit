# RV1126B 工程治理方案

> 历史记录：本文保留对应阶段的判断、路径和命令，不作为当前操作指南。现行入口见[文档索引](../README.md)，结果以[验证状态](../validation/README.md)为准。

状态：历史方案；正用于实施 0.2.0 工程治理候选版。审查日期：2026-11-13。

原始审查发现和阶段验收标准保留于下文。现行结构见[架构指南](../development/architecture.zh-CN.md)，构建/依赖及制品机制见[开发文档](../README.md)，实际执行结果见[验证状态](../validation/README.md)。本轮新增 CLI/运行保护、ALSA/网络生命周期与制品检查；视频的更多故障注入和真机专项仍按风险后续补充，不将结构重组视为全部专项已完成。

源码基线为 `dev@0589cde`。方案审查阶段仅检查本地仓库并编写文档，未迁移生产代码、访问设备、提交或发布。

目标是让新用户能独立构建、贡献者能判断修改边界、维护者能持续验证和发布。以现有已验收的 0.1.1 为行为基线，分开处理工程组织与功能演进。

行为对照包为 `mybot-rv1126b-0.1.1-aarch64.tar.gz`，SHA256 为 `b028d775d3e91858c54f4b1ac336da4f7eeea41cfe618acfb9ade90e69e2abf7`，保留原包供后续回归。

## 1. 现状判断

当前工程已经具备真实 LVGL 渲染测试、输入和 DRM 生命周期测试、UBSan、AArch64 依赖闭包检查、字体生成记录和第三方许可。这些应保留并接入日常维护流程。

主要短板是作者环境、构建输入、模块边界和交付流程仍靠人工串联。改目录名只能解决其中一部分。

| 领域 | 已确认的问题与依据 | 治理结果 |
| --- | --- | --- |
| 构建入口 | `scripts/configure-local-sdk.py` 默认依赖特定本机工具链、开发目录和 runtime manifest | 新 clone 在取得文档明确的依赖后，使用显式参数在独立目录构建 |
| 模块边界 | `CMakeLists.txt:85` 的平台 target 同时编译硬件适配、SDK Linux 服务、UI worker 和 DRM；`:40`、`:93` 将实现目录作为 PUBLIC include | 每个 target 有明确资源归属，只公开必要头文件；依赖由 target 显式表达 |
| 应用入口 | `app/main.c:85`、`:124`、`:156` 同时包含锁、存储准备、参数解析、板级默认值和生命周期 | 入口只组织启动与关闭，配置、运行保护和硬件默认值各自归属清楚 |
| 硬件事实 | `app/main.c:159`、`:170`、`:304`，`ui/drm.c:52`，`platforms/rv1126b/rv1126b_video.c:27` 分散定义设备名、方向、型号和视频参数 | 一个已验证板型 profile，逻辑画布和物理显示契约分开定义 |
| 测试门禁 | 无项目级 CI；`tests/CMakeLists.txt:29` 在缺少 DRM 头文件时省略测试 | 标准 CI 运行完整测试集，缺依赖失败；精简测试必须显式选择并报告跳过项 |
| 制品身份 | `scripts/package-rv1126b.py:21` 按当前源码版本命名，`:34` 仅核对已有 ABI/二进制与 RTSA 哈希，`:32` 根据旧视频文件是否存在决定包内容 | 版本、源码、工具链、语言、音频时长和视频配置与实际二进制绑定；拒绝混包与过期构建 |
| 依赖管理 | `docs/imported-sources.json` 保存原始导入哈希，但构建和检查脚本未消费它；其中部分文件已有已知适配或再生成 | 原始导入、当前内容、补丁和生成资源分别可追溯，并能自动验证 |
| 开源入口 | 缺少项目级 CONTRIBUTING、SUPPORT、CHANGELOG、VENDORED_SOURCES、AGENTS 与 PR 模板 | 用户、贡献者和维护者分别有清晰入口，并配套实际执行的检查 |
| 文档适用性 | 依赖集成说明和视图资源说明仍描述旧布局与旧生成器 | 来源与许可准确，当前 Linux 文档提供现行命令；用户不必从过程记录推断用法 |

当前跟踪 1,291 个文件，其中 1,216 个属于 `third_party/`。依赖体积不是首要问题，应先建立导入和验真机制，再决定是否裁剪。

## 2. 治理原则

工程规范应覆盖以下机制：

- `AGENTS.md` 与 `CONTRIBUTING.md` 明确自维护代码、依赖快照和外部仓库的修改边界。
- 平台组件区分 boards、services、drivers、internal 和公开头文件。
- `VENDORED_SOURCES.md` 配合第三方声明记录来源、固定版本和本地适配。
- CI 同时执行格式、许可证标识、主机生命周期测试和明确的构建矩阵。
- `docs/VALIDATION.md` 区分计划覆盖、实际构建、主机测试和真机结果。
- PR 模板与发布指南要求说明兼容性、已执行的验证和剩余限制。

RV1126B 继续采用普通 CMake 和 Linux ALSA/DRM/evdev/V4L2/MPP，保持单一已验证板型。当前匹配 RTSA 的音频时长为 20 ms，作为构建与运行契约固定检查。

## 3. 建议目录结构

保留 `app/` 作为应用入口；将自维护平台和纯渲染代码组织为两个组件。第三方快照与集成包装分开。

```text
mybot-rv1126b/
├── README.md / README.zh-CN.md
├── CONTRIBUTING.md / CONTRIBUTING.zh-CN.md
├── SUPPORT.md / SUPPORT.zh-CN.md
├── CHANGELOG.md
├── AGENTS.md
├── LICENSE
├── VENDORED_SOURCES.md
├── THIRD_PARTY_NOTICES.md
├── dependencies.lock.json
├── CMakeLists.txt
├── CMakePresets.json
├── .github/
│   ├── PULL_REQUEST_TEMPLATE.md
│   ├── ISSUE_TEMPLATE/
│   └── workflows/ci.yml
├── app/
│   ├── CMakeLists.txt
│   ├── main.c
│   ├── config.c / config.h
│   ├── runtime_guard.c / runtime_guard.h
│   └── device_identity.c / device_identity.h
├── components/
│   ├── mybot_platform/
│   │   ├── CMakeLists.txt
│   │   ├── include/mybot_platform/
│   │   ├── boards/rv1126b-test2-rk805b-v10/
│   │   │   ├── README.md
│   │   │   ├── board.cmake
│   │   │   └── board_config.h
│   │   └── src/
│   │       ├── platform.c
│   │       ├── internal/
│   │       ├── services/         # 网络监测、显示 worker 等 Linux 适配
│   │       └── drivers/          # audio、input、display、video
│   └── mybot_ui/
│       ├── CMakeLists.txt
│       ├── include/mybot_ui/
│       ├── src/                  # renderer、固定画布契约、表情策略
│       ├── views/pixel_glasses/  # 视图、图形和遮罩
│       ├── fonts/
│       │   ├── sources.json
│       │   ├── generated/       # 已提交的 C 字库
│       │   └── licenses/
│       └── lv_conf.h
├── cmake/
│   ├── dependencies/            # SDK、Linux adapters、LVGL 和系统库包装
│   └── toolchains/
├── third_party/
│   ├── README.md
│   ├── mybot/                   # 保留 SDK/AOSL 已验证快照布局
│   └── lvgl/                    # 固定子集，登记已应用补丁
├── tools/ui_preview/
├── scripts/                     # 薄入口：准备、检查、构建、打包、生成字体
├── tests/
│   ├── unit/
│   ├── integration/
│   ├── tooling/
│   └── support/
├── packaging/                   # run.sh、环境示例、包内文档模板
├── docs/
│   ├── README.md
│   ├── user/                    # 上手、构建、配置、升级、故障排查
│   ├── development/             # 架构、测试、依赖更新、发布
│   ├── validation/              # 唯一当前状态表及版本证据
│   ├── dependencies/            # 原始导入清单、RTSA 来源、本地补丁
│   └── history/                 # 首次移植评估、历史设计和本治理方案
├── build/                       # 忽略：每个 preset 独立目录
├── local/                       # 忽略：工具链、SDK、字体原件、用户配置
└── dist/                        # 忽略：交付制品与预览
```

不预建没有内容的目录。迁移时同步修改构建、测试、生成脚本、打包许可路径和文档链接。旧脚本保留薄兼容包装，用户仍可使用已经验证的入口。

### 3.1 模块与资源归属

| 模块 / target | 职责 | 边界 |
| --- | --- | --- |
| `mybot-rv1126b` | CLI、身份、进程保护、运行目录、SDK 与平台启动关闭 | 不包含具体硬件 I/O；保留单一清理出口 |
| SDK public-header INTERFACE target | 供 host renderer/tests 使用 agent-studio-device-firmware 公共类型 | 不链接 RTSA，不导出 SDK 私有目录 |
| `mybot_ui` | 固定 640×480 renderer、view、表情、字库 | 通过已有 present 回调交付画布，不依赖 DRM、ALSA、MPP 或 pthread |
| `mybot_linux_display` | UI worker、DRM、旋转/blit、显示失败清理 | 位于平台组件中；拥有线程及显示资源，调用 renderer |
| `mybot_platform` | 平台描述符、ALSA、evdev、网络监测、可选视频和 Linux 服务集成 | 调用 SDK 公共接口，不拥有第二套配对/会话状态机 |
| 依赖包装 targets | 固定 SDK、LVGL、系统依赖和现有 Linux adapters 的构建方式 | CMake 包装放在快照外；上游文件不为目录美观而重写 |

依赖方向为：应用调用平台；平台及 Linux display 调用 UI/SDK；UI 仅消费 LVGL 和 SDK 公共类型。主机预览复用纯 UI 与像素转换，不启动设备栈。仅对跨组件需要的头文件使用 PUBLIC，其余均为 PRIVATE。

旋转/blit 的资源归属在显示 backend，但 `pixels.c` 保持为纯函数实现，供 display、host preview 和测试共享。主机复用该 helper 时不链接整个含 DRM/pthread 的 display target；尺寸和 dirty rect 类型来自 UI canvas 契约，无须为此增加公开组件。

平台公共头保留配置、注册、显示生命周期，以及应用使用的网络就绪和退出查询。audio/input/video 的 ops getter、DRM helper 和 worker 细节收进内部头文件。先保留现有符号名，避免路径迁移与批量 API 改名混在一个改动中。

SDK 自带的 HTTPS/KV/announcement/console 实现继续从固定快照编译，由外部集成包装统一声明来源；无须复制一份到平台组件。嵌套 AOSL 首轮保持原位，不引入子模块迁移。

### 3.2 配置边界

- 板型 profile 记录型号、物理屏幕、默认旋转、ALSA 名称、输入设备规则和相机默认参数。
- UI 自己定义逻辑画布、排版和字体规格；统一重复尺寸常量，保留原生 640×480 契约。
- `app/config` 管理服务地址、身份策略、环境与 CLI 覆盖，并保持当前优先级。
- CMake preset 管理 host/target、语言、视频和 sanitizer，构建参数不混入运行配置。
- 继续通过既有 Linux 网络配置获得连接，不增加配网、自启动安装或设备固件管理能力。

## 4. 独立构建与依赖治理

### 4.1 两条明确的开发路径

1. **主机开发**：安装文档列出的 C/C++、CMake、OpenSSL、libdrm 头文件后，即可测试和预览 UI；不需要 RTSA、设备 staging 或 ADB。
2. **目标构建**：提供匹配的 AArch64 工具链、完整开发 sysroot、RTSA 头文件和库，以及目标 runtime manifest 后，运行目标 preset 和 ABI 检查。

优先建立 Linux x86_64 的受控发布构建环境，再维护 macOS x86_64/arm64 的主机开发入口。当前 Intel macOS Zig 路径保留为已验证兼容方案；其他组合在实际通过前只列为待验证，不先宣布支持。

新增独立依赖准备与 `doctor` 检查：校验工具版本、宿主/目标架构、依赖目录、sysroot 完整性、RTSA 哈希/功能/20 ms 配置、开发头文件与目标运行库的对应关系。运行库快照继续明确标为 runtime snapshot，不能充当完整 sysroot。

`configure-local-sdk.py` 通过显式参数配置工具链与依赖。标准文档不出现维护者个人绝对目录；用户私有 SDK 路径放入忽略的本地配置或 `CMakeUserPresets.json`，不提交生成的环境文件。

### 4.2 清单各有单一职责

| 文件 | 维护内容 |
| --- | --- |
| `dependencies.lock.json` | 当前构建输入的固定版本、来源、获取方法、架构、SHA256、依赖作用域与补丁结果哈希 |
| `VENDORED_SOURCES.md` | 人可读的导入范围、本地修改、更新流程及锁文件入口 |
| `THIRD_PARTY_NOTICES.md` | 许可证、版权与适用范围；依赖明细链接锁文件 |
| 原 `imported-sources.json` | 历史导入基线，保留原始路径与哈希，不伪装为当前源码清单 |
| 字体 `sources.json` | 字体输入、字符集、生成工具、工具版本与输出身份 |

依赖检查区分原始输入、有序补丁、当前结果和生成资源。对已经修改的文件，验证登记的变换结果，不能直接要求其等于原始导入哈希。历史路径与当前路径使用映射，不抹去原始记录。

公开源码构建不自动捆入 RTSA 二进制或设备系统库。现有自维护代码许可证、AOSL 等附加条款和 RTSA 使用条件继续分别说明；公开发布的依赖范围按实际可分发内容确定。

### 4.3 构建入口

采用 `CMakePresets.json`，计划提供 `host-zh`、`host-en`、`host-ubsan`、`rv1126b-voice-zh/en`、`rv1126b-video-zh/en`。输出统一放入 `build/<preset>/`，防止语言、工具链和能力切换污染缓存。

根 CMake 只保留项目版本、选项、依赖和子目录组织；组件各自声明源文件与依赖。LVGL 使用受控源码清单，避免未来新增 `.c` 被递归 glob 自动纳入。工具最低版本根据 preset schema 与 CI 实测统一声明。

脚本负责环境检查与串联现有工具，不再实现另一套构建系统。普通构建使用已提交字库，不要求安装 Pillow/FreeType；再生成字体使用独立、固定版本环境。

## 5. CI 与测试治理

### 5.1 门禁矩阵

| 场景 | 必须执行 | 验证边界 |
| --- | --- | --- |
| 每个 PR 的公共检查 | 自维护源码格式/SPDX/空白、Python/shell 基础检查、文档链接、依赖清单一致性 | 第三方快照与生成文件不批量格式化 |
| 公共主机 CI | Linux zh/en 真实 renderer、完整原有测试集、UBSan | 缺 DRM 测试依赖直接失败；记录测试名称及跳过项 |
| 目标构建 CI | zh/en × voice/video，加载器、依赖闭包、预期 RPATH 与 manifest 检查 | 以取得匹配 SDK/sysroot 为前提；单独展示实际运行结果 |
| 制品检查 | 显式变体、版本/源码/配置匹配、校验和、解包结构和许可文件 | 无需设备，不把本机构建路径带入公开报告 |
| 字体/依赖变更 | 固定输入与工具环境下的生成 `--check`、补丁结果与来源检查 | 普通文档变更无需重复全部字体再生成 |
| 发布前硬件回归 | 已验证语音/UI流程、绑定复用、显示、启停与此次改动相关场景 | 用户或授权硬件环境执行，结果独立登记 |

若完整目标 SDK 不能在公共 runner 获取，公共 host gate 必须照常运行；目标构建在授权环境运行并留下证据。不能以未执行或静默跳过的 target job 宣称完整 CI 通过。

### 5.2 优先补充的测试

保留已有 7 项主机测试和英文 renderer 回归，不以增加测试数量为目标。新增测试按风险安排：

1. 制品工具：拒绝版本/语言/视频配置不符、过期 ABI、缺少所选变体与危险归档路径。
2. 应用边界：CLI 校验与优先级、`--help/--version` 无副作用、运行锁、存储准备失败后的清理。
3. 音频生命周期：ALSA 部分初始化、短读写、恢复与停止；网络可用性和路由变化。
4. 视频生命周期：V4L2/MPP 部分失败、重复启停、停止后不再调用借用帧回调。

故障注入围绕真实 I/O 边界。现有直接 include `.c` 的 DRM/input worker 测试先随路径迁移保留；仅在其妨碍维护时增加窄测试接口，不为形式统一重写已有有效测试。

不把编译、mock 测试或预览等同于音质、AEC、物理按键位置、相机效果或长期并发验证。

## 6. 制品、版本与发布

保留 CMake 项目版本为单一版本源。构建时生成 manifest，完成链接后将它与二进制哈希和 ABI 报告绑定：

- 项目版本、源码 revision 与 dirty 状态、构建变体、语言和目标板型；
- ptime、Cloud AEC、视频能力、RTSA 与 SDK 身份；
- 工具链、sysroot/runtime 清单身份和关键构建选项；
- 二进制、ABI 报告与随包资源的 SHA256。

打包显式选择 voice/video/all 和 locale，不根据历史目录中是否碰巧有可执行文件决定内容。发布模式拒绝脏源码、过期构建和不匹配配置；开发构建可以保留，但必须标明身份。

默认中文包继续使用 `mybot-rv1126b-<version>-aarch64` 名称；英文包及其解包根目录使用 `mybot-rv1126b-<version>-en-US-aarch64`，所有 manifest 显式记录 locale。同一语言下，voice/video/all 选择使用独立输出目录，按需单独分发时在名称中追加 profile，避免覆盖。

ABI 检查沿用当前闭包与符号版本检查，补充可执行文件预期 `$ORIGIN/../lib`、动态加载器及运行基线策略。公开报告使用归一化路径，同时保留依赖哈希。该检查不能证明全部运行时行为。

包内 README 改为现行安装/运行说明，不再引用“首版验证”过程文档。包继续保持 `bin/`、`lib/`、`assets/`、`run.sh` 与 `SHA256SUMS`，避免打断用户流程。

确定性归档使用固定文件顺序、`SOURCE_DATE_EPOCH`、uid/gid、权限和 tar/gzip 时间。相同已构建输入在两个独立目录打包应得到相同 SHA256；编译本身的字节可复现另行验证，不能由归档一致推出。

`CHANGELOG.md` 记录用户可见变化。发布指南定义版本/tag/manifest 一致性、验证证据与制品清单；实际 tag、push、GitHub Release 和附件发布按用户明确任务执行。当前交付包不因目录治理而自动替换。

## 7. 文档与协作治理

README 中英文对等维护，提供项目边界、实际支持的板卡/系统、功能与限制、最短构建/运行入口、验证矩阵和许可范围。双语文档共享同一命令来源，避免复制维护版本号与支持状态。

- 用户文档：首次上手、独立构建、CLI/环境配置、升级与绑定保留、故障排查。
- 开发文档：架构与线程/资源所有权、模块公共接口、测试、依赖更新、字体生成和发布。
- 贡献规则：Conventional Commits、小范围 PR、格式和验证命令、公共 SDK API 边界、依赖更新要求。
- 支持模板：commit/版本、板型/系统、语言/视频配置、复现步骤、脱敏日志及实际测试环境。
- 当前验证矩阵：版本/commit、变体、测试类别、执行者/证据、结果和限制，作为当前状态唯一来源。
- 历史记录：保留初始可行性、已验证结果和旧布局资料，明确适用时期并链接现行指南。

AGENTS 约束应简明：自维护代码可编辑；依赖更新经显式同步与补丁流程；生成文件通过生成器修改；外部仓库和设备操作遵循当前任务授权。规则应与 Linux/CMake 工程保持一致。

当前 0.1.0 的语音/UI业务和 0.1.1 的 UI 效果已由用户验证通过，视频、专项音质和长时间并发保持原有待验证状态。目录重构后的新二进制单独登记，不能继承旧产物的硬件结果。

## 8. 实施顺序与验收

建议拆为以下连续阶段，每阶段保持可构建、可审查、可回退：

| 阶段 | 范围 | 完成条件 |
| --- | --- | --- |
| G0：建立基线 | 登记 0.1.1 行为、依赖与制品身份；补项目级贡献/支持入口；先将现有 host 检查接入 CI | 新增文档可用，完整原有测试集可在标准环境执行，未改变运行行为 |
| G1：独立构建与制品身份 | 锁文件、独立 SDK 配置/doctor、presets、目标矩阵与 build manifest、打包配置一致性及对应负向测试 | 干净 clone 使用显式依赖输入；旧二进制不能被伪装成新版本；voice/video 和语言互不污染 |
| G2a：目录与 target 迁移 | 组件化移动、public/private 头文件、CMake 包装、脚本/测试/许可路径与文档链接更新 | 源码移动与构建整理单独 review；符号、控制流、资源字节和配置不变，原有验证通过 |
| G2b：运行职责提取 | 依次拆 CLI/config、runtime guard、板级默认值；明确显示线程和资源归属 | 每次提取有对应边界检查，不同时改变音频、网络或视频策略 |
| G3：故障路径与发布治理 | 选择性补生命周期测试和其余制品检查、归档确定性、完整发布指南和现行文档 | 故障路径覆盖有效；相同输入可重复打包；公开发布范围、证据和限制清楚 |

G1 的目标构建环境应先用于验证现有工程，再执行 G2 的路径迁移。不要把重写构建器、替换 SDK、依赖裁剪和驱动重构塞进一次提交。

### 8.1 必须保留的行为

- 所有现有 CLI/环境名及覆盖优先级、`run.sh`、两种二进制用途和包内布局。
- 默认 server、CPU Serial/MAC 身份派生、`/userdata/mybot-rv1126b` KV 位置与旧绑定复用。
- 16 kHz mono S16、20 ms、云端 AEC、显式开始会话、输入语义和音量行为。
- 原生 640×480、默认 270°、18/20/40/64 px 字体、两像素镜框网格与 23 种表情。
- 单实例与原厂服务互斥、`--help/--version` 无硬件/文件副作用、失败清理与正常退出恢复。

### 8.2 每阶段如何验证

文档改动检查链接与空白；目录迁移和职责提取执行完整 host/UBSan、英文 renderer、voice/video AArch64 构建及 ABI 检查。G2a 的目录/target 迁移必须比较固定时间点的中英文预览像素，确认未误换字体、宏或编译资源。涉及字体/资源时另执行生成 `--check`；涉及打包时验证 manifest、校验和与解包布局。

结构重构完成后生成独立候选包，由用户执行一次现有语音/UI流程回归。未取得额外授权前，设备继续只读；工程检查与打包不隐式连接设备或修改系统服务。

验收重点是独立复现、边界可执行、回归可发现、制品可追溯。目录数量、文档篇数或徽章数量不作为质量指标。
