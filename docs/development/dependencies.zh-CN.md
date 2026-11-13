# 依赖、补丁与生成资源

固定来源和许可见 [VENDORED_SOURCES](../../VENDORED_SOURCES.md) 与 [THIRD_PARTY_NOTICES](../../THIRD_PARTY_NOTICES.md)。本工程保留自维护代码与快照的边界，构建包装位于 `cmake/dependencies/`，不为迁移目录而重写上游源码。

MyBot、AOSL、LVGL 分别位于 `third_party/mybot/`、`third_party/aosl/`、`third_party/lvgl/`，各自拥有独立源码快照。MyBot 通过父工程设置的 `AOSL_DIR` 引用平级 AOSL，只创建一个 `aosl` target。视图和生成字库属于 `components/mybot_ui/` 的项目管理资源，不作为第三方源码依赖。目录范围及不可删除的构建输入见[第三方目录规则](third-party-layout.zh-CN.md)。

## 清单职责

| 记录 | 作用 |
| --- | --- |
| `dependencies.lock.json` | 独立源码快照、项目管理资源、版本/身份、结果哈希、路径映射、显式省略与必需输入 |
| `docs/dependencies/imported-sources.json` | 原始导入时的历史路径及哈希；不能当作当前工作树哈希直接比较 |
| `docs/dependencies/patches/` | 有意保留的本地上游修补 |
| `docs/dependencies/RTSA_SOURCE.txt` | 外部 RTSA 包、归档与目标库身份、配置、获取和许可边界 |
| `components/mybot_ui/fonts/sources.json` | 字体输入、字符集、生成工具版本与输出身份 |

```sh
python3 scripts/check-dependencies.py
```

该检查验证已登记的快照、迁移/裁剪结果和生成资源。源码快照目录互不重叠，每个第三方文件有唯一归属，`vendor_root` 内未登记文件会失败。历史导入记录保持原样，旧路径通过当前锁中的映射或显式省略解释；已应用补丁的文件应等于登记的修补结果，不能要求其回到原始导入哈希。省略项必须记录原始哈希、先前目标路径和理由，且这些位置不能残留文件。许可、补丁和 `required_inputs` 中的构建输入受保护，不能作为普通裁剪项。

## 更新流程

先选择准确的源版本或归档，核对官方来源、SHA256 与实际许可证；记录导入范围及原有本地修改。将可复用的修补保存为有序补丁，确认补丁适用于新的上游输入。分别更新当前内容哈希、来源记录和许可，不把原始导入历史改写成新基线。

变更 SDK 时验证公共 API 兼容、音频 20 ms 契约、Linux adapters 和 RTSA exports；变更 LVGL 时保留 mask callback 类型修补或确认上游已包含等价修复。执行依赖检查、双语/UBSan、受影响目标构建和预览。说明新增许可或分发条件，更新验证矩阵；不得由版本号推断设备已通过。

[LVGL 集成说明](../dependencies/lvgl.md)与像素眼镜来源说明记录集成范围与适用许可，当前 Linux 构建以本目录和现行架构指南为准。`imported-sources.json` 内历史 `local_patches` 路径属于原始记录，补丁现位于 `docs/dependencies/patches/`。

更新结果哈希使用仓库维护脚本，先预览再写入：

```sh
python3 scripts/update-dependencies-lock.py
python3 scripts/update-dependencies-lock.py --write
python3 scripts/check-dependencies.py
```

更新器保留原始哈希及适配理由，拒绝历史清单被改写。新增或改变布局时使用以下参数；更新已有哈希不需要再次提供身份参数：

| 参数 | 用途 |
| --- | --- |
| `--map ORIGINAL=CURRENT` 与 `--reason ORIGINAL=说明` | 登记单文件迁移或新适配 |
| `--map-prefix OLD=NEW` 与 `--reason-prefix OLD=说明` | 将目录移动展开成逐文件映射，拒绝重复、重叠和循环规则 |
| `--omit ORIGINAL=理由` | 为已经审查并删除的历史输入登记省略，保留原始 SHA256 与此前目标位置 |
| `--add-snapshot NAME=DIR`、`--revision NAME=HASH`、`--source NAME=URL` | 登记独立的第三方源码快照及准确来源 |
| `--managed-asset NAME=DIR` | 登记项目管理资源分组 |
| `--required-input PATH`、`--license PATH` | 固定构建必需输入或保留许可，保护其不被裁剪 |
| `--vendor-root third_party` | 要求第三方根目录仅含 README 与已登记的直接依赖目录 |

同一变更先执行不带 `--write` 的预览，核对每项变化，再以相同参数加 `--write` 写入。映射和省略不能同时作用于同一个历史输入。先平移再裁剪时，先登记完整移动，再逐文件登记删除；省略记录会替代该文件的适配记录，历史原路径和此前目标均不能残留。移动到 `docs/dependencies/` 的集成文档仍是映射保留，不登记为省略。

裁剪前从实际编译命令与头依赖确定闭包，并显式保留 CMake、模板、配置、许可证等必要输入。`required_inputs` 的更新不是自动允许删除文件：先核对构建与许可契约，再审查锁差异。更新器再次运行应无变化。更新锁后重新运行显式 SDK 配置，避免旧本地配置继续绑定过期锁；旧二进制/manifest/ABI 报告必须重建。

## 外部 SDK 与系统库

RTSA、工具链、开发 staging/sysroot 和运行库快照显式配置，见[构建指南](../user/build.zh-CN.md)。RTSA 固定为 AArch64 Linux GNU 1.10.1 的已记录 20 ms 包；归档元数据可能混入 x86_64 描述，需核对实际 ELF 与目标库哈希。配置/doctor 不下载依赖或连接设备。

运行库 manifest 证明 ABI 检查使用的依赖身份；它不是设备完整固件记录，也不是开发 sysroot。目标 ALSA/OpenSSL/libdrm/MPP/libc 仍使用系统库，不向设备系统目录覆盖这些依赖。源码仓库不包含 RTSA 二进制或个人 SDK。

## 字库

已提交 C 字库直接编入应用。普通构建无需 Pillow/FreeType；再生成需固定字体文件放入忽略的 `local/fonts/`，使用 sources 清单记录的 **Pillow 10.2.0 / FreeType 2.13.2** 环境：

```sh
local/font-tools/bin/python scripts/generate-ui-fonts.py
local/font-tools/bin/python scripts/generate-ui-fonts.py --check
```

环境路径只是示例，关键是输入哈希和工具版本一致。终端/签名为 DejaVu Sans Mono Bold 18/20 px，中文为 Source Han Sans SC Normal 40 px，配对码为 Montserrat Medium 64 px；4 bpp 抗锯齿。英文输入来自 Matplotlib 3.8.2 的对应字体，并与官方 wheel 核对。生成文件保留派生名称和原 Bitstream/SIL OFL 许可，不手工修改位图数据。0.1.1 的历史实现依据见[原生字体记录](../history/ui-native-fonts.zh-CN.md)。
