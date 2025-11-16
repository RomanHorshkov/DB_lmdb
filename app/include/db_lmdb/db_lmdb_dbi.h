/**
 * @file db_lmdb_dbi.h
 * @brief Thin descriptor for LMDB named sub-databases.
 *
 * Stores the DBI handle plus cached flags so callers do not need to
 * re-query lmdb_dbi_flags() in hot paths.
 */

#ifndef DB_LMDB_DBI_H
#define DB_LMDB_DBI_H

#include <lmdb.h>

#include "db_lmdb.h" /* dbi_t / dbi_type_t */

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Sentinel to request automatic default put flags selection.
 *
 * - Non-DUPSORT: MDB_NOOVERWRITE (unique keys).
 * - DUPSORT:     MDB_NODUPDATA   (unique duplicates).
 */
#define DBI_PUT_FLAGS_AUTO 0xFFFFFFFFu

/**
 * @brief Cached descriptor for one LMDB named database.
 */
typedef struct
{
    MDB_dbi   dbi;               /**< LMDB handle. */
    const char* name;            /**< Logical name (non-owning). */
    unsigned  open_flags;        /**< Flags used at mdb_dbi_open (includes MDB_CREATE). */
    unsigned  db_flags;          /**< Cached mdb_dbi_flags(txn, dbi). */
    unsigned  put_flags_default; /**< Default flags to OR into mdb_put calls. */
    int       is_dupsort;        /**< Convenience: db_flags & MDB_DUPSORT. */
    int       is_dupfixed;       /**< Convenience: db_flags & MDB_DUPFIXED. */
} dbi_desc_t;

/**
 * @brief Compute LMDB open flags from dbi_type_t, always including MDB_CREATE.
 */
static inline unsigned dbi_desc_open_flags(dbi_type_t type)
{
    unsigned flags = MDB_CREATE;
    if(type & DBI_TYPE_DUPSORT) flags |= MDB_DUPSORT;
    if(type & DBI_TYPE_DUPFIXED) flags |= MDB_DUPFIXED;
    return flags;
}

/**
 * @brief Pick safe default put flags based on cached DB flags.
 *
 * Non-DUPSORT -> MDB_NOOVERWRITE to enforce unique keys.
 * DUPSORT     -> MDB_NODUPDATA to enforce unique duplicates.
 */
static inline unsigned dbi_desc_default_put_flags(unsigned db_flags)
{
    return (db_flags & MDB_DUPSORT) ? MDB_NODUPDATA : MDB_NOOVERWRITE;
}

/**
 * @brief Open a named DBI and populate a descriptor with cached flags.
 *
 * @param txn                 Active transaction (write).
 * @param name                Database name (must outlive descriptor).
 * @param open_flags          Flags for mdb_dbi_open (should include MDB_CREATE).
 * @param put_flags_default   Default put flags; use DBI_PUT_FLAGS_AUTO to derive.
 * @param out                 Destination descriptor.
 * @return 0 on success, negative errno on failure.
 */
int dbi_desc_init(MDB_txn* txn, const char* name, unsigned open_flags,
                  unsigned put_flags_default, dbi_desc_t* out);

#ifdef __cplusplus
}
#endif

#endif /* DB_LMDB_DBI_H */
