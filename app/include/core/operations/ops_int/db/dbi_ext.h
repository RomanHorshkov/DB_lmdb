/**
 * @file dbi_ext.h
 */

#ifndef DB_LMDB_DBI_EXT_H
#define DB_LMDB_DBI_EXT_H

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
    DBI_TYPE_DEFAULT     = 0,      /**< default DBI */
    DBI_TYPE_NOOVERWRITE = 1 << 0, /**< disallow overwriting of existing keys */
    DBI_TYPE_APPENDABLE  = 1 << 1, /**< append-only DBI (new keys only, no updates) */
    DBI_TYPE_DUPSORT     = 1 << 5, /**< duplicate keys allowed, sorted */
    DBI_TYPE_DUPFIXED    = 1 << 6, /**< duplicate keys allowed, fixed-size values */

    // TO IMPLEMENT LATER:
    // DBI_TYPE_INTEGERKEY  = 1 << 2, /**< keys are binary integers in native byte order */
} dbi_type_t;

/****************************************************************************
 * PUBLIC FUNCTION PROTOTYPES
 ****************************************************************************
*/
/* None */

#ifdef __cplusplus
}
#endif

#endif /* DB_LMDB_DBI_EXT_H */
