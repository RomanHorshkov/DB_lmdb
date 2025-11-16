/**
 * @file db_internal.h
 * @brief 
 *
 * @author  Roman Horshkov <roman.horshkov@gmail.com>
 * @date    2025
 * (c) 2025
 */

#ifndef DB_INTERNAL_H
#define DB_INTERNAL_H

#include "db_lmdb.h"        /* interface dbi_decl_t */
#include "db_lmdb_config.h" /* config file */
#include "emlog.h"          /* EML_ERROR etc */

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * PUBLIC DEFINES
 ****************************************************************************
 */

/* Log LMDB ret with a stable format */
#define LMDB_LOG_ERR(tag, where, ret) \
    EML_ERROR("%s", "%s: ret=%d (%s)", tag, where, (ret), mdb_strerror(ret))

#define LMDB_EML_WARN(tag, where, ret) \
    EML_WARN("%s", "%s: ret=%d (%s)", tag, where, (ret), mdb_strerror(ret))

/****************************************************************************
 * PUBLIC STRUCTURED VARIABLES
 ****************************************************************************
*/

/**
 * @brief Main LMDB database structure.
 * Holds the LMDB environment and DBI handles.
 */
struct DB
{
    MDB_env*    env;                /* LMDB environment */
    dbi_desc_t* dbis;               /* array of DBI descriptors */
    uint8_t     n_dbis;             /* number of DBIs in array */
    size_t      map_size_bytes;     /* current map size */
    size_t      map_size_bytes_max; /* maximum map size */

    // /* USER DBIs */
    // MDB_dbi db_user_id2meta; /* ID -> META */
    // MDB_dbi db_user_mail2id; /* Email -> ID */
    // MDB_dbi db_user_id2pwd;  /* ID -> pwd hash */

    // /* DATA DBIs */
    // MDB_dbi db_data_id2meta; /* Data meta DBI */
    // MDB_dbi db_data_sha2id;  /* SHA -> data_id DBI */

    // /* AUTH DBIs */
    // /* Authentication DBIs — must mirror the open order in db_env.c (_start_auth_sub_dbis)
    //  * Order:
    //  *  - Primary: ref_hash -> refresh_record_t
    //  *  - Reverse per-user: user_id -> ref_hash (dupsort|dupfixed)
    //  *  - Reverse per-device: device_id -> ref_hash (dupsort|dupfixed)
    //  *  - Expiry queue: expires_be||ref_hash -> sentinel
    //  *  - Single-use barrier: ref_hash -> 1
    //  *  - DPoP nonce store: device_id -> meta_nonce_t
    //  *  - Device metadata store: device_id -> meta_device_t
    //  */
    // MDB_dbi db_auth_refhash2refrec; /* primary: ref_hash -> record */
    // MDB_dbi db_auth_user2refhash;   /* reverse: user_id -> ref_hash (dupsort, 32B dupfixed) */
    // MDB_dbi db_auth_device2ref;     /* reverse: device_id -> ref_hash (dupsort, 32B dupfixed) */
    // MDB_dbi db_auth_exp2ref;        /* expiry queue: expires_be||ref_hash -> sentinel */
    // MDB_dbi db_auth_once;           /* single-use barrier: ref_hash -> 1 */
    // /* DPoP nonce store: device_id -> meta_nonce_t. No dupes, overwrite allowed for rotation */
    // MDB_dbi db_dev_id2nonce; /* device_id -> meta_nonce_t for DPoP binding */
    // /* Device metadata store: device_id -> meta_device_t */
    // MDB_dbi db_dev_id2meta; /* device_id -> meta_device_t (auth_device) */

    // /* ACL DBIs */
    // MDB_dbi db_acl_fwd; /* key=principal(16)|rtype(1)|data(16), val=uint8_t(1) */
    // MDB_dbi db_acl_rel; /* key=data(16)|rtype(1), val=principal(16) (dupsort, dupfixed) */
};

/* DATABASE */
extern struct DB* DB; /* defined in db_env.c */

/****************************************************************************
 * PUBLIC DEFINES
 ****************************************************************************
 */

/**
 * @brief Map LMDB return codes to our DB error codes.
 * 
 * @param mdb_rc LMDB return code.
 * @return Mapped DB error code.
 */
int db_map_mdb_err(int mdb_rc);

/**
 * @brief Expand LMDB environment map size by DB_ENV_MAPSIZE_EXPAND_STEP bytes.
 * 
 * @return 0 on success, negative error code otherwise.
 */
int db_env_mapsize_expand(void);

#ifdef __cplusplus
}
#endif

#endif /* DB_INTERNAL_H */
