# 贡献指南

[English](CONTRIBUTING.md) | [简体中文](CONTRIBUTING.zh-CN.md)

每次贡献围绕一个可观察的问题，说明兼容性影响及验证方法。较大的平台或依赖变更，先在 issue 或 PR 中说明边界，避免同时混入无关改动。

## 代码边界

应用配置与生命周期放在 `src/application/`；板级默认值、Linux 服务和驱动放在 `src/platform/`。`src/ui/` 的 renderer 与视图保持独立，不依赖 DRM、ALSA、MPP 或平台 worker 线程。平台仅使用 MyBot 公共头文件，不引用 SDK 私有接口。

内部路径调整保留 `mybot_platform/`、`mybot_ui/` 公共头文件命名空间和 CMake target。开发工具程序放在 `tools/`；构建、检查、生成与打包自动化放在 `scripts/`。

`vendor/` 为固定依赖快照，必需的 RTSA 快照完整保留官方包中的二进制和文档。更新时记录准确来源/revision、保留声明、登记本地补丁，并按[依赖流程](docs/development/dependencies.zh-CN.md)同步锁文件与来源。不要批量格式化导入文件或生成文件；字体位图通过固定输入再生成。

不要提交 token、私有设备身份、凭据、运行数据、个人 SDK 路径或无关构建产物。设备操作及其他仓库修改遵循所有者授权。

## 检查

需要 Python 3.10+、CMake 3.25+、OpenSSL 与 libdrm 开发文件。macOS 头文件配置见[构建指南](docs/user/build.zh-CN.md)。

```sh
python3 scripts/doctor.py --host
python3 scripts/check-dependencies.py
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

执行 [CI](.github/workflows/ci.yml) 使用的工程质量检查；仅对自维护 C/C++ 使用 `.clang-format`。改动影响目标输入时检查语音/视频构建和打包；仅改文档不必运行硬件测试。未执行的检查说明原因。[测试指南](docs/development/testing.zh-CN.md)区分编译、主机测试和真机证据。

中英文 README 保持行为和命令一致；按改动更新配置文档、`CHANGELOG.md` 及[验证状态](docs/validation/README.md)。旧交付包的真机结果不能移用于新二进制。

## 提交信息

使用 Conventional Commits：

```text
<type>[optional scope][!]: <subject>

[optional body]

[optional footer]
```

允许的 type 为 `feat`、`fix`、`docs`、`style`、`refactor`、`perf`、`test`、`build`、`ci`、`chore` 和 `revert`。subject 不超过 72 个字符，使用祈使表达，例如 `refactor(platform): isolate display ownership`。可选正文和 footer 之间留空行。不兼容变更同时使用 `!` 标记和 `BREAKING CHANGE:` footer 说明。

提交前安装仓库 hooks：

```sh
./scripts/setup-githooks.sh
```

PR 描述问题、最终行为、兼容性、实际检查及剩余硬件验证。贡献默认适用 [LICENSE](LICENSE)，具体文件的其他兼容许可优先。
