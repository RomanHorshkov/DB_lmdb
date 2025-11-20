# Static Architecture: LMDB Wrapper

This document captures the structural view of the LMDB wrapper around `db_lmdb_*`. Diagrams use PlantUML and are rendered locally into SVGs (no Mermaid).

## Scope

- Public surface: what headers a consumer includes (`db_lmdb_core.h`) and how it re-exports DBI and operation types.
- Internal layering: Core → Ops → DB/Security, with Core depending only on the ops facade and DB types.
- External services: LMDB C API and EMlog reached only through `config.h`/`common.h` and the security policy.

## Packages and boundaries (PlantUML-rendered)

![Static packages](../plantuml/generated/static_architecture.svg)

### DB package (core/operations/db)

Dedicated view of the `db` package and its internal DBI types:

![DB package](../plantuml/generated/db_package.svg)

### Security package (core/operations/security)

Centralized LMDB safety policy and errno mapping used by all operations:

![Security package](../plantuml/generated/security_package.svg)

### Ops package (core/operations)

Facade plus internal helpers for environment setup, transactions and batched operations:

![Ops package](../plantuml/generated/ops_package.svg)

## Responsibility map

- `app/include/db_lmdb_core.h` — single public entrypoint; re-exports the core facade.
- `app/src/core/core.c` — core orchestration: env/DBI init via ops, add/execute ops, shutdown.
- `app/include/core/operations/ops_facade.h` — ops facade types (`op_type_t`) and linkage to ops internals.
- `app/src/core/operations/ops_int/ops_init.c` — LMDB env creation, mapsize/max-db configuration, DBI open/flag caching.
- `app/src/core/operations/ops_int/ops_actions.c` — transaction helpers and single PUT/GET operations.
- `app/src/core/operations/ops_int/ops_exec.c` — batched operations and retry policy around transactions.
- `app/src/core/operations/ops_int/security/security.c` — LMDB→errno mapping, safety decisions, mapsize expansion.
- `app/include/core/operations/ops_int/db/db.h` — `DataBase_t` and global `DataBase` handle, owned by the DB package.
- `app/include/core/operations/ops_int/db/dbi_ext.h` — public DBI declarations (`dbi_type_t`); exported via the core header.

## Dependency rules (keep the separation of concerns)

- Public consumers include only `db_lmdb_core.h`; they should not reach into `ops_int` or `security` directly.
- Core may depend on `ops_facade.h` and DB headers, but not on LMDB or EMlog headers directly.
- Ops internals depend on DB and Security to perform work, but Security is the only module allowed to interpret raw LMDB return codes.
- Cross-layer calls flow downward (Core → Ops → DB/Security → LMDB/EMlog). Avoid lateral shortcuts that bypass the facade or security policy.
