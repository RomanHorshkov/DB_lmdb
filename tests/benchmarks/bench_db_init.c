/**
 * @file bench_db_init.c
 * @brief Benchmark for database initialization (creation from scratch).
 *
 * This benchmark measures ONLY the time required to initialize the database
 * environment with 1 sub-DBI from scratch (folder creation + environment setup).
 * 
 * Timing starts before db_core_init() and stops immediately after.
 * Shutdown time is NOT included in measurements.
 *
 * The test runs multiple iterations, cleaning the database directory between
 * each iteration to ensure every measurement is a true "from scratch" init.
 */

#include "core.h"
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
#define BENCH_ITERATIONS 100    /* Total number of init operations to test */
#define BENCH_DB_PATH "/tmp/bench_lmdb_test"
#define BENCH_DB_MODE 0700

/* System information structure */
typedef struct {
    char hostname[256];
    char cpu_model[256];
    char os_info[256];
    long cpu_cores;
    long cpu_freq_mhz;
    unsigned long total_ram_mb;
    char storage_type[64];  /* SSD or HDD */
    char filesystem[64];
} sys_info_t;

/* Statistics structure */
typedef struct {
    double mean;
    double std_dev;
    double min;
    double max;
    double median;
} stats_t;

/**
 * @brief Get system information
 */
static void get_system_info(sys_info_t* info) {
    FILE* fp;
    char buffer[256];
    
    /* Hostname */
    gethostname(info->hostname, sizeof(info->hostname));
    
    /* CPU model */
    fp = fopen("/proc/cpuinfo", "r");
    if (fp) {
        while (fgets(buffer, sizeof(buffer), fp)) {
            if (strncmp(buffer, "model name", 10) == 0) {
                char* colon = strchr(buffer, ':');
                if (colon) {
                    colon += 2;  /* Skip ": " */
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
    if (fp) {
        while (fgets(buffer, sizeof(buffer), fp)) {
            if (strncmp(buffer, "cpu MHz", 7) == 0) {
                char* colon = strchr(buffer, ':');
                if (colon) {
                    info->cpu_freq_mhz = (long)atof(colon + 1);
                    break;
                }
            }
        }
        fclose(fp);
    }
    
    /* Total RAM */
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        info->total_ram_mb = si.totalram / (1024 * 1024);
    }
    
    /* OS information */
    fp = fopen("/etc/os-release", "r");
    if (fp) {
        while (fgets(buffer, sizeof(buffer), fp)) {
            if (strncmp(buffer, "PRETTY_NAME=", 12) == 0) {
                char* start = strchr(buffer, '"');
                if (start) {
                    start++;
                    char* end = strchr(start, '"');
                    if (end) {
                        size_t len = end - start;
                        if (len < sizeof(info->os_info)) {
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
    if (fp) {
        if (fgets(buffer, sizeof(buffer), fp)) {
            int rota = atoi(buffer);
            strcpy(info->storage_type, rota == 0 ? "SSD" : "HDD");
        }
        pclose(fp);
    }
    
    /* If /tmp not in lsblk output, check root */
    if (strcmp(info->storage_type, "Unknown") == 0) {
        fp = popen("lsblk -o NAME,ROTA,MOUNTPOINT 2>/dev/null | grep ' /$' | awk '{print $2}'", "r");
        if (fp) {
            if (fgets(buffer, sizeof(buffer), fp)) {
                int rota = atoi(buffer);
                strcpy(info->storage_type, rota == 0 ? "SSD" : "HDD");
            }
            pclose(fp);
        }
    }
    
    /* Filesystem type */
    struct statvfs vfs;
    if (statvfs("/tmp", &vfs) == 0) {
        fp = popen("df -T /tmp 2>/dev/null | tail -1 | awk '{print $2}'", "r");
        if (fp) {
            if (fgets(buffer, sizeof(buffer), fp)) {
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
static double get_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000000.0 + (double)ts.tv_nsec / 1000.0;
}

/**
 * @brief Compare function for qsort (doubles)
 */
static int compare_double(const void* a, const void* b) {
    double da = *(const double*)a;
    double db = *(const double*)b;
    return (da > db) - (da < db);
}

/**
 * @brief Calculate statistics from an array of samples
 */
static void calculate_stats(double* samples, size_t n, stats_t* out) {
    double sum = 0.0;
    double sum_sq = 0.0;
    
    /* Sort samples for median and min/max */
    qsort(samples, n, sizeof(double), compare_double);
    
    /* Calculate mean */
    for (size_t i = 0; i < n; i++) {
        sum += samples[i];
    }
    out->mean = sum / (double)n;
    
    /* Calculate standard deviation */
    for (size_t i = 0; i < n; i++) {
        double diff = samples[i] - out->mean;
        sum_sq += diff * diff;
    }
    out->std_dev = sqrt(sum_sq / (double)n);
    
    /* Min, max, median */
    out->min = samples[0];
    out->max = samples[n - 1];
    out->median = (n % 2 == 0) 
        ? (samples[n/2 - 1] + samples[n/2]) / 2.0
        : samples[n/2];
}

/**
 * @brief Run a single benchmark iteration
 * @return Time in microseconds for INITIALIZATION ONLY, or -1.0 on error
 */
static double bench_single_init_only(void) {
    const char* dbi_names[] = {"test_dbi"};
    dbi_type_t dbi_types[] = {DBI_TYPE_DEFAULT};
    
    /* TIMING STARTS: Measure ONLY initialization time */
    double start = get_time_us();

    /* Initialize database (folder creation + environment setup) */
    int rc = db_core_init(BENCH_DB_PATH, BENCH_DB_MODE, dbi_names, dbi_types, 1);
    
    /* TIMING ENDS: Stop measurement immediately after init */
    double end = get_time_us();
    
    if (rc != 0) {
        fprintf(stderr, "ERROR: db_core_init failed with rc=%d\n", rc);
        return -1.0;
    }
    
    /* Shutdown database - NOT TIMED */
    (void)db_core_shutdown();
    
    return end - start;
}

/**
 * @brief Run benchmark suite
 */
static int run_benchmark(const char* output_file) {
    sys_info_t sys_info = {0};
    get_system_info(&sys_info);
    
    double* all_times = malloc(BENCH_ITERATIONS * sizeof(double));
    if (!all_times) {
        fprintf(stderr, "ERROR: Failed to allocate memory for results\n");
        return -ENOMEM;
    }
    
    printf("=================================================================\n");
    printf("Database Initialization Benchmark (FROM SCRATCH)\n");
    printf("=================================================================\n");
    printf("\n");
    printf("SYSTEM INFORMATION:\n");
    printf("-------------------\n");
    printf("Hostname:       %s\n", sys_info.hostname);
    printf("OS:             %s\n", sys_info.os_info);
    printf("CPU:            %s\n", sys_info.cpu_model);
    printf("CPU Cores:      %ld\n", sys_info.cpu_cores);
    printf("CPU Frequency:  %ld MHz\n", sys_info.cpu_freq_mhz);
    printf("Total RAM:      %lu MB\n", sys_info.total_ram_mb);
    printf("Storage Type:   %s\n", sys_info.storage_type);
    printf("Filesystem:     %s\n", sys_info.filesystem);
    printf("\n");
    printf("BENCHMARK CONFIGURATION:\n");
    printf("------------------------\n");
    printf("Test Type:      Database initialization ONLY (from scratch)\n");
    printf("Measured:       Folder creation + environment setup time\n");
    printf("NOT Measured:   Shutdown/cleanup time\n");
    printf("Iterations:     %d (each starting from clean state)\n", BENCH_ITERATIONS);
    printf("DB Path:        %s\n", BENCH_DB_PATH);
    printf("DB Mode:        0%o\n", BENCH_DB_MODE);
    printf("Sub-DBIs:       1\n");
    printf("=================================================================\n\n");
    
    printf("Running benchmark...\n");
    
    /* Run all iterations */
    for (int iter = 0; iter < BENCH_ITERATIONS; iter++) {
        /* Measure ONLY initialization time (NOT shutdown) */
        double iter_time = bench_single_init_only();
        if (iter_time < 0.0) {
            fprintf(stderr, "ERROR: Benchmark iteration %d failed\n", iter);
            free(all_times);
            return -1;
        }
        all_times[iter] = iter_time;
        
        /* Clean up database directory for next iteration (NOT TIMED) */
        if (remove_directory(BENCH_DB_PATH) != 0) {
            fprintf(stderr, "WARNING: Failed to remove directory %s\n", BENCH_DB_PATH);
        }
        
        /* Progress indicator */
        if ((iter + 1) % 100 == 0) {
            printf("Progress: %d/%d iterations completed\n", iter + 1, BENCH_ITERATIONS);
        }
    }
    
    printf("\nBenchmark completed!\n\n");
    
    /* Calculate statistics */
    stats_t stats;
    /* Use a sorted copy for statistics so that the original per-iteration
     * ordering is preserved for the detailed timing section. */
    double* sorted = malloc(BENCH_ITERATIONS * sizeof(double));
    if(!sorted)
    {
        fprintf(stderr, "ERROR: Failed to allocate memory for statistics\n");
        free(all_times);
        return -ENOMEM;
    }
    memcpy(sorted, all_times, BENCH_ITERATIONS * sizeof(double));
    calculate_stats(sorted, BENCH_ITERATIONS, &stats);
    free(sorted);
    
    /* Display results to console */
    printf("=================================================================\n");
    printf("DATABASE INITIALIZATION RESULTS (from scratch, per operation)\n");
    printf("=================================================================\n");
    printf("Total Iterations:  %d\n", BENCH_ITERATIONS);
    printf("\nPer-Operation Statistics:\n");
    printf("  Mean:            %.2f μs (%.4f ms)\n", stats.mean, stats.mean / 1000.0);
    printf("  Std Dev:         %.2f μs (%.4f ms)\n", stats.std_dev, stats.std_dev / 1000.0);
    printf("  Median:          %.2f μs (%.4f ms)\n", stats.median, stats.median / 1000.0);
    printf("  Min:             %.2f μs (%.4f ms)\n", stats.min, stats.min / 1000.0);
    printf("  Max:             %.2f μs (%.4f ms)\n", stats.max, stats.max / 1000.0);
    printf("=================================================================\n\n");
    
    /* Write detailed results to file */
    FILE* fp = fopen(output_file, "w");
    if (!fp) {
        fprintf(stderr, "ERROR: Failed to open output file %s\n", output_file);
        free(all_times);
        return -errno;
    }
    
    fprintf(fp, "╔════════════════════════════════════════════════════════════════╗\n");
    fprintf(fp, "║     Database Initialization Benchmark Results                  ║\n");
    fprintf(fp, "╚════════════════════════════════════════════════════════════════╝\n\n");
    
    time_t now = time(NULL);
    fprintf(fp, "Timestamp: %s\n", ctime(&now));
    
    fprintf(fp, "═══════════════════════════════════════════════════════════════\n");
    fprintf(fp, "SYSTEM INFORMATION\n");
    fprintf(fp, "═══════════════════════════════════════════════════════════════\n");
    fprintf(fp, "Hostname:          %s\n", sys_info.hostname);
    fprintf(fp, "Operating System:  %s\n", sys_info.os_info);
    fprintf(fp, "CPU Model:         %s\n", sys_info.cpu_model);
    fprintf(fp, "CPU Cores:         %ld\n", sys_info.cpu_cores);
    fprintf(fp, "CPU Frequency:     %ld MHz\n", sys_info.cpu_freq_mhz);
    fprintf(fp, "Total RAM:         %lu MB\n", sys_info.total_ram_mb);
    fprintf(fp, "Storage Type:      %s\n", sys_info.storage_type);
    fprintf(fp, "Filesystem:        %s\n", sys_info.filesystem);
    
    fprintf(fp, "\n═══════════════════════════════════════════════════════════════\n");
    fprintf(fp, "BENCHMARK CONFIGURATION\n");
    fprintf(fp, "═══════════════════════════════════════════════════════════════\n");
    fprintf(fp, "Test Type:         Database initialization ONLY (from scratch)\n");
    fprintf(fp, "What is Measured:  Folder creation + environment setup time\n");
    fprintf(fp, "NOT Measured:      Shutdown/cleanup time (excluded)\n");
    fprintf(fp, "Total Iterations:  %d\n", BENCH_ITERATIONS);
    fprintf(fp, "DB Path:           %s\n", BENCH_DB_PATH);
    fprintf(fp, "DB Mode:           0%o\n", BENCH_DB_MODE);
    fprintf(fp, "Sub-DBIs:          1\n");
    fprintf(fp, "Note:              Each iteration starts from a completely clean state\n");
    fprintf(fp, "                   (directory deleted between iterations)\n");
    
    fprintf(fp, "\n═══════════════════════════════════════════════════════════════\n");
    fprintf(fp, "RESULTS - Per-Operation Statistics\n");
    fprintf(fp, "═══════════════════════════════════════════════════════════════\n");
    fprintf(fp, "Operations Tested: %d (each from scratch)\n\n", BENCH_ITERATIONS);
    fprintf(fp, "Mean:              %12.2f μs  (%10.6f ms)\n", stats.mean, stats.mean / 1000.0);
    fprintf(fp, "Standard Deviation:%12.2f μs  (%10.6f ms)\n", stats.std_dev, stats.std_dev / 1000.0);
    fprintf(fp, "Median:            %12.2f μs  (%10.6f ms)\n", stats.median, stats.median / 1000.0);
    fprintf(fp, "Minimum:           %12.2f μs  (%10.6f ms)\n", stats.min, stats.min / 1000.0);
    fprintf(fp, "Maximum:           %12.2f μs  (%10.6f ms)\n", stats.max, stats.max / 1000.0);
    
    fprintf(fp, "\n═══════════════════════════════════════════════════════════════\n");
    fprintf(fp, "DETAILED TIMING DATA (all %d operations)\n", BENCH_ITERATIONS);
    fprintf(fp, "═══════════════════════════════════════════════════════════════\n");
    for (int i = 0; i < BENCH_ITERATIONS; i++) {
        fprintf(fp, "Iteration %4d: %12.2f μs  (%10.6f ms)\n", i + 1, all_times[i], all_times[i] / 1000.0);
    }
    
    fclose(fp);
    free(all_times);
    
    printf("Detailed results written to: %s\n\n", output_file);
    
    return 0;
}

int main(int argc, char* argv[]) {
    const char* output_file = "tests/benchmarks/results/bench_db_init_results.txt";

    /* Allow custom output file from command line */
    if (argc > 1) {
        output_file = argv[1];
    }
    
    /* Ensure results directory exists */
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
    
    int rc = run_benchmark(output_file);
    
    /* Final cleanup */
    remove_directory(BENCH_DB_PATH);
    
    if (rc == 0) {
        printf("Benchmark completed successfully!\n");
        return 0;
    } else {
        fprintf(stderr, "Benchmark failed with error code: %d\n", rc);
        return 1;
    }
}
