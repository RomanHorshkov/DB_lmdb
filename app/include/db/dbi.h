/**
 * @file db_lmdb_dbi.h
 * @brief Thin descriptor for LMDB named sub-databases.
 *
 * The types and helpers in this header provide a small, efficient
 * abstraction over LMDB named databases (DBIs). They cache the LMDB
 * handle and commonly-used flags so hot-path code can avoid repeated
 * flag queries and type checks.
 */

#ifndef DB_LMDB_DBI_H
#define DB_LMDB_DBI_H

#include <lmdb.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Sentinel value used to request automatic default put flags.
 *
 * When a caller supplies this value for the @p put_flags parameter
 * of `db_lmdb_dbi_init`, the implementation will select a safe default based
 * on the resolved DB flags:
 *  - Non-DUPSORT: MDB_NOOVERWRITE (enforce unique keys)
 *  - DUPSORT:     MDB_NODUPDATA   (enforce unique duplicates)
 */
#define DBI_PUT_FLAGS_AUTO 0xFFFFFFFFu
/**
 * @brief Cached, persistent descriptor for an opened named DBI.
 *
 * The runtime stores an array of these descriptors for quick access to the
 * MDB_dbi handle and precomputed flags used by higher-level operations.
 */
typedef struct
{
    MDB_dbi     dbi;         /**< LMDB handle. */
    const char* name;        /**< Logical name (non-owning). */
    dbi_type_t  type;        /**< Requested logical type (flags). */
    unsigned    open_flags;  /**< Flags used at mdb_dbi_open (includes MDB_CREATE). */
    unsigned    db_flags;    /**< Cached mdb_dbi_flags(txn, dbi). */
    unsigned    put_flags;   /**< Default flags to OR into mdb_put calls. */
    int         is_dupsort;  /**< Non-zero if DB uses MDB_DUPSORT. */
    int         is_dupfixed; /**< Non-zero if DB uses MDB_DUPFIXED. */
} dbi_desc_t;

/**
 * @brief Initialize all declared DBIs for the current DB.
 *
 * Opens each named DBI, caches flags, and records descriptors in the global DB.
 *
 * @param dbis    Array of declarations (names must outlive DB lifetime).
 * @param n_dbis  Number of declarations.
 * @return 0 on success, negative errno on failure.
 */
int db_lmdb_dbi_init(const dbi_decl_t* dbis, const size_t n_dbis);

#ifdef __cplusplus
}
#endif

#endif /* DB_LMDB_DBI_H */
