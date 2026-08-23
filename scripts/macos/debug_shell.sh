#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd "${script_dir}/../.." && pwd -P)"
build_dir="${HAILORT_BUILD_DIR:-${repo_root}/build-macos-usb}"
cli="${build_dir}/hailort/hailortcli/hailortcli"
token_file="${HAILO_DEBUG_SHELL_TOKEN_FILE:-${repo_root}/runtime-lab/debug-shell.token}"

if [[ ! -x "${cli}" ]]; then
    echo "hailortcli is not built: ${cli}" >&2
    echo "Run scripts/macos/build_hailort_usb.sh first." >&2
    exit 1
fi

token_args=()
if [[ -f "${token_file}" ]]; then
    token_args=(--token-file "${token_file}")
elif [[ -z "${HAILO_DEBUG_SHELL_TOKEN:-}" ]]; then
    echo "Debug shell token not found: ${token_file}" >&2
    echo "Set HAILO_DEBUG_SHELL_TOKEN_FILE or HAILO_DEBUG_SHELL_TOKEN." >&2
    exit 2
fi

export HAILORT_LOGGER_PATH="${HAILORT_LOGGER_PATH:-NONE}"
exec "${cli}" fw-control debug shell "${token_args[@]}" "$@"
