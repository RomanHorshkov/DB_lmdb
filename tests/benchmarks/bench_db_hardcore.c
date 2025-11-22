/**
 * @file bench_db_hardcore.c
 * @brief HARDCORE benchmark: 10M batched inserts + 100k random GETs.
 *
 * Measures total time to:
 *  - Insert 10,000,000 users into a single appendable DBI (batches of 8)
 *  - Fetch 100,000 random users from that DBI (batches of 8)
 *
 * Minimal console noise; detailed results are written to
 * tests/benchmarks/results/bench_hardcore.txt.
 */

#include "db_lmdb_core.h"
#include "bench_logging.h"
#include "bench_system_info.h"
#include <dirent.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define BENCH_NAME         "HARDCORE bench"
#define BENCH_DB_PATH      "/tmp/bench_lmdb_hardcore"
#define BENCH_DB_MODE      0700
#define BENCH_DBI_NAME     "bench_hardcore_users"
#define BENCH_NUM_USERS    10000000u
#define BENCH_NUM_GETS     100000u
#define BENCH_BATCH_SIZE   8
#define BENCH_VALUE_SIZE   1
#define BENCH_RANDOM_SEED  424242u
#define BENCH_RESULTS_FILE "tests/benchmarks/results/bench_hardcore.txt"
#define KEY_BUFFER_SIZE    32

typedef struct
{
    size_t count;
    double mean;
    double m2;
    double min;
    double max;
} running_stats_t;

typedef struct
{
    double          total_us;
    size_t          ops;
    size_t          batches;
    running_stats_t batch_exec;
} phase_result_t;

static double get_time_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1e6 + (double)tv.tv_usec;
}

static void running_stats_reset(running_stats_t* stats)
{
    if(!stats) return;
    stats->count = 0u;
    stats->mean  = 0.0;
    stats->m2    = 0.0;
    stats->min   = DBL_MAX;
    stats->max   = 0.0;
}

static void running_stats_update(running_stats_t* stats, double sample)
{
    if(!stats) return;
    stats->count++;
    if(sample < stats->min) stats->min = sample;
    if(sample > stats->max) stats->max = sample;
    double delta = sample - stats->mean;
    stats->mean += delta / (double)stats->count;
    stats->m2 += delta * (sample - stats->mean);
}

static double running_stats_stddev(const running_stats_t* stats)
{
    if(!stats || stats->count < 2u) return 0.0;
    return sqrt(stats->m2 / (double)(stats->count - 1u));
}

static size_t format_user_key(uint32_t idx, char* buf, size_t buf_size)
{
    int written = snprintf(buf, buf_size, "user%08u", idx);
    if(written < 0 || (size_t)written >= buf_size) return 0u;
    return (size_t)written;
}

/**
 * @brief Get system information
 */
static void get_system_info(sys_info_t* info)
{
    FILE* fp;
    char  buffer[256];

    /* Hostname */
    gethostname(info->hostname, sizeof(info->hostname));

    /* CPU model */
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
                    colon += 2; /* Skip ": " */
                    strncpy(info->cpu_model, colon, sizeof(info->cpu_model) - 1);
                    info->cpu_model[strcspn(info->cpu_model, "\n")] = 0;
                    break;
                }
            }
        }
        fclose(fp);
    }

    /* CPU cores */
    info->cpu_cores = sysconf(_SC_NPROCESSORS_ONLN);

    /* CPU frequency (from /proc/cpuinfo) */
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

    /* Total RAM */
    struct sysinfo si;
    if(sysinfo(&si) == 0)
    {
        info->total_ram_mb = si.totalram / (1024 * 1024);
    }

    /* OS information */
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

    /* Storage type - detect SSD vs HDD */
    /* Check if /tmp is on SSD by looking at rotational flag */
    strcpy(info->storage_type, "Unknown");
    fp = popen("lsblk -o NAME,ROTA,MOUNTPOINT 2>/dev/null | grep '/tmp' | awk '{print $2}'", "r");
    if(fp)
    {
        if(fgets(buffer, sizeof(buffer), fp))
        {
            int rota = atoi(buffer);
            strcpy(info->storage_type, (rota == 0) ? "SSD" : "HDD");
        }
        pclose(fp);
    }

    /* If /tmp not in lsblk output, check root */
    if(strcmp(info->storage_type, "Unknown") == 0)
    {
        fp = popen("lsblk -o NAME,ROTA,MOUNTPOINT 2>/dev/null | grep ' /$' | awk '{print $2}'", "r");
        if(fp)
        {
            if(fgets(buffer, sizeof(buffer), fp))
            {
                int rota = atoi(buffer);
                strcpy(info->storage_type, (rota == 0) ? "SSD" : "HDD");
            }
            pclose(fp);
        }
    }

    /* Filesystem type */
    struct statvfs vfs;
    if(statvfs("/tmp", &vfs) == 0)
    {
        fp = popen("df -T /tmp 2>/dev/null | tail -1 | awk '{print $2}'", "r");
        if(fp)
        {
            if(fgets(buffer, sizeof(buffer), fp))
            {
                buffer[strcspn(buffer, "\n")] = 0;
                strncpy(info->filesystem, buffer, sizeof(info->filesystem) - 1);
            }
            pclose(fp);
        }
    }
}

/**
 * @brief Recursively remove a directory tree.
 *
 * This is a simple, benchmark-oriented equivalent of `rm -rf path`.
 * Best-effort: on failure it returns -1 but continues as far as possible.
 */
static int remove_directory(const char* path)
{
    struct stat st;

    if(stat(path, &st) != 0)
    {
        /* Treat non-existent path as success. */
        return (errno == ENOENT) ? 0 : -1;
    }

    if(!S_ISDIR(st.st_mode))
    {
        return (unlink(path) == 0) ? 0 : -1;
    }

    DIR* dir = opendir(path);
    if(!dir)
    {
        return -1;
    }

    struct dirent* entry;
    while((entry = readdir(dir)) != NULL)
    {
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }

        char child_path[PATH_MAX];
        snprintf(child_path, sizeof(child_path), "%s/%s", path, entry->d_name);

        if(remove_directory(child_path) != 0)
        {
            /* Continue trying to clean up other entries. */
        }
    }

    closedir(dir);
    return (rmdir(path) == 0) ? 0 : -1;
}

static void fill_random_indices(uint32_t* indices, size_t n, uint32_t max_value)
{
    if(!indices || n == 0u || max_value == 0u) return;

    unsigned int seed = BENCH_RANDOM_SEED;
    for(size_t i = 0; i < n; ++i)
    {
        indices[i] = rand_r(&seed) % (max_value + 1u);
    }
}

static int insert_users(phase_result_t* result)
{
    if(!result) return -EINVAL;

    char               key_buf[BENCH_BATCH_SIZE][KEY_BUFFER_SIZE];
    const unsigned char value_buf[BENCH_VALUE_SIZE] = { 0 };
    int                pending                     = 0;
    int                rc                          = 0;

    running_stats_reset(&result->batch_exec);
    result->ops     = BENCH_NUM_USERS;
    result->batches = 0u;

    double start_us = get_time_us();

    for(uint32_t i = 0u; i < BENCH_NUM_USERS; ++i)
    {
        size_t key_len = format_user_key(i, key_buf[pending], sizeof(key_buf[pending]));
        if(key_len == 0u)
        {
            fprintf(stderr, "ERROR: Key formatting failed for user %u\n", i);
            return -EINVAL;
        }

        rc = db_core_set_op(
            0u,
            DB_OPERATION_PUT,
            &(op_key_t){ .kind = OP_KEY_KIND_PRESENT,
                         .present = { .data = (void*)key_buf[pending], .size = key_len } },
            &(op_key_t){ .kind = OP_KEY_KIND_PRESENT,
                         .present = { .data = (void*)value_buf, .size = BENCH_VALUE_SIZE } });
        if(rc != 0)
        {
            fprintf(stderr, "ERROR: db_core_set_op failed during insert (user=%u, rc=%d)\n", i,
                    rc);
            return rc;
        }

        pending++;

        if(pending == BENCH_BATCH_SIZE)
        {
            double exec_start = get_time_us();
            rc                = db_core_exec_ops();
            double exec_end   = get_time_us();
            running_stats_update(&result->batch_exec, exec_end - exec_start);
            result->batches++;

            if(rc != 0)
            {
                fprintf(stderr,
                        "ERROR: db_core_exec_ops failed during insert batch (batch=%zu, rc=%d)\n",
                        result->batches, rc);
                return rc;
            }
            pending = 0;
        }
    }

    if(pending > 0)
    {
        double exec_start = get_time_us();
        rc                = db_core_exec_ops();
        double exec_end   = get_time_us();
        running_stats_update(&result->batch_exec, exec_end - exec_start);
        result->batches++;

        if(rc != 0)
        {
            fprintf(stderr,
                    "ERROR: db_core_exec_ops failed during final insert flush (pending=%d, rc=%d)\n",
                    pending, rc);
            return rc;
        }
    }

    result->total_us = get_time_us() - start_us;
    return 0;
}

static int perform_random_reads(const uint32_t* indices, phase_result_t* result)
{
    if(!indices || !result) return -EINVAL;

    char               key_buf[BENCH_BATCH_SIZE][KEY_BUFFER_SIZE];
    unsigned char      value_buf[BENCH_BATCH_SIZE][BENCH_VALUE_SIZE];
    int                pending = 0;
    int                rc      = 0;

    running_stats_reset(&result->batch_exec);
    result->ops     = BENCH_NUM_GETS;
    result->batches = 0u;

    double start_us = get_time_us();

    for(size_t i = 0u; i < BENCH_NUM_GETS; ++i)
    {
        uint32_t idx = indices[i];

        size_t key_len = format_user_key(idx, key_buf[pending], sizeof(key_buf[pending]));
        if(key_len == 0u)
        {
            fprintf(stderr, "ERROR: Key formatting failed during GET (idx=%u)\n", idx);
            return -EINVAL;
        }

        memset(value_buf[pending], 0, sizeof(value_buf[pending]));

        rc = db_core_set_op(
            0u,
            DB_OPERATION_GET,
            &(op_key_t){ .kind = OP_KEY_KIND_PRESENT,
                         .present = { .data = (void*)key_buf[pending], .size = key_len } },
            &(op_key_t){ .kind = OP_KEY_KIND_PRESENT,
                         .present = { .data = (void*)value_buf[pending],
                                      .size = BENCH_VALUE_SIZE } });
        if(rc != 0)
        {
            fprintf(stderr, "ERROR: db_core_set_op failed during GET (idx=%zu, rc=%d)\n", i, rc);
            return rc;
        }

        pending++;

        if(pending == BENCH_BATCH_SIZE)
        {
            double exec_start = get_time_us();
            rc                = db_core_exec_ops();
            double exec_end   = get_time_us();
            running_stats_update(&result->batch_exec, exec_end - exec_start);
            result->batches++;

            if(rc != 0)
            {
                fprintf(stderr,
                        "ERROR: db_core_exec_ops failed during GET batch (batch=%zu, rc=%d)\n",
                        result->batches, rc);
                return rc;
            }
            pending = 0;
        }
    }

    if(pending > 0)
    {
        double exec_start = get_time_us();
        rc                = db_core_exec_ops();
        double exec_end   = get_time_us();
        running_stats_update(&result->batch_exec, exec_end - exec_start);
        result->batches++;

        if(rc != 0)
        {
            fprintf(stderr,
                    "ERROR: db_core_exec_ops failed during final GET flush (pending=%d, rc=%d)\n",
                    pending, rc);
            return rc;
        }
    }

    result->total_us = get_time_us() - start_us;
    return 0;
}

static double ops_per_second(const phase_result_t* res)
{
    if(!res || res->total_us <= 0.0) return 0.0;
    return (double)res->ops / (res->total_us / 1e6);
}

static double batches_per_second(const phase_result_t* res)
{
    if(!res || res->total_us <= 0.0) return 0.0;
    return (double)res->batches / (res->total_us / 1e6);
}

static void write_results_file(const sys_info_t*      sys_info,
                               const phase_result_t*  write_res,
                               const phase_result_t*  read_res,
                               size_t                 map_size_bytes)
{
    FILE* fp = fopen(BENCH_RESULTS_FILE, "w");
    if(!fp)
    {
        fprintf(stderr, "ERROR: Failed to open %s for writing: %s\n", BENCH_RESULTS_FILE,
                strerror(errno));
        return;
    }

    fprintf(fp, "==================== %s ====================\n", BENCH_NAME);
    bench_print_system_info(fp, sys_info);

    fprintf(fp, "CONFIGURATION:\n");
    fprintf(fp, "  DB Path:            %s\n", BENCH_DB_PATH);
    fprintf(fp, "  DBI Name:           %s\n", BENCH_DBI_NAME);
    fprintf(fp, "  DB Mode:            0%o\n", BENCH_DB_MODE);
    fprintf(fp, "  Inserts:            %u users\n", BENCH_NUM_USERS);
    fprintf(fp, "  Random GETs:        %u lookups\n", BENCH_NUM_GETS);
    fprintf(fp, "  Batch Size:         %d\n", BENCH_BATCH_SIZE);
    fprintf(fp, "  Value Size:         %d bytes\n", BENCH_VALUE_SIZE);
    fprintf(fp, "  DBI Type:           appendable (strictly increasing keys)\n");
    fprintf(fp, "  Random Seed:        %u\n\n", BENCH_RANDOM_SEED);

    fprintf(fp, "WRITE PHASE (10,000,000 inserts)\n");
    fprintf(fp, "---------------------------------\n");
    fprintf(fp, "  Total time:         %.3f s (%.0f ms)\n", write_res->total_us / 1e6,
            write_res->total_us / 1000.0);
    fprintf(fp, "  Ops/sec:            %.2f\n", ops_per_second(write_res));
    fprintf(fp, "  Avg per insert:     %.2f us\n", write_res->total_us / (double)write_res->ops);
    fprintf(fp, "  Batches:            %zu\n", write_res->batches);
    fprintf(fp, "  Avg per batch:      %.2f us\n",
            write_res->total_us / (double)write_res->batches);
    fprintf(fp, "  Exec batch mean:    %.2f us\n", write_res->batch_exec.mean);
    fprintf(fp, "  Exec batch stddev:  %.2f us\n", running_stats_stddev(&write_res->batch_exec));
    fprintf(fp, "  Exec batch min/max: %.2f / %.2f us\n\n", write_res->batch_exec.min,
            write_res->batch_exec.max);

    fprintf(fp, "READ PHASE (100,000 random gets)\n");
    fprintf(fp, "--------------------------------\n");
    fprintf(fp, "  Total time:         %.3f s (%.0f ms)\n", read_res->total_us / 1e6,
            read_res->total_us / 1000.0);
    fprintf(fp, "  Ops/sec:            %.2f\n", ops_per_second(read_res));
    fprintf(fp, "  Avg per get:        %.2f us\n", read_res->total_us / (double)read_res->ops);
    fprintf(fp, "  Batches:            %zu\n", read_res->batches);
    fprintf(fp, "  Avg per batch:      %.2f us\n",
            read_res->total_us / (double)read_res->batches);
    fprintf(fp, "  Exec batch mean:    %.2f us\n", read_res->batch_exec.mean);
    fprintf(fp, "  Exec batch stddev:  %.2f us\n", running_stats_stddev(&read_res->batch_exec));
    fprintf(fp, "  Exec batch min/max: %.2f / %.2f us\n\n", read_res->batch_exec.min,
            read_res->batch_exec.max);

    fprintf(fp, "FINAL STATE\n");
    fprintf(fp, "-----------\n");
    fprintf(fp, "  LMDB map size:      %.2f MiB\n", map_size_bytes / (1024.0 * 1024.0));

    fclose(fp);
}

static void print_console_summary(const phase_result_t* write_res,
                                  const phase_result_t* read_res,
                                  size_t                map_size_bytes)
{
    printf("=== %s ===\n", BENCH_NAME);
    printf("Write:  %.2f s total | %.2f M ops/s | %.2f us/op | batches: %zu (%.2f us/batch)\n",
           write_res->total_us / 1e6, ops_per_second(write_res) / 1e6,
           write_res->total_us / (double)write_res->ops, write_res->batches,
           write_res->total_us / (double)write_res->batches);
    printf("        exec batch mean=%.2f us std=%.2f us min=%.2f max=%.2f\n",
           write_res->batch_exec.mean, running_stats_stddev(&write_res->batch_exec),
           write_res->batch_exec.min, write_res->batch_exec.max);

    printf("Read:   %.2f s total | %.2f K ops/s | %.2f us/op | batches: %zu (%.2f us/batch)\n",
           read_res->total_us / 1e6, ops_per_second(read_res) / 1e3,
           read_res->total_us / (double)read_res->ops, read_res->batches,
           read_res->total_us / (double)read_res->batches);
    printf("        exec batch mean=%.2f us std=%.2f us min=%.2f max=%.2f\n",
           read_res->batch_exec.mean, running_stats_stddev(&read_res->batch_exec),
           read_res->batch_exec.min, read_res->batch_exec.max);

    printf("Final LMDB map size: %.2f MiB\n", map_size_bytes / (1024.0 * 1024.0));
    printf("Detailed results written to %s\n", BENCH_RESULTS_FILE);
}

int main(void)
{
    const char*      dbi_names[] = { BENCH_DBI_NAME };
    const dbi_type_t dbi_types[] = { DBI_TYPE_APPENDABLE };

    sys_info_t     sys_info   = { 0 };
    phase_result_t write_res  = { 0 };
    phase_result_t read_res   = { 0 };
    uint32_t*      rand_idxs  = NULL;
    size_t         map_size   = 0u;
    int            rc         = 0;

    bench_silence_emlog();

    /* Ensure results directory exists. */
    struct stat st;
    if(stat("tests/benchmarks/results", &st) != 0)
    {
        if(errno != ENOENT || mkdir("tests/benchmarks/results", 0755) != 0)
        {
            perror("mkdir tests/benchmarks/results");
            return 1;
        }
    }
    else if(!S_ISDIR(st.st_mode))
    {
        fprintf(stderr, "ERROR: tests/benchmarks/results exists and is not a directory\n");
        return 1;
    }

    get_system_info(&sys_info);

    /* Start from a clean slate. */
    if(remove_directory(BENCH_DB_PATH) != 0)
    {
        fprintf(stderr, "WARNING: Failed to remove existing path %s\n", BENCH_DB_PATH);
    }

    rand_idxs = malloc(BENCH_NUM_GETS * sizeof(uint32_t));
    if(!rand_idxs)
    {
        fprintf(stderr, "ERROR: Failed to allocate random index buffer\n");
        return 1;
    }
    fill_random_indices(rand_idxs, BENCH_NUM_GETS, BENCH_NUM_USERS - 1u);

    rc = db_core_init(BENCH_DB_PATH, BENCH_DB_MODE, dbi_names, dbi_types, 1u);
    if(rc != 0)
    {
        fprintf(stderr, "ERROR: db_core_init failed with rc=%d\n", rc);
        free(rand_idxs);
        return 1;
    }

    rc = insert_users(&write_res);
    if(rc != 0) goto shutdown;

    rc = perform_random_reads(rand_idxs, &read_res);
    if(rc != 0) goto shutdown;

shutdown:
    map_size = db_core_shutdown();
    free(rand_idxs);

    if(rc != 0)
    {
        fprintf(stderr, "%s failed with rc=%d\n", BENCH_NAME, rc);
        return 1;
    }

    /* Clean up heavy on-disk state to keep /tmp tidy. */
    if(remove_directory(BENCH_DB_PATH) != 0)
    {
        fprintf(stderr, "WARNING: Failed to clean up %s after benchmark\n", BENCH_DB_PATH);
    }

    print_console_summary(&write_res, &read_res, map_size);
    write_results_file(&sys_info, &write_res, &read_res, map_size);
    return 0;
}
