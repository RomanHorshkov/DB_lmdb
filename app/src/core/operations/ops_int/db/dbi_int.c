#include "dbi_int.h"

#include "lmdb.h"

unsigned dbi_open_flags_from_type(dbi_type_t type)
{
    unsigned flags = MDB_CREATE;

    if(type & DBI_TYPE_DUPSORT) flags |= MDB_DUPSORT;
    if(type & DBI_TYPE_DUPFIXED) flags |= MDB_DUPFIXED;

    return flags;
}

unsigned dbi_desc_default_put_flags(unsigned db_flags)
{
    (void)db_flags;
    /* For now use no extra put flags; callers may refine this later. */
    return 0U;
}

