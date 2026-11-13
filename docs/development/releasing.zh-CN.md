# 候选制品与发布

项目版本以根 `CMakeLists.txt` 为单一来源。当前为 **0.2.1 工程治理候选版**，尚未继承旧包的真机验收。[验证矩阵](../validation/README.md)区分本地检查、CI 和硬件状态。

## 构建身份

每个构建目录保留 `build-config.json`（CMake 配置）、`build-inputs.json`（编译前输入快照）与 `build-manifest.json`（最终二进制、ABI 与输入身份）。manifest 绑定版本、源码 revision/dirty、语言、voice/video、板型、SDK/RTSA、音频时长、工具链、运行库和资源。不能手工修改这些文件来给旧程序改版本。

工程治理完成后的开发候选允许脏源码，但必须如实标注。正式发布使用已提交的源码重新构建，再加 `--release`；仅提交已构建的源码不会让旧 manifest 自动成为干净发布身份。

## 生成候选包

先执行依赖检查、双语/UBSan、所选目标构建与 ABI 检查，详见[测试](testing.zh-CN.md)。默认中文全配置包：

```sh
python3 scripts/package-rv1126b.py --profile all --language zh-CN
```

默认读取 `build/rv1126b-voice-zh` 和 `build/rv1126b-video-zh`；可用 `--voice-build PATH` / `--video-build PATH` 显式覆盖。`all` 必须两个配置均齐全，不能依赖目录中碰巧存在的旧文件。单配置选择 `--profile voice` 或 `--profile video`，英文选择 `--language en-US`。

中文全配置名称为 `mybot-rv1126b-<version>-aarch64`；英文为 `mybot-rv1126b-<version>-en-US-aarch64`，单配置再附加 `-voice` / `-video`。交付输出包括解包目录、`.tar.gz` 和相邻 `.sha256`；`--output-dir PATH` 可指定独立输出位置。同名已有结果拒绝覆盖，保留已验收的 0.1.1 包。

包内包含所选二进制、RTSA 库、中英文 PCM 提示音、run.sh、用户说明、许可、字体来源、`build-manifest-voice.json` / `build-manifest-video.json`、对应 ABI 报告及 `SHA256SUMS`。公开 manifest/ABI 归一化宿主路径，保留依赖身份；本地报告仍保留诊断信息。

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
