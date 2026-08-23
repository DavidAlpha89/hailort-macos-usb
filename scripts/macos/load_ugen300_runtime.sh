#!/usr/bin/env bash
set -euo pipefail

loader_name="UGen300 USB Loader"
runtime_name="${HAILO_RUNTIME_USB_PRODUCT:-UGen300 USB AI Accelerator (Hailo-10H)}"

usage() {
    echo "Usage: $0 [hailo-loader.qcow2]" >&2
    echo "The disk can also be set with HAILO_LOADER_VM_DISK." >&2
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi
if [[ $# -gt 1 ]]; then
    usage
    exit 2
fi

find_qemu_firmware() {
    local candidate
    if command -v brew >/dev/null 2>&1; then
        for candidate in \
            "$(brew --prefix qemu 2>/dev/null || true)/share/qemu/edk2-aarch64-code.fd" \
            "$(brew --prefix 2>/dev/null || true)/share/qemu/edk2-aarch64-code.fd"; do
            if [[ -f "${candidate}" ]]; then
                echo "${candidate}"
                return 0
            fi
        done
    fi
    return 1
}

vm_disk="${1:-${HAILO_LOADER_VM_DISK:-}}"
timeout_seconds="${HAILO_LOADER_TIMEOUT:-90}"
qemu_binary="${HAILO_QEMU_SYSTEM_AARCH64:-$(command -v qemu-system-aarch64 || true)}"
qemu_firmware="${HAILO_QEMU_EFI:-$(find_qemu_firmware || true)}"

usb_tree_contains() {
    ioreg -p IOUSB -l -w 0 | grep -Fq "$1"
}

if usb_tree_contains "${runtime_name}"; then
    echo "UGen300 runtime is already active."
    exit 0
fi
if ! usb_tree_contains "${loader_name}"; then
    echo "UGen300 loader was not detected." >&2
    exit 2
fi
if [[ -z "${vm_disk}" || ! -f "${vm_disk}" ]]; then
    echo "Provide the prepared ARM64 Ubuntu qcow2 image as an argument or set HAILO_LOADER_VM_DISK." >&2
    exit 3
fi
if [[ -z "${qemu_binary}" || ! -x "${qemu_binary}" ]]; then
    echo "qemu-system-aarch64 is required (brew install qemu)." >&2
    exit 4
fi
if [[ -z "${qemu_firmware}" || ! -f "${qemu_firmware}" ]]; then
    echo "AArch64 UEFI firmware was not found. Install QEMU or set HAILO_QEMU_EFI." >&2
    exit 5
fi

runtime_dir="$(mktemp -d "${TMPDIR:-/tmp}/hailo-loader.XXXXXX")"
qemu_log="${runtime_dir}/qemu.log"
qemu_pid=""

cleanup() {
    if [[ -n "${qemu_pid}" ]] && kill -0 "${qemu_pid}" 2>/dev/null; then
        kill "${qemu_pid}" 2>/dev/null || true
        wait "${qemu_pid}" 2>/dev/null || true
    fi
    rm -rf "${runtime_dir}"
}
trap cleanup EXIT INT TERM

"${qemu_binary}" \
    -name Hailo-Loader \
    -machine virt,accel=hvf,highmem=on \
    -cpu host -smp 4 -m 4096 \
    -bios "${qemu_firmware}" \
    -drive "if=none,file=${vm_disk},format=qcow2,id=hd0,snapshot=on" \
    -device virtio-blk-pci,drive=hd0 \
    -device qemu-xhci,id=xhci \
    -device usb-host,bus=xhci.0,vendorid=0x0b05,productid=0x1d6f \
    -display none -serial none -monitor none \
    >"${qemu_log}" 2>&1 &
qemu_pid=$!

for ((elapsed = 0; elapsed < timeout_seconds; elapsed++)); do
    if usb_tree_contains "${runtime_name}"; then
        kill "${qemu_pid}" 2>/dev/null || true
        wait "${qemu_pid}" 2>/dev/null || true
        qemu_pid=""
        echo "UGen300 runtime is active."
        exit 0
    fi
    if ! kill -0 "${qemu_pid}" 2>/dev/null; then
        echo "Loader VM exited before UGen300 entered runtime mode." >&2
        tail -n 80 "${qemu_log}" >&2 || true
        exit 6
    fi
    sleep 1
done

echo "Timed out waiting for UGen300 runtime after ${timeout_seconds}s." >&2
tail -n 80 "${qemu_log}" >&2 || true
exit 7
