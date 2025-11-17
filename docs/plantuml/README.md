# PlantUML Assets

Purpose: provide reusable PlantUML sources rendered locally into SVGs (no CI pipeline). Mermaid is avoided entirely.

## Files
- `base.puml` — shared style (colors, fonts, sequence/package defaults). Include it for local rendering.
- `static_architecture.puml` — static package view (standalone, inline style).
- `dynamic_init.puml` — init flow sequence (standalone).
- `dynamic_ops.puml` — operation flow sequence (standalone).
- `examples/architecture_example.puml` — style demo using the shared base.

## How the Markdown is rendered
- `docs/architecture/*.md` reference the generated SVGs at `docs/plantuml/generated/*.svg`.
- The SVGs are versioned; regenerate them manually when `.puml` files change.

## Regenerating SVGs (manual)
Option A — local PlantUML:
1. Install PlantUML and Graphviz locally.
2. Run `./scripts/render_plantuml.sh`.

Option B — download from plantuml.com (no local PlantUML needed, requires network):
1. Run `PLANTUML_USE_SERVER=1 ./scripts/render_plantuml.sh` (default path also downloads if `plantuml` is absent).

All SVGs land in `docs/plantuml/generated/`; the Markdown files already point there.

Tips:
- Keep diagram sources in `docs/plantuml/` and regenerate as styles change.
- Generated artifacts belong in `docs/plantuml/generated/` so GitHub can display them directly.
