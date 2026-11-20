# 依赖、补丁与生成资源

固定来源和许可见 [VENDORED_SOURCES](../../VENDORED_SOURCES.md) 与 [THIRD_PARTY_NOTICES](../../THIRD_PARTY_NOTICES.md)。本工程保留自维护代码与快照的边界，构建包装位于 `cmake/dependencies/`，不为迁移目录而重写上游源码。

## 快照与构建边界

| 快照 | 构建边界和必需输入 |
| --- | --- |
| `vendor/mybot/` | SDK 公共头、实现、Linux adapters 和 PCM 资源。SDK 的 CMake 使用 `docs/Doxyfile.in`、RTSA 帧长检查及包配置模板。 |
| `vendor/aosl/` | 通用实现及 Linux HAL。父工程设置 `AOSL_DIR`，由 MyBot 创建唯一的 `aosl` target。库构建还使用 `test/aosl_test.c`。 |
| `vendor/lvgl/` | 生产渲染所需源码、头文件、许可及已登记的 mask callback 补丁。见[LVGL 集成说明](../dependencies/lvgl.md)。 |
| `vendor/rtsa/` | 完整官方 SDK 包，包含所有架构的库、示例和文档。RV1126B 链接其中的 AArch64 库。 |

`src/ui/` 中的视图由项目维护，生成字库由字体输入和生成器清单管理。ALSA、OpenSSL、libdrm、MPP 和 libc 使用目标系统提供的库。

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

## 完整 RTSA 包与外部系统依赖

当前 RTSA 版本、归档 SHA256 和目标库身份见[固定来源记录](../dependencies/RTSA_SOURCE.txt)。构建选择 `vendor/rtsa/agora_sdk/lib/aarch64/libagora-rtc-sdk.so`，同时核对 AArch64 ELF、库摘要和 20 ms 配置。

升级 RTSA 时从官方来源核验归档，完整导入，更新 `archive_files`、头文件、库、配置和来源记录。普通锁刷新只接受与官方包文件清单一致的内容。旧本地配置绑定仓库外路径或旧依赖锁时，先重新配置 SDK。

工具链、开发 staging/sysroot 和运行库快照仍需显式配置，见[构建指南](../user/build.zh-CN.md)。运行库 manifest 证明 ABI 检查使用的依赖身份；它不是设备完整固件记录，也不是开发 sysroot。目标 ALSA/OpenSSL/libdrm/MPP/libc 仍使用系统库，不向设备系统目录覆盖这些依赖。个人 SDK 路径与构建缓存不进入源码仓库。配置/doctor 不下载依赖或连接设备。

## 字库

已提交 C 字库直接编入应用。普通构建无需 Pillow/FreeType；再生成需固定字体文件放入忽略的 `local/fonts/`，使用 sources 清单记录的 **Pillow 10.2.0 / FreeType 2.13.2** 环境：

```sh
local/font-tools/bin/python scripts/generate-ui-fonts.py
local/font-tools/bin/python scripts/generate-ui-fonts.py --check
```

环境路径只是示例，关键是输入哈希和工具版本一致。终端/签名为 DejaVu Sans Mono Bold 18/20 px，中文为 Source Han Sans SC Normal 40 px，配对码为 Montserrat Medium 64 px；4 bpp 抗锯齿。英文输入来自 Matplotlib 3.8.2 的对应字体，并与官方 wheel 核对。生成文件保留派生名称和原 Bitstream/SIL OFL 许可，不手工修改位图数据。
