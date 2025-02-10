/*
 *  Dirtyrate common functions
 *
 *  Copyright (c) 2020 HUAWEI TECHNOLOGIES CO., LTD.
 *
 *  Authors:
 *  Chuan Zheng <zhengchuan@huawei.com>
 *
 *  This work is licensed under the terms of the GNU GPL, version 2 or later.
 *  See the COPYING file in the top-level directory.
 */

#ifndef QEMU_MIGRATION_DIRTYRATE_H
#define QEMU_MIGRATION_DIRTYRATE_H

#include "system/dirtyrate.h"

/*
 * Sample 512 pages per GB as default.
 */
#define DIRTYRATE_DEFAULT_SAMPLE_PAGES            512

/*
 * Record ramblock idstr
 */
#define RAMBLOCK_INFO_MAX_LEN                     256

/*
 * Minimum RAMBlock size to sample, in megabytes.
 */
#define MIN_RAMBLOCK_SIZE                         128

/*
 * Allowed range for dirty page rate calculation (in milliseconds).
 * Lower limit relates to the smallest realistic downtime it
 * makes sense to impose on migration.
 */
#define MIN_CALC_TIME_MS                          50
#define MAX_CALC_TIME_MS                       60000

/*
 * Take 1/16 pages in 1G as the maxmum sample page count
 */
#define MIN_SAMPLE_PAGE_COUNT                     128
#define MAX_SAMPLE_PAGE_COUNT                     16384

struct DirtyRateConfig {
    uint64_t sample_pages_per_gigabytes; /* sample pages per GB */
    int64_t calc_time_ms; /* desired calculation time (in milliseconds) */
    DirtyRateMeasureMode mode; /* mode of dirtyrate measurement */
};

/*
 * Store dirtypage info for each ramblock.
 */
struct RamblockDirtyInfo {
    char idstr[RAMBLOCK_INFO_MAX_LEN]; /* idstr for each ramblock */
    uint8_t *ramblock_addr; /* base address of ramblock we measure */
    uint64_t ramblock_pages; /* ramblock size in TARGET_PAGE_SIZE */
    uint64_t *sample_page_vfn; /* relative offset address for sampled page */
    uint64_t sample_pages_count; /* count of sampled pages */
    uint64_t sample_dirty_count; /* count of dirty pages we measure */
    uint32_t *hash_result; /* array of hash result for sampled pages */
};

typedef struct SampleVMStat {
    uint64_t total_dirty_samples; /* total dirty sampled page */
    uint64_t total_sample_count; /* total sampled pages */
    uint64_t total_block_mem_MB; /* size of total sampled pages in MB */
} SampleVMStat;

/*
 * Store calculation statistics for each measure.
 */
struct DirtyRateStat {
    int64_t dirty_rate; /* dirty rate in MB/s */
    int64_t start_time; /* calculation start time in units of second */
    int64_t calc_time_ms; /* actual calculation time (in milliseconds) */
    uint64_t sample_pages; /* sample pages per GB */
    union {
        SampleVMStat page_sampling;
        VcpuStat dirty_ring;
    };
};

void *get_dirtyrate_thread(void *arg);
#endif
