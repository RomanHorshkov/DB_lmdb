/**
 * @file db_lmdb_core.c
 */

#include "db_lmdb.h"
#include "db_lmdb_config.h"
#include "db_lmdb_internal.h"

#include "emlog.h"

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

#define LOG_TAG "lmdb_core"

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
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */
int db_lmdb_init(int n_dbis, const char* meta_dir)
{
    int        res    = -EINVAL;
    struct DB* new_db = NULL;

    if(DB)
    {
        EML_ERROR(LOG_TAG, "_lmdb_init: database already initialized");
        return -EALREADY;
    }

    if(n_dbis <= 0 || n_dbis > DB_MAX_DBIS || !meta_dir || !(*meta_dir))
    {
        EML_ERROR(LOG_TAG, "_lmdb_init: invalid arguments n_dbis=%d", n_dbis);
        return -EINVAL;
    }

    /* Allocate DB */
    new_db = calloc(1, sizeof(struct DB));
    if(!new_db)
    {
        res = -ENOMEM;
        goto fail_mem;
    }

    /* Allocate sub-dbis (descriptor form) */
    new_db->dbis = calloc((size_t)n_dbis, sizeof(dbi_desc_t));
    if(!new_db->dbis)
    {
        res = -ENOMEM;
        goto fail_mem;
    }

    /* Set number of dbis */
    new_db->n_dbis = (uint8_t)n_dbis;

    /* Create LMDB environment */
    res = mdb_env_create(&new_db->env);
    if(res != MDB_SUCCESS)
    {
        EML_ERROR(LOG_TAG, "mdb_env_create failed with err %d", res);
        res = db_map_mdb_err(res);
        goto fail_lmdb;
    }

    /* Set max sub-dbis */
    res = mdb_env_set_maxdbs(new_db->env, DB_MAX_DBIS);
    if(res != MDB_SUCCESS)
    {
        LMDB_LOG_ERR(LOG_TAG, "mdb_env_set_maxdbs failed", res);
        goto fail_lmdb;
    }

    /* Set initial map size */
    res = mdb_env_set_mapsize(new_db->env, DB_MAP_SIZE_INIT);
    if(res != MDB_SUCCESS)
    {
        LMDB_LOG_ERR(LOG_TAG, "mdb_env_set_mapsize failed", res);
        goto fail_lmdb;
    }

    /* Open environment */
    res = mdb_env_open(new_db->env, meta_dir, 0, 0770);
    if(res != MDB_SUCCESS)
    {
        LMDB_LOG_ERR(LOG_TAG, "mdb_env_open failed", res);
        goto fail_lmdb;
    }

    /* Set map size values */
    new_db->map_size_bytes     = DB_MAP_SIZE_INIT;
    new_db->map_size_bytes_max = DB_MAP_SIZE_MAX;

    DB = new_db;

    EML_DBG(LOG_TAG, "LMDB environment opened at %s", meta_dir);
    EML_DBG(LOG_TAG, "LMDB map size: initial=%zu bytes, max=%zu bytes", new_db->map_size_bytes,
            new_db->map_size_bytes_max);

    return 0;

fail_lmdb:
    res = db_map_mdb_err(res);

fail_mem:
    EML_PERR(LOG_TAG, "db_lmdb_init failed with err %d", res);
    if(new_db)
    {
        if(new_db->env)
        {
            mdb_env_close(new_db->env);
        }
        free(new_db->dbis);
        free(new_db);
    }
    return res;
}

int db_lmdb_open_dbis(dbi_t* dbis, size_t n_dbis)
{
    if(!DB || !DB->env || !DB->dbis || n_dbis > DB->n_dbis) return -EINVAL;

    /* Open a transaction to open sub-dbis */
    MDB_txn* txn = NULL;

    int ret = mdb_txn_begin(DB->env, NULL, 0, &txn);
    if(ret != MDB_SUCCESS)
    {
        LMDB_LOG_ERR(LOG_TAG, "_open_dbis _txn_begin failed", ret);
        return db_map_mdb_err(ret);
    }

    for(size_t i = 0; i < DB->n_dbis; i++)
    {
        unsigned open_flags = dbi_desc_open_flags(dbis[i].type);
        ret = dbi_desc_init(txn, dbis[i].name, open_flags, DBI_PUT_FLAGS_AUTO, &DB->dbis[i]);
        if(ret != 0)
        {
            LMDB_LOG_ERR(LOG_TAG, "_open_dbis dbi_desc_init failed", ret);
            goto fail_txn;
        }
    }
    ret = mdb_txn_commit(txn);
    if(ret != MDB_SUCCESS)
    {
        LMDB_LOG_ERR(LOG_TAG, "_open_dbis _txn_commit failed", ret);
        goto fail;
    }

    EML_DBG(LOG_TAG, "Successfully opened all sub dbis");
    return 0;

fail_txn:
    mdb_txn_abort(txn);
fail:
    return (ret > 0) ? db_map_mdb_err(ret) : ret;
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
