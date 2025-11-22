/**
 * @file bench_db_get_batch.c
 * @brief Benchmark for batched vs non-batched GET operations.
 *
 * This benchmark measures ONLY the time required to perform GETs on an
 * already-populated database with a single sub-DBI:
 *
 *   - Scenario 1: non-batched GETs (one op per exec)
 *   - Scenario 2: batched GETs (8 ops per exec)
 *
 * For each run:
 *   - The database directory is cleaned.
 *   - A new environment + single DBI is created.
 *   - 1000 key/value pairs are inserted (NOT TIMED).
 *   - Timing starts before the first GET and stops after the last exec.
 *   - Shutdown is NOT included in the timing.
 */

#include "db_lmdb_core.h"
#include "bench_logging.h"
#include "bench_system_info.h"
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

/* Benchmark configuration */
#define BENCH_DB_PATH        "/tmp/bench_lmdb_get"
#define BENCH_DB_MODE        0700
#define BENCH_NUM_USERS      1000
#define BENCH_VALUE_SIZE     1024
#define BENCH_NUM_GETS       1000
#define BENCH_RUNS           10
#define BENCH_BATCH_SIZE     8

/* Statistics structure */
typedef struct
{
    double mean;
    double std_dev;
    double min;
    double max;
    double median;
} stats_t;

/* Pre-generated test data */
static char g_keys[BENCH_NUM_USERS][32];
static char g_value[BENCH_VALUE_SIZE];

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

    struct dirent* ent;
    int            rc = 0;

    while((ent = readdir(dir)) != NULL)
    {
        if(strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;

        char child[PATH_MAX];
        int  n = snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
        if(n <= 0 || (size_t)n >= sizeof(child))
        {
            rc = -1;
            continue;
        }

        struct stat child_st;
        if(lstat(child, &child_st) != 0)
        {
            rc = -1;
            continue;
        }

        if(S_ISDIR(child_st.st_mode))
        {
            if(remove_directory(child) != 0) rc = -1;
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

/**
 * @brief Get current time in microseconds
 */
static double get_time_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000000.0 + (double)ts.tv_nsec / 1000.0;
}

/**
 * @brief Compare function for qsort (doubles)
 */
static int compare_double(const void* a, const void* b)
{
    double da = *(const double*)a;
    double db = *(const double*)b;
    return (da > db) - (da < db);
}

/**
 * @brief Calculate statistics from an array of samples
 */
static void calculate_stats(double* samples, size_t n, stats_t* out)
{
    double sum    = 0.0;
    double sum_sq = 0.0;

    /* Sort samples for median and min/max */
    qsort(samples, n, sizeof(double), compare_double);

    /* Calculate mean */
    for(size_t i = 0; i < n; i++)
    {
        sum += samples[i];
    }
    out->mean = sum / (double)n;

    /* Calculate standard deviation */
    for(size_t i = 0; i < n; i++)
    {
        double diff = samples[i] - out->mean;
        sum_sq += diff * diff;
    }
    out->std_dev = sqrt(sum_sq / (double)n);

    /* Min, max, median */
    out->min    = samples[0];
    out->max    = samples[n - 1];
    out->median = (n % 2 == 0) ? (samples[n / 2 - 1] + samples[n / 2]) / 2.0 : samples[n / 2];
}

/**
 * @brief Initialize synthetic user keys and value buffer.
 */
static void init_test_data(void)
{
    for(int i = 0; i < BENCH_NUM_USERS; ++i)
    {
        snprintf(g_keys[i], sizeof(g_keys[i]), "user_%04d", i);
    }

    /* Fill value buffer with a deterministic pattern. */
    for(int i = 0; i < BENCH_VALUE_SIZE; ++i)
    {
        g_value[i] = (char)('A' + (i % 26));
    }
}

/**
 * @brief Populate the DB with BENCH_NUM_USERS entries (not timed).
 */
static int populate_db_for_gets(void)
{
    int rc;
    int pending = 0;

    for(int i = 0; i < BENCH_NUM_USERS; ++i)
    {
        rc = db_core_set_op(
            0u,
            DB_OPERATION_PUT,
            &(op_key_t){ .kind = OP_KEY_KIND_PRESENT,
                         .present = { .data = (void*)g_keys[i],
                                      .size = strlen(g_keys[i]) } },
            &(op_key_t){ .kind = OP_KEY_KIND_PRESENT,
                         .present = { .data = (void*)g_value,
                                      .size = BENCH_VALUE_SIZE } });
        if(rc != 0)
        {
            fprintf(stderr, "ERROR: populate_db_for_gets: db_core_set_op failed (user=%d, rc=%d)\n",
                    i, rc);
            return rc;
        }

        pending++;

        if(pending == BENCH_BATCH_SIZE)
        {
            rc = db_core_exec_ops();
            if(rc != 0)
            {
                fprintf(stderr,
                        "ERROR: populate_db_for_gets: db_core_exec_ops failed (pending=%d, rc=%d)\n",
                        pending, rc);
                return rc;
            }
            pending = 0;
        }
    }

    if(pending > 0)
    {
        rc = db_core_exec_ops();
        if(rc != 0)
        {
            fprintf(stderr,
                    "ERROR: populate_db_for_gets: final db_core_exec_ops failed (pending=%d, rc=%d)\n",
                    pending, rc);
            return rc;
        }
    }

    return 0;
}

/**
 * @brief Run one benchmark run for the given batch size (GETs only).
 *
 * The run:
 *   - starts from a clean directory
 *   - creates the environment + single DBI
 *   - populates DB with 1000 entries (not timed)
 *   - measures ONLY the time spent performing BENCH_NUM_GETS GET operations
 */
static int run_single_get_run(int batch_size, int run_index, double* out_total_us)
{
    static const char*      dbi_names[] = { "bench_users" };
    static const dbi_type_t dbi_types[] = { DBI_TYPE_DEFAULT };

    /* Clean up any previous database state (not timed). */
    if(remove_directory(BENCH_DB_PATH) != 0)
    {
        fprintf(stderr, "WARNING: Failed to remove directory %s\n", BENCH_DB_PATH);
    }

    /* Create database environment + single DBI (not timed). */
    int rc = db_core_init(BENCH_DB_PATH, BENCH_DB_MODE, dbi_names, dbi_types, 1u);
    if(rc != 0)
    {
        fprintf(stderr, "ERROR: db_core_init failed with rc=%d\n", rc);
        return rc;
    }

    /* Populate the DB with data (not timed). */
    rc = populate_db_for_gets();
    if(rc != 0)
    {
        (void)db_core_shutdown();
        return rc;
    }

    /* Prepare deterministic random sequence per run. */
    srand((unsigned int)(1234 + run_index));

    char  val_buf[BENCH_VALUE_SIZE];
    double start_us = get_time_us();

    if(batch_size <= 1)
    {
        /* Non-batched: one GET per exec. */
        for(int i = 0; i < BENCH_NUM_GETS; ++i)
        {
            int idx = rand() % BENCH_NUM_USERS;

            memset(val_buf, 0, sizeof(val_buf));
            rc = db_core_set_op(
                0u,
                DB_OPERATION_GET,
                &(op_key_t){ .kind = OP_KEY_KIND_PRESENT,
                             .present = { .data = (void*)g_keys[idx],
                                          .size = strlen(g_keys[idx]) } },
                &(op_key_t){ .kind = OP_KEY_KIND_PRESENT,
                             .present = { .data = (void*)val_buf,
                                          .size = BENCH_VALUE_SIZE } });
            if(rc != 0)
            {
                fprintf(stderr,
                        "ERROR: run_single_get_run: db_core_set_op failed (idx=%d, rc=%d)\n", idx,
                        rc);
                (void)db_core_shutdown();
                return rc;
            }

            rc = db_core_exec_ops();
            if(rc != 0)
            {
                fprintf(stderr,
                        "ERROR: run_single_get_run: db_core_exec_ops failed (idx=%d, rc=%d)\n",
                        idx, rc);
                (void)db_core_shutdown();
                return rc;
            }
        }
    }
    else
    {
        /* Batched: accumulate batch_size GETs, then exec. */
        int pending = 0;

        for(int i = 0; i < BENCH_NUM_GETS; ++i)
        {
            int idx = rand() % BENCH_NUM_USERS;

            memset(val_buf, 0, sizeof(val_buf));
            rc = db_core_set_op(
                0u,
                DB_OPERATION_GET,
                &(op_key_t){ .kind = OP_KEY_KIND_PRESENT,
                             .present = { .data = (void*)g_keys[idx],
                                          .size = strlen(g_keys[idx]) } },
                &(op_key_t){ .kind = OP_KEY_KIND_PRESENT,
                             .present = { .data = (void*)val_buf,
                                          .size = BENCH_VALUE_SIZE } });
            if(rc != 0)
            {
                fprintf(stderr,
                        "ERROR: run_single_get_run: db_core_set_op failed (idx=%d, rc=%d)\n", idx,
                        rc);
                (void)db_core_shutdown();
                return rc;
            }

            pending++;

            if(pending == batch_size)
            {
                rc = db_core_exec_ops();
                if(rc != 0)
                {
                    fprintf(stderr,
                            "ERROR: run_single_get_run: db_core_exec_ops failed during batch "
                            "(pending=%d, rc=%d)\n",
                            pending, rc);
                    (void)db_core_shutdown();
                    return rc;
                }
                pending = 0;
            }
        }

        /* Flush any remaining operations in the cache. */
        if(pending > 0)
        {
            rc = db_core_exec_ops();
            if(rc != 0)
            {
                fprintf(stderr,
                        "ERROR: run_single_get_run: db_core_exec_ops failed during final flush "
                        "(pending=%d, rc=%d)\n",
                        pending, rc);
                (void)db_core_shutdown();
                return rc;
            }
        }
    }

    double end_us = get_time_us();

    /* Shutdown database (not timed). */
    (void)db_core_shutdown();

    *out_total_us = end_us - start_us;
    return 0;
}

/**
 * @brief Run the full GET benchmark for a given pattern (batch_size).
 */
static int run_get_benchmark(const char* label,
                             int         batch_size,
                             const char* output_file)
{
    sys_info_t sys_info = {0};
    get_system_info(&sys_info);

    double* all_times = malloc(BENCH_RUNS * sizeof(double));
    if(!all_times)
    {
        fprintf(stderr, "ERROR: Failed to allocate memory for results\n");
        return -ENOMEM;
    }

    printf("=================================================================\n");
    printf("Database GET Benchmark (%s)\n", label);
    printf("=================================================================\n\n");

    bench_print_system_info(stdout, &sys_info);

    printf("BENCHMARK CONFIGURATION:\n");
    printf("Test Type:      GET operations from single DBI\n");
    printf("Measured:       db_core_set_op + db_core_exec_ops only\n");
    printf("NOT Measured:   Environment/DBI init, population, shutdown, directory cleanup\n");
    printf("Users stored:   %d\n", BENCH_NUM_USERS);
    printf("Value size:     %d bytes\n", BENCH_VALUE_SIZE);
    printf("GETs per run:   %d\n", BENCH_NUM_GETS);
    printf("Batch size:     %d\n", batch_size <= 1 ? 1 : batch_size);
    printf("Runs:           %d\n", BENCH_RUNS);
    printf("DB Path:        %s\n", BENCH_DB_PATH);
    printf("DB Mode:        0%o\n", BENCH_DB_MODE);
    printf("=================================================================\n\n");

    for(int run = 0; run < BENCH_RUNS; ++run)
    {
        double total_us = 0.0;
        int    rc       = run_single_get_run(batch_size, run, &total_us);
        if(rc != 0)
        {
            fprintf(stderr, "ERROR: GET benchmark run %d failed with rc=%d\n", run + 1, rc);
            free(all_times);
            return rc;
        }
        all_times[run] = total_us;

        printf("  Run %2d/%d: total = %.2f μs (%.4f ms), per-op ≈ %.2f μs\n", run + 1,
               BENCH_RUNS, total_us, total_us / 1000.0,
               total_us / (double)BENCH_NUM_GETS);
    }

    /* Compute statistics on total run times. */
    stats_t stats;
    double* sorted = malloc(BENCH_RUNS * sizeof(double));
    if(!sorted)
    {
        fprintf(stderr, "ERROR: Failed to allocate memory for statistics\n");
        free(all_times);
        return -ENOMEM;
    }
    memcpy(sorted, all_times, BENCH_RUNS * sizeof(double));
    calculate_stats(sorted, BENCH_RUNS, &stats);
    free(sorted);

    printf("=================================================================\n");
    printf("GET BENCHMARK RESULTS (%s)\n", label);
    printf("=================================================================\n");
    printf("Total runs:     %d\n", BENCH_RUNS);
    printf("GETs per run:   %d\n", BENCH_NUM_GETS);
    printf("\nPer-run totals (microseconds):\n");
    printf("  Mean:         %.2f μs (%.4f ms)\n", stats.mean, stats.mean / 1000.0);
    printf("  Std Dev:      %.2f μs (%.4f ms)\n", stats.std_dev, stats.std_dev / 1000.0);
    printf("  Median:       %.2f μs (%.4f ms)\n", stats.median, stats.median / 1000.0);
    printf("  Min:          %.2f μs (%.4f ms)\n", stats.min, stats.min / 1000.0);
    printf("  Max:          %.2f μs (%.4f ms)\n", stats.max, stats.max / 1000.0);
    printf("\nPer-operation mean (approx):\n");
    printf("  Mean:         %.2f μs (%.4f ms)\n", stats.mean / (double)BENCH_NUM_GETS,
           (stats.mean / (double)BENCH_NUM_GETS) / 1000.0);
    printf("=================================================================\n\n");

    /* Write detailed results to file. */
    FILE* fp = fopen(output_file, "w");
    if(!fp)
    {
        fprintf(stderr, "ERROR: Failed to open output file %s\n", output_file);
        free(all_times);
        return -errno;
    }

    fprintf(fp, "╔════════════════════════════════════════════════════════════════╗\n");
    fprintf(fp, "║           Database GET Benchmark (%-28s)           ║\n", label);
    fprintf(fp, "╚════════════════════════════════════════════════════════════════╝\n\n");

    fprintf(fp, "SYSTEM INFORMATION\n");
    fprintf(fp, "-------------------\n");
    fprintf(fp, "Hostname:          %s\n", sys_info.hostname);
    fprintf(fp, "Operating System:  %s\n", sys_info.os_info);
    fprintf(fp, "CPU Model:         %s\n", sys_info.cpu_model);
    fprintf(fp, "CPU Cores:         %ld\n", sys_info.cpu_cores);
    fprintf(fp, "CPU Frequency:     %ld MHz\n", sys_info.cpu_freq_mhz);
    fprintf(fp, "Total RAM:         %lu MB\n", sys_info.total_ram_mb);
    fprintf(fp, "Storage Type:      %s\n", sys_info.storage_type);
    fprintf(fp, "Filesystem:        %s\n", sys_info.filesystem);

    fprintf(fp, "\nBENCHMARK CONFIGURATION\n");
    fprintf(fp, "------------------------\n");
    fprintf(fp, "Test Type:         GET operations from single DBI\n");
    fprintf(fp, "What is Measured:  db_core_set_op + db_core_exec_ops only\n");
    fprintf(fp, "NOT Measured:      Environment/DBI init, population, shutdown, directory cleanup\n");
    fprintf(fp, "Users stored:      %d\n", BENCH_NUM_USERS);
    fprintf(fp, "Value size:        %d bytes\n", BENCH_VALUE_SIZE);
    fprintf(fp, "GETs per run:      %d\n", BENCH_NUM_GETS);
    fprintf(fp, "Batch size:        %d\n", batch_size <= 1 ? 1 : batch_size);
    fprintf(fp, "Runs:              %d\n", BENCH_RUNS);
    fprintf(fp, "DB Path:           %s\n", BENCH_DB_PATH);
    fprintf(fp, "DB Mode:           0%o\n", BENCH_DB_MODE);

    fprintf(fp, "\nRESULTS - Per-run Totals\n");
    fprintf(fp, "------------------------\n");
    fprintf(fp, "Total Runs:        %d\n", BENCH_RUNS);
    fprintf(fp, "Mean (total):      %12.2f μs  (%10.6f ms)\n", stats.mean,
            stats.mean / 1000.0);
    fprintf(fp, "Std Dev (total):   %12.2f μs  (%10.6f ms)\n", stats.std_dev,
            stats.std_dev / 1000.0);
    fprintf(fp, "Median (total):    %12.2f μs  (%10.6f ms)\n", stats.median,
            stats.median / 1000.0);
    fprintf(fp, "Min (total):       %12.2f μs  (%10.6f ms)\n", stats.min,
            stats.min / 1000.0);
    fprintf(fp, "Max (total):       %12.2f μs  (%10.6f ms)\n", stats.max,
            stats.max / 1000.0);

    fprintf(fp, "\nRESULTS - Per-operation (approx)\n");
    fprintf(fp, "--------------------------------\n");
    double mean_per_op = stats.mean / (double)BENCH_NUM_GETS;
    fprintf(fp, "Mean per-op:       %12.2f μs  (%10.6f ms)\n", mean_per_op,
            mean_per_op / 1000.0);

    fprintf(fp, "\nDETAILED TIMING DATA (all %d runs)\n", BENCH_RUNS);
    fprintf(fp, "----------------------------------\n");
    for(int i = 0; i < BENCH_RUNS; ++i)
    {
        fprintf(fp, "Run %4d: %12.2f μs  (%10.6f ms)  [per-op ≈ %.2f μs]\n", i + 1,
                all_times[i], all_times[i] / 1000.0,
                all_times[i] / (double)BENCH_NUM_GETS);
    }

    fclose(fp);
    free(all_times);

    printf("Detailed results written to: %s\n\n", output_file);
    return 0;
}

int main(void)
{
    const char* output_single = "tests/benchmarks/results/bench_get_users_single.txt";
    const char* output_batch8 = "tests/benchmarks/results/bench_get_users_batch8.txt";

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

    /* Prepare synthetic data once. */
    init_test_data();

    int rc_single =
        run_get_benchmark("Single GET (no batching)", 1, output_single);
    if(rc_single != 0)
    {
        fprintf(stderr, "Single GET benchmark failed with rc=%d\n", rc_single);
    }

    /* Clean up completely between patterns. */
    (void)remove_directory(BENCH_DB_PATH);

    int rc_batch =
        run_get_benchmark("Batched GET (8 ops)", BENCH_BATCH_SIZE, output_batch8);
    if(rc_batch != 0)
    {
        fprintf(stderr, "Batched GET benchmark failed with rc=%d\n", rc_batch);
    }

    if(rc_single == 0 && rc_batch == 0)
    {
        printf("All GET benchmarks completed successfully!\n");
        return 0;
    }

    fprintf(stderr, "One or more GET benchmarks failed (single rc=%d, batch rc=%d)\n",
            rc_single, rc_batch);
    return 1;
}
