#!/usr/bin/env bash
set -euo pipefail

# This script builds the project with optimizations, runs the db_core
# unit tests (cmocka-based) and collects gcov coverage reports into
# tests/UT/results. Test output is printed on the terminal only.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-build-ut}"
RESULTS_DIR="${ROOT_DIR}/tests/UT/results"

mkdir -p "${RESULTS_DIR}"

BLUE=$'\033[34m'
GREEN=$'\033[32m'
RED=$'\033[31m'
RESET=$'\033[0m'

echo "${BLUE}[UT] Configuring (Release + coverage) in ${BUILD_DIR}${RESET}"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DDB_LMDB_ENABLE_UT_COVERAGE=ON

echo "${BLUE}[UT] Building unit tests...${RESET}"
cmake --build "${BUILD_DIR}" -j"$(nproc)"

UT_BIN="${BUILD_DIR}/db_core_ut_security_errno_map"
if [[ ! -x "${UT_BIN}" ]]; then
    echo "${RED}[UT] Unit test binary not found: ${UT_BIN}${RESET}" >&2
    exit 1
fi

echo "${BLUE}[UT] Running unit tests...${RESET}"
if "${UT_BIN}"; then
    echo "${GREEN}[UT] All unit tests PASSED.${RESET}"
else
    status=$?
    echo "${RED}[UT] Unit tests FAILED (rc=${status}).${RESET}"
    exit "${status}"
fi

# Collect coverage if gcov data is present.
if command -v gcov >/dev/null 2>&1; then
    echo "${BLUE}[UT] Collecting gcov coverage...${RESET}"
    pushd "${BUILD_DIR}" >/dev/null

    gcno_files=$(find . -name '*.gcno' -print)
    if [[ -n "${gcno_files}" ]]; then
        # Generate .gcov files for all instrumented objects.
        gcov ${gcno_files} >/dev/null 2>&1 || true
    fi

    popd >/dev/null

    # Move all generated .gcov files under results for inspection.
    find "${BUILD_DIR}" -name '*.gcov' -exec mv {} "${RESULTS_DIR}" \; || true
else
    echo "${RED}[UT] gcov not found; skipping coverage collection${RESET}" >&2
fi

# Remove any stale UT .log files; coverage is what we keep under results.
rm -f "${RESULTS_DIR}"/*.log 2>/dev/null || true

echo "${GREEN}[UT] Coverage (if available) stored in ${RESULTS_DIR}${RESET}"
