#!/usr/bin/env bash
#
# Run multithreaded GET benchmark (bench_db_read_mt)
#
# Builds and executes bench_db_read_mt. Results are printed to stdout.
#

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build}"
BENCHMARK="${BENCHMARK:-bench_db_read_mt}"

echo "========================================"
echo "Multithreaded DB GET Benchmark Runner"
echo "========================================"
echo

if [ ! -x "${BUILD_DIR}/${BENCHMARK}" ]; then
    echo "Benchmark executable not found. Building..."
    if [ ! -f "${BUILD_DIR}/Makefile" ]; then
        echo "Build directory not configured. Running build.sh..."
        "${ROOT_DIR}/utils/build.sh"
    else
        echo "Building benchmark target..."
        make -C "${BUILD_DIR}" "${BENCHMARK}"
    fi
    echo
fi

echo "==============================================================================="
echo "Running benchmark: ${BENCHMARK}"
echo

"${BUILD_DIR}/${BENCHMARK}"

echo
echo "Benchmark execution completed!"
echo "==============================================================================="
