



#include "db_lmdb.h"
#include "db_lmdb_config.h"

int db_lmdb_init(void)
{
    if(!DB)
    {
        EML_ERROR(LOG_TAG, "DB is not initialized");
        return -EIO;
    }

    int ret = mdb_env_create(&DB->env);
    if(ret != MDB_SUCCESS)
    {
        EML_ERROR(LOG_TAG, "mdb_env_create failed with err %d", ret);
        return ret;
    }

    ret = mdb_env_set_maxdbs(DB->env, DB_MAX_DBIS);
    if(ret != MDB_SUCCESS)
    {
        EML_ERROR(LOG_TAG, "mdb_env_set_maxdbs failed with err %d", ret);
        return ret;
    }

    ret = mdb_env_set_mapsize(DB->env, DB_MAP_SIZE_INIT);
    if(ret != MDB_SUCCESS)
    {
        EML_ERROR(LOG_TAG, "mdb_env_set_mapsize failed with err %d", ret);
        return ret;
    }

    ret = mdb_env_open(DB->env, DB_META_DIR, 0, 0770);
    if(ret != MDB_SUCCESS)
    {
        EML_ERROR(LOG_TAG, "mdb_env_open failed with err %d", ret);
        return ret;
    }

    DB->map_size_bytes     = DB_MAP_SIZE_INIT;
    DB->map_size_bytes_max = DB_MAP_SIZE_MAX;

    EML_INFO(LOG_TAG, "LMDB environment opened at %s", DB_META_DIR);
    EML_INFO(LOG_TAG, "LMDB map size: initial=%zu bytes, max=%zu bytes", DB->map_size_bytes,
             DB->map_size_bytes_max);

    return 0;
}
