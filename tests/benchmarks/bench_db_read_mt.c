/**
 * @file bench_db_read_mt.c
 * @brief Multithreaded batched GET benchmark.
 *
 * - Builds a single DBI populated with 1000 keys (512-byte values).
 * - Spawns multiple reader threads (clamped to a safe max) sharing the same env/DBI.
 * - Each thread reads all keys in batches of 8 using independent read-only txns,
 *   copies values to a private buffer, and reports its elapsed time.
 * - Reports per-run stats across threads and aggregated stats across runs.
 */

#include "core.h"
#include "bench_logging.h"
#include "bench_system_info.h"
#include "db.h"
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define BENCH_DB_PATH        "/tmp/bench_lmdb_read_mt"
#define BENCH_DB_MODE        0700
#define BENCH_NUM_USERS      1000
#define BENCH_VALUE_SIZE     512
#define BENCH_BATCH_SIZE     8
#define BENCH_RUNS           5
#define BENCH_MAX_THREADS    48

typedef struct
{
    double mean;
    double std_dev;
    double min;
    double max;
    double median;
} stats_t;

typedef struct
{
    MDB_env*      env;
    MDB_dbi       dbi;
    size_t        value_size;
    size_t        key_count;
    char (*keys)[32];
    double*       out_us;
    size_t        idx;
    int           rc;
} reader_args_t;

static char g_keys[BENCH_NUM_USERS][32];
static char g_value[BENCH_VALUE_SIZE];

static void get_system_info(sys_info_t* info)
{
    FILE* fp;
    char  buffer[256];

    gethostname(info->hostname, sizeof(info->hostname));

    fp = fopen("/proc/cpuinfo", "r");
    if(fp)
    {
        while(fgets(buffer, sizeof(buffer), fp))
        {
            if(strncmp(buffer, "model name", 10) == 0)
            {
                char* colon = strchr(buffer, ':');
                if(colon)
                {
                    colon += 2;
                    strncpy(info->cpu_model, colon, sizeof(info->cpu_model) - 1);
                    info->cpu_model[strcspn(info->cpu_model, "\n")] = 0;
                    break;
                }
            }
        }
        fclose(fp);
    }

    info->cpu_cores = sysconf(_SC_NPROCESSORS_ONLN);

    fp = fopen("/proc/cpuinfo", "r");
    if(fp)
    {
        while(fgets(buffer, sizeof(buffer), fp))
        {
            if(strncmp(buffer, "cpu MHz", 7) == 0)
            {
                char* colon = strchr(buffer, ':');
                if(colon)
                {
                    info->cpu_freq_mhz = (long)atof(colon + 1);
                    break;
                }
            }
        }
        fclose(fp);
    }

    struct sysinfo si;
    if(sysinfo(&si) == 0)
    {
        info->total_ram_mb = si.totalram / (1024 * 1024);
    }

    fp = fopen("/etc/os-release", "r");
    if(fp)
    {
        while(fgets(buffer, sizeof(buffer), fp))
        {
            if(strncmp(buffer, "PRETTY_NAME=", 12) == 0)
            {
                char* start = strchr(buffer, '"');
                if(start)
                {
                    start++;
                    char* end = strchr(start, '"');
                    if(end)
                    {
                        size_t len = (size_t)(end - start);
                        if(len < sizeof(info->os_info))
                        {
                            memcpy(info->os_info, start, len);
                            info->os_info[len] = '\0';
                        }
                    }
                }
                break;
            }
        }
        fclose(fp);
    }

    strcpy(info->storage_type, "Unknown");
    FILE* rota = popen("lsblk -o NAME,ROTA,MOUNTPOINT 2>/dev/null | grep '/tmp' | awk '{print $2}'",
                       "r");
    if(rota)
    {
        if(fgets(buffer, sizeof(buffer), rota))
        {
            int r = atoi(buffer);
            strcpy(info->storage_type, r == 0 ? "SSD" : "HDD");
        }
        pclose(rota);
    }

    struct statvfs vfs;
    if(statvfs("/tmp", &vfs) == 0)
    {
        FILE* fs = popen("df -T /tmp 2>/dev/null | tail -1 | awk '{print $2}'", "r");
        if(fs)
        {
            if(fgets(buffer, sizeof(buffer), fs))
            {
                buffer[strcspn(buffer, "\n")] = 0;
                strncpy(info->filesystem, buffer, sizeof(info->filesystem) - 1);
            }
            pclose(fs);
        }
    }
}

static double get_time_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1e6 + (double)tv.tv_usec;
}

static int remove_directory(const char* path)
{
    struct stat st;
    if(stat(path, &st) != 0)
    {
        return (errno == ENOENT) ? 0 : -1;
    }

    if(!S_ISDIR(st.st_mode))
    {
        return (unlink(path) == 0) ? 0 : -1;
    }

    DIR* dir = opendir(path);
    if(!dir) return -1;

    struct dirent* ent;
    int            rc = 0;
    while((ent = readdir(dir)) != NULL)
    {
        if(strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char child[PATH_MAX];
        snprintf(child, sizeof child, "%s/%s", path, ent->d_name);
        struct stat cst;
        if(stat(child, &cst) != 0)
        {
            rc = -1;
            continue;
        }
        if(S_ISDIR(cst.st_mode))
        {
            rc |= remove_directory(child);
        }
        else
        {
            if(unlink(child) != 0) rc = -1;
        }
    }
    closedir(dir);
    if(rmdir(path) != 0) rc = -1;
    return rc;
}

static void calculate_stats(double* samples, int n, stats_t* out)
{
    if(n <= 0 || !out) return;
    double sum = 0.0;
    for(int i = 0; i < n; ++i) sum += samples[i];
    out->mean = sum / (double)n;

    double sum_sq = 0.0;
    for(int i = 0; i < n; ++i)
    {
        double diff = samples[i] - out->mean;
        sum_sq += diff * diff;
    }
    out->std_dev = sqrt(sum_sq / (double)n);

    // samples expected sorted by caller
    out->min    = samples[0];
    out->max    = samples[n - 1];
    out->median = (n % 2 == 0) ? (samples[n / 2 - 1] + samples[n / 2]) / 2.0 : samples[n / 2];
}

static void init_data(void)
{
    for(int i = 0; i < BENCH_NUM_USERS; ++i)
    {
        snprintf(g_keys[i], sizeof(g_keys[i]), "user_%04d", i);
    }
    for(int i = 0; i < BENCH_VALUE_SIZE; ++i)
    {
        g_value[i] = (char)('A' + (i % 26));
    }
}

static int populate_db(void)
{
    static const char*      dbi_names[] = { "bench_users" };
    static const dbi_type_t dbi_types[] = { DBI_TYPE_DEFAULT };

    if(remove_directory(BENCH_DB_PATH) != 0)
    {
        fprintf(stderr, "WARNING: failed to clean %s\n", BENCH_DB_PATH);
    }

    int rc = db_core_init(BENCH_DB_PATH, BENCH_DB_MODE, dbi_names, dbi_types, 1u);
    if(rc != 0)
    {
        fprintf(stderr, "ERROR: db_core_init failed rc=%d\n", rc);
        return rc;
    }

    int pending = 0;
    for(int i = 0; i < BENCH_NUM_USERS; ++i)
    {
        rc = db_core_add_op(0u, DB_OPERATION_PUT, g_keys[i], strlen(g_keys[i]),
                            g_value, BENCH_VALUE_SIZE);
        if(rc != 0)
        {
            fprintf(stderr, "ERROR: db_core_add_op failed at i=%d rc=%d\n", i, rc);
            return rc;
        }
        ++pending;
        if(pending == BENCH_BATCH_SIZE)
        {
            rc      = db_core_exec_ops();
            pending = 0;
            if(rc != 0)
            {
                fprintf(stderr, "ERROR: db_core_exec_ops failed rc=%d\n", rc);
                return rc;
            }
        }
    }
    if(pending > 0)
    {
        rc = db_core_exec_ops();
        if(rc != 0)
        {
            fprintf(stderr, "ERROR: db_core_exec_ops final flush failed rc=%d\n", rc);
            return rc;
        }
    }
    return 0;
}

static void* reader_thread(void* arg)
{
    reader_args_t* a = (reader_args_t*)arg;
    MDB_txn*       txn;
    int            rc = mdb_txn_begin(a->env, NULL, MDB_RDONLY, &txn);
    if(rc != MDB_SUCCESS)
    {
        a->rc = rc;
        return NULL;
    }

    char  buf[BENCH_VALUE_SIZE];
    MDB_val key, val;

    double start = get_time_us();
    for(size_t i = 0; i < a->key_count; i += BENCH_BATCH_SIZE)
    {
        size_t batch = (i + BENCH_BATCH_SIZE <= a->key_count) ? BENCH_BATCH_SIZE
                                                              : (a->key_count - i);
        for(size_t j = 0; j < batch; ++j)
        {
            key.mv_size = strlen(a->keys[i + j]);
            key.mv_data = a->keys[i + j];

            rc = mdb_get(txn, a->dbi, &key, &val);
            if(rc != MDB_SUCCESS)
            {
                a->rc = rc;
                goto out;
            }
            if(val.mv_size > a->value_size)
            {
                a->rc = -EOVERFLOW;
                goto out;
            }
            memcpy(buf, val.mv_data, val.mv_size);
        }
    }
out:
    mdb_txn_abort(txn);
    double end = get_time_us();
    *(a->out_us) = end - start;
    return NULL;
}

static int run_benchmark(int thread_count)
{
    double   run_means[BENCH_RUNS];
    double   run_throughput[BENCH_RUNS];
    sys_info_t sys_info = {0};
    get_system_info(&sys_info);

    printf("=== Multithreaded GET Benchmark ===\n");
    bench_print_system_info(stdout, &sys_info);
    printf("CONFIG:\n");
    printf("DB Path:        %s\n", BENCH_DB_PATH);
    printf("DB Mode:        0%o\n", BENCH_DB_MODE);
    printf("Users:          %d\n", BENCH_NUM_USERS);
    printf("Value size:     %d bytes\n", BENCH_VALUE_SIZE);
    printf("Batch size:     %d\n", BENCH_BATCH_SIZE);
    printf("Threads:        %d\n", thread_count);
    printf("Runs:           %d\n\n", BENCH_RUNS);

    for(int run = 0; run < BENCH_RUNS; ++run)
    {
        double thread_us[BENCH_MAX_THREADS] = {0};
        pthread_t threads[BENCH_MAX_THREADS];
        reader_args_t args[BENCH_MAX_THREADS];

        for(int t = 0; t < thread_count; ++t)
        {
            args[t].env        = DataBase->env;
            args[t].dbi        = DataBase->dbis[0].dbi;
            args[t].value_size = BENCH_VALUE_SIZE;
            args[t].key_count  = BENCH_NUM_USERS;
            args[t].keys       = g_keys;
            args[t].out_us     = &thread_us[t];
            args[t].idx        = (size_t)t;
            args[t].rc         = 0;
            int rc = pthread_create(&threads[t], NULL, reader_thread, &args[t]);
            if(rc != 0)
            {
                fprintf(stderr, "ERROR: pthread_create failed rc=%d\n", rc);
                return rc;
            }
        }

        int rc_threads = 0;
        for(int t = 0; t < thread_count; ++t)
        {
            pthread_join(threads[t], NULL);
            if(args[t].rc != 0)
            {
                fprintf(stderr, "ERROR: reader thread %d failed rc=%d\n", t, args[t].rc);
                rc_threads = args[t].rc;
            }
        }
        if(rc_threads != 0) return rc_threads;

        // sort copies for stats
        double sorted[BENCH_MAX_THREADS];
        memcpy(sorted, thread_us, sizeof(double) * (size_t)thread_count);
        for(int i = 0; i < thread_count - 1; ++i)
        {
            for(int j = i + 1; j < thread_count; ++j)
            {
                if(sorted[j] < sorted[i])
                {
                    double tmp = sorted[i];
                    sorted[i]  = sorted[j];
                    sorted[j]  = tmp;
                }
            }
        }

        stats_t st;
        calculate_stats(sorted, thread_count, &st);
        double total_reads = (double)thread_count * (double)BENCH_NUM_USERS;
        double mean_sec    = st.mean / 1e6;
        double rps         = total_reads / mean_sec;

        run_means[run]      = st.mean;
        run_throughput[run] = rps;

        double per_read_us = st.mean / (double)BENCH_NUM_USERS;
        printf("Run %d/%d: mean thread time = %.3f ms (min=%.3f, max=%.3f), "
               "per-read ≈ %.3f μs, throughput ≈ %.0f reads/s\n",
               run + 1, BENCH_RUNS, st.mean / 1000.0, st.min / 1000.0, st.max / 1000.0,
               per_read_us, rps);
    }

    // aggregate over runs
    double sorted_means[BENCH_RUNS];
    memcpy(sorted_means, run_means, sizeof run_means);
    for(int i = 0; i < BENCH_RUNS - 1; ++i)
    {
        for(int j = i + 1; j < BENCH_RUNS; ++j)
        {
            if(sorted_means[j] < sorted_means[i])
            {
                double tmp      = sorted_means[i];
                sorted_means[i] = sorted_means[j];
                sorted_means[j] = tmp;
            }
        }
    }
    stats_t st_runs;
    calculate_stats(sorted_means, BENCH_RUNS, &st_runs);

    double sorted_rps[BENCH_RUNS];
    memcpy(sorted_rps, run_throughput, sizeof run_throughput);
    for(int i = 0; i < BENCH_RUNS - 1; ++i)
    {
        for(int j = i + 1; j < BENCH_RUNS; ++j)
        {
            if(sorted_rps[j] < sorted_rps[i])
            {
                double tmp   = sorted_rps[i];
                sorted_rps[i] = sorted_rps[j];
                sorted_rps[j] = tmp;
            }
        }
    }
    stats_t st_rps;
    calculate_stats(sorted_rps, BENCH_RUNS, &st_rps);

    printf("\nSummary across %d runs:\n", BENCH_RUNS);
    printf("Mean thread time: %.3f ms (std=%.3f, min=%.3f, max=%.3f)\n",
           st_runs.mean / 1000.0, st_runs.std_dev / 1000.0, st_runs.min / 1000.0,
           st_runs.max / 1000.0);
    printf("Per-read (mean thread): %.3f μs (std=%.3f, min=%.3f, max=%.3f)\n",
           st_runs.mean / (double)BENCH_NUM_USERS, st_runs.std_dev / (double)BENCH_NUM_USERS,
           st_runs.min / (double)BENCH_NUM_USERS, st_runs.max / (double)BENCH_NUM_USERS);
    printf("Throughput:       %.0f reads/s (std=%.0f, min=%.0f, max=%.0f)\n",
           st_rps.mean, st_rps.std_dev, st_rps.min, st_rps.max);
    printf("\n");
    return 0;
}

int main(void)
{
    bench_silence_emlog();
    init_data();

    int rc = populate_db();
    if(rc != 0)
    {
        fprintf(stderr, "ERROR: populate_db failed rc=%d\n", rc);
        return 1;
    }

    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    int  threads =
        (cores <= 0) ? 4 : (int)((cores < BENCH_MAX_THREADS) ? cores : BENCH_MAX_THREADS);
    if(threads < 2) threads = 2;

    rc = run_benchmark(threads);
    (void)db_core_shutdown();
    return rc == 0 ? 0 : 1;
}
