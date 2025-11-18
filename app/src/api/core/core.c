/**
 * @file db_lmdb_core.c
 */

#include "core.h"
#include "ops_exec.h"  /* ops_init_env etc */
#include "ops_setup.h" /* ops_init_dbi etc */

/************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

#define LOG_TAG                 "db_core"

#define DB_LMDB_RETRY_OPERATION 3

/************************************************************************
 * PRIVATE STUCTURED VARIABLES
 ****************************************************************************
 */
/* None */

/************************************************************************
 * PRIVATE VARIABLES
 ****************************************************************************
 */
/* None */

/************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int core_db_init(const char* const path, const unsigned int mode, dbi_init_t* init_dbis,
                 unsigned n_dbis)
{
    if(DataBase)
    {
        EML_ERROR(LOG_TAG, "_db_init: database already initialized");
        return -EALREADY;
    }

    /* Check input */
    if(!path || !(*path) || !init_dbis || n_dbis == 0)
    {
        EML_ERROR(LOG_TAG, "_init_db: invalid input");
        return -EINVAL;
    }

    /* Prepare error output */
    int  out_err_val = -EINVAL;
    int* out_err     = &out_err_val;

    /* Allocate DB */
    DataBase_t* new_DataBase = NULL;
    new_DataBase             = calloc(1, sizeof(DataBase_t));
    if(!new_DataBase)
    {
        EML_ERROR(LOG_TAG, "_init_db: calloc(DataBase) failed");
        return -ENOMEM;
    }

    /* Set map size values */
    // new_DataBase->map_size_bytes     = (size_t)DB_MAP_SIZE_INIT;
    new_DataBase->map_size_bytes_max = (size_t)DB_MAP_SIZE_MAX;
    /* Set the global handle */
    DataBase                         = new_DataBase;

    /* Do not allow retry on init */
    switch(ops_init_env(DB_MAX_DBIS, path, mode, out_err))
    {
        case DB_SAFETY_OK:
            break;
        default:
            EML_ERROR(LOG_TAG, "_init_db: _init_env failed, err=%d", out_err_val);
            goto fail;
    }

    /* Init a transaction */
    MDB_txn* txn = NULL;

    switch(ops_txn_begin(&txn, 0, out_err))
    {
        case DB_SAFETY_OK:
            break;
        default:
            EML_ERROR(LOG_TAG, "_init_db: _txn_begin failed, err=%d", out_err_val);
            goto fail;
    }

    /* Initialize all requested dbis */
    for(size_t i = 0; i < (size_t)n_dbis; i++)
    {
        if(!&init_dbis[i])
        {
            EML_ERROR(LOG_TAG, "_init_db: invalid dbi_init_t at index %zu", i);
            out_err_val = -EINVAL;
            goto fail;
        }

        dbi_init_t* init_dbi = &init_dbis[i];
        switch(ops_init_dbi(txn, init_dbi->name, init_dbi->dbi_idx, init_dbi->type, out_err))
        {
            case DB_SAFETY_OK:
                break;
            default:
                EML_ERROR(LOG_TAG, "_init_db: _init_dbi failed for dbi %s, err=%d", init_dbi->name,
                          (out_err) ? *out_err : -1);
                goto fail;
        }
    }

    switch(ops_txn_commit(txn, out_err))
    {
        case DB_SAFETY_OK:
            break;
        default:
            EML_PERR(LOG_TAG, "_init_db: _txn_commit failed err=%d", *out_err);
            goto fail;
    }

    EML_INFO(LOG_TAG, "_init_db: database initialized with %u dbis at %s", n_dbis, path);
    return 0;

fail:
    return out_err_val;
}

int core_db_op_add(const op_type_t op_type, const unsigned int dbi_idx, op_key_ref_t ket_ref, void_store_t* key_vs, void_store_t* val_vs,
                   DB_operation_t** out_op)
{
    
    return 0;
}

size_t core_db_shutdown(void)
{
    /* No database initialized: idempotent no-op. */
    if(!DataBase) return 0;

    size_t final_mapsize = 0;

    /* Best-effort: ask LMDB for the current mapsize. */
    if(DataBase->env)
    {
        MDB_envinfo info;
        int         rc = mdb_env_info(DataBase->env, &info);
        if(rc == MDB_SUCCESS)
        {
            final_mapsize = (size_t)info.me_mapsize;
        }
        else
        {
            LMDB_EML_WARN(LOG_TAG, "_shutdown:mdb_env_info failed", rc);
            final_mapsize = 0;
        }

        /* Close any opened DBIs before closing the environment. */
        if(DataBase->dbis && DataBase->n_dbis > 0)
        {
            for(uint8_t i = 0; i < DataBase->n_dbis; ++i)
            {
                MDB_dbi handle = (MDB_dbi)DataBase->dbis[i].dbi;
                if(handle != (MDB_dbi)0)
                {
                    mdb_dbi_close(DataBase->env, handle);
                }
            }
        }

        /* Close LMDB environment (aborts any remaining txns). */
        mdb_env_close(DataBase->env);
        DataBase->env = NULL;
    }

    /* Free DBI descriptor array. */
    if(DataBase->dbis)
    {
        free(DataBase->dbis);
        DataBase->dbis = NULL;
    }
    DataBase->n_dbis             = 0;
    DataBase->map_size_bytes_max = 0;

    /* Free main DB structure and clear global. */
    free(DataBase);
    DataBase = NULL;

    EML_INFO(LOG_TAG, "_shutdown: database shut down, final mapsize=%zu", final_mapsize);

    return final_mapsize;
}
