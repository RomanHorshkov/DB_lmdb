/**
 * @file db_lmdb_safety.h
 * @brief Centralized LMDB return-code policy and retry/resize guidance.
 *
 * This layer converts raw LMDB status codes into actionable decisions
 * so call sites stay small and consistent.
 */

#ifndef DB_LMDB_CORE_H
#define DB_LMDB_CORE_H

// #include "ops_facade.h" /* op_type_t */

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
 * PUBLIC FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

/**
 * @brief Initialize the LMDB environment and named DBIs.
 *
 * The function creates and opens the LMDB environment at the specified
 * filesystem path with the given mode, then opens/creates the named
 * sub-databases (DBIs) as described in the @p init_dbis array of length
 * @p n_dbis. On success, the global `DataBase` pointer is populated and
 * ready for use by higher-level operations.
 *
 * @param path       Filesystem path to the database directory.
 * @param mode       Filesystem mode (owner/group/other bits) used when
 *                   creating directories or files.
 * @param init_dbis  Array of DBI initialization descriptors.
 * @param n_dbis     Number of entries in the @p init_dbis array.
 * @return 0 on success; negative POSIX-style errno on failure.
 */
int db_core_init(const char* const path, const unsigned int mode, dbi_init_t* init_dbis,
                 unsigned n_dbis);

int db_core_op_add(const unsigned int dbi_idx, const op_type_t op_type, const op_key_t* key, const op_key_t* val);

int core_db_op_execute(/* TODO params */);

/**
 * @brief Gracefully shut down the LMDB environment and free DB resources.
 *
 * The function is idempotent: calling it when the database is not
 * initialized is a no-op and returns 0. On a valid, initialized database
 * it closes any opened DBIs, closes the LMDB environment, frees the
 * internal `DataBase` structure and clears the global pointer.
 *
 * @return The final configured LMDB map size in bytes (as known to the
 *         core layer) at the moment of shutdown. Returns 0 when the
 *         database was not initialized.
 */
size_t db_core_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* DB_LMDB_CORE_H */
