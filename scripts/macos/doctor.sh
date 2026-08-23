#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd "${script_dir}/../.." && pwd -P)"
build_dir="${HAILORT_BUILD_DIR:-${repo_root}/build-macos-usb}"
cli="${build_dir}/hailort/hailortcli/hailortcli"
failures=0
warnings=0

pass() { echo "PASS  $*"; }
warn() { echo "WARN  $*"; warnings=$((warnings + 1)); }
fail() { echo "FAIL  $*"; failures=$((failures + 1)); }

if [[ "$(uname -s)" == "Darwin" ]]; then
    pass "macOS host detected"
else
    fail "this port requires macOS"
fi

if [[ "$(uname -m)" == "arm64" ]]; then
    pass "Apple Silicon architecture detected"
else
    fail "this port has not been validated on $(uname -m)"
fi

for command_name in cmake pkg-config qemu-system-aarch64; do
    if command -v "${command_name}" >/dev/null 2>&1; then
        pass "${command_name} is available"
    else
        fail "${command_name} is missing"
    fi
done

if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists libusb-1.0; then
    pass "libusb-1.0 is available through pkg-config"
else
    fail "libusb-1.0 is missing"
fi

usb_tree="$(ioreg -p IOUSB -l -w0 2>/dev/null || true)"
if grep -Fq "UGen300 USB AI Accelerator (Hailo-10H)" <<<"${usb_tree}"; then
    pass "UGen300 runtime device is connected"
    if [[ -x "${cli}" ]]; then
        if identify_output="$(HAILORT_LOGGER_PATH=NONE "${cli}" fw-control identify 2>&1)"; then
            pass "HailoRT can identify the device"
            grep -E "USB-C source electrical current|Firmware Version|Device Architecture" <<<"${identify_output}" \
                | sed 's/^/      /'
            if grep -Fq "advertised: 1.5A" <<<"${identify_output}"; then
                warn "USB-C source advertises 1.5A; full performance requires a 3.0A source"
            fi
        else
            fail "HailoRT identify failed"
            sed 's/^/      /' <<<"${identify_output}"
        fi
    else
        warn "HailoRT CLI is not built yet"
    fi
elif grep -Fq "UGen300 USB Loader" <<<"${usb_tree}"; then
    warn "UGen300 is in loader mode; load the runtime before inference"
    if [[ -n "${HAILO_LOADER_VM_DISK:-}" && -f "${HAILO_LOADER_VM_DISK}" ]]; then
        pass "HAILO_LOADER_VM_DISK points to an existing file"
    else
        warn "HAILO_LOADER_VM_DISK is not configured"
    fi
else
    warn "UGen300 was not detected"
fi

echo "Summary: failures=${failures} warnings=${warnings}"
if ((failures > 0)); then
    exit 1
fi
