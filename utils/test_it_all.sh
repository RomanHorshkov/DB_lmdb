#!/usr/bin/env bash
set -euo pipefail

# This script builds the project with optimizations, runs the db_core
# integration tests (cmocka-based) and collects coverage into
# tests/IT/results.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-build-it}"
RESULTS_DIR="${ROOT_DIR}/tests/IT/results"

mkdir -p "${RESULTS_DIR}"

# Configure with Release flags and coverage instrumentation for the IT target.
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DDB_LMDB_ENABLE_IT_COVERAGE=ON

cmake --build "${BUILD_DIR}" -j"$(nproc)"

# Run the IT binary directly so we can capture its output and exit code.
IT_BIN="${BUILD_DIR}/db_core_it_db_core_init"
if [[ ! -x "${IT_BIN}" ]]; then
    echo "Integration test binary not found: ${IT_BIN}" >&2
    exit 1
fi

# Run tests and tee output into results.
"${IT_BIN}" | tee "${RESULTS_DIR}/IT_db_core_init.log"

# Collect coverage if gcov data is present.
if command -v gcov >/dev/null 2>&1; then
    pushd "${BUILD_DIR}" >/dev/null
    gcov -o . $(find . -name '*.gcno' -o -name '*.gcda' | sed 's%^./%%') >/dev/null 2>&1 || true
    popd >/dev/null

    # Move all generated .gcov files under results for inspection.
    find "${BUILD_DIR}" -name '*.gcov' -exec mv {} "${RESULTS_DIR}" \; || true
else
    echo "gcov not found; skipping coverage collection" >&2
fi

echo "Integration tests and coverage (if available) stored in ${RESULTS_DIR}" 
