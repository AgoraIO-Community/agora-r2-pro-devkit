# 第三方依赖目录与维护规则

`third_party/` 按直接依赖平铺：每项依赖保存一份源码，具有独立版本、文件清单与许可。项目构建集成位于 `cmake/dependencies/`，项目专用说明位于 `docs/dependencies/`，应用视图和生成资源位于 `components/`。

## 现行结构

```text
third_party/
├── README.md
├── mybot/                # MyBot SDK 自身源码、必要 Linux adapters 与构建输入
├── aosl/                 # AOSL 公共实现与 Linux HAL
└── lvgl/                 # LVGL 固定生产子集

cmake/dependencies/
├── sdk.cmake             # MyBot、AOSL 和外部 RTSA 集成
├── public-headers.cmake
├── lvgl.cmake
└── lvgl-sources.cmake

docs/dependencies/        # 集成说明、补丁和原始导入记录
dependencies.lock.json   # 当前身份、文件清单、迁移、裁剪与资源记录
```

MyBot 依赖 AOSL 和 RTSA，UI 依赖 LVGL；目录平铺后，这些链接依赖继续由 CMake targets 表达。RTSA 是显式配置的外部 SDK；ALSA、OpenSSL、libdrm、MPP 和 libc 使用目标系统开发文件与运行库，不复制到 `third_party/`。

`components/mybot_ui/` 中的视图由锁文件的 `managed_assets` 登记；字库及生成输入由 `generated_fonts` 登记。两者均由项目维护，与三项第三方源码快照分别校验，资源携带的文件级许可继续适用。

## MyBot 与 AOSL 的构建边界

AOSL 原嵌套目录的 330 个文件先原样移动，再单独登记裁剪；版本保持为已有记录的固定 commit `84e086084ebcd0ae2455a0ce5721950c5fe2e656`，不将其描述为本轮重新核验过的上游版本。

父工程在调用 MyBot 的 `add_subdirectory()` 前设置 `AOSL_DIR`，由 SDK 创建唯一的 `aosl` target：

```cmake
set(AOSL_DIR "${PROJECT_SOURCE_DIR}/third_party/aosl"
    CACHE PATH "Pinned AOSL source snapshot" FORCE)
```

`FORCE` 覆盖旧构建缓存中的嵌套位置；不存在对应 CMake 输入时配置失败。父工程不再创建第二个 AOSL target，也不修改 SDK/AOSL 的运行代码。打包从 `third_party/aosl/LICENSE` 获取完整许可，包内名称仍为 `aosl-LICENSE.txt`。

路径迁移会改变构建身份，已有二进制、manifest 和 ABI 报告需要重新生成。验收使用隔离的全新构建目录，并检查旧缓存重新配置后采用新 `AOSL_DIR`。

## 固定保留范围

| 依赖 | 保留内容 | 有意裁剪 |
| --- | --- | --- |
| MyBot | SDK 公共头与实现、Linux adapters、PCM 提示资源、必需 CMake 模板和完整许可/notice | 仓库管理及重复协作流程、未使用 host 工具链、未启用测试和示例 |
| AOSL | 根 CMake、通用源码与头文件、Linux HAL、库内测试实现和完整许可 | 非 Linux HAL、仓库管理文件、独立测试入口 |
| LVGL | 现有生产子集、所需公共头、许可与版权记录、字体许可、已登记遮罩修补 | 项目集成说明外置到 [docs/dependencies/lvgl.md](../dependencies/lvgl.md) |

两个容易误删的输入必须保留：

- `third_party/mybot/docs/Doxyfile.in` 被 SDK 根 CMake 无条件配置；关闭 SDK 测试不解除该引用。`cmake/mybot_rtsa_ptime.cmake` 和 `cmake/mybotConfig.cmake.in` 也属于构建输入。
- `third_party/aosl/test/aosl_test.c` 被现有库构建逻辑引用；`AOSL_COMPILE_TEST=OFF` 只关闭独立测试入口，不能据此删除整个 `test/`。

LVGL 公共头可能包含未启用模块的声明，不能仅凭某个 `.c` 没有编译就删除对应头文件。本轮保留既有生产子集；mask callback 类型修补仍由独立补丁和结果哈希记录。

本轮从迁移前的 166 个 MyBot 自身文件、330 个 AOSL 文件及 720 个 LVGL 文件，整理为 110、124、719 个文件。262 个文件属于显式裁剪；LVGL 集成说明属于路径迁移，不计作裁剪。原始版权、许可与必需 notice 未裁剪。所有文件以锁清单为准，测试结果另见[验证状态](../validation/README.md)。

## 锁文件与历史记录

`docs/dependencies/imported-sources.json` 保持原始字节，不将其旧路径与哈希改写为新布局。每个历史文件只能处于原位保留、映射保留或显式省略的一种状态：

| 记录 | 约束 |
| --- | --- |
| `snapshots` | MyBot、AOSL、LVGL 各自记录身份和文件；目录互不包含，每个文件归属唯一 |
| `import_adaptations` | 历史路径映射到当前路径，保留原始哈希、当前结果哈希和适配理由 |
| `import_omissions` | 记录历史路径、`original_sha256`、`previous_path` 和理由；原路径与先前目标均不得残留 |
| `managed_assets` | 项目维护的 UI 视图，独立于第三方源码快照 |
| `generated_fonts` | 字库输入、生成器、工具版本及生成字库 |
| `required_inputs` | 已确认构建闭包内的源码、头、模板和许可输入，禁止省略 |
| `vendor_root` | 指定 `third_party/` 为完整登记边界，未登记的新文件或依赖不能静默通过 |

文件移动与裁剪分开登记。先移动后裁剪的文件，省略项保留此前目标位置，替代其适配项。锁更新器不能把有意删除的文件当作新基线，也不能用省略声明掩盖仍存在的文件。必需输入之外还必须保留适用版权和许可，工具保护不能代替许可判断。

## 维护流程

先核对上游身份、实际构建/头文件闭包、许可和现有补丁，再修改内容。批量映射、裁剪与独立快照登记使用同一更新器；每次先预览，再以完全相同参数加 `--write` 写入。具体命令见[依赖工作流](dependencies.zh-CN.md)。

```sh
python3 scripts/check-dependencies.py
python3 scripts/update-dependencies-lock.py
```

更新后必须检查差异，确认重复更新无变化，再执行双语/UBSan 主机检查、UI 基线、四套 AArch64 构建、ABI 与制品检查。记录真实命令和结果，不将旧版真机验收算作新二进制的验收。构建、检查和打包脚本均不得连接设备或隐式部署。
