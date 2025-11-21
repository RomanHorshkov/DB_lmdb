# DB LMDB – Tests TODO / Observations

This file summarizes issues, oddities, and potential improvements discovered while building unit tests for the core modules (`security`, `dbi_int`, `ops_init`, `ops_actions`, `ops_exec`). It is meant as a living checklist of places where behavior is surprising, fragile, or underspecified.

## Global patterns / Infrastructure

- **Global state (`DataBase`, `ops_cache`, LMDB stubs)**  
  - All core modules rely heavily on globals; tests need strict reset discipline (`ut_reset_lmdb_stubs()`, zeroing `ops_cache`, resetting `DataBase`).  
  - Any future tests must keep this in mind; otherwise, inter-test bleed will be very hard to debug.

- **LMDB abstraction via stubs**  
  - `tests/UT/ut_env.c` now owns LMDB behavior for tests (env, txn, dbi, put/get, etc.). Bugs in these stubs will leak into every UT.  
  - When changing LMDB usage, update stubs and hook types in one place; avoid ad-hoc mocks in individual UTs.

## `security.c`

- **Maps size expansion contracts**  
  - `_expand_env_mapsize()` assumes `DataBase` and `DataBase->env` are valid; otherwise it returns `-EIO`. Callers must treat `-EIO` as “fatal ENVs state” and not retry.  
  - When `mdb_env_info` keeps failing, the function eventually returns `-EIO` without distinguishing *why*; this collapses several LMDB error modes.

- **Error mapping and default path**  
  - `_map_mdb_err_to_errno()`’s default `return -rc` path is now tested for both positive and negative `rc`.  
  - Tests rely on the mapping table being stable; any future changes in mapping need UT updates (especially for corner LMDB codes: `MDB_DBS_FULL`, `MDB_BAD_DBI`, etc.).

## `dbi_int.c`

- **Flag composition**  
  - `dbi_open_flags_from_type()` ignores `DBI_TYPE_NOOVERWRITE`, which is correct for LMDB open flags but non-obvious from the type name alone.  
  - All other bits (`DUPSORT`, `DUPFIXED`) are OR’d with `MDB_CREATE`. If new `dbi_type_t` flags are added, this function will need a review.

- **`dbi_put_flags_from_type()` semantics**  
  - Only `DBI_TYPE_NOOVERWRITE` influences `MDB_NOOVERWRITE`.  
  - Combining `DBI_TYPE_NOOVERWRITE` with other bits is allowed but currently ignored for put flags; future “type bits” must be carefully checked not to require additional put flags.

## `ops_init.c`

- **Return type vs. values**  
  - Private helpers (`_db_create_env`, `_db_set_max_dbis`, `_db_set_map_size`, `_db_open_env`, `_dbi_open`, `_dbi_get_flags`) are declared as `db_security_ret_code_t` but often return plain `0` instead of `DB_SAFETY_SUCCESS`. This works only because `DB_SAFETY_SUCCESS == 0`. Any change to the enum values would silently break these helpers.

- **Environment creation and cleanup**  
  - `_db_create_env()` correctly closes and nulls `DataBase->env` on `mdb_env_create` failure, but leaves it untouched on success. Tests confirm the expected handle assignment.  
  - On failure in later steps (`_db_set_max_dbis`, `_db_set_map_size`, `_db_open_env`), `ops_init_env()` returns `DB_SAFETY_FAIL` without explicit cleanup. Current higher-level `db_core_init()` calls `db_core_shutdown()` on failure; that coupling is easy to miss.

- **Error propagation consistency**  
  - `_db_set_max_dbis()` and `_db_set_map_size()` propagate LMDB errors via `security_check`, but callers (like `ops_init_env()`) ignore the exact `DB_SAFETY_*` code and collapse everything into `DB_SAFETY_FAIL`.  
  - This makes it impossible for `ops_init_env()` to express “retryable” vs “fatal” initialization errors, even if `security_check()` is willing to signal `DB_SAFETY_RETRY`.

- **Directory helper `_ensure_env_dir`**  
  - Correctly rejects:
    - Paths that exist but are not directories (`-ENOTDIR`).  
    - Directories with any group/other bits set (`-EACCES`).  
  - On failing `stat()`, it returns `-errno`, which is useful but unusual compared to the “always negative errno” convention used elsewhere. Callers must remember this is *not* `-mdb_rc`.

## `ops_actions.c`

- **Logging strings & copy-paste**  
  - Several error messages in `act_put()` and `act_get()` still use `_op_get` in their text, which is confusing in logs (`"_op_get: invalid input"` inside `act_put`, etc.).  
  - This does not affect behavior but is a debugging trap; UTs don’t rely on log text.

- **`act_txn_begin` contract vs implementation**  
  - The header claims: if return != `DB_SAFETY_SUCCESS`, `*out_txn` is guaranteed to be NULL.  
  - Implementation:
    - On LMDB error path, this guarantee currently relies on LMDB (or the stub) not writing a non-NULL pointer into `out_txn`.  
    - On invalid-input path (`!DataBase || !DataBase->env || !out_txn`), `*out_txn` is not touched at all.  
  - Tests currently ensure the LMDB-failure path leaves `*out_txn` as NULL (via the stub), but the invalid-input path is still “unspecified” in practice. This asymmetry is worth fixing in the implementation (explicitly null the pointer on any failure).

- **`act_get` buffer semantics**  
  - When the user provides a buffer (PRESENT), the function:
    - Fails with `DB_SAFETY_FAIL` and does **not** set `out_err` if the buffer is too small.  
    - UTs enforce this behavior; higher layers must treat this as a hard logic error, not a retriable LMDB issue.
  - When no buffer is provided (`val.kind != PRESENT`), the code:
    - Replaces `op->val` with a shallow view into the LMDB-managed buffer (pointer + size).  
    - This is only safe for the life of the transaction and until no writes invalidate the page; ops_exec’s RW cache is meant to work around that, but reuse outside this context is dangerous.

- **`_resolve_desc` recursion and indexing**  
  - `op_key_t.lookup.op_index` is treated as “how many positions back from the current op”; the helper uses `base - op_index` without bounds checks.  
  - `ops_add_operation()` does enforce `op_index <= n_ops` on *key lookup*, but nothing prevents a val lookup or manually constructed ops from going out-of-bounds if misused.  
  - Chains of lookups (key-from-key-from-val, etc.) are supported and now tested, but this recursion can easily blow up if an operation accidentally references itself or a forward op.

## `ops_exec.c`

- **API / header oddities**  
  - `ops_exec.h` declares `_exec_ops` as `static db_security_ret_code_t _exec_ops(...)` in a public header. This is effectively a private helper but exposed in the header, which is confusing for users and can cause ODR issues if included in multiple TUs.

- **Logging inconsistencies**  
  - In `_exec_rw_ops()`, the final debug log message says `"_exec_ro_ops: RO txn completed, aborted"` in the RW path. This is clearly a copy-paste error and misleading when reading logs.

- **Batch type & transaction flags**  
  - `_batch_type_from_op_type()` maps:
    - `DB_OPERATION_GET` → RO  
    - Everything else (PUT/DEL/UNKNOWN) → RW  
  - `_txn_type_from_batch_type()` then maps RO batches to `MDB_RDONLY`.  
  - This means a batch that *starts* with GETs but later receives a PUT will flip to RW. UTs confirm this behavior via `ops_add_operation()` tests; users must be careful not to rely on early GETs forcing a read-only transaction.

- **Ops cache lifetime and reset**  
  - Both `_exec_rw_ops()` and `_exec_ro_ops()` clear `ops_cache` on exit, and `ops_execute_operations()` also clears it after dispatching. Double-clearing is harmless but redundant; however, any new code that expects `ops_cache` contents *after* execution will be broken.

- **RW cache semantics**  
  - `_rw_cache_alloc()` correctly rejects allocations that exceed `DB_LMDB_RW_OPS_CACHE_SIZE`, returning NULL and keeping `rw_cache_used` unchanged.  
  - `_exec_op()` for GET in RW batches:
    - Relies on `act_get()` guaranteeing a PRESENT value; if `present.ptr` or `present.size` is invalid, it returns `DB_SAFETY_FAIL` and may set `*out_err = -EIO`.  
    - Copies the GET result into the internal RW cache and rewrites `op->val.present.ptr` to point into this cache.  
  - This design is subtle: callers must not assume the pointer returned by `act_get()` is stable; they must use `op->val` after `_exec_op()` has run, not before.

- **Retry loops**  
  - Both `_exec_rw_ops()` and `_exec_ro_ops()` implement retry loops based on `DB_LMDB_RETRY_OPS_EXEC`. They only retry on `DB_SAFETY_RETRY` from `act_txn_begin` or `_exec_ops`, not on commit failures.  
  - UTs exercise the RETRY propagation at the `_exec_ops` level (via stubbed `act_get`); future changes to retry policy must maintain this contract.

## Things to validate or refine later

- **`act_txn_begin` and `act_txn_commit` error semantics**  
  - Consider tightening guarantees:
    - Always set `*out_txn = NULL` on any failure path in `act_txn_begin`.  
    - Ensure `act_txn_commit` sets `*out_err` consistently for all error modes.

- **`ops_execute_operations` vs. RW cache**  
  - The RW cache is only used in the RW code path for GETs; there is no higher-level API yet that exposes cached values after `ops_execute_operations()`.  
  - Before adding such an API, clarify how long cached pointers remain valid and whether a second execution may reuse or clear the cache.

- **Global `ops_cache` and concurrency**  
  - Current design is single-threaded: `ops_cache` is a global, unprotected structure.  
  - If concurrent use is ever needed, the interface will need explicit serialization or rework (e.g., per-thread caches).

- **Coverage gaps**  
  - Security, `dbi_int`, `ops_init`, `ops_actions`, and `ops_exec` are all exercised by UTs and visible in the aggregated coverage report, but coverage for some helper paths (e.g. rare LMDB errors, deep lookup chains) is still partial by design.  
  - When adding new features, prefer writing UTs that target specific branches instead of relying solely on ITs or manual testing.

These notes are intentionally conservative: tests assert behavior where it is clear and strictly defined; where semantics are ambiguous or surprising, UTs avoid locking in questionable behavior and this document instead flags them for future design decisions or fixes. 

