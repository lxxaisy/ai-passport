<p align="right"><strong>简体中文</strong> · <a href="README.md">English</a></p>

# FoloToy AI Passport 固件

本个人 fork 保留 FoloToy AI Passport 上游硬件测试基线，并增加当前的 **Agent Monitor
随身终端** 应用，目标为 ESP32-C3（8 MB Flash、无 PSRAM、240×320 屏幕、三键）。

终端通过 USB Serial/JTAG 或加密 BLE 接收
[Agent Monitor](https://github.com/lxxaisy/agent-monitor) 发送的有限任务、提醒、额度和
设置快照，只读显示，不提供 Agent 审批、取消、重试或执行命令。完整的界面、协议、
资源边界、中文字体和主机渲染测试说明见：

- [Agent Monitor 随身终端说明](docs/agent-monitor-terminal.zh_CN.md)
- [English guide](docs/agent-monitor-terminal.md)
- [完整上游开发文档](docs/README.zh_CN.md)

## 构建与验证

使用 ESP-IDF 5.5.3，并按仓库环境文档完成准备，然后运行：

```sh
./tools/validate.sh --static
./tools/validate.sh --firmware
```

固件门禁会生成从 `0x0` 写入的合并镜像
`build/FoloToy-AI-Passport-full.bin`，构建产物已被忽略。主机 UI 渲染可以脱离硬件
运行；真机烧录、配对和验收仍需单独进行。

仓库只包含源代码、测试、文档和随附字体许可证。凭据、设备二维码密钥、构建产物及
个人数据不会纳入版本控制。

## 上游来源

本 fork 基于 [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport)，遵循 MIT
许可证。上游 `main` 基线保留在 fork 历史中；当前下游应用位于
`feature/agent-monitor-terminal` 分支。
