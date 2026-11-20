# 候选制品与发布

项目版本以根 `CMakeLists.txt` 为单一来源，执行 `python3 scripts/project-version.py` 查看。更新版本时执行 `python3 scripts/project-version.py --set X.Y.Z`，同步中英文 README 中带标记的版本声明；`python3 scripts/project-version.py --check` 检查是否一致。CMake 配置使用根版本，打包再从构建 manifest 读取版本，不在指南中维护另一份当前版本号。[验证矩阵](../validation/README.md)区分本地检查、CI 和硬件状态，旧包的真机验收不自动适用于新包。

版本、UI、手册源稿或生成输入改变后，先按[手册维护指南](manual.zh-CN.md)刷新两种语言的配图及 PDF，再运行质量门禁：

```sh
build/manual-tools/bin/python scripts/check-device-manual.py --refresh \
  --build-zh build/host-zh --build-en build/host-en
python3 scripts/check-quality.py
```

首次使用先按维护指南准备固定工具和双语主机构建；需要更新文档日期时显式提供 `--date YYYY-MM-DD`。普通质量检查使用标准库核对手册 manifest，故意拒绝版本、UI 或源稿已更新而图片/PDF 仍旧的状态。PDF 的内容基线与生成日期来自 manifest，不随提交 revision 漂移。

## 构建身份

每个构建目录保留 `build-config.json`（CMake 配置）、`build-inputs.json`（编译前输入快照）与 `build-manifest.json`（最终二进制、ABI 与输入身份）。ELF 绑定产品源码、CMake 定义、嵌入门户等生成输入，以及语言、voice/video、板型、SDK、运行库和工具链。构建 manifest 的 `inputs.source` 同时记录编译时 Git 提交和工作树状态。

构建输入和 manifest 使用 schema 2，嵌入式输入身份使用独立 schema 1。本地完整快照保留原始路径、参数、编译器/工具链字节摘要及 Git 诊断信息，仍逐项检查 freshness；二进制中的输入摘要改由可迁移身份计算，绑定源码内容、依赖内容和完整参数语义，归一化明确配置的 SDK/构建路径及路径参数。生成的 Zig wrapper 归一化编译器位置和缓存目录，同时保留其他 shell 文本、参数、引号及实际编译器内容的身份。未知参数仍参与摘要，不直接公开其可能包含的本机路径。

旧构建目录必须重新配置并编译，不能重新封装旧二进制生成新格式包。已安装的旧 release 仍按摘要、profile 和 `--version` 校验后回退，显式 0.1.1 归档导入保持兼容。输入摘要可迁移不等同于完整目标 ELF 的逐字节可复现；后者仍需独立工具链与构建证据。

打包时，`package-manifest.json` 记录打包时 Git 提交和工作树状态；`SHA256SUMS` 登记归档内的程序、资源、说明、许可证和这份 manifest。仅更新验证记录、操作说明或运行时资源，可以复用编译输入一致的 ELF，其构建 manifest 仍保留原提交。正式发布要求编译和打包都来自干净提交。脏源码的开发构建须在干净源码上重新构建才能正式发布。

正式打包再次核对目标运行库清单与 `dependencies.lock.json` 的固件基线，校验 staging 库的实际字节，并从应用、监督器和重置工具的 ELF 读取动态依赖。缺少解释器、未锁定的系统库、缺失或摘要不匹配的清单都会阻止归档。

## 生成候选包

完整运行包由 `scripts/package-rv1126b.py` 统一装配和校验，普通 `cmake --install` 不安装应用运行环境。CMake 负责程序与 ABI/构建 manifest，打包脚本负责入口、运行库、资源、许可及摘要，避免维护两套不一致的交付布局。

先执行依赖检查、双语/UBSan、所选目标构建与 ABI 检查，详见[测试](testing.zh-CN.md)。默认中文全配置包：

```sh
python3 scripts/package-rv1126b.py --profile all --language zh-CN
```

默认读取 `build/rv1126b-voice-zh` 和 `build/rv1126b-video-zh`；可用 `--voice-build PATH` / `--video-build PATH` 显式覆盖。`all` 必须两个配置均齐全，不能依赖目录中碰巧存在的旧文件。单配置选择 `--profile voice` 或 `--profile video`，英文选择 `--language en-US`。

中文全配置名称为 `mybot-rv1126b-<version>-aarch64`；英文为 `mybot-rv1126b-<version>-en-US-aarch64`，单配置再附加 `-voice` / `-video`。交付输出包括解包目录、`.tar.gz` 和相邻 `.sha256`；`--output-dir PATH` 可指定独立输出位置。同名已有结果拒绝覆盖，保留已验收的 0.1.1 包。

包内包含所选二进制、仓库内固定的 AArch64 RTSA 库、中英文 PCM 提示音、run.sh、用户说明、许可、字体来源、`build-manifest-voice.json` / `build-manifest-video.json`、对应 ABI 报告及 `SHA256SUMS`。RTSA 来源记录、原始中英文 README 和 `PACKAGE_INFO` 随包保存在 `licenses/`，名称分别为 `RTSA_SOURCE.txt`、`RTSA-README.zh-CN.md`、`RTSA-README.en.md`、`RTSA-PACKAGE_INFO.txt`。源码树保留完整 SDK，设备运行包只携带选定 AArch64 库。公开 manifest/ABI 归一化宿主路径，保留依赖身份；本地报告仍保留诊断信息。

中文包附 `README.zh-CN.md`，英文包附 `README.en.md`，各自附带对应语言的完整 PDF，解包后可从 README 离线打开。其他项目文档链接固定到 `package-manifest.json` 中的打包提交，该提交推送后可在线打开。项目自有授权摘要和第三方总声明的包内副本将源码相对链接转换为已附许可文件或 HTTPS 源码链接，正文与官方商务入口保留；vendor 原始许可及 SDK 文档按原字节附带。

`licenses/cjson-LICENSE.txt` 保留 namespaced cJSON 原注释中的完整 Dave Gamble MIT 版权和条款；测试逐字比较源码注释提取结果、保留副本及归档内容。许可副本另在依赖锁登记，不以根许可证或简述替代。

部署归档门禁要求应用及监督器 ABI 报告明确包含 `errors=[]`，并将实际 RTSA 运行库逐 profile 与二进制已绑定的 manifest 摘要比较。删除验证结果或替换运行库后重新计算归档摘要，不能代替原有构建/ABI 证据。

归档顺序、所有者和时间固定。默认时间来自 Git HEAD 提交时间（无 Git 时为 0），可用 `SOURCE_DATE_EPOCH` 显式指定。相同输入和时间重复打包应产生相同字节，需选择两个不同且空的输出目录比较，避免覆盖规则影响检查。

## 正式发布

1. 完成候选包的静态/host/目标与制品检查，保存命令、版本和包哈希。
2. 由授权设备操作者执行现有语音/UI回归，另记录所宣称的视频/音质/长期场景；未完成的场景明确保留限制。
3. 核对 RTSA 及其他内容的实际再分发条件和许可清单。测试包曾被本地使用不自动赋予公开分发权。
4. 更新 CHANGELOG 与验证矩阵，在获准提交后从干净 revision 重新构建。正式打包示例：

   ```sh
   python3 scripts/package-rv1126b.py --profile all --language zh-CN --release
   ```

5. 检查最终归档校验和、解包路径、包内校验和、manifest 和文档。发布/推送/tag/设备部署依据当前任务的实际授权执行，不由打包脚本隐式完成。

本指南是维护流程，不代表当前候选已发布或任何远端 CI 已运行。上传包与上板步骤见[上手指南](../user/getting-started.zh-CN.md)。
