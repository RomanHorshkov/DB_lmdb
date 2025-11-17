# LMDB Wrapper Architecture

This document summarizes the LMDB wrapper around `db_lmdb_*` in terms of components (static view) and typical control flows (dynamic view). Diagrams use Mermaid for quick inspection.

## Static Architecture

### Packages overview

```mermaid
%% Column view with logging visible
flowchart LR
    subgraph App [DB app packages]
        api[API]
        core[CORE]
        ops[OPS]
        api --> core --> ops
    end

    subgraph DB_specific [DB specific]
        db[DB]
        dbi[DBI]
        database[(DataBase)]
        db --> database
        dbi --> database
    end

    subgraph Internals [Internals]
        config[CONF]
    end

    subgraph Externals [Externals]
        emlog[EMlog]
        lmdb[lmdb]
    end

    
    DB_specific --> Internals
    core --> DB_specific
    ops --> DB_specific

    Internals --> Externals
```

### DB app packages

```mermaid
%% Core-only wiring (no externals shown)
flowchart TB
    api_core[db_lmdb API]
    core_core[db_lmdb_core safety/retry]
    dbi_core[db_lmdb_dbi descriptor cache]
    ops_core[db_lmdb_ops PUT/GET/REP/DEL]
    vs_core[void_store ops-only]
    internal_core[db_lmdb_internal.h / DB struct + config]

    api_core --> core_core
    core_core --> dbi_core
    dbi_core --> ops_core
    ops_core --> vs_core
    core_core --> internal_core
    dbi_core --> internal_core
    ops_core --> internal_core
```

```mermaid
%% Package view without logging clutter
flowchart LR
    subgraph Public
        api_pkg[db_lmdb]
    end
    subgraph Core
        core_pkg[db_lmdb_core]
        dbi_pkg[db_lmdb_dbi]
        internal_pkg[db_lmdb_internal.h / DB struct + config]
    end
    subgraph Ops
        ops_pkg[db_lmdb_ops]
        vs_pkg[void_store]
    end
    lmdb_pkg[(LMDB)]

    api_pkg --> core_pkg
    core_pkg --> dbi_pkg
    dbi_pkg --> ops_pkg
    ops_pkg --> vs_pkg
    core_pkg --> internal_pkg
    dbi_pkg --> internal_pkg
    ops_pkg --> internal_pkg
    internal_pkg --> lmdb_pkg
```

Guiding rules (key boundaries):
- `void_store` is an ops-only utility; keep it out of other packages.
- `emlog` is pulled through `db_lmdb_internal.h`, so everything logs via that shim (shown once in the Infra/EMlog columns).
- Public callers only depend on `db_lmdb.h` and optionally `db_lmdb_ops.h`.
- DBI descriptors (`dbi_desc_t`) bridge DBI setup and ops; they live on the `DB` singleton.

## Dynamic Architecture

### Environment and DBI setup

Sequence of a typical startup via `db_lmdb_init` using an array of `dbi_decl_t` declarations:

```mermaid
sequenceDiagram
    participant App as Caller
    participant API as db_lmdb_init
    participant DBI as db_lmdb_dbi_init
    participant Core as db_lmdb_core
    participant LMDB as LMDB

    App->>API: db_lmdb_init(dbi_decls, n_dbis, meta_dir)
    API->>Core: db_lmdb_create_env_safe(meta_dir, max_dbis, map_size)
    Core->>LMDB: mdb_env_create / set_maxdbs / set_mapsize / env_open
    Core-->>API: env ready (DB->env set)
    API->>DBI: db_lmdb_dbi_init(decls, n_dbis)
    DBI->>Core: db_lmdb_txn_begin_safe(DB->env)
    loop for each declaration
        DBI->>Core: db_lmdb_dbi_open_safe(txn, name, flags)
        DBI->>Core: db_lmdb_dbi_get_flags_safe(txn, dbi)
        DBI-->>DBI: cache dbi_desc_t (put_flags, dupsort/dupfixed)
    end
    DBI->>Core: db_lmdb_txn_commit_safe(txn)
    DBI-->>API: descriptors stored on DB->dbis
    API-->>App: ready to serve operations
```

Notes and invariants:

- The `DB` singleton is allocated once during init and carries the live LMDB env plus the DBI descriptor array. `db_lmdb_close` tears it down.
- Retry/resize policy lives in `db_lmdb_core` and is reused across env setup, DBI opening, and data operations.
- Operations (`ops_put_one_desc`, `ops_exec`, etc.) expect initialized descriptors and map/resize budgets; they batch work inside single transactions and are the only consumers of `void_store`.
