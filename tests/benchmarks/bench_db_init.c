/**
 * @file bench_db_init.c
 * @brief Benchmark for database initialization and cleanup operations.
 *
 * This benchmark measures the time required to:
 * 1. Initialize the database environment with 1 sub-DBI
 * 2. Close the database
 * 
 * The test runs 10 iterations per round, then cleans up the database directory.
 * This entire process repeats for 100 rounds to gather statistical data.
 */

#include "core.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <math.h>

/* Benchmark configuration */
#define BENCH_ROUNDS 100         /* Number of complete test rounds */
#define BENCH_ITERATIONS 10      /* Iterations per round */
#define BENCH_DB_PATH "/tmp/bench_lmdb_test"
#define BENCH_DB_MODE 0700

/* Statistics structure */
typedef struct {
    double mean;
    double std_dev;
    double min;
    double max;
    double median;
} stats_t;

/**
 * @brief Remove directory recursively (simple rm -rf implementation)
 */
static int remove_directory(const char* path) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", path);
    return system(cmd);
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
 * @return Time in microseconds, or -1.0 on error
 */
static double bench_single_init_close(void) {
    const char* dbi_names[] = {"test_dbi"};
    dbi_type_t dbi_types[] = {DBI_TYPE_DEFAULT};
    
    double start = get_time_us();
    
    /* Initialize database */
    int rc = db_core_init(BENCH_DB_PATH, BENCH_DB_MODE, dbi_names, dbi_types, 1);
    if (rc != 0) {
        fprintf(stderr, "ERROR: db_core_init failed with rc=%d\n", rc);
        return -1.0;
    }
    
    double end = get_time_us();
    /* Shutdown database */
    (void)db_core_shutdown();
    
    return end - start;
}

/**
 * @brief Run benchmark suite
 */
static int run_benchmark(const char* output_file) {
    double* round_times = malloc(BENCH_ROUNDS * sizeof(double));
    if (!round_times) {
        fprintf(stderr, "ERROR: Failed to allocate memory for results\n");
        return -ENOMEM;
    }
    
    printf("=================================================================\n");
    printf("Database Initialization Benchmark\n");
    printf("=================================================================\n");
    printf("Configuration:\n");
    printf("  - Rounds:        %d\n", BENCH_ROUNDS);
    printf("  - Iterations:    %d per round\n", BENCH_ITERATIONS);
    printf("  - DB Path:       %s\n", BENCH_DB_PATH);
    printf("  - DB Mode:       0%o\n", BENCH_DB_MODE);
    printf("  - Sub-DBIs:      1\n");
    printf("=================================================================\n\n");
    
    /* Run benchmark rounds */
    double round_start = .0f;
    double round_end = .0f;
    for (int round = 0; round < BENCH_ROUNDS; round++) {
        
        
        /* Run iterations within this round */
        for (int iter = 0; iter < BENCH_ITERATIONS; iter++) {
            round_start = get_time_us();
            double iter_time = bench_single_init_close();
            if (iter_time < 0.0)
            {
                fprintf(stderr, "ERROR: Benchmark iteration failed at round %d, iter %d\n", 
                        round, iter);
                free(round_times);
                return -1;
            }
        }
        
        round_end = get_time_us();
        round_times[round] = round_end - round_start;
        
        /* Clean up database directory between rounds */
        if (remove_directory(BENCH_DB_PATH) != 0) {
            fprintf(stderr, "WARNING: Failed to remove directory %s\n", BENCH_DB_PATH);
        }
        
        /* Progress indicator */
        if ((round + 1) % 10 == 0) {
            printf("Progress: %d/%d rounds completed\n", round + 1, BENCH_ROUNDS);
        }
    }
    
    printf("\nBenchmark completed!\n\n");
    
    /* Calculate statistics */
    stats_t stats;
    calculate_stats(round_times, BENCH_ROUNDS, &stats);
    
    /* Display results to console */
    printf("=================================================================\n");
    printf("Results (time per round of %d iterations)\n", BENCH_ITERATIONS);
    printf("=================================================================\n");
    printf("Mean:           %.2f μs (%.4f ms)\n", stats.mean, stats.mean / 1000.0);
    printf("Std Dev:        %.2f μs (%.4f ms)\n", stats.std_dev, stats.std_dev / 1000.0);
    printf("Median:         %.2f μs (%.4f ms)\n", stats.median, stats.median / 1000.0);
    printf("Min:            %.2f μs (%.4f ms)\n", stats.min, stats.min / 1000.0);
    printf("Max:            %.2f μs (%.4f ms)\n", stats.max, stats.max / 1000.0);
    printf("\n");
    printf("Per-iteration averages:\n");
    printf("Mean/iter:      %.2f μs (%.4f ms)\n", 
           stats.mean / BENCH_ITERATIONS, stats.mean / BENCH_ITERATIONS / 1000.0);
    printf("=================================================================\n\n");
    
    /* Write detailed results to file */
    FILE* fp = fopen(output_file, "w");
    if (!fp) {
        fprintf(stderr, "ERROR: Failed to open output file %s\n", output_file);
        free(round_times);
        return -errno;
    }
    
    fprintf(fp, "Database Initialization Benchmark Results\n");
    fprintf(fp, "==========================================\n\n");
    
    time_t now = time(NULL);
    fprintf(fp, "Timestamp: %s", ctime(&now));
    fprintf(fp, "\nConfiguration:\n");
    fprintf(fp, "  Rounds:             %d\n", BENCH_ROUNDS);
    fprintf(fp, "  Iterations/round:   %d\n", BENCH_ITERATIONS);
    fprintf(fp, "  Total operations:   %d\n", BENCH_ROUNDS * BENCH_ITERATIONS);
    fprintf(fp, "  DB Path:            %s\n", BENCH_DB_PATH);
    fprintf(fp, "  DB Mode:            0%o\n", BENCH_DB_MODE);
    fprintf(fp, "  Sub-DBIs:           1\n");
    
    fprintf(fp, "\n\nStatistics (time per round of %d iterations):\n", BENCH_ITERATIONS);
    fprintf(fp, "==========================================\n");
    fprintf(fp, "Mean:      %12.2f μs  (%8.4f ms)\n", stats.mean, stats.mean / 1000.0);
    fprintf(fp, "Std Dev:   %12.2f μs  (%8.4f ms)\n", stats.std_dev, stats.std_dev / 1000.0);
    fprintf(fp, "Median:    %12.2f μs  (%8.4f ms)\n", stats.median, stats.median / 1000.0);
    fprintf(fp, "Min:       %12.2f μs  (%8.4f ms)\n", stats.min, stats.min / 1000.0);
    fprintf(fp, "Max:       %12.2f μs  (%8.4f ms)\n", stats.max, stats.max / 1000.0);
    
    fprintf(fp, "\n\nPer-iteration statistics:\n");
    fprintf(fp, "==========================================\n");
    fprintf(fp, "Mean/iter: %12.2f μs  (%8.4f ms)\n", 
            stats.mean / BENCH_ITERATIONS, stats.mean / BENCH_ITERATIONS / 1000.0);
    fprintf(fp, "StdDev/iter: %10.2f μs  (%8.4f ms)\n", 
            stats.std_dev / BENCH_ITERATIONS, stats.std_dev / BENCH_ITERATIONS / 1000.0);
    fprintf(fp, "Min/iter:  %12.2f μs  (%8.4f ms)\n", 
            stats.min / BENCH_ITERATIONS, stats.min / BENCH_ITERATIONS / 1000.0);
    fprintf(fp, "Max/iter:  %12.2f μs  (%8.4f ms)\n", 
            stats.max / BENCH_ITERATIONS, stats.max / BENCH_ITERATIONS / 1000.0);
    
    fprintf(fp, "\n\nDetailed Round Times (μs):\n");
    fprintf(fp, "==========================================\n");
    for (int i = 0; i < BENCH_ROUNDS; i++) {
        fprintf(fp, "Round %3d: %12.2f\n", i + 1, round_times[i]);
    }
    
    fclose(fp);
    free(round_times);
    
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
    system("mkdir -p tests/benchmarks/results");
    
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
