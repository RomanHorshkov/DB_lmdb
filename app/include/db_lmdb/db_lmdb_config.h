/**
 * @file db_lmdb_config.h
 * @brief LMDB-specific database configuration
 */


#ifndef DB_LMDB_CONFIG_H
#define DB_LMDB_CONFIG_H

/* size helpers */
#define KiB(x)                         ((size_t)(x) * 1024ULL)
#define MiB(x)                         (KiB(x) * 1024ULL)
#define GiB(x)                         (MiB(x) * 1024ULL)

/* Initial maximum LMDB map size */
#define DB_MAP_SIZE_INIT            MiB(256)

/* Maximum LMDB map size */
#define DB_MAP_SIZE_MAX             GiB(1)

#endif /* DB_LMDB_CONFIG_H */