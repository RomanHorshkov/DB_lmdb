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
    DBI_TYPE_DEFAULT     = 0,      /* no special flags */
    DBI_TYPE_NOOVERWRITE = 1 << 0, /* disallow overwrites */
    DBI_TYPE_DUPSORT     = 1 << 1, /* sorted duplicate keys */
    DBI_TYPE_DUPFIXED    = 1 << 2  /* fixed-size duplicate keys */
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
