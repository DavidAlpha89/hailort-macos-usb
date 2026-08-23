# Apple Silicon macOS 上的 HailoRT USB

[![macOS](https://img.shields.io/badge/macOS-Apple%20Silicon-000000?logo=apple)](https://github.com/relaxcloud-cn/hailort-macos-usb)
[![HailoRT](https://img.shields.io/badge/HailoRT-5.3.0-38a169)](https://github.com/hailo-ai/hailort/tree/v5.3.0)
[![Device](https://img.shields.io/badge/device-ASUS%20UGen300-2563eb)](https://github.com/relaxcloud-cn/hailort-macos-usb)
[![License](https://img.shields.io/badge/license-MIT-yellow)](hailort/LICENSE)

[English](README.md) | [简体中文](README.zh-CN.md)

这是一个面向 Apple Silicon macOS 的实验性 HailoRT 5.3.0 用户态移植，当前目标硬件为
ASUS UGen300 USB AI 加速器。

它通过 Homebrew `libusb` 与 Hailo-10H 通信，不安装 Kext、DriverKit 扩展、系统扩展或
特权守护进程。

> [!IMPORTANT]
> 这是社区项目，不是 Hailo 或 ASUS 官方发布的 macOS 驱动。目前只在一台 Mac Studio
> M2 Ultra 及下表所列设备和工作负载上完成验证。

## 当前能力

| 能力 | 状态 |
|---|---|
| Apple Silicon 原生构建 | ✅ |
| UGen300 USB 扫描及 Hailo HRPC 传输 | ✅ |
| 固件与设备识别 | ✅ |
| HAILO10H 视觉 HEF 推理 | ✅ |
| Qwen3-1.7B GenAI 推理 | ✅ |
| LLM 加载、首字延迟和吞吐基准 | ✅ |
| Ollama 与 OpenAI 兼容接口 | ✅ [配套仓库](https://github.com/relaxcloud-cn/hailo-ollama-macos) |
| 交互式 Runtime RPC Shell | 🧪 实验功能，设备端构建默认关闭 |
| Intel Mac、PCIe、DMA-BUF、GStreamer | ❌ 暂不支持 |
| pyHailoRT wheel | ❌ 尚未打包 |
| 代码签名与公证 | ❌ 尚未实现 |

## 实验性 Runtime RPC Shell

`codex/runtime-rpc-shell` 分支提供一个面向本地实验 Runtime 的交互式 Root PTY。设备端必须
使用 `HAILO_ENABLE_DEBUG_RPC_SHELL=ON` 构建；该选项默认关闭，官方 Runtime 不会注册这些
HRPC action。

> [!WARNING]
> 这是设备端 Root Shell。只应在你拥有或获准测试的设备上使用，不要公开令牌或分发修改后
> 的厂商 Runtime。

加载匹配的实验 Runtime 后，在 macOS 上进入交互 Shell：

```bash
scripts/macos/debug_shell.sh
```

执行单条短命令：

```bash
scripts/macos/debug_shell.sh -c 'id; uname -a'
```

实验 Runtime 由 Loader 上传到 RAM；本项目不会调用 Flash 更新流程，设备 reboot 或断电后
会回到 Loader。令牌、Runtime、qcow2 和 HEF 均不应提交到 Git。构建、ABI 兼容和协议边界
请参阅[交互式 Runtime RPC Shell 指南](docs/runtime-rpc-shell.md)。

## 快速开始

### 1. 克隆并构建

```bash
git clone https://github.com/relaxcloud-cn/hailort-macos-usb.git
cd hailort-macos-usb

brew install cmake libusb pkg-config qemu
scripts/macos/build_hailort_usb.sh
scripts/macos/doctor.sh
```

主要构建产物：

```text
build-macos-usb/hailort/hailortcli/hailortcli
build-macos-usb/hailort/libhailort/examples/genai/chat_example/chat_example
build-macos-usb/hailort/libhailort/examples/genai/llm_benchmark/llm_benchmark
```

### 2. 准备 UGen300 Loader 虚拟机

UGen300 每次断电后都会重新枚举为 `UGen300 USB Loader`。必须通过 ASUS/Hailo 官方
Linux 软件包上传 runtime，macOS 原生 HailoRT 才能访问加速器。

请按照 [UGen300 Loader VM 准备指南](docs/macos-loader-vm.md)创建本地 ARM64 Ubuntu
qcow2。本仓库不分发 ASUS 安装包、固件、runtime 镜像、虚拟机磁盘、HEF 或模型权重。

### 3. 加载 Runtime

```bash
scripts/macos/load_ugen300_runtime.sh /absolute/path/to/hailo-loader.qcow2
```

该脚本会以一次性 QEMU snapshot 启动 qcow2，仅透传 USB ID `0b05:1d6f`，等待设备切换到
runtime 产品形态后自动关闭 QEMU。

### 4. 验证设备

```bash
export HAILORT_LOGGER_PATH=NONE

build-macos-usb/hailort/hailortcli/hailortcli scan
build-macos-usb/hailort/hailortcli/hailortcli fw-control identify
scripts/macos/doctor.sh
```

预期设备信息：

```text
Firmware Version: 5.3.0 (release,app)
Device Architecture: HAILO10H
```

### 5. 运行推理

视觉 HEF：

```bash
build-macos-usb/hailort/hailortcli/hailortcli \
  run2 -t 3 set-net /path/to/model.hef
```

交互式 LLM：

```bash
build-macos-usb/hailort/libhailort/examples/genai/chat_example/chat_example \
  /path/to/llm.hef
```

输入 `/exit` 可正常释放 VDevice。

LLM 性能基准：

```bash
build-macos-usb/hailort/libhailort/examples/genai/llm_benchmark/llm_benchmark \
  /path/to/llm.hef 128
```

## 实测基线

以下 Qwen3-1.7B 数据来自 USB-C 仅协商到 1.5A 的测试环境。它是当前机器上的可复现实验
证据，不是硬件性能承诺。

| 指标 | 结果 |
|---|---:|
| 模型加载 | 平均 7.90 秒 |
| 首字延迟 TTFT | 平均 0.816 秒 |
| 持续 completion read | 3.84 次/秒 |

一个 completion read 通常对应一个生成 token，但 UTF-8 缓冲意味着该指标不是经过
tokenizer 核验的严格 TPS。满性能测试应使用明确宣告 3.0A 的 USB-C 电源。

## 架构

```text
macOS 应用
  └─ libhailort.dylib
      └─ 基于 libusb 的 Hailo HRPC
          └─ 由官方 USB Loader 上传的 UGen300 Runtime
              └─ Hailo-10H 加速器与设备内存
```

Linux 虚拟机只承担专有 Loader 的启动桥接。设备重新枚举后，HailoRT Host Runtime 和
推理程序均原生运行在 macOS 上。

## 常见恢复流程

如果重新插拔设备后 `hailortcli` 立即提示固件不兼容，先检查 USB 状态，不要直接执行
固件升级：

```bash
ioreg -p IOUSB -w0 | grep UGen300
```

看到 `UGen300 USB Loader` 表示只需重新上传 runtime，本身不能证明持久化固件损坏或
版本过旧。

## 文档

- [完整 macOS USB 指南](docs/macos-usb.md)
- [Loader VM 准备指南](docs/macos-loader-vm.md)
- [交互式 Runtime RPC Shell](docs/runtime-rpc-shell.md)
- [配套 Hailo-Ollama/OpenAI 服务](https://github.com/relaxcloud-cn/hailo-ollama-macos)
- [贡献指南](CONTRIBUTING.md)
- [安全策略](SECURITY.md)

## 上游与许可证

本仓库保留完整上游历史，当前基于
[`hailo-ai/hailort` v5.3.0](https://github.com/hailo-ai/hailort/tree/v5.3.0)，本地 Git
将官方仓库配置为 `upstream`。

HailoRT 原有许可证保持不变：

- libhailort、pyhailort、hailortcli：[MIT](hailort/LICENSE)
- HailoNet GStreamer 插件：[LGPL-2.1](hailort/libhailort/bindings/gstreamer/LICENSE)
- 第三方声明：[LICENSE-3RD-PARTY.md](hailort/LICENSE-3RD-PARTY.md)

厂商固件、USB Loader 软件包、HEF 和模型文件仍受各自许可证约束，本仓库不会包含这些
制品。
