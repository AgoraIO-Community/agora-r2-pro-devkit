# 依赖、补丁与生成资源

固定来源和许可见 [VENDORED_SOURCES](../../VENDORED_SOURCES.md) 与 [THIRD_PARTY_NOTICES](../../THIRD_PARTY_NOTICES.md)。本工程保留自维护代码与快照的边界，构建包装位于 `cmake/dependencies/`，不为迁移目录而重写上游源码。

## 清单职责

| 记录 | 作用 |
| --- | --- |
| `dependencies.lock.json` | 当前构建依赖的版本、身份、结果哈希、适配/路径映射与检查输入 |
| `docs/dependencies/imported-sources.json` | 原始导入时的历史路径及哈希；不能当作当前工作树哈希直接比较 |
| `docs/dependencies/patches/` | 有意保留的本地上游修补 |
| `docs/dependencies/RTSA_SOURCE.txt` | 外部 RTSA 包、归档与目标库身份、配置、获取和许可边界 |
| `components/mybot_ui/fonts/sources.json` | 字体输入、字符集、生成工具版本与输出身份 |

```sh
python3 scripts/check-dependencies.py
```

该检查验证已登记的快照/迁移结果和生成资源。历史导入记录保持原样，旧路径通过当前锁中的映射解释；已应用补丁的文件应等于登记的修补结果，不能要求其回到原始导入哈希。

## 更新流程

先选择准确的源版本或归档，核对官方来源、SHA256 与实际许可证；记录导入范围及原有本地修改。将可复用的修补保存为有序补丁，确认补丁适用于新的上游输入。分别更新当前内容哈希、来源记录和许可，不把原始导入历史改写成新基线。

变更 SDK 时验证公共 API 兼容、音频 20 ms 契约、Linux adapters 和 RTSA exports；变更 LVGL 时保留 mask callback 类型修补或确认上游已包含等价修复。执行依赖检查、双语/UBSan、受影响目标构建和预览。说明新增许可或分发条件，更新验证矩阵；不得由版本号推断设备已通过。

`third_party/lvgl/README.integration.md` 和像素眼镜来源说明记录集成范围与适用许可，当前 Linux 构建以本目录和现行架构指南为准。`imported-sources.json` 内历史 `local_patches` 路径属于原始记录，补丁现位于 `docs/dependencies/patches/`。

更新结果哈希使用仓库维护脚本，先预览再写入：

```sh
python3 scripts/update-dependencies-lock.py
python3 scripts/update-dependencies-lock.py --write
python3 scripts/check-dependencies.py
```

更新器保留原始哈希及适配理由，拒绝历史清单被改写。新增路径映射使用 `--map ORIGINAL=CURRENT`，新增适配必须同时给 `--reason ORIGINAL=说明`；检查差异后再提交。更新锁后重新运行显式 SDK 配置，避免旧本地配置继续绑定过期锁。

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
