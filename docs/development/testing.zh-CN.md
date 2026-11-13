# 测试与验证

[验证矩阵](../validation/README.md)是当前结果的唯一来源。测试以能发现行为回归为目标；编译、host mock、真实 renderer、ABI 检查和真机验收分别记录。

## 标准主机门禁

格式工具基线为 clang-format 11（CI 使用 `requirements-dev.txt` 固定版本）；可在忽略的 `local/dev-tools` 虚拟环境安装该依赖。准备环境后，在仓库根目录执行：

```sh
python3 scripts/doctor.py --host
python3 scripts/check-dependencies.py
python3 scripts/check-quality.py
cmake --preset host-zh
cmake --build --preset host-zh
ctest --preset host-zh
cmake --preset host-en
cmake --build --preset host-en
ctest --preset host-en
cmake --preset host-ubsan
cmake --build --preset host-ubsan
ctest --preset host-ubsan
git diff --check
```

macOS 需要时为 doctor 加 `--drm-include PATH`，为 configure 加 `-DRV1126B_TEST_DRM_INCLUDE=PATH`，见[构建指南](../user/build.zh-CN.md)。完整门禁不能因缺少 DRM 头文件而静默省略测试。`scripts/test-host.sh` 保留兼容入口；运行的实际环境与日志分别登记。

现有检查覆盖像素旋转/dirty rectangle/pitch、输入语义和关闭、真实 LVGL 流程/表情/字形/文字边界、稳定身份 HMAC、显示 worker、DRM 失败清理与原输出恢复。治理增加配置/运行保护、ALSA 与网络失败清理/线程退出，以及工具链/制品一致性的边界检查。准确测试名称以 `ctest --preset host-zh -N` 和测试报告为准，不把测试数量当作验收标准。

UI 测试保留配对码前导零、最长状态行、23 种表情、双语文案以及禁止重新引入整帧 2×2 放大的像素检查。`ui-baseline` 会在临时目录生成中/英文对应的 34 帧，并与已验收 0.1.1 的 SHA256 清单逐一比较，防止迁移时误换字体、宏或资源。

## 目标与制品检查

语音/视频及中英文使用隔离目录。目标脚本应运行 ABI 检查并生成绑定源码/配置/二进制的 manifest，见[构建](../user/build.zh-CN.md)和[发布](releasing.zh-CN.md)。

目标检查验证 AArch64、动态加载器、依赖闭包、GLIBC 版本、RPATH、所选 RTSA 和配置身份；打包校验所选 profile/language、构建是否过期、manifest/ABI 与二进制是否对应及输出结构。负向场景包括缺失变体、混用语言/版本、修改已构建输入及过期 ABI。不能为了交付通过而手工编辑报告或关闭一致性检查。

第三方目录或导入范围变更必须额外检查：每个文件归属唯一、未登记文件被拒绝、批量映射冲突被拒绝、受保护输入不能省略、已省略文件不能残留、历史清单不变，以及锁更新器重复运行无变化。AOSL 平移先逐文件验证字节一致，再独立记录裁剪。四套目标构建使用全新目录；额外重新配置旧缓存，确认只有新 `AOSL_DIR` 生效、仍只有一个 `aosl` target。运行代码不变不代表可以省略目标构建与打包检查。

只有字体/生成器相关变更需要固定输入与工具环境下的 `generate-ui-fonts.py --check`。普通源码构建消费已提交 C 字库，不安装字体工具。源码格式、SPDX、脚本语法和文档链接按 CI 中的实际质量入口执行；第三方及生成文件不批量重排。

## CI 与真实覆盖

公共 CI 在 Linux 上执行主机中英文/UBSan、依赖/工程质量和工具测试。目标 SDK 不作为公开源码的一部分；需要匹配依赖的目标构建在具备授权 SDK 的环境执行并保存证据。workflow 存在不代表某个提交的 CI 已运行成功。

候选包真机回归由授权设备操作者按[上手指南](../user/getting-started.zh-CN.md)执行：校验/version、UI 字体/方向、旧绑定复用、配对、双向语音、输入/音量、重复开始/挂断、退出与再次启动。涉及视频时另验首帧/接收、方向/颜色、启停及并发；音质/AEC 和长期稳定性需专项场景。

记录版本/commit、包 SHA256、语言/profile、板型/系统、命令、执行者、结果及限制。不得将旧版本用户验收、host 预览或 ABI 通过写成新包真机通过。

目标 CI 为手动触发的 `target_build` 工作流，要求带 `rv1126b-sdk` 标签的授权 self-hosted runner，并通过环境提供工具链、staging、RTSA 和 runtime manifest；这些依赖存放在 checkout 之外。公共 PR job 不读取该环境，也不把未执行的目标 job 算作验证通过。
