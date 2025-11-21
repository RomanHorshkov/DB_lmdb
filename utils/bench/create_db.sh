#!/usr/bin/env bash
#
# run_bench.sh - Run database benchmarks
#
# This script builds and executes database benchmarks.
# Results are stored in tests/benchmarks/results/
#

set -euo pipefail

# Resolve repository root (two levels up from this script).
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

# Configuration
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build}"
BENCHMARK="${BENCHMARK:-bench_db_init}"
RESULTS_DIR="${ROOT_DIR}/tests/benchmarks/results"

echo "========================================"
echo "Database Benchmark Runner"
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
echo "Running benchmark: ${BENCHMARK}"
echo "========================================"
echo

"${BUILD_DIR}/${BENCHMARK}"

echo
echo "========================================"
echo "Benchmark execution completed!"
echo "========================================"
