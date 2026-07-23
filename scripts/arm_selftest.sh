#!/usr/bin/env bash
# arm_selftest.sh
#
# Cross compiles the suite for AArch64 and runs the correctness tests under
# qemu-user. This proves the NEON code paths (FLOPS and STREAM kernels) are
# correct on ARM. Timing is never taken here: qemu emulation timing is
# meaningless and is never reported (Section 4 rule 3). Real ARM numbers come
# from the native runner in .github/workflows/arm_native.yml.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build-arm"

echo "configuring AArch64 cross build"
cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${REPO_ROOT}/cmake/toolchain-aarch64.cmake" \
    -DCMAKE_BUILD_TYPE=Release >/dev/null

echo "building AArch64 binaries"
cmake --build "${BUILD_DIR}" -j"$(nproc)" >/dev/null

echo "confirming the binaries are actually AArch64"
file "${BUILD_DIR}/src/peak_flops" | grep -q "ARM aarch64" \
    && echo "  peak_flops is ARM aarch64" \
    || { echo "  ERROR: not an ARM binary" >&2; exit 1; }

echo "running correctness tests under qemu-user"
cd "${BUILD_DIR}"
ctest --output-on-failure
