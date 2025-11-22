/**
 * @file dbi_int.c
 * @brief Internal DBI helper functions.
 */

#include "dbi_int.h"

#include "lmdb.h"

unsigned int dbi_open_flags_from_type(dbi_type_t type)
{
    unsigned flags = MDB_CREATE;

    if(type & DBI_TYPE_DUPSORT) flags |= MDB_DUPSORT;
    if(type & DBI_TYPE_DUPFIXED) flags |= MDB_DUPFIXED;

    return flags;
}

unsigned int dbi_put_flags_from_type(dbi_type_t type)
{
    unsigned int flags = 0U;

    if(type & DBI_TYPE_NOOVERWRITE)
    {
        flags |= MDB_NOOVERWRITE;
    }

    if(type & DBI_TYPE_APPENDABLE)
    {
        /* Use append fast-path; for dupsort DBIs pick the duplicate-aware variant. */
        flags |= (type & DBI_TYPE_DUPSORT) ? MDB_APPENDDUP : MDB_APPEND;
    }

    return flags;
}
