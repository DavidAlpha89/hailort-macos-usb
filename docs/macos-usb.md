# HailoRT USB on macOS (experimental)

This port targets Apple Silicon macOS hosts and the ASUS UGen300 USB AI Accelerator
(`0b05:1d6f`). It uses Homebrew `libusb` in user space; it does not install a Kext or
DriverKit extension.

## Current scope

- HailoRT 5.3.0 USB scan and HRPC transport
- `hailortcli fw-control identify`
- HAILO10H HEF execution through `run2`
- Apple Silicon (`arm64`)

Not implemented or validated: PCIe, DMA-BUF, GStreamer, Python wheels, macOS
notarization, and direct macOS implementation of `hailo_usb_loader`.

## Build

```bash
brew install cmake libusb pkg-config qemu
scripts/macos/build_hailort_usb.sh
```

The binary is written to:

```text
build-macos-usb/hailort/hailortcli/hailortcli
```

## UGen300 boot requirement

UGen300 initially enumerates as `UGen300 USB Loader`. The functional root filesystem
must be uploaded after each power cycle. Hailo does not publish the source for
`hailo_usb_loader`, so this repository does not reimplement or redistribute it.

Prepare an ARM64 Ubuntu 24.04 qcow2 image and install the official ASUS package
`hailort-usb-driver_5.3.0_arm64.deb`. Confirm that the image contains:

```text
/usr/bin/hailo_usb_loader
/lib/firmware/hailo/hailo10h_usb/core-image-hailo-hailo10-usb-dongle.ext4
```

Then load the runtime from macOS:

```bash
export HAILO_LOADER_VM_DISK=/absolute/path/to/hailo-loader.qcow2
scripts/macos/load_ugen300_runtime.sh
```

The helper starts the prepared VM as a disposable QEMU snapshot, passes through the
loader device, waits for the functional USB product to appear, and terminates QEMU so
native macOS HailoRT can claim the re-enumerated device.

## Verify and run

```bash
export HAILORT_LOGGER_PATH=NONE
build-macos-usb/hailort/hailortcli/hailortcli scan
build-macos-usb/hailort/hailortcli/hailortcli fw-control identify
build-macos-usb/hailort/hailortcli/hailortcli run2 -t 3 set-net /path/to/model.hef
build-macos-usb/hailort/libhailort/examples/genai/chat_example/chat_example /path/to/llm.hef
```

Use a USB-C port that advertises 3A for performance testing. A 1.5A port can place the
module in reduced-performance mode.

## Firmware and licensing boundary

Use only the official ASUS/Hailo firmware update workflow. The Hailo-10H firmware
license permits binary redistribution under its terms but prohibits modification and
reverse engineering. This port changes only the MIT-licensed host HailoRT code.
