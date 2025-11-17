# PlantUML Assets

Purpose: provide reusable PlantUML sources rendered by CI into SVGs. Mermaid is avoided entirely.

## Files
- `base.puml` — shared style (colors, fonts, sequence/package defaults). Include it for local rendering.
- `static_architecture.puml` — static package view (standalone, inline style).
- `dynamic_init.puml` — init flow sequence (standalone).
- `dynamic_ops.puml` — operation flow sequence (standalone).
- `examples/architecture_example.puml` — style demo using the shared base.

## CI pipeline
- `.github/workflows/plantuml.yml` installs PlantUML/Graphviz, runs `scripts/render_plantuml.sh`, and fails if generated SVGs are out of date.
- `docs/architecture/*.md` reference the generated SVGs at `docs/plantuml/generated/*.svg`.

## Local rendering (optional)
1. Install PlantUML and Graphviz.
2. Run `./scripts/render_plantuml.sh`.
3. The SVGs land in `docs/plantuml/generated/`; the Markdown files already point there.

Tips:
- Keep diagram sources in `docs/plantuml/` and regenerate as styles change.
- Use the standalone `.puml` variants when embedding via alternative workflows; the generated SVGs live in `docs/plantuml/generated/`.
