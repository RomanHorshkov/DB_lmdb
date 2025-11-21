#!/usr/bin/env bash
set -euo pipefail

# Build and run db_lmdb unit tests with coverage and store the
# coverage report under tests/UT/results. This is the single
# entrypoint for UT coverage.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build-ut-cov}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
RESULT_DIR="${ROOT_DIR}/tests/UT/results"

BLUE=$'\033[34m'
GREEN=$'\033[32m'
RED=$'\033[31m'
RESET=$'\033[0m'

echo "${BLUE}[UT] configuring (instrumented) build in ${BUILD_DIR}${RESET}"
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DDB_LMDB_ENABLE_UT_COVERAGE=ON

echo "${BLUE}[UT] cleaning previous coverage data...${RESET}"
find "${BUILD_DIR}" -name '*.gcda' -delete 2>/dev/null || true

echo "${BLUE}[UT] building unit tests...${RESET}"
cmake --build "${BUILD_DIR}" -j"$(nproc)"

UT_BINS=(
    "${BUILD_DIR}/db_core_ut_security"
    "${BUILD_DIR}/db_core_ut_dbi_int"
    "${BUILD_DIR}/db_core_ut_ops_init"
)

echo "${BLUE}[UT] running unit tests (with coverage)...${RESET}"
for bin in "${UT_BINS[@]}"; do
    if [[ ! -x "${bin}" ]]; then
        echo "${RED}[UT] Unit test binary not found: ${bin}${RESET}" >&2
        exit 1
    fi

    echo "${BLUE}[UT] running $(basename "${bin}")...${RESET}"
    if ! "${bin}"; then
        status=$?
        echo "${RED}[UT] Unit tests FAILED in $(basename "${bin}") (rc=${status}).${RESET}" >&2
        exit "${status}"
    fi
done

echo "${GREEN}[UT] All unit test binaries PASSED.${RESET}"

mkdir -p "${RESULT_DIR}"

if command -v gcovr >/dev/null 2>&1; then
    echo "${BLUE}[UT] cleaning previous coverage results...${RESET}"
    rm -f "${RESULT_DIR}"/* 2>/dev/null || true

    echo "${BLUE}[UT] generating coverage report...${RESET}"
    gcovr -r "${ROOT_DIR}" \
          --object-directory "${BUILD_DIR}" \
          --exclude 'tests/' \
          --exclude '/usr/*' \
          --html --html-details \
          -o "${RESULT_DIR}/UT_coverage.html"
    echo "${GREEN}[UT] coverage report stored in:${RESET} ${RESULT_DIR}"
else
    echo "${RED}[UT] gcovr not found; cannot generate coverage report.${RESET}" >&2
    exit 1
fi
