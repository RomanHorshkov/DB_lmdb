#!/usr/bin/env bash
set -euo pipefail

# Render PlantUML diagrams to SVGs under docs/plantuml/generated.
# Intended for CI; keep local workstations free from SVG generation if desired.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PLANTUML_DIR="${ROOT_DIR}/docs/plantuml"
OUT_DIR="${PLANTUML_DIR}/generated"

mkdir -p "${OUT_DIR}"

pushd "${PLANTUML_DIR}" >/dev/null
plantuml -tsvg -o "$(basename "${OUT_DIR}")" \
  static_architecture.puml \
  dynamic_init.puml \
  dynamic_ops.puml \
  examples/architecture_example.puml
popd >/dev/null

echo "Rendered SVGs to ${OUT_DIR}"
