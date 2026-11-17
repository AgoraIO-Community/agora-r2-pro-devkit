# 操作手册的维护与生成

[中文手册 PDF](../device-operations.zh-CN.pdf)与[英文手册 PDF](../device-operations.en.pdf)分别由[中文源稿](../user/device-manual.zh-CN.md)、[英文源稿](../user/device-manual.en.md)及对应的 UI PNG 生成。截图分别保存在源稿同目录的 `assets/device-manual/` 和 `assets/device-manual-en/`。PDF 是面向操作者的独立交付文件，封面自动读取 CMake 版本和 Git revision。源稿使用 `<!-- page -->` 分页，每个步骤独占一页，目录和书签自动生成。

## 生成

仅编辑文档时不需要设备或目标 SDK。以下依赖只用于可选的 PDF 生成，不加入普通应用构建：

```sh
python3 -m venv build/manual-tools
build/manual-tools/bin/python -m pip install -r scripts/requirements-manual.txt
```

Ubuntu 24.04 安装字体后分别生成两种语言：

```sh
sudo apt install fonts-wqy-zenhei fonts-dejavu-core
build/manual-tools/bin/python scripts/generate-device-manual.py --language zh-CN
build/manual-tools/bin/python scripts/generate-device-manual.py --language en-US
```

macOS 中文默认使用系统简体黑体，英文使用 Arial；Ubuntu 英文使用 DejaVu Sans。其他环境可显式提供允许嵌入的 TrueType 字体；TTC 集合可用 `--font-index`、`--bold-index` 选择字形：

```sh
build/manual-tools/bin/python scripts/generate-device-manual.py \
  --font /path/to/chinese-regular.ttf \
  --bold-font /path/to/chinese-bold.ttf
```

`--language` 默认为 `zh-CN`，输出 `docs/device-operations.zh-CN.pdf`；`en-US` 输出 `docs/device-operations.en.pdf`。两种语言分别生成封面、目录、页眉及正文，并使用对应语言截图。`--date YYYY-MM-DD` 可固定文档日期，`--output PATH` 可输出检查副本。同一环境、字体、输入、revision 和日期下可重复生成；字体变化会改变排版和 PDF 字节。脚本检查命令宽度及预期页数，溢出时拒绝发布新 PDF。

## 更新 UI 配图

配图是当前 `src/ui/` renderer 的逻辑正向输出，不是屏幕照片。图片采用模拟流程和会话状态；热点与配对码必须为虚构示例，不能来自私有设备数据。当前截图的应用代码基线为 `33b0ceb`，分别使用 `zh-CN` 和 `en-US` 构建，画布为 640×480。不能通过覆盖图片文字将中文截图伪造成英文。

标准流程和设置页可由主机 UI 预览导出；补充 AP、时间和会话场景时，通过 renderer 公共接口提供合成状态。不要手工修改截图中的 UI 文案来模拟尚未实现的功能。生成器只排版已保存 PNG，不自动重建截图，不访问设备。

UI 或业务行为改变时，同步更新两种语言源稿和截图，再生成 PDF。中文版以中文构建和国内服务为新设备示例；英文版以英文构建和国际服务为新设备示例，已有绑定的设备始终保留实际配置。逐页检查代码可选取、字体嵌入、目录跳转、截图比例，以及图注和当前功能一致。具体设备验收继续记录在[验证状态](../validation/README.md)，截图或 PDF 生成不视为新真机验证。
