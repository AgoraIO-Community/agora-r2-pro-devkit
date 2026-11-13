# 故障排查

先记录 `./run.sh --version`、包名/校验和、实际命令和运行配置。结果范围见[验证矩阵](../validation/README.md)，反馈格式见[支持](../../SUPPORT.zh-CN.md)。

| 现象 | 排查与处理 |
| --- | --- |
| Python/CMake 版本错误 | 检查实际解释器与 `cmake --version`；需要 Python 3.10+、CMake 3.25+；脚本可用 `PYTHON` 指定解释器 |
| host 配置缺 OpenSSL/libdrm | 安装开发包；macOS 显式传 OpenSSL 前缀及 `RV1126B_TEST_DRM_INCLUDE`，不要为绕过错误静默跳过 DRM 门禁 |
| SDK 配置找不到目录 | 检查工具链、staging、运行库清单及仓库内 `third_party/rtsa/` 是否完整，再执行 `python3 scripts/doctor.py --target` |
| RTSA 哈希/架构/ptime 不匹配 | 恢复 `third_party/rtsa/` 的固定完整快照并重新配置；不要改锁文件绕过校验，或只根据包名、`.config` 推断库架构 |
| ABI 检查失败 | 查看 `abi-report.json`；确认运行库 manifest、开发文件、加载器、GLIBC 符号版本及 RPATH 匹配，不复制不明库覆盖系统 |
| 打包提示缺少变体或构建过期 | 显式选择 profile/language，重新构建对应目录；不要把旧二进制改名为新版本或关闭身份检查 |
| 打包拒绝已有目录 | 保留旧包后移动输出目录，或用新的 `--output-dir`；不要直接覆盖已验收制品 |
| existing service / lock | 退出另一个测试实例，确认原厂服务已完整停止；不要删除仍被进程持有的锁来绕过互斥 |
| DRM 初始化失败 | 检查显示占用与权限；用 `--no-ui` 隔离语音；确认物理模式及 `/dev/dri/card0` |
| 显示方向不对 | 用 `--ui-demo --rotation 90` 等检查；分清逻辑 640×480 与物理 480×640 |
| 英文重新出现明显 2×2 颗粒 | 确认运行 0.1.1 或之后的正确包；原生画布与字体不应被输出层再次放大 |
| ALSA busy / configure 失败 | 确认原程序声卡已关闭，检查现有 `botstation_capture` / `botstation_playback` 和 codec/mixer 配置 |
| private runtime directory | 检查 `/userdata` 已挂载，KV 目录为绝对路径、属于运行用户且权限 0700；不要删除绑定数据来试错 |
| 长时间等待网络/时间 | 检查可用 IPv4、默认路由、DNS 和系统时间；该应用只监测既有网络，不负责配网 |
| TLS 或服务错误 | 检查 CA bundle、系统日期、HTTPS 地址和服务端响应；保持证书及 hostname 校验 |
| ready 后不自动开聊 | 短触、Menu 或前台输入 `s` 后 Enter；这是默认显式开始行为 |
| 退出略慢 | 正在进行的 HTTPS 可能等待超时预算；记录时长及日志以判断是否超出预期 |
| 视频无画面而语音正常 | 确认选中视频程序、`rkaiq_3A_server` 仍运行、相机未占用、MPP 兼容及云端支持 JPEG；多相机时显式选节点 |

先从 UI 演示、`--no-ui` 和默认语音配置隔离范围，再改变一个参数复现。音质、AEC、物理按键位置、相机方向及长时间并发需要真机证据；主机预览和 ABI 检查无法给出这些结论。
