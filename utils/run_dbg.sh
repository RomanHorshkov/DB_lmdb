#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build-debug}"
EXECUTABLE="${EXECUTABLE:-db_core_demo}"

if [ ! -x "${BUILD_DIR}/${EXECUTABLE}" ]; then
    echo "Executable ${BUILD_DIR}/${EXECUTABLE} not found. Did you run ./utils/build_dbg.sh ?" >&2
    exit 1
fi

"${BUILD_DIR}/${EXECUTABLE}"

