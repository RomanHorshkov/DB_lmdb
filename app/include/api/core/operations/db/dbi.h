/**
 * @file dbi.h
 * @brief Thin descriptor for LMDB named sub-databases.
 *
 * The types and helpers in this header provide a small, efficient
 * abstraction over LMDB named databases (DBIs). They cache the LMDB
 * handle and commonly-used flags so hot-path code can avoid repeated
 * flag queries and type checks.
 */

#ifndef DB_LMDB_DBI_H
#define DB_LMDB_DBI_H

#include <lmdb.h>   /* propagate up to operations */
#include <stddef.h> /* size_t */
#include <stdint.h> /* uintxx_t */

#include "common.h" /* EMlog, config */

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
 * @brief Cached, persistent descriptor for an opened named DBI.
 *
 * The runtime stores an array of these descriptors for quick access to the
 * MDB_dbi handle and precomputed flags used by higher-level operations.
 */
typedef struct
{
    unsigned int dbi;         /**< LMDB handle. */
    unsigned     db_flags;    /**< Cached mdb_dbi_flags(txn, dbi). */
    unsigned     put_flags;   /**< Default flags to OR into mdb_put calls. */
    unsigned     is_dupsort;  /**< Non-zero if DB uses MDB_DUPSORT. */
    unsigned     is_dupfixed; /**< Non-zero if DB uses MDB_DUPFIXED. */
    // const char*  name;        /**< Logical name (non-owning). */
    // dbi_type_t type;        /**< Requested logical type (flags). */
    // unsigned   open_flags;  /**< Flags used at mdb_dbi_open (includes MDB_CREATE). */
} dbi_desc_t;

/**
 * @brief Declaration for a named LMDB database (non-owning strings).
 *
 * Intended for transient arrays passed to db_lmdb_init/db_lmdb_dbi_init.
 */
typedef struct
{
    const char* name;
    dbi_type_t  type;
    unsigned    dbi_idx;
} dbi_decl_t;

/****************************************************************************
 * PUBLIC FUNCTION PROTOTYPES
 ****************************************************************************
*/

/**
 * @brief Initialize all declared DBIs for the current DB.
 *
 * Opens each named DBI, caches flags, and records descriptors in the global DB.
 *
 * @param dbis    Array of declarations (names must outlive DB lifetime).
 * @param n_dbis  Number of declarations.
 * @return 0 on success, negative errno on failure.
 */
int db_lmdb_dbi_init(const dbi_decl_t* dbis, const uint8_t n_dbis);

/**
 * @brief Compute default put flags from DB flags.
 * 
 * @param db_flags DB flags as returned by mdb_dbi_flags().
 * @return Computed default put flags.
 */
inline unsigned dbi_desc_default_put_flags(unsigned db_flags)
{
    return (db_flags & MDB_DUPSORT) ? MDB_NODUPDATA : MDB_NOOVERWRITE;
}

/**
 * Compute mdb_dbi_open() flags from a dbi_type_t bitmask.
 *
 * This helper builds the set of flags to pass to mdb_dbi_open() by starting
 * with MDB_CREATE (so the DBI is created if it does not exist) and then
 * enabling additional LMDB behaviors based on the supplied dbi type bits:
 * - DBI_TYPE_DUPSORT  -> adds MDB_DUPSORT  (allows multiple values per key, sorted)
 * - DBI_TYPE_DUPFIXED -> adds MDB_DUPFIXED (duplicates are fixed-size values)
 *
 * Any dbi_type_t bits that do not map to a known LMDB flag are ignored.
 *
 * @param type Bitmask of DBI type flags (e.g. DBI_TYPE_DUPSORT, DBI_TYPE_DUPFIXED).
 * @return Unsigned flags suitable for passing to mdb_dbi_open().
 */
/**
 * @brief Compute mdb_dbi_open flags from requested dbi_type_t.
 * 
 * @param type Requested DBI type.
 * @return Computed mdb_dbi_open flags.
 */
inline unsigned dbi_open_flags_from_type(dbi_type_t type)
{
    /* basic mdb create flag */
    unsigned flags = MDB_CREATE;
    if(type & DBI_TYPE_DUPSORT) flags |= MDB_DUPSORT;
    if(type & DBI_TYPE_DUPFIXED) flags |= MDB_DUPFIXED;
    return flags;
}

#ifdef __cplusplus
}
#endif

#endif /* DB_LMDB_DBI_H */
