# Contributing to the macOS USB port

This branch tracks upstream HailoRT v5.3.0 and keeps Apple-specific changes behind CMake
or preprocessor platform guards wherever possible.

Before opening a change:

1. Build with `scripts/macos/build_hailort_usb.sh`.
2. Run `scripts/macos/doctor.sh` with the UGen300 connected.
3. Verify `hailortcli scan`, `fw-control identify`, one vision HEF, and any affected GenAI
   path.
4. State the Mac model, macOS version, cable/power topology, firmware version, and exact
   HEF used for the test.

Do not commit or attach proprietary firmware, ASUS/Hailo packages, VM disks, HEFs, model
weights, credentials, device serial numbers, or private logs. Static code review does not
count as hardware validation; clearly label untested devices and workflows.
