/**
 * @file db_lmdb.h
 *
 * @brief LMDB database interface.
 */

#ifndef DB_LMDB_H
#define DB_LMDB_H

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * PUBLIC DEFINES
 ****************************************************************************
 */

typedef enum
{
    DBI_TYPE_DEFAULT  = 0,     /* no special flags */
    DBI_TYPE_DUPSORT  = 1,     /* sorted duplicate keys */
    DBI_TYPE_DUPFIXED = 1 << 1 /* fixed-size duplicate keys */
} dbi_type_t;

typedef struct
{
    char*      name; /* DBI name */
    dbi_type_t type; /* DBI type */
} dbi_t;

/****************************************************************************
 * PUBLIC FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

int db_lmdb_init(int n_dbis, const char* meta_dir);

int db_lmdb_open_dbis(dbi_t* dbis, size_t n_dbis);

int db_lmdb_metrics(uint64_t* used, uint64_t* mapsize, uint32_t* psize);

void db_lmdb_close(void);

#ifdef __cplusplus
}
#endif

#endif /* DB_LMDB_H */
