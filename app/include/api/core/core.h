/**
 * @file db_lmdb_safety.h
 * @brief Centralized LMDB return-code policy and retry/resize guidance.
 *
 * This layer converts raw LMDB status codes into actionable decisions
 * so call sites stay small and consistent.
 */

#ifndef DB_LMDB_CORE_H
#define DB_LMDB_CORE_H

#include "dbi.h" /* DB, DataBase, common, stddef, stdintm  */

#ifdef __cplusplus
extern "C"
{
#endif

#define DB_LMDB_RETRY_TRANSACTION 2
#define DB_LMDB_RETRY_DBI_OPEN    3
#define DB_LMDB_RETRY_DBI_FLAGS   3

int core_init_db(const char* const path, const unsigned int mode, dbi_init_t* init_dbis,
                 unsigned n_dbis, int* const out_err);

#ifdef __cplusplus
}
#endif

#endif /* DB_LMDB_CORE_H */
