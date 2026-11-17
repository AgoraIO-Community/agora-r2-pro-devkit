# 依赖、补丁与生成资源

固定来源和许可见 [VENDORED_SOURCES](../../VENDORED_SOURCES.md) 与 [THIRD_PARTY_NOTICES](../../THIRD_PARTY_NOTICES.md)。本工程保留自维护代码与快照的边界，构建包装位于 `cmake/dependencies/`，不为迁移目录而重写上游源码。

MyBot、AOSL、LVGL、RTSA 分别位于 `vendor/mybot/`、`vendor/aosl/`、`vendor/lvgl/`、`vendor/rtsa/`，各自拥有独立依赖快照。RTSA 是工程必需依赖，完整保留官方包。MyBot 通过父工程设置的 `AOSL_DIR` 引用平级 AOSL，只创建一个 `aosl` target。视图和生成字库属于 `src/ui/` 的项目管理资源，不作为第三方源码依赖。目录范围及不可删除的构建输入见[第三方目录规则](third-party-layout.zh-CN.md)。

## 当前清单与历史档案

| 记录 | 作用 |
| --- | --- |
| `dependencies.lock.json`，schema 2 | 当前四项 vendor 快照、补丁、字库、许可、必需输入和外部运行库身份 |
| `docs/history/dependencies-lock-v1.json` | 最后一个 schema 1 清单的原始字节，保留此前目录迁移、适配和省略记录 |
| `docs/dependencies/imported-sources.json` | 原始导入记录；字节不变，不按其中的旧路径检查当前源码 |
| `docs/dependencies/patches/` | 上游本地修补及其原始输入、补丁和当前结果身份 |
| `src/ui/fonts/sources.json` | 字体输入、字符集、生成工具版本与输出身份 |

活动 lock 通过 `history` 中的路径和 SHA256 固定两份历史档案，只校验档案身份，不将历史映射作为当前源码约束。应用与平台的正常维护不再需要更新历史导入记录。生成字库及适用许可仍有当前资源记录；自有 UI 视图源码不再另行冻结。

```sh
python3 scripts/check-dependencies.py
```

该检查离线验证当前目录与哈希、快照唯一归属、补丁结果、生成资源、许可和必需输入。`vendor_root` 中存在未登记文件或目录会失败。路径不能越界、含歧义或通过符号链接引用输入。完整 RTSA 另按官方包内清单核对，不能以重写普通文件列表接受裁剪。

## 更新流程

先核对准确的上游版本、官方来源、实际许可证和完整构建/头文件闭包，再修改依赖。更新当前来源、revision、范围、补丁及哈希时应解释原因；不要改写历史档案。新增依赖先登记名称、来源、revision、目录和范围，再显式选择该快照；目录调整直接审查当前路径差异，不维护新的历史映射链。

正常自有源码更新无需刷新依赖锁。重新生成字库后可预览：

```sh
python3 scripts/update-dependencies-lock.py
python3 scripts/update-dependencies-lock.py --write
python3 scripts/check-dependencies.py
```

更新器默认只刷新生成字库。vendor 内容改变时必须显式指定快照；补丁或结果、许可文字、必需输入改变时有独立选择项。参数路径均相对于仓库根目录：

| 参数 | 用途 |
| --- | --- |
| `--snapshot NAME` | 接受已经核对的普通 vendor 快照内容；不刷新完整官方包 |
| `--patch PATH` | 接受已审查的补丁及结果，不修改登记的原始上游哈希 |
| `--license PATH` | 接受或新增已核对的保留许可身份 |
| `--required-input PATH` | 接受或新增已经确认的构建必需输入身份 |
| `--write` | 原子写入；默认仅预览 |
| `--json` | 输出结构化变化，便于自动检查 |

例如核对过的 LVGL 修补变更，先预览，再用相同参数写入：

```sh
python3 scripts/update-dependencies-lock.py \
  --snapshot lvgl \
  --patch docs/dependencies/patches/lvgl-mask-callbacks.patch
python3 scripts/update-dependencies-lock.py \
  --snapshot lvgl \
  --patch docs/dependencies/patches/lvgl-mask-callbacks.patch --write
```

选择快照不会自动接受许可文字变更，也不能移除已固定的必需输入或许可/notice 文件。确需改变这些依赖契约时，应单独审查当前清单和构建，明确结果，保留所需版权与许可。工具保护不能代替上游身份和许可判断。

同一变更重复更新应无差异。更新锁后重新执行 SDK 配置，旧本地配置绑定的是旧 lock 哈希；目标二进制、manifest 和 ABI 报告需重新构建。运行双语、UBSan、相关目标构建、资源基线及制品检查，并记录实际证据；不将旧包的真机结果算作新制品验收。

旧版 `--map`、`--omit`、目录迁移等参数已退出活动工具；其历史结果完整保存在 schema 1 档案。schema 2 不再按旧路径冻结当前自有源码。

## 完整 RTSA 包与外部系统依赖

RTSA 固定为 AArch64 Linux GNU 1.10.1 的已记录 20 ms 包，完整内置于 `vendor/rtsa/`。官方归档 SHA256 为 `850834b5b925ffb7fb447e97cf9ec5dc33fe0a0f68108e1a4954ee7590870fbe`；99 个文件共 38,268,142 字节，逐文件登记在独立快照中。保留全部头文件、库、示例、原始说明与协作资源，不因当前构建仅使用部分内容而裁剪。归档内两个空 `bin/` 架构目录不含文件，不影响 Git 中完整文件集合的记录。

目标配置自动使用 `vendor/rtsa/agora_sdk/` 和其中的 `lib/aarch64/libagora-rtc-sdk.so`，不再要求外部 `--rtsa-sdk`。CMake 覆盖旧缓存的 RTSA 路径；本地配置仍绑定仓库外 SDK 或旧锁时，重新运行配置命令。原始 `.config` 含 x86_64 包描述，原样保留；实际目标选择同时核对 AArch64 ELF、库哈希和功能配置，不凭元数据中的名称判断架构。

更新 RTSA 时从已核对官方来源与 SHA256 的完整归档重新导入，保留全部官方文件，并同步快照、归档身份、头文件/库哈希、配置契约和来源记录。不得只替换动态库或删除示例与厂商文档；完整包是本项目的依赖契约。

RTSA 快照中的 `archive_files` 保存相对于包根目录的官方文件哈希，与仓库路径清单 `files` 一一对应。普通锁刷新会拒绝完整包的文件修改、增添或删除；升级版本时需审查并显式更新官方归档身份与完整清单，不能通过刷新把裁剪后的目录接受为新基线。

工具链、开发 staging/sysroot 和运行库快照仍需显式配置，见[构建指南](../user/build.zh-CN.md)。运行库 manifest 证明 ABI 检查使用的依赖身份；它不是设备完整固件记录，也不是开发 sysroot。目标 ALSA/OpenSSL/libdrm/MPP/libc 仍使用系统库，不向设备系统目录覆盖这些依赖。个人 SDK 路径与构建缓存不进入源码仓库。配置/doctor 不下载依赖或连接设备。

## 字库

已提交 C 字库直接编入应用。普通构建无需 Pillow/FreeType；再生成需固定字体文件放入忽略的 `local/fonts/`，使用 sources 清单记录的 **Pillow 10.2.0 / FreeType 2.13.2** 环境：

```sh
local/font-tools/bin/python scripts/generate-ui-fonts.py
local/font-tools/bin/python scripts/generate-ui-fonts.py --check
```

环境路径只是示例，关键是输入哈希和工具版本一致。终端/签名为 DejaVu Sans Mono Bold 18/20 px，中文为 Source Han Sans SC Normal 40 px，配对码为 Montserrat Medium 64 px；4 bpp 抗锯齿。英文输入来自 Matplotlib 3.8.2 的对应字体，并与官方 wheel 核对。生成文件保留派生名称和原 Bitstream/SIL OFL 许可，不手工修改位图数据。0.1.1 的历史实现依据见[原生字体记录](../history/ui-native-fonts.zh-CN.md)。
