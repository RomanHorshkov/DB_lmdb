/**
 * @file security.c
 * @brief Centralized LMDB return-code policy and retry/resize guidance.
 */

#include "security.h"

#include <errno.h>
// #include <lmdb.h>   /* ret codes */

#include "db.h"         /* DB, DataBase, common, stddef, stdintm  */


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
 * @brief Map LMDB error code to errno.
 *
 * @param rc LMDB return code.
 * @return Mapped errno value.
 */
int _map_mdb_err_to_errno(int rc);

/**
 * @brief Expand LMDB environment map size by DB_ENV_MAPSIZE_EXPAND_STEP bytes.
 * 
 * @return 0 on success, negative error code otherwise.
 */
int db_env_mapsize_expand(void);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

db_security_ret_code_t security_check(const int mdb_rc, int* const out_errno)
{
    /* Fast path */
    if(mdb_rc == MDB_SUCCESS) return DB_SAFETY_OK;

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
            return DB_SAFETY_RETRY;
        case MDB_MAP_FULL: /* Need memory expansion */
            if(db_env_mapsize_expand() == 0) return DB_SAFETY_RETRY;
            EML_ERROR(LOG_TAG, "security_check: mapsize_expand failed");
            return DB_SAFETY_FAIL;

        /* Failure Cases */
        case MDB_NOTFOUND:
        case MDB_KEYEXIST:
        default:
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


int db_env_mapsize_expand(void)
{
    if(!(DB && DB->env)) return -EIO;
    uint64_t desired = DB->map_size_bytes * 2;
    if(desired > DB->map_size_bytes_max)
    {
        EML_WARN(LOG_TAG, "db_env_mapsize_expand: desired size %zu exceeds max %zu",
                 (size_t)desired, DB->map_size_bytes_max);
        return -ENOSPC;
    }
    return mdb_env_set_mapsize(DB->env, desired);
}
