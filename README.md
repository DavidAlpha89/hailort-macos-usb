# HailoRT USB for Apple Silicon macOS

[![macOS](https://img.shields.io/badge/macOS-Apple%20Silicon-000000?logo=apple)](https://github.com/relaxcloud-cn/hailort-macos-usb)
[![HailoRT](https://img.shields.io/badge/HailoRT-5.3.0-38a169)](https://github.com/hailo-ai/hailort/tree/v5.3.0)
[![Device](https://img.shields.io/badge/device-ASUS%20UGen300-2563eb)](https://github.com/relaxcloud-cn/hailort-macos-usb)
[![License](https://img.shields.io/badge/license-MIT-yellow)](hailort/LICENSE)

[English](README.md) | [简体中文](README.zh-CN.md)

An experimental userspace port of HailoRT 5.3.0 for the ASUS UGen300 USB AI
Accelerator on Apple Silicon macOS.

It communicates with the Hailo-10H through Homebrew `libusb`. No Kext, DriverKit
extension, System Extension, or privileged daemon is installed.

> [!IMPORTANT]
> This is a community project, not an official Hailo or ASUS macOS release. It has been
> validated on one Mac Studio M2 Ultra and the device/workloads listed below.

## What works

| Capability | Status |
|---|---|
| Apple Silicon native build | ✅ |
| UGen300 USB scan and Hailo HRPC transport | ✅ |
| Firmware/device identification | ✅ |
| HAILO10H vision HEF inference | ✅ |
| Qwen3-1.7B GenAI inference | ✅ |
| LLM load, TTFT and throughput benchmark | ✅ |
| Ollama and OpenAI-compatible API | ✅ [companion repository](https://github.com/relaxcloud-cn/hailo-ollama-macos) |
| Interactive Runtime RPC shell | 🧪 Experimental; disabled in device builds by default |
| Intel Mac, PCIe, DMA-BUF, GStreamer | ❌ Not supported |
| pyHailoRT wheel | ❌ Not packaged |
| Signing and notarization | ❌ Not implemented |

## Experimental Runtime RPC shell

The `codex/runtime-rpc-shell` branch provides an interactive root PTY for a local
experimental Runtime. The device-side server must be built with
`HAILO_ENABLE_DEBUG_RPC_SHELL=ON`; this option is off by default, and the official
Runtime does not register these HRPC actions.

> [!WARNING]
> This is a device-side root shell. Use it only on hardware you own or are authorized
> to test. Never publish its token or redistribute a modified vendor Runtime.

After loading the matching experimental Runtime, open an interactive shell from macOS:

```bash
scripts/macos/debug_shell.sh
```

Execute one short command:

```bash
scripts/macos/debug_shell.sh -c 'id; uname -a'
```

The Loader uploads this Runtime into RAM. This project does not invoke the Flash update
path, so a device reboot or power cycle returns it to Loader mode. Tokens, Runtime
images, qcow2 disks, and HEFs must stay out of Git. See the
[interactive Runtime RPC shell guide](docs/runtime-rpc-shell.md) for build, ABI, and
protocol limits.

## Quick start

### 1. Clone and build

```bash
git clone https://github.com/relaxcloud-cn/hailort-macos-usb.git
cd hailort-macos-usb

brew install cmake libusb pkg-config qemu
scripts/macos/build_hailort_usb.sh
scripts/macos/doctor.sh
```

Build outputs:

```text
build-macos-usb/hailort/hailortcli/hailortcli
build-macos-usb/hailort/libhailort/examples/genai/chat_example/chat_example
build-macos-usb/hailort/libhailort/examples/genai/llm_benchmark/llm_benchmark
```

### 2. Prepare the UGen300 loader VM

UGen300 returns to `UGen300 USB Loader` after every power cycle. The official ASUS/Hailo
Linux package must upload the runtime before native macOS HailoRT can use the device.

Follow [Preparing the UGen300 loader VM](docs/macos-loader-vm.md). The repository does
not distribute the ASUS package, firmware, runtime image, VM disk, HEF, or model weights.

### 3. Load the runtime

```bash
scripts/macos/load_ugen300_runtime.sh /absolute/path/to/hailo-loader.qcow2
```

The helper boots the qcow2 as a disposable QEMU snapshot, passes through USB ID
`0b05:1d6f`, waits for the runtime product, and stops QEMU.

### 4. Verify the device

```bash
export HAILORT_LOGGER_PATH=NONE

build-macos-usb/hailort/hailortcli/hailortcli scan
build-macos-usb/hailort/hailortcli/hailortcli fw-control identify
scripts/macos/doctor.sh
```

Expected identity:

```text
Firmware Version: 5.3.0 (release,app)
Device Architecture: HAILO10H
```

### 5. Run inference

Vision HEF:

```bash
build-macos-usb/hailort/hailortcli/hailortcli \
  run2 -t 3 set-net /path/to/model.hef
```

Interactive LLM:

```bash
build-macos-usb/hailort/libhailort/examples/genai/chat_example/chat_example \
  /path/to/llm.hef
```

Use `/exit` to release the VDevice cleanly.

LLM benchmark:

```bash
build-macos-usb/hailort/libhailort/examples/genai/llm_benchmark/llm_benchmark \
  /path/to/llm.hef 128
```

## Tested baseline

The Qwen3-1.7B baseline below was measured over a USB-C connection that advertised only
1.5A. Treat it as reproducible evidence from the tested machine, not a product guarantee.

| Metric | Result |
|---|---:|
| Model load | 7.90 s average |
| TTFT | 0.816 s average |
| Sustained completion reads | 3.84/s |

A completion read normally corresponds to one generated token, but UTF-8 buffering means
the last number is not tokenizer-verified TPS. Use a source that advertises 3.0A for
full-performance testing.

## Architecture

```text
macOS application
  └─ libhailort.dylib
      └─ Hailo HRPC over libusb
          └─ UGen300 runtime uploaded by the official USB loader
              └─ Hailo-10H accelerator + device memory
```

The Linux VM is only a bootstrapping bridge for the proprietary loader. Inference and the
HailoRT host runtime run natively on macOS after the device re-enumerates.

## Common recovery path

If `hailortcli` reports incompatible firmware immediately after reconnecting the module,
check its USB state before running any firmware update:

```bash
ioreg -p IOUSB -w0 | grep UGen300
```

`UGen300 USB Loader` means the device needs another runtime upload. It does not by itself
mean that persistent firmware is damaged or outdated.

## Documentation

- [Complete macOS USB guide](docs/macos-usb.md)
- [Loader VM preparation](docs/macos-loader-vm.md)
- [Interactive Runtime RPC shell](docs/runtime-rpc-shell.md)
- [Companion Hailo-Ollama/OpenAI server](https://github.com/relaxcloud-cn/hailo-ollama-macos)
- [Contributing](CONTRIBUTING.md)
- [Security policy](SECURITY.md)

## Upstream and licensing

This repository preserves the complete upstream history and currently branches from
[`hailo-ai/hailort` v5.3.0](https://github.com/hailo-ai/hailort/tree/v5.3.0). The official
repository is configured locally as the `upstream` remote.

HailoRT licensing remains unchanged:

- libhailort, pyhailort and hailortcli: [MIT](hailort/LICENSE)
- HailoNet GStreamer plugin: [LGPL-2.1](hailort/libhailort/bindings/gstreamer/LICENSE)
- Third-party notices: [LICENSE-3RD-PARTY.md](hailort/LICENSE-3RD-PARTY.md)

Vendor firmware, USB loader packages, HEFs and model artifacts remain subject to their
own licenses and are intentionally excluded from this repository.
