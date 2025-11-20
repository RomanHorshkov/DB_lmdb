#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"

cmake -S . -B "${BUILD_DIR}"
cmake --build "${BUILD_DIR}" -j"$(nproc)"

