# 操作手册的维护与生成

[中文 PDF](../device-operations.zh-CN.pdf)和[英文 PDF](../device-operations.en.pdf)分别由
[中文源稿](../user/device-manual.zh-CN.md)、[英文源稿](../user/device-manual.en.md)及对应 UI 图片生成。
手册为完整的逐步操作指南，专题行为以[文档索引](../README.md)所列参考指南为准。
源稿使用 `<!-- page -->` 分页；目录、书签及可选取的代码由生成器排版。

## 一次准备

手册工具独立于应用构建，不需要设备或目标 SDK：

```sh
python3 -m venv build/manual-tools
build/manual-tools/bin/python -m pip install -r scripts/requirements-manual.txt
cmake --preset host-zh
cmake --preset host-en
```

macOS configure 的 libdrm 头文件设置与普通主机构建相同，见[构建指南](../user/build.zh-CN.md)。
中英文使用同一套仓库内可再分发的固定字体，不依赖 macOS/Ubuntu 系统字体。
`MyBot Manual Sans` 是由已锁定的 Source Han Sans 输入转换并重命名的 TrueType 子集，
使用 SIL OFL；代码字体为未修改的 DejaVu Sans Mono Bold。完整条款、输入 URL、摘要、
派生方式及工具版本见 [fonts/sources.json](../user/assets/fonts/sources.json)及同目录许可。
普通手册生成不下载字体，也不需要原始 OTF。

## 更新图片及 PDF

修改 UI、源稿、版本或生成工具后，在仓库根执行：

```sh
build/manual-tools/bin/python scripts/check-device-manual.py --refresh \
  --build-zh build/host-zh --build-en build/host-en \
  --date 2026-11-17
build/manual-tools/bin/python scripts/check-device-manual.py --check
build/manual-tools/bin/python scripts/check-device-manual.py --reproduce \
  --build-zh build/host-zh --build-en build/host-en
```

`--date` 改为本次文档日期；不指定时沿用 manifest 日期。刷新命令先构建两种语言的
`mybot-ui-preview`，通过受维护的 `--manual` 场景导出 16 张 640×480 逻辑画面，
再生成 PNG 和 PDF，最后登记输入/输出摘要。场景使用虚构热点 `mybot-a1b2` 和配对码
`000042`，图片是 renderer 输出，不是真机照片，不代表新的硬件验证。

`docs/user/assets/manual-manifest.json` 固定文档日期、应用版本、UI 内容摘要及输入/输出
身份。封面显示 UI 内容摘要，不读取 Git HEAD；相同内容在后续提交或无 `.git` 的源码包
中仍使用相同基线。普通质量检查只用标准库校验 manifest，发现 UI、文档、字体或版本
变动后没有同步图片/PDF 时失败。完整重现另外比较图片像素与 PDF 字节，并检查页数、
字体嵌入、可选取文字和目录跳转；CI 单独运行此链路。

只需要排版草稿时，可用 `generate-device-manual.py --language zh-CN --output /tmp/manual.pdf`；
`--language en-US` 使用英文源稿。`--date`、`--baseline` 及字体覆盖只用于明确的草稿输入，
正式刷新使用登记的内容基线与固定字体。生成器拒绝代码行超宽、缺字和页面溢出。

## 新增文字字形

缺字检查失败时，先通过[UI 字体输入记录](../../src/ui/fonts/sources.json)取得原始
`SourceHanSansSC-Normal.otf`，保持其锁定摘要，再重新生成手册字体：

```sh
build/manual-tools/bin/python scripts/generate-manual-font.py \
  --source local/fonts/SourceHanSansSC-Normal.otf
```

该脚本使用固定 fonttools 4.60.1，保留原著作权，重命名字体，采用固定时间戳并按源稿
选字。脚本同步 `fonts/sources.json` 的输出摘要；保留完整 OFL，再执行上述刷新及重现。
不要手工修改字体轮廓或用系统字体替代正式输入。

逐页检查标题、代码、图注、截图比例及操作与实际功能一致。手册生成与设备测试分别
记录，实际验收范围以[验证状态](../validation/README.md)为准。
