<p align="left">
  <img src=".hailort.jpg" />
</p>

> [!IMPORTANT]
> This branch is an experimental, community-maintained Apple Silicon macOS port based on
> upstream HailoRT v5.3.0. It is not an official Hailo or ASUS release. Linux and Windows
> behavior remains upstream-owned; the macOS path is validated only for the USB device and
> workloads listed below.

## Experimental Apple Silicon macOS USB port

This fork adds a userspace `libusb` transport for the ASUS UGen300 USB AI Accelerator
(Hailo-10H, USB ID `0b05:1d6f`). It does not install a Kext or DriverKit extension.

Validated on an Apple Silicon Mac with:

- HailoRT and device runtime 5.3.0;
- `hailortcli scan` and `fw-control identify`;
- HAILO10H vision HEF execution;
- Qwen3-1.7B GenAI execution and repeatable TTFT/throughput measurements;
- the companion Hailo-Ollama macOS port.

Quick start:

```bash
brew install cmake libusb pkg-config qemu
scripts/macos/build_hailort_usb.sh
scripts/macos/doctor.sh
```

UGen300 returns to a USB loader after every power cycle. Follow
[the macOS USB guide](docs/macos-usb.md) before running inference. The repository contains
host-side source and automation only; it does not redistribute ASUS packages, firmware,
VM images, HEFs, or model weights.

Note: The `master` branch supports only the Hailo-10 and Hailo-15 device families. For Hailo-8, Hailo-8R, and Hailo-8L devices, please use the `hailo8` branch.

# HailoRT #

HailoRT is a lightweight, production-grade runtime library that runs on the host processor and provides a robust
user-space library (the HailoRT Library) with intuitive APIs in C/C++ for optimized performance

HailoRT consists of the following main components:
- HailoRT Library.
- HailoRT CLI - a command line application used to control the Hailo device, run inferences, collect statistics and device events, etc.
- [**HailoRT PCIe Driver**](https://github.com/hailo-ai/hailort-drivers) - the device driver used to manage the Hailo device, communicate with the device,
and transfer data to/from the device; it includes the Hailo-8 firmware that runs on the Hailo device and manages its boot and control.
- pyHailoRT - HailoRT Python API, which wraps the runtime library.
- HailoRT GStreamer element (HailoNet).

Upstream HailoRT supports Linux and Windows and can be integrated with various x86 and ARM
processors. This fork adds the experimental macOS USB path described above.

## Usage

See [**hailo.ai developer zone documentation**](https://hailo.ai/developer-zone/documentation/hailort/latest/) (registration is required for  full documentation access).

For compilation instructions, see  [**Compiling HailoRT from Sources**](https://hailo.ai/developer-zone/documentation/hailort/latest/?sp_referrer=install/install.html#compiling-from-sources).

For HailoRT API examples - see [**HailoRT examples**](https://github.com/hailo-ai/hailort/tree/master/hailort/libhailort/examples).

## Changelog

See [**hailo.ai developer zone - HailoRT changelog**](https://hailo.ai/developer-zone/documentation/hailort/latest/?sp_referrer=changelog/changelog.html) (registration required).

## Licenses

HailoRT uses 2 licenses:
- libhailort, pyhailort & hailortcli - distributed under the [**MIT license**](https://opensource.org/licenses/MIT)
- hailonet (GStreamer plugin) - distributed under the [**LGPL 2.1 license**](https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)

## Contributing to the macOS port

See [CONTRIBUTING.md](CONTRIBUTING.md) for the hardware evidence expected with changes and
[SECURITY.md](SECURITY.md) for private vulnerability reporting guidance.

## Support

If you need support, please post your question on our [**Hailo community Forum**](https://community.hailo.ai/) for assistance.

Contact information is available at [**hailo.ai**](https://hailo.ai/contact-us/).

## About Hailo

Hailo offers breakthrough AI Inference Accelerators and AI Vision Processors uniquely designed to accelerate embedded deep learning applications on edge devices.

The Hailo AI Inference Accelerators allow edge devices to run deep learning applications at full scale more efficiently, effectively, and sustainably, with an architecture that takes advantage of the core properties of neural networks.

The Hailo AI Vision Processors (SoC) combine Hailo's patented and field proven AI inferencing capabilities with advanced computer vision engines, generating premium image quality and advanced video analytics.

For more information, please visit [**hailo.ai**](https://hailo.ai/).
