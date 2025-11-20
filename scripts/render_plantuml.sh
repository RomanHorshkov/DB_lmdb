#!/usr/bin/env bash
set -euo pipefail

# Render PlantUML diagrams to SVGs under docs/plantuml/generated.
# Purely local: requires a working `plantuml` binary, no server calls.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PLANTUML_DIR="${ROOT_DIR}/docs/plantuml/puml"
OUT_DIR="${ROOT_DIR}/docs/plantuml/generated"

mkdir -p "${OUT_DIR}"

if ! command -v plantuml >/dev/null 2>&1; then
  echo "Error: plantuml binary not found in PATH. Please install PlantUML locally." >&2
  exit 1
fi

echo "Rendering diagrams with local plantuml..."
pushd "${PLANTUML_DIR}" >/dev/null
plantuml -tsvg -o "../$(basename "${OUT_DIR}")" \
  static_architecture.puml \
  dynamic_init.puml \
  dynamic_ops.puml \
  db_package.puml
popd >/dev/null

echo "Rendered SVGs to ${OUT_DIR}"
