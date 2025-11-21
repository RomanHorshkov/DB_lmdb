/**
 * @file db_lmdb_config.h
 * @brief LMDB-specific database configuration
 */

#ifndef DB_LMDB_CONFIG_H
#define DB_LMDB_CONFIG_H

#define DB_VER_MAJ                0
#define DB_VER_FIX                1
#define DB_VER_MIN                0

#define DB_VER                    0 /* Versioning */

/* size helpers */
#define KiB(x)                    ((size_t)(x) * 1024ULL)
#define MiB(x)                    (KiB(x) * 1024ULL)
#define GiB(x)                    (MiB(x) * 1024ULL)

/* Initial maximum LMDB map size */
#define DB_MAP_SIZE_INIT          MiB(256)

/* Maximum LMDB map size */
#define DB_MAP_SIZE_MAX           GiB(1)

/* max sub-dbis */
#define DB_MAX_DBIS               16

/* operation batch retry times */
#define DB_LMDB_RETRY_OPS_EXEC    3

/* operation batch RW cache size */
#define DB_LMDB_RW_OPS_CACHE_SIZE KiB(2)

/* Default filesystem mode for LMDB environment files (data.mdb/lock.mdb). */
#define DB_LMDB_ENV_MODE          0600u

/* Default filesystem mode for LMDB environment directory (owner-only). */
#define DB_LMDB_DIR_MODE          0700u

#endif /* DB_LMDB_CONFIG_H */
