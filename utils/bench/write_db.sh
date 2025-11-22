#!/usr/bin/env bash
#
# run_bench2.sh - Run write benchmarks (single/batched, append/non-append)
#
# This script builds and executes the bench_db_write benchmark.
# Results are stored in tests/benchmarks/results/.
#

set -euo pipefail

# Resolve repository root (two levels up from this script).
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

# Configuration
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build}"
BENCHMARK="${BENCHMARK:-bench_db_write}"
RESULTS_DIR="${ROOT_DIR}/tests/benchmarks/results"

echo "========================================"
echo "Database PUT Benchmark Runner"
echo "========================================"
echo

# Step 1: Build the benchmark if needed
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

# Step 2: Ensure results directory exists
mkdir -p "${RESULTS_DIR}"

# Step 3: Run the benchmark
echo "==============================================================================="
echo "Running benchmark: ${BENCHMARK}"
echo

"${BUILD_DIR}/${BENCHMARK}"

echo
echo "Benchmark execution completed!"
echo "==============================================================================="
