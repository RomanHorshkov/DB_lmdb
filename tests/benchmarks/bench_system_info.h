/**
 * @file bench_system_info.h
 * @brief Shared helpers for benchmark system information.
 */
#ifndef BENCH_SYSTEM_INFO_H
#define BENCH_SYSTEM_INFO_H

#include <stdio.h>

typedef struct
{
    char           hostname[256];
    char           cpu_model[256];
    char           os_info[256];
    long           cpu_cores;
    long           cpu_freq_mhz;
    unsigned long  total_ram_mb;
    char           storage_type[64]; /* SSD or HDD */
    char           filesystem[64];
} sys_info_t;

static inline void bench_print_system_info(FILE* out, const sys_info_t* sys_info)
{
    if(!out || !sys_info) return;
    fprintf(out, "SYSTEM INFORMATION:\n");
    fprintf(out, "Hostname:       %s\n", sys_info->hostname);
    fprintf(out, "OS:             %s\n", sys_info->os_info);
    fprintf(out, "CPU:            %s\n", sys_info->cpu_model);
    fprintf(out, "CPU Cores:      %ld\n", sys_info->cpu_cores);
    fprintf(out, "CPU Frequency:  %ld MHz\n", sys_info->cpu_freq_mhz);
    fprintf(out, "Total RAM:      %lu MB\n", sys_info->total_ram_mb);
    fprintf(out, "Storage Type:   %s\n", sys_info->storage_type);
    fprintf(out, "Filesystem:     %s\n", sys_info->filesystem);
    fprintf(out, "\n");
}

#endif /* BENCH_SYSTEM_INFO_H */
