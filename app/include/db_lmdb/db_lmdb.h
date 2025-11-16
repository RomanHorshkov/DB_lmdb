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

/****************************************************************************
 * PUBLIC FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

int db_lmdb_db_init(const unsigned int n_dbis, const char const* meta_dir);

int db_lmdb_open_dbis(dbi_t* dbis, unsigned int n_dbis);

int db_lmdb_metrics(uint64_t* used, uint64_t* mapsize, uint32_t* psize);

void db_lmdb_close(void);

#ifdef __cplusplus
}
#endif

#endif /* DB_LMDB_H */
