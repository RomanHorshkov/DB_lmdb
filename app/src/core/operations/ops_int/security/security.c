/**
 * @file security.c
 * @brief Centralized LMDB -> errno policy and transaction retry guidance.
 *
 * @details
 * This module centralizes the handling of LMDB (mdb) return codes and
 * converts them into the project's `db_security_ret_code_t` policy answers
 * (safe/ retry / fail) and a POSIX-style `errno` mapping when requested.
 *
 * Responsibilities:
 * - Map LMDB errors to POSIX `errno` (see `_map_mdb_err_to_errno`).
 * - Decide whether an LMDB error should cause the caller to retry the
 *   operation, expand the environment map size (when appropriate), or fail
 *   the operation permanently (`security_check`).
 * - Attempt a mapsize expansion in a safe manner when `MDB_MAP_FULL`
 *   is encountered (`_expand_env_mapsize`).
 *
 * Notes & guarantees:
 * - `security_check` will abort the supplied `MDB_txn *txn` when the
 *   LMDB error requires transaction invalidation (for example: map full,
 *   corruption, or other fatal conditions). Callers should not attempt to
 *   reuse a transaction after it has been aborted here.
 * - `_expand_env_mapsize` must be called with no open transactions that
 *   use the environment (this function itself checks `DataBase`/`env`).
 * - The code attempts to double the current map size up to the configured
 *   maximum and returns LMDB style result codes on failure so callers may
 *   inspect LMDB's detailed cause when present.
 *
 * Thread-safety:
 * - The underlying LMDB environment functions are responsible for
 *   concurrency; this module does not enforce additional locking. If the
 *   embedding application requires serialized mapsize changes, it must
 *   provide that coordination.
 *
 * Usage example:
 * @code
 * int err_no;
 * db_security_ret_code_t safety = security_check(mdb_rc, txn, &err_no);
 * switch (safety) {
 * case DB_SAFETY_SUCCESS:    // success, continue
 * case DB_SAFETY_RETRY: // caller should retry the operation
 * case DB_SAFETY_FAIL:  // terminal failure, propagate error
 * }
 * @endcode
 */

#include "security.h"

#include <errno.h>

#include "db.h"     /* DB, DataBase, lmdb  */
#include "common.h" /* EML_* macros, LMDB_EML_* */

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

#define LOG_TAG "db_security"

/****************************************************************************
 * PRIVATE STUCTURED VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

/**
 * @brief Convert an LMDB return code to a POSIX-style `errno` value.
 *
 * @details
 * The mapping is selected to best express the semantic meaning of each
 * LMDB error using standard POSIX errno codes. For unmapped/unknown LMDB
 * return codes this function returns `-rc` to allow callers to observe the
 * original LMDB numeric value.
 *
 * @param rc LMDB return code (one of the MDB_* constants from lmdb.h).
 * @return A negative errno value (e.g. -ENOENT) for mapped errors, 0 on
 *         success (when `rc == MDB_SUCCESS`) or `-rc` for unknown LMDB values
 *         so callers still have the original numeric rc available as a
 *         negative error.
 *
 * @note This function intentionally returns negative `errno` values so they
 *       can be returned directly from functions which use the `-errno`
 *       convention. Callers that expect the positive `errno` should negate
 *       the return value.
 */
int _map_mdb_err_to_errno(int rc);

/**
 * @brief Attempt to expand the LMDB environment's map size.
 *
 * @details
 * This helper doubles the current `DataBase->map_size_bytes` up to the
 * configured maximum `DataBase->map_size_bytes_max`. It uses
 * `mdb_env_set_mapsize()` to request the new mapsize and updates the in-
 * process `DataBase` bookkeeping on success.
 *
 * @pre There must be no active LMDB transactions that might be using the
 *      environment while this function is called. The caller is responsible
 *      for ensuring that precondition.
 *
 * @return MDB_SUCCESS (0) on success. On failure, returns the LMDB error
 *         code returned by `mdb_env_set_mapsize()` (positive LMDB rc), or a
 *         negative POSIX-style code when `DataBase`/`env` is invalid.
 */
int _expand_env_mapsize(void);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

db_security_ret_code_t security_check(const int mdb_rc, MDB_txn* const txn, int* const out_errno)
{
    /* Fast path */
    if(mdb_rc == MDB_SUCCESS) return DB_SAFETY_SUCCESS;

    /* Map error if requested */
    if(out_errno) *out_errno = _map_mdb_err_to_errno(mdb_rc);

    /* Check lmdb return */
    switch(mdb_rc)
    {
        /* Retry Cases */
        case MDB_MAP_RESIZED:
        case MDB_PAGE_FULL:
        case MDB_TXN_FULL:
        case MDB_CURSOR_FULL:
        case MDB_BAD_RSLOT:
        case MDB_READERS_FULL:
        case MDB_MAP_FULL:
            /* Here the transaction has to be aborted */
            if(txn) mdb_txn_abort(txn);
            /* Check if need to resize */
            if(mdb_rc == MDB_MAP_FULL)
            {
                /* resize */
                int expand = _expand_env_mapsize();
                if(expand == 0)
                {
                    EML_INFO(LOG_TAG, "_check: mapsize expanded on MDB_MAP_FULL");
                    return DB_SAFETY_RETRY;
                }

                EML_ERROR(LOG_TAG, "_check: mapsize expand failed, lmdb_ret=%d", expand);
                return DB_SAFETY_FAIL;
            }

            return DB_SAFETY_RETRY;

        /* Logic failures which do not invalidate the transaction */
        case MDB_NOTFOUND:
        case MDB_KEYEXIST:
            return DB_SAFETY_FAIL;

        /* Anything else just invalidate */
        default:
            EML_ERROR(LOG_TAG, "_check: unknown lmdb error %d", mdb_rc);
            if(txn) mdb_txn_abort(txn);
            return DB_SAFETY_FAIL;
    }
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int _map_mdb_err_to_errno(int rc)
{
    if(rc == MDB_SUCCESS) return 0;

    switch(rc)
    {
        case MDB_NOTFOUND:
            return -ENOENT;    /* absent key/EOF */
        case MDB_KEYEXIST:
            return -EEXIST;    /* unique constraint */
        case MDB_MAP_FULL:
            return -ENOSPC;    /* env mapsize reached */
        case MDB_DBS_FULL:
            return -ENOSPC;    /* max named DBs */
        case MDB_READERS_FULL:
            return -EAGAIN;    /* too many readers */
        case MDB_TXN_FULL:
            return -EOVERFLOW; /* too many dirty pages */
        case MDB_CURSOR_FULL:
            return -EOVERFLOW; /* internal stack too deep */
        case MDB_PAGE_FULL:
            return -ENOSPC;    /* internal page space */
        case MDB_MAP_RESIZED:
            return -EAGAIN;    /* retry after resize */
        case MDB_INCOMPATIBLE:
            return -EPROTO;    /* flags/type mismatch */
        case MDB_VERSION_MISMATCH:
            return -EINVAL;    /* library/env mismatch */
        case MDB_INVALID:
            return -EINVAL;    /* not an LMDB file */
        case MDB_PAGE_NOTFOUND:
            return -EIO;       /* likely corruption */
        case MDB_CORRUPTED:
            return -EIO;       /* detected corruption */
        case MDB_PANIC:
            return -EIO;       /* fatal env error */
        case MDB_BAD_RSLOT:
            return -EBUSY;     /* reader slot misuse */
        case MDB_BAD_TXN:
            return -EINVAL;    /* invalid/child txn */
        case MDB_BAD_VALSIZE:
            return -EINVAL;    /* key/data size wrong */
        case MDB_BAD_DBI:
            return -ESTALE;    /* DBI changed/dropped */
        default:
            return -rc;        /* unknown error, let pass the code */
    }
}

int _expand_env_mapsize(void)
{
    /* Check database min health */
    if(!(DataBase && DataBase->env)) return -EIO;

    /* Get straigth environment info */
    unsigned int retries = 3;
    MDB_envinfo  info;
    while((mdb_env_info(DataBase->env, &info) != MDB_SUCCESS) && retries > 0)
    {
        retries--;
    }
    if(retries == 0)
    {
        EML_ERROR(LOG_TAG, "_expand_env_mapsize: mdb_env_info failed after retries");
        return -EIO;
    }

    /* Double current map size */
    size_t desired = info.me_mapsize * 2;

    /* Check against max, allow equal */
    if(desired > DataBase->map_size_bytes_max)
    {
        EML_ERROR(LOG_TAG, "_expand_env_mapsize: desired size %zu exceeds max %zu", desired,
                  DataBase->map_size_bytes_max);
        return MDB_MAP_FULL;
    }

    int set = mdb_env_set_mapsize(DataBase->env, desired);
    if(set != MDB_SUCCESS)
    {
        EML_ERROR(LOG_TAG, "_expand_env_mapsize: mdb_env_set_mapsize failed %d", set);
        return set;
    }

    return set;
}
