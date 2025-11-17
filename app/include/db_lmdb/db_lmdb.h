/**
 * @file db_lmdb.h
 * @brief Public interface for the project's LMDB wrapper.
 *
 * This header exposes a small, stable API for initializing and shutting
 * down the LMDB-backed storage used across the application, plus a
 * lightweight metrics helper to inspect the current environment.
 *
 * The implementation hides LMDB internals and provides a higher-level
 * initialization convenience that accepts an array of DB declarations
 * (`dbi_decl_t`) describing the named sub-databases to create/open.
 */

#ifndef DB_LMDB_H
#define DB_LMDB_H

#include <stddef.h> /* size_t */
#include <stdint.h> /* uintxx_t */

// #include "db_lmdb_dbi.h" /* dbi_decl_t */

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Types of LMDB named databases.
 */
typedef enum
{
    DBI_TYPE_DEFAULT  = 0,     /* no special flags */
    DBI_TYPE_DUPSORT  = 1,     /* sorted duplicate keys */
    DBI_TYPE_DUPFIXED = 1 << 1 /* fixed-size duplicate keys */
} dbi_type_t;

/**
 * @brief Declaration for a named LMDB database (non-owning strings).
 *
 * Intended for transient arrays passed to db_lmdb_init/db_lmdb_dbi_init.
 */
typedef struct
{
    const char* name;
    dbi_type_t  type;
} dbi_decl_t;

/****************************************************************************
 * PUBLIC DEFINES
 ****************************************************************************/

/**
 * @brief Initialize the LMDB environment and open the requested DBIs.
 *
 * This function creates and configures the LMDB environment, sets up any
 * required directories under @p meta_dir, and opens each named database
 * described by the @p dbis array. Each entry in @p dbis is a transient
 * descriptor (see `dbi_decl_t`) that describes the logical name and type.
 *
 * The caller retains ownership of the memory backing @p dbis; this function
 * only reads the strings during initialization. The implementation will
 * cache the MDB_dbi handles internally for subsequent operations.
 *
 * @param dbis      Pointer to an array of `dbi_decl_t` describing each
 *                  named database to open (must be non-NULL).
 * @param n_dbis    Number of entries in @p dbis (must be > 0).
 * @param meta_dir  Filesystem path to the LMDB environment directory where
 *                  the data files and lock files will be stored. Must be
 *                  a valid, writable directory path or a path whose
 *                  parent directory is writable (the implementation may
 *                  create the directory).
 *
 * @return 0 on success, negative errno-style error code on failure.
 *         Typical failures include EINVAL for invalid arguments, ENOENT
 *         or EACCES for filesystem-related errors, or other errno codes
 *         propagated from LMDB initialization.
 */
int db_lmdb_init(const dbi_decl_t* dbis, size_t n_dbis, const char* meta_dir);

/**
 * @brief Retrieve basic LMDB environment metrics.
 *
 * This helper returns the current size of used pages in the environment,
 * the currently configured mapsize, and the page size in bytes.
 * Any of the output pointers may be NULL if the caller does not need that
 * specific value.
 *
 * @param used     Optional out-parameter that receives the number of bytes
 *                 currently used by the environment (approximate).
 * @param mapsize  Optional out-parameter that receives the configured
 *                 mapsize (in bytes).
 * @param psize    Optional out-parameter that receives the LMDB page size
 *                 (in bytes).
 *
 * @return 0 on success, negative errno-style error code on failure.
 */
int db_lmdb_metrics(uint64_t* used, uint64_t* mapsize, uint32_t* psize);

/**
 * @brief Close the LMDB environment and free internal resources.
 *
 * After calling this function, the LMDB environment is shut down and any
 * cached DB handles become invalid. It is safe to call this function when
 * the environment is not initialized (no-op), but repeated calls should
 * be avoided in normal operation.
 */
void db_lmdb_close(void);

#ifdef __cplusplus
}
#endif

#endif /* DB_LMDB_H */
