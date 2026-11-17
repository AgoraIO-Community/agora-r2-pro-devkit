# 安全政策

[English](SECURITY.md) | [简体中文](SECURITY.zh-CN.md)

疑似安全漏洞请通过仓库的
[私密漏洞报告](https://github.com/AgoraIO-Community/agora-r2-pro-devkit/security/advisories/new)
提交。不要在公开 issue 中发布凭据、利用细节或设备私密数据。项目不承诺固定响应时限。

请说明受影响版本/commit、板型和固件基线、构建 profile、复现步骤、影响及最小
脱敏示例。不要发送 token、Wi-Fi 密码或 PSK、配对码、完整 KV 目录、私有 SDK
归档或设备标识。需要敏感证据时，请先描述内容，由维护方另行安排适当的交换方式。

安全修复在 `dev` 开发并纳入下一标签版本。支持的报告对象为最新标签版本（存在时）和当前
`dev`；旧版问题在同样影响支持对象时评估。不承诺历史版本回补或固定支持周期。
部署候选和硬件覆盖以[验证状态](docs/validation/README.md)为准。

本应用以 root 管理网络和硬件资源。涉及上游组件时，请提供
[依赖锁](dependencies.lock.json)中的版本；维护方通过相应上游报告渠道协调修复。
普通缺陷使用公开 issue，见[支持说明](SUPPORT.zh-CN.md)。
