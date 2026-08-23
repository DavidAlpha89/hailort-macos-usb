# Preparing the UGen300 loader VM

The ASUS UGen300 enumerates as `UGen300 USB Loader` after every power cycle. The official
ASUS/Hailo package uploads the runtime root filesystem and makes the device re-enumerate as
`UGen300 USB AI Accelerator (Hailo-10H)`.

This repository cannot redistribute the ASUS package, the runtime image inside it, or a VM
containing either artifact. Prepare the VM locally from software you obtained under the
applicable vendor terms.

## 1. Create an ARM64 Linux VM

Create an Ubuntu 24.04 ARM64 VM using UTM or QEMU. A practical minimum is 4 CPU cores,
4 GB RAM, and a 16 GB qcow2 disk. Complete the Ubuntu installation and shut the VM down
cleanly before using the disk with the loader helper.

## 2. Install the official USB package

Obtain `hailort-usb-driver_5.3.0_arm64.deb` from the official ASUS/Hailo distribution
channel and copy it into the VM. Inside the VM, run:

```bash
sudo dpkg -i hailort-usb-driver_5.3.0_arm64.deb
sudo apt-get -f install
command -v hailo_usb_loader
test -f /lib/firmware/hailo/hailo10h_usb/core-image-hailo-hailo10-usb-dongle.ext4
```

Both checks must succeed. Shut the VM down; do not leave the qcow2 mounted by UTM while
the macOS helper is using it.

## 3. Load the runtime from macOS

```bash
scripts/macos/load_ugen300_runtime.sh /absolute/path/to/hailo-loader.qcow2
```

The helper opens the disk as a disposable QEMU snapshot, passes only USB ID `0b05:1d6f`
to the guest, waits for the runtime product, and stops QEMU. Changes made during that boot
are discarded.

The runtime must be loaded again after unplugging, rebooting, or resetting the module. If
`hailortcli` reports incompatible firmware immediately after reconnecting, check `ioreg -p
IOUSB` first: the module is usually back in loader mode and should not be firmware-updated
from macOS.

## Overrides

- `HAILO_LOADER_VM_DISK`: qcow2 path instead of the positional argument.
- `HAILO_LOADER_TIMEOUT`: runtime enumeration timeout in seconds; default `90`.
- `HAILO_QEMU_SYSTEM_AARCH64`: alternate QEMU binary.
- `HAILO_QEMU_EFI`: alternate AArch64 UEFI firmware file.
