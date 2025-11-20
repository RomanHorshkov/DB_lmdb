/**
 * @file dbi_int.h
 *
 */

#ifndef DB_LMDB_DBI_INT_H
#define DB_LMDB_DBI_INT_H

#include "dbi_ext.h" /* dbi_type_t and other external DBI types */

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

/****************************************************************************
 * PUBLIC FUNCTION PROTOTYPES
 ****************************************************************************
*/
unsigned dbi_open_flags_from_type(dbi_type_t type);
unsigned dbi_desc_default_put_flags(unsigned db_flags);

#ifdef __cplusplus
}
#endif

#endif /* DB_LMDB_DBI_INT_H */
