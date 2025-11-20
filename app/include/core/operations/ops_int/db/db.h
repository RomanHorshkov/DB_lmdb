/**
 * @file db.h
 * @brief Main LMDB database structure and global instance.
 */

#ifndef DB_LMDB_DB_H
#define DB_LMDB_DB_H

#include "dbi_int.h" /* dbi_desc_t, dbi_init_t... */
#include "lmdb.h"    /* MDB_env, MDB_dbi */

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * PUBLIC DEFINES
 ****************************************************************************
*/
/* None */

/****************************************************************************
 * PUBLIC STRUCTURED VARIABLES
 ****************************************************************************
*/

/**
 * @brief Main LMDB database structure.
 */
typedef struct
{
    MDB_env* env;                /* LMDB environment */
    dbi_t*   dbis;               /* array of DBI descriptors */
    size_t   n_dbis;             /* number of DBIs in array */
    size_t   map_size_bytes_max; /* maximum map size */
} DataBase_t;

/************************************************************************
 * PUBLIC VARIABLES
 ****************************************************************************
 */

/* Global DB handle */
extern DataBase_t* DataBase;

/****************************************************************************
 * PUBLIC VARIABLES 
 ****************************************************************************
*/
/* None */

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

#ifdef __cplusplus
}
#endif

#endif /* DB_LMDB_DB_H */
