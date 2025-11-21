#!/usr/bin/env bash
set -euo pipefail

# Legacy convenience wrapper: build + run all unit tests with coverage,
# delegating to run_ut_cov.sh (single source of truth).

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

exec "${ROOT_DIR}/utils/run_ut_cov.sh"
