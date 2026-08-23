# HailoRT USB on macOS

This experimental port runs HailoRT 5.3.0 on Apple Silicon macOS and communicates with
the ASUS UGen300 USB AI Accelerator (`0b05:1d6f`) through Homebrew `libusb`. It is a
userspace implementation: no Kext, DriverKit extension, System Extension, or privileged
daemon is installed.

## Support status

| Area | Status | Evidence boundary |
|---|---|---|
| Host | Validated | Mac Studio M2 Ultra; other Apple Silicon Macs are candidates, not verified |
| Device | Validated | ASUS UGen300 with Hailo-10H and 8 GB device memory |
| HailoRT/runtime | Validated | Host library 5.3.0 with device runtime 5.3.0 |
| USB scan/control | Validated | `hailortcli scan` and `fw-control identify` |
| Vision HEF | Validated | HAILO10H YOLOv8n execution through `run2` |
| GenAI HEF | Validated | Qwen3-1.7B chat and repeatable benchmark |
| Hailo-Ollama | Validated separately | Ollama and OpenAI-compatible HTTP APIs in the companion fork |
| PCIe, DMA-BUF, GStreamer | Not implemented | USB-only scope |
| pyHailoRT wheel | Not implemented | Native C/C++ library and examples only |
| Intel macOS | Not supported | No build or device evidence |
| Signing/notarization | Not implemented | Local source build only |

The measured Qwen3-1.7B result on the tested 1.5A USB-C connection was approximately
7.90 seconds to load, 0.816 seconds TTFT, and 3.84 completion reads per second. A
completion read usually corresponds to a generated token, but UTF-8 buffering means this
is not tokenizer-verified TPS. Treat these values as a reproducible local baseline, not a
hardware guarantee.

## Build

```bash
brew install cmake libusb pkg-config qemu
scripts/macos/build_hailort_usb.sh
scripts/macos/doctor.sh
```

Main outputs:

```text
build-macos-usb/hailort/hailortcli/hailortcli
build-macos-usb/hailort/libhailort/examples/genai/chat_example/chat_example
build-macos-usb/hailort/libhailort/examples/genai/llm_benchmark/llm_benchmark
```

Pass a different build directory as the first build-script argument when needed.
Set `HAILORT_RECONFIGURE=1` to force a fresh CMake configure pass while keeping the build
directory.

## Load the UGen300 runtime

After every power cycle, UGen300 initially enumerates as `UGen300 USB Loader`. The
functional runtime must be uploaded before native macOS HailoRT can open the accelerator.

Prepare an ARM64 Ubuntu qcow2 containing the official ASUS package, following
[the loader VM guide](macos-loader-vm.md). Then run:

```bash
scripts/macos/load_ugen300_runtime.sh /absolute/path/to/hailo-loader.qcow2
```

The helper boots the disk as a disposable QEMU snapshot, passes through the loader,
waits for the runtime USB product, and stops QEMU. It does not alter the base qcow2.

If `hailortcli` reports incompatible firmware just after the module was reconnected, do
not immediately run the suggested firmware update. First check:

```bash
ioreg -p IOUSB -w0 | grep UGen300
```

`UGen300 USB Loader` means the runtime simply needs to be loaded again.

## Verify and run

```bash
export HAILORT_LOGGER_PATH=NONE
build-macos-usb/hailort/hailortcli/hailortcli scan
build-macos-usb/hailort/hailortcli/hailortcli fw-control identify
build-macos-usb/hailort/hailortcli/hailortcli run2 -t 3 set-net /path/to/model.hef
build-macos-usb/hailort/libhailort/examples/genai/chat_example/chat_example /path/to/llm.hef
build-macos-usb/hailort/libhailort/examples/genai/llm_benchmark/llm_benchmark /path/to/llm.hef 128
```

Use `/exit` to leave `chat_example` cleanly and release its VDevice. The benchmark reports
device creation, model load, TTFT, total generation time, and completion-read throughput.

For full-performance testing, use a USB-C source that advertises 3.0A. HailoRT reports a
warning when the source advertises only 1.5A; changing ports alone may not change the
negotiation, so validate the cable and any powered hub as well.

## Runtime architecture

```text
macOS application
  -> libhailort.dylib
  -> Hailo HRPC over libusb
  -> UGen300 runtime loaded by the official USB loader
  -> Hailo-10H accelerator and device memory
```

The macOS host does not run the device firmware or replace the proprietary loader. The
temporary Linux VM is used only to perform the vendor-supported runtime upload after a
power cycle.

## Source and licensing boundary

This fork changes the MIT-licensed host HailoRT source. Keep the upstream copyright and
license files intact.

The following artifacts are intentionally ignored and must not be committed to a public
fork:

- `hailort-usb-driver_*.deb`;
- Hailo firmware and runtime root filesystems;
- qcow2/UTM VM disks;
- HEFs and model weights;
- local logs, credentials, and device-specific identifiers.

Obtain vendor artifacts from official ASUS/Hailo channels and use them under their own
terms. This repository does not grant redistribution rights for those artifacts.
