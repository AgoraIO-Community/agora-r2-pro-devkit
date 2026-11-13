# 第三方依赖目录与维护规则

`src/` 集中项目维护的应用、平台和 UI 模块，`vendor/` 按直接依赖平铺：每项依赖保存一份快照，具有独立版本、文件清单与适用条款。RTSA 保存完整官方包，其他依赖按已登记范围保留源码。项目构建集成位于 `cmake/dependencies/`，项目专用说明位于 `docs/dependencies/`，应用视图和生成资源位于 `src/ui/`。

## 现行结构

```text
src/
├── application/          # 配置、身份、保护与生命周期
├── platform/             # 板型、Linux 服务与驱动
└── ui/                   # 渲染、视图与生成字库

vendor/
├── README.md
├── mybot/                # MyBot SDK 自身源码、必要 Linux adapters 与构建输入
├── aosl/                 # AOSL 公共实现与 Linux HAL
├── lvgl/                 # LVGL 固定生产子集
└── rtsa/                 # RTSA 1.10.1 完整官方包（含 agora_sdk/、示例与文档）

cmake/dependencies/
├── sdk.cmake             # MyBot、AOSL 和内置 RTSA 集成
├── public-headers.cmake
├── lvgl.cmake
└── lvgl-sources.cmake

docs/dependencies/        # 集成说明、补丁和原始导入记录
dependencies.lock.json   # 当前身份、文件清单、迁移、裁剪与资源记录
```

MyBot 依赖 AOSL 和 RTSA，UI 依赖 LVGL；目录平铺后，这些链接依赖继续由 CMake targets 表达。RTSA 完整包内置于 `vendor/rtsa/`，构建固定选择 `agora_sdk/lib/aarch64/libagora-rtc-sdk.so`。ALSA、OpenSSL、libdrm、MPP 和 libc 使用目标系统开发文件与运行库，不复制到 `vendor/`。

`src/ui/` 中的视图由锁文件的 `managed_assets` 登记；字库及生成输入由 `generated_fonts` 登记。两者均由项目维护，与四项第三方依赖快照分别校验，资源携带的文件级许可继续适用。

## MyBot 与 AOSL 的构建边界

AOSL 原嵌套目录的 330 个文件先原样移动，再单独登记裁剪；版本保持为已有记录的固定 commit `84e086084ebcd0ae2455a0ce5721950c5fe2e656`，不将其描述为本轮重新核验过的上游版本。

父工程在调用 MyBot 的 `add_subdirectory()` 前设置 `AOSL_DIR`，由 SDK 创建唯一的 `aosl` target：

```cmake
set(AOSL_DIR "${PROJECT_SOURCE_DIR}/vendor/aosl"
    CACHE PATH "Pinned AOSL source snapshot" FORCE)
```

`FORCE` 覆盖旧构建缓存中的嵌套位置；不存在对应 CMake 输入时配置失败。父工程不再创建第二个 AOSL target，也不修改 SDK/AOSL 的运行代码。打包从 `vendor/aosl/LICENSE` 获取完整许可，包内名称仍为 `aosl-LICENSE.txt`。

路径迁移会改变构建身份，已有二进制、manifest 和 ABI 报告需要重新生成。验收使用隔离的全新构建目录，并检查旧缓存重新配置后采用新 `AOSL_DIR`、`AGORA_SDK_DIR` 与 `AGORA_RTC_LIBRARY`。目录简化不修改 `mybot_platform/`、`mybot_ui/` 公共头文件命名空间及现有 CMake target。

开发工具程序保存在 `tools/`，当前包括 UI 预览。构建、检查、生成和打包自动化保存在 `scripts/`；交付模板保存在 `packaging/`。这些目录不拥有依赖快照。

## 固定保留范围

| 依赖 | 保留内容 | 有意裁剪 |
| --- | --- | --- |
| MyBot | SDK 公共头与实现、Linux adapters、PCM 提示资源、必需 CMake 模板和完整许可/notice | 仓库管理及重复协作流程、未使用 host 工具链、未启用测试和示例 |
| AOSL | 根 CMake、通用源码与头文件、Linux HAL、库内测试实现和完整许可 | 非 Linux HAL、仓库管理文件、独立测试入口 |
| LVGL | 现有生产子集、所需公共头、许可与版权记录、字体许可、已登记遮罩修补 | 项目集成说明外置到 [docs/dependencies/lvgl.md](../dependencies/lvgl.md) |
| RTSA | 官方 1.10.1 归档的全部 99 个文件：SDK 头/库、示例、说明和协作资源 | 不裁剪、不修改厂商文件 |

两个容易误删的输入必须保留：

- `vendor/mybot/docs/Doxyfile.in` 被 SDK 根 CMake 无条件配置；关闭 SDK 测试不解除该引用。`cmake/mybot_rtsa_ptime.cmake` 和 `cmake/mybotConfig.cmake.in` 也属于构建输入。
- `vendor/aosl/test/aosl_test.c` 被现有库构建逻辑引用；`AOSL_COMPILE_TEST=OFF` 只关闭独立测试入口，不能据此删除整个 `test/`。

LVGL 公共头可能包含未启用模块的声明，不能仅凭某个 `.c` 没有编译就删除对应头文件。本轮保留既有生产子集；mask callback 类型修补仍由独立补丁和结果哈希记录。

此前目录治理从 166 个 MyBot 自身文件、330 个 AOSL 文件及 720 个 LVGL 文件，整理为 110、124、719 个文件。262 个文件属于显式裁剪；LVGL 集成说明属于路径迁移，不计作裁剪。新增 RTSA 完整包的 99 个文件不属于该裁剪范围；官方 x86_64 内容与未使用示例也完整保留。原始版权、许可与必需 notice 未裁剪。所有文件以锁清单为准，测试结果另见[验证状态](../validation/README.md)。

## 锁文件与历史记录

`docs/dependencies/imported-sources.json` 保持原始字节，不将其旧路径与哈希改写为新布局。每个历史文件只能处于原位保留、映射保留或显式省略的一种状态：

| 记录 | 约束 |
| --- | --- |
| `snapshots` | MyBot、AOSL、LVGL、RTSA 各自记录身份和文件；目录互不包含，每个文件归属唯一 |
| `import_adaptations` | 历史路径映射到当前路径，保留原始哈希、当前结果哈希和适配理由 |
| `import_omissions` | 记录历史路径、`original_sha256`、`previous_path` 和理由；原路径与先前目标均不得残留 |
| `path_migrations` | 保存目录整体迁移的 `from`、`to` 和 `reason`，拒绝旧目录重新出现、目标缺失或循环规则 |
| `managed_assets` | 项目维护的 UI 视图，独立于第三方源码快照 |
| `generated_fonts` | 字库输入、生成器、工具版本及生成字库 |
| `required_inputs` | 已确认构建闭包内的源码、头、模板和许可输入，禁止省略 |
| `vendor_root` | 指定 `vendor/` 为完整登记边界，未登记的新文件或依赖不能静默通过 |

RTSA 作为新依赖快照登记，不回写原始导入历史。其官方归档和 AArch64 库哈希另见 [RTSA_SOURCE.txt](../dependencies/RTSA_SOURCE.txt)。

文件移动与裁剪分开登记。先移动后裁剪的文件，省略项保留此前目标位置，替代其适配项。整体迁移使用 `--relocate-prefix` 与 `--relocation-reason`，同步当前快照、资源、构建输入和映射目标，核对快照字节一致；历史导入键、原始哈希、省略项的历史路径和 RTSA 包内相对清单保持不变。锁更新器不能把有意删除的文件当作新基线，也不能用省略声明掩盖仍存在的文件。必需输入之外还必须保留适用版权和许可，工具保护不能代替许可判断。

## 维护流程

先核对上游身份、实际构建/头文件闭包、许可和现有补丁，再修改内容。批量映射、裁剪与独立快照登记使用同一更新器；每次先预览，再以完全相同参数加 `--write` 写入。具体命令见[依赖工作流](dependencies.zh-CN.md)。

```sh
python3 scripts/check-dependencies.py
python3 scripts/update-dependencies-lock.py
```

更新后必须检查差异，确认重复更新无变化，再执行双语/UBSan 主机检查、UI 基线、四套 AArch64 构建、ABI 与制品检查。记录真实命令和结果，不将旧版真机验收算作新二进制的验收。构建、检查和打包脚本均不得连接设备或隐式部署。
