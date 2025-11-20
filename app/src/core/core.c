/**
 * @file db_lmdb_core.c
 */

#include <errno.h>       /* EINVAL, ENOMEM, EALREADY */
#include <stdint.h>      /* uint8_t */
#include <stdlib.h>      /* calloc, free */

#include "core.h"
#include "common.h"        /* EML_* macros, LMDB_EML_* */
#include "db.h"            /* DataBase_t, MDB_envinfo */
#include "dbi_int.h"       /* dbi_t */
#include "ops_init.h"      /* ops_init_env, ops_init_dbi */
#include "ops_actions.h"   /* act_txn_begin, act_txn_commit */
#include "ops_exec.h"      /* ops_add_operation, ops_execute_operations */
#include "ops_internals.h" /* op_t, op_key_t, op_type_t */
#include "ops_facade.h"    /* DB_OPERATION_* */

/* Definition of the global DB handle declared in db.h */
DataBase_t* DataBase = NULL;

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

int db_core_init(const char* const path, const unsigned int mode,
                 const char* const* dbi_names, const dbi_type_t* dbi_types,
                 unsigned n_dbis)
{
    EML_INFO(LOG_TAG, "_init_db: starting init (path=%s, mode=%o, n_dbis=%u)",
             path ? path : "(null)", mode, n_dbis);

    if(DataBase)
    {
        EML_ERROR(LOG_TAG, "_db_init: database already initialized");
        return -EALREADY;
    }

    /* Check input */
    if(!path || !(*path) || !dbi_names || !dbi_types || n_dbis == 0)
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

    /* Set max map size */
    new_DataBase->map_size_bytes_max = (size_t)DB_MAP_SIZE_MAX;

    /* Allocate DBI descriptor array */
    new_DataBase->dbis = calloc(n_dbis, sizeof(dbi_t));
    if(!new_DataBase->dbis)
    {
        EML_ERROR(LOG_TAG, "_init_db: calloc(dbis) failed");
        free(new_DataBase);
        return -ENOMEM;
    }
    new_DataBase->n_dbis = n_dbis;

    /* Set the global handle */
    DataBase = new_DataBase;

    /* Do not allow retry on init */
    switch(ops_init_env(DB_MAX_DBIS, path, mode, out_err))
    {
        case DB_SAFETY_SUCCESS:
            EML_INFO(LOG_TAG, "_init_db: environment initialized (max_dbis=%u)", DB_MAX_DBIS);
            break;
        default:
            EML_ERROR(LOG_TAG, "_init_db: _init_env failed, err=%d", out_err_val);
            goto fail;
    }

    /* Init a transaction */
    MDB_txn* txn = NULL;

    switch(act_txn_begin(&txn, 0, out_err))
    {
        case DB_SAFETY_SUCCESS:
            EML_DBG(LOG_TAG, "_init_db: init transaction begun");
            break;
        default:
            EML_ERROR(LOG_TAG, "_init_db: _txn_begin failed, err=%d", out_err_val);
            goto fail;
    }

    /* Initialize all requested DBIs */
    for(unsigned i = 0; i < n_dbis; ++i)
    {
        const char*     name = dbi_names[i];
        const dbi_type_t type = dbi_types[i];

        if(!name || !(*name))
        {
            EML_ERROR(LOG_TAG, "_init_db: invalid dbi name at index %u", i);
            out_err_val = -EINVAL;
            goto fail;
        }

        switch(ops_init_dbi(txn, name, i, type, out_err))
        {
            case DB_SAFETY_SUCCESS:
                EML_INFO(LOG_TAG, "_init_db: DBI[%u] \"%s\" initialized (type=%d)", i, name, type);
                break;
            default:
                EML_ERROR(LOG_TAG, "_init_db: _init_dbi failed for dbi %s, err=%d", name,
                          (out_err) ? *out_err : -1);
                goto fail;
        }
    }

    switch(act_txn_commit(txn, out_err))
    {
        case DB_SAFETY_SUCCESS:
            EML_DBG(LOG_TAG, "_init_db: init transaction committed");
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

int db_core_add_op(unsigned dbi_idx, op_type_t type,
                   const void* key_data, size_t key_size,
                   const void* val_data, size_t val_size)
{
    /* Validate global DB and DBI index */
    if(!DataBase || !DataBase->dbis || dbi_idx >= DataBase->n_dbis)
    {
        EML_ERROR(LOG_TAG, "db_core_add_op: invalid db/dbi (db=%p idx=%u n_dbis=%zu)",
                  (void*)DataBase, dbi_idx, DataBase ? DataBase->n_dbis : 0);
        return -EINVAL;
    }

    /* Validate operation type: for now only PUT/GET are supported. */
    if(type != DB_OPERATION_PUT && type != DB_OPERATION_GET)
    {
        EML_ERROR(LOG_TAG, "db_core_add_op: unsupported op type=%d", type);
        return -EINVAL;
    }

    /* Validate key buffer */
    if(!key_data || key_size == 0)
    {
        EML_ERROR(LOG_TAG, "db_core_add_op: invalid key buffer");
        return -EINVAL;
    }

    /* Build op_key_t descriptors */
    op_key_t key_desc = {0};
    key_desc.kind         = OP_KEY_KIND_PRESENT;
    key_desc.present.size = key_size;
    key_desc.present.ptr  = (void*)key_data; /* safe: ops layer treats as read-only */

    op_key_t val_desc = {0};
    if(type == DB_OPERATION_PUT)
    {
        if(!val_data || val_size == 0)
        {
            EML_ERROR(LOG_TAG, "db_core_add_op: invalid value buffer for PUT");
            return -EINVAL;
        }
        val_desc.kind         = OP_KEY_KIND_PRESENT;
        val_desc.present.size = val_size;
        val_desc.present.ptr  = (void*)val_data;
    }

    /* Assemble operation */
    op_t op;
    op.dbi  = dbi_idx;
    op.type = type;
    op.key  = key_desc;
    op.val  = val_desc;

    EML_DBG(LOG_TAG,
            "db_core_add_op: queued op (dbi=%u type=%d key_size=%zu val_size=%zu)",
            dbi_idx, (int)type, key_size, val_size);

    return ops_add_operation(&op);
}

int db_core_exec_ops(void)
{
    EML_INFO(LOG_TAG, "db_core_exec_ops: executing queued operations");
    int rc = ops_execute_operations();
    if(rc == 0)
    {
        EML_INFO(LOG_TAG, "db_core_exec_ops: batch completed successfully");
    }
    else
    {
        EML_ERROR(LOG_TAG, "db_core_exec_ops: batch failed, rc=%d", rc);
    }
    return rc;
}

size_t db_core_shutdown(void)
{
    /* No database initialized: idempotent no-op. */
    if(!DataBase) return 0;

    size_t final_mapsize = 0;

    /* Best-effort: ask LMDB for the current mapsize. */
    if(DataBase->env)
    {
        EML_INFO(LOG_TAG, "_shutdown: starting LMDB env teardown");
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
