/**
 * @file bench_logging.h
 * @brief Helper to silence EMlogger output in benchmarks.
 */
#ifndef BENCH_LOGGING_H
#define BENCH_LOGGING_H

#include "emlog.h"

static inline ssize_t bench_emlog_sink(eml_level_t lvl, const char* line, size_t n, void* user)
{
    (void)lvl;
    (void)line;
    (void)user;
    return (ssize_t)n;
}

static inline void bench_silence_emlog(void)
{
    /* Disable all logging noise for benchmarks. */
    emlog_init(EML_LEVEL_CRIT + 1, false);
    emlog_set_writer(bench_emlog_sink, NULL);
    emlog_set_writev_flush(false);
}

#endif /* BENCH_LOGGING_H */
