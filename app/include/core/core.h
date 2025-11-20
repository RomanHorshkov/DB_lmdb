/**
 * @file db_lmdb_safety.h
 * @brief Centralized LMDB return-code policy and retry/resize guidance.
 *
 * This layer converts raw LMDB status codes into actionable decisions
 * so call sites stay small and consistent.
 */

#ifndef DB_LMDB_CORE_H
#define DB_LMDB_CORE_H

#include <stddef.h>           /* size_t */
#include "dbi_ext.h"          /* dbi_type_t */
#include "operations/ops_facade.h" /* op_type_t */

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
 * PUBLIC STRUCTURED TYPES
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
 * @param dbi_names  Array of NUL-terminated DBI names.
 * @param dbi_types  Array of dbi_type_t values matching @p dbi_names.
 * @param n_dbis     Number of entries in the @p init_dbis array.
 * @return 0 on success; negative POSIX-style errno on failure.
 */
int db_core_init(const char* const path, const unsigned int mode,
                 const char* const* dbi_names, const dbi_type_t* dbi_types,
                 unsigned n_dbis);

/**
 * @brief Queue a single database operation into the current batch.
 *
 * The operation is described using simple C types; the core converts
 * it into an internal op_t and caches it until @ref db_core_exec_ops
 * is called.
 *
 * For now only DB_OPERATION_PUT and DB_OPERATION_GET are supported; other
 * values will return -EINVAL.
 *
 * The data pointers in @p key and @p val must remain valid until
 * after db_core_exec_ops has been called.
 *
 * @param dbi_idx  Index of the target DBI (0-based).
 * @param type     Operation kind (DB_OPERATION_*).
 * @param key_data Pointer to key bytes.
 * @param key_size Size of key buffer in bytes.
 * @param val_data Pointer to value bytes (for PUT).
 * @param val_size Size of value buffer in bytes (for PUT).
 * @return 0 on success, negative errno-style code on failure.
 */
int db_core_add_op(unsigned dbi_idx, op_type_t type,
                   const void* key_data, size_t key_size,
                   const void* val_data, size_t val_size);

/**
 * @brief Execute all queued operations as a single batch.
 *
 * This function delegates to the internal ops execution engine which
 * groups operations into an LMDB transaction and applies retry /
 * safety policy.
 *
 * @return 0 on success; negative errno-style code on failure.
 */
int db_core_exec_ops(void);

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
