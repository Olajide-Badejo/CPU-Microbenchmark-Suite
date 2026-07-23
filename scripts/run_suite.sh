#!/usr/bin/env bash
# run_suite.sh
#
# Runs the full measurement suite, writes per benchmark JSON into a timestamped
# raw directory, captures the reproducibility environment, and assembles
# summary.json (the single input to all plots and tables).
#
# Resumable: a benchmark whose JSON already exists in the current raw directory
# is skipped, so an interrupted run resumes where it stopped. --fresh forces a
# new timestamped directory. --mini runs the fast smoke versions (for CI and
# the integration test). The suite prints an overall stage bar; each benchmark
# prints its own inner progress to stderr.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build"
RESULTS_DIR="${REPO_ROOT}/experiments/results"
RAW_ROOT="${RESULTS_DIR}/raw"

MINI=""
FRESH=""
PIN_CPU=""
for arg in "$@"; do
    case "$arg" in
        --mini) MINI="--mini" ;;
        --fresh) FRESH=1 ;;
        --cpu=*) PIN_CPU="${arg#*=}" ;;
        *) echo "unknown arg: $arg" >&2; exit 2 ;;
    esac
done

if [[ ! -x "${BUILD_DIR}/src/pointer_chase" ]]; then
    echo "benchmarks not built. Run: cmake -S . -B build && cmake --build build -j" >&2
    exit 1
fi

# Pick or create the raw directory. Without --fresh, reuse the newest existing
# one so the run is resumable; if none exists, make a new one.
mkdir -p "${RAW_ROOT}"
if [[ -z "${FRESH}" ]] && compgen -G "${RAW_ROOT}/*/" > /dev/null; then
    RAW_DIR="$(ls -dt "${RAW_ROOT}"/*/ | head -1)"
    RAW_DIR="${RAW_DIR%/}"
else
    RAW_DIR="${RAW_ROOT}/$(date +%Y%m%d_%H%M%S)"
    mkdir -p "${RAW_DIR}"
fi
echo "raw directory: ${RAW_DIR}"

# Capture the reproducibility environment.
GPP_VER="$(g++ --version | head -1)"
# The model name and cpu MHz fields exist on x86 /proc/cpuinfo but not on
# AArch64, so these must tolerate no match (the trailing || true keeps the empty
# result from tripping set -e and pipefail on ARM runners).
CPU_MODEL="$(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2- | sed 's/^ *//' || true)"
KERNEL="$(uname -r)"
IS_WSL="false"; grep -qi microsoft /proc/version 2>/dev/null && IS_WSL="true"
GIT_COMMIT="$(git -C "${REPO_ROOT}" rev-parse --short HEAD 2>/dev/null || echo unknown)"
CPUINFO_MHZ="$(grep -m1 'cpu MHz' /proc/cpuinfo 2>/dev/null | awk '{print $4}' || true)"

cat > "${RAW_DIR}/env.json" <<JSON
{
  "timestamp": "$(date -Is)",
  "compiler": "${GPP_VER}",
  "flags": "-O3 -march=native (vector), -O3 -march=native -fno-tree-vectorize (scalar baseline), LTO off",
  "cpu_model": "${CPU_MODEL}",
  "kernel": "${KERNEL}",
  "wsl": ${IS_WSL},
  "cpuinfo_mhz_base": ${CPUINFO_MHZ:-null},
  "git_commit": "${GIT_COMMIT}",
  "mini": $([[ -n "${MINI}" ]] && echo true || echo false)
}
JSON

# Stage bar helper.
STAGES=(pointer_chase stream peak_flops)
NSTAGES=${#STAGES[@]}
stage_i=0
run_stage() {
    local name="$1"; shift
    stage_i=$((stage_i + 1))
    local out="${RAW_DIR}/${name}.json"
    printf '\n[stage %d/%d] %s\n' "${stage_i}" "${NSTAGES}" "${name}" >&2
    if [[ -s "${out}" ]]; then
        echo "  (skip, ${name}.json already present)" >&2
        return
    fi
    local pin=()
    [[ -n "${PIN_CPU}" ]] && pin=(taskset -c "${PIN_CPU}")
    "${pin[@]}" "${BUILD_DIR}/src/${name}" ${MINI} --out "${out}" "$@"
}

run_stage pointer_chase
run_stage stream
run_stage peak_flops

# GEMM validation (Phase 5) runs if built.
if [[ -x "${BUILD_DIR}/src/gemm_validate" ]]; then
    stage_i=0; NSTAGES=1
    local_out="${RAW_DIR}/gemm.json"
    if [[ ! -s "${local_out}" ]]; then
        printf '\n[extra] gemm_validate\n' >&2
        "${BUILD_DIR}/src/gemm_validate" ${MINI} --out "${local_out}" || true
    fi
fi

# Assemble the canonical summary.
python3 "${REPO_ROOT}/scripts/assemble_summary.py" "${RAW_DIR}" \
    "${RESULTS_DIR}/summary.json" "${RAW_DIR}/env.json"

echo "suite complete. summary at ${RESULTS_DIR}/summary.json" >&2
