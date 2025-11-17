# LMDB Wrapper Architecture

This index points to the detailed docs that now live under `docs/`. CI renders PlantUML to SVGs and the Markdown links to those generated assets.

- `docs/architecture/STATIC.md` — package boundaries, what is public vs. private, and how `db.h` exposes `dbi.h` while keeping LMDB/EMlog hidden behind the internal shim.
- `docs/architecture/DYNAMIC.md` — control-flow sequences for init, ops, and teardown.
- `docs/plantuml/` — reusable PlantUML styles, standalone diagrams, and generated SVGs.

Tip: `./scripts/render_plantuml.sh` regenerates the SVGs; CI enforces freshness via `.github/workflows/plantuml.yml`.
