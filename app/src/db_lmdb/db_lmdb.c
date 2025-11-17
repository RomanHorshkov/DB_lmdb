/**
 * @file db_lmdb.c
 */

#include "db_lmdb_core.h"     /* db_lmdb_create_env_safe etc */
#include "db_lmdb_dbi.h"      /* db_lmdb_dbi_* */
#include "db_lmdb_internal.h" /* interface, config, emlog */

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

#define LOG_TAG "db_lmdb"

/****************************************************************************
 * PRIVATE STUCTURED VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE VARIABLES
 ****************************************************************************
 */

/* Global DB handle */
struct DB* DB = NULL;

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */
int db_lmdb_init(const dbi_decl_t* dbi_decls, size_t n_dbis, const char* meta_dir)
{
    if(DB)
    {
        EML_ERROR(LOG_TAG, "_db_init: database already initialized");
        return -EALREADY;
    }

    if(!dbi_decls || n_dbis == 0 || n_dbis > DB_MAX_DBIS || !meta_dir || !(*meta_dir))
    {
        EML_ERROR(LOG_TAG, "_db_init: invalid arguments n_dbis=%zu", n_dbis);
        return -EINVAL;
    }

    int res = -EINVAL;

    /* Allocate DB */
    struct DB* new_db = NULL;
    new_db            = calloc(1, sizeof(struct DB));
    if(!new_db)
    {
        res = -ENOMEM;
        goto fail;
    }

    /* Create LMDB environment (no retry) */
    res = db_lmdb_create_env_safe(new_db, meta_dir, DB_MAX_DBIS, DB_MAP_SIZE_INIT);
    if(res != 0)
    {
        EML_ERROR(LOG_TAG, "_db_init: db_lmdb_create_env_safe failed %d", res);
        goto fail;
    }

    /* Set map size values */
    new_db->map_size_bytes     = DB_MAP_SIZE_INIT;
    new_db->map_size_bytes_max = DB_MAP_SIZE_MAX;
    DB                         = new_db; /* expose for db_lmdb_dbi_init */

    /* Initialize sub-dbis */
    res = db_lmdb_dbi_init(dbi_decls, n_dbis);
    if(res != 0)
    {
        EML_ERROR(LOG_TAG, "_db_init: failed to open dbis %d", res);
        goto fail;
    }

    EML_DBG(LOG_TAG, "_db_init: LMDB environment opened at %s", meta_dir);
    EML_DBG(LOG_TAG, "_db_init: LMDB map size: initial=%zu bytes, max=%zu bytes",
            new_db->map_size_bytes, new_db->map_size_bytes_max);

    return 0;

fail:
    DB = NULL;
    EML_PERR(LOG_TAG, "_db_init: failed with err %d", res);
    if(new_db)
    {
        if(new_db->env) mdb_env_close(new_db->env);
        free(new_db->dbis);
        free(new_db);
    }
    return res;
}

int db_lmdb_metrics(uint64_t* used, uint64_t* mapsize, uint32_t* psize)
{
    if(!DB || !DB->env) return -EINVAL;
    MDB_envinfo info;
    MDB_stat    st;
    int         rc;
    rc = mdb_env_info(DB->env, &info);
    if(rc != MDB_SUCCESS) return -EIO;
    rc = mdb_env_stat(DB->env, &st);
    if(rc != MDB_SUCCESS) return -EIO;
    if(mapsize) *mapsize = (uint64_t)info.me_mapsize;
    if(psize) *psize = (uint32_t)st.ms_psize;
    if(used) *used = ((uint64_t)info.me_last_pgno + 1ull) * (uint64_t)st.ms_psize;
    return 0;
}

void db_lmdb_close(void)
{
    if(!DB) return;

    if(DB->env) mdb_env_close(DB->env);

    if(DB->dbis) free(DB->dbis);

    free(DB);
    DB = NULL;
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int db_map_mdb_err(int rc)
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
