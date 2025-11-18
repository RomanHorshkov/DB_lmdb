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

/* propagate these includes up to operations */
#include <lmdb.h>   /* MDB_env, MDB_dbi etc */

#include "common.h" /* EMlog, config */
// for now present in emlog.h
// #include <stddef.h> /* size_t */
// #include <stdint.h> /* uintxx_t */

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
} dbi_t;

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
 * Intended for transient arrays passed for initialization .
 */
typedef struct
{
    const char* name;    /**< Logical name (non-owning). */
    dbi_type_t  type;    /**< Requested logical type (flags). */
    unsigned    dbi_idx; /**< Index in DataBase->dbis array. */
} dbi_init_t;

/****************************************************************************
 * PUBLIC FUNCTION PROTOTYPES
 ****************************************************************************
*/
/* None */

#ifdef __cplusplus
}
#endif

#endif /* DB_LMDB_DBI_H */
