#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"
build_dir="${1:-${repo_root}/build-macos-usb}"

if ! command -v cmake >/dev/null 2>&1; then
    echo "cmake is required (brew install cmake)" >&2
    exit 1
fi
if ! pkg-config --exists libusb-1.0; then
    echo "libusb is required (brew install libusb pkg-config)" >&2
    exit 1
fi

cmake -S "${repo_root}" -B "${build_dir}" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_BUILD_TYPE=Release \
    -DHAILO_BUILD_USB=ON \
    -DHAILO_BUILD_EXAMPLES=ON \
    -DHAILO_BUILD_GSTREAMER=OFF \
    -DHAILO_BUILD_HAILORT_SERVER=OFF \
    -DHAILO_BUILD_GENAI_SERVER=OFF

cmake --build "${build_dir}" --target hailortcli chat_example llm_benchmark --parallel "$(getconf _NPROCESSORS_ONLN)"

echo "Built: ${build_dir}/hailort/hailortcli/hailortcli"
echo "Built: ${build_dir}/hailort/libhailort/examples/genai/chat_example/chat_example"
echo "Built: ${build_dir}/hailort/libhailort/examples/genai/llm_benchmark/llm_benchmark"
