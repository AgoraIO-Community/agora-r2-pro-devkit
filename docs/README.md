# 文档索引

用户入口：[项目说明](../README.zh-CN.md) / [English README](../README.md)。所有构建和打包均在主机完成；设备部署由操作者单独执行。

| 用户指南 | 内容 |
| --- | --- |
| [上手与升级](user/getting-started.zh-CN.md) | 设备要求、解包、资源占用、UI/语音检查、回退 |
| [构建](user/build.zh-CN.md) | 主机开发、独立目标 SDK、presets 与交付包 |
| [配置](user/configuration.zh-CN.md) | CLI、环境变量、身份、语言与音频契约 |
| [故障排查](user/troubleshooting.zh-CN.md) | 构建、显示、音频、网络和摄像头问题 |

| 开发指南 | 内容 |
| --- | --- |
| [架构](development/architecture.zh-CN.md) | 模块边界、依赖方向和资源所有权 |
| [测试](development/testing.zh-CN.md) | 本地检查、CI、预览和真机回归 |
| [依赖与字体](development/dependencies.zh-CN.md) | 锁文件、快照、补丁、生成资源和 SDK 输入 |
| [第三方目录规则](development/third-party-layout.zh-CN.md) | 平级源码快照、唯一归属、迁移和显式裁剪 |
| [发布](development/releasing.zh-CN.md) | 制品身份、打包、验证及版本发布 |

[验证状态](validation/README.md)是当前结果的唯一来源。[依赖来源](../VENDORED_SOURCES.md)与[许可声明](../THIRD_PARTY_NOTICES.md)说明分发范围。

依赖集成记录：[LVGL](dependencies/lvgl.md)、[RTSA 来源与配置](dependencies/RTSA_SOURCE.txt)、[原始导入清单](dependencies/imported-sources.json)。

`history/` 保留实施当时的判断与命令，供追溯使用。历史方案不代表当前支持状态：

- [最初移植评估](history/rv1126b-feasibility.zh-CN.md)
- [0.1 首版测试步骤](history/first-validation.zh-CN.md)
- [0.1.1 原生文字渲染](history/ui-native-fonts.zh-CN.md)
- [0.1 构建与验收记录](history/validation-0.1.zh-CN.md)
- [工程治理方案与实施对应](history/governance-plan.zh-CN.md)
