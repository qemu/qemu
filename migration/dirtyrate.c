/*
 * Dirtyrate implement code
 *
 * Copyright (c) 2020 HUAWEI TECHNOLOGIES CO.,LTD.
 *
 * Authors:
 *  Chuan Zheng <zhengchuan@huawei.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "qemu/config-file.h"
#include "exec/memory.h"
#include "exec/ramblock.h"
#include "exec/target_page.h"
#include "qemu/rcu_queue.h"
#include "qapi/qapi-commands-migration.h"
#include "migration.h"
#include "dirtyrate.h"

static int CalculatingState = DIRTY_RATE_STATUS_UNSTARTED;
static struct DirtyRateStat DirtyStat;

static int dirtyrate_set_state(int *state, int old_state, int new_state)
{
    assert(new_state < DIRTY_RATE_STATUS__MAX);
    if (qatomic_cmpxchg(state, old_state, new_state) == old_state) {
        return 0;
    } else {
        return -1;
    }
}

static void reset_dirtyrate_stat(void)
{
    DirtyStat.total_dirty_samples = 0;
    DirtyStat.total_sample_count = 0;
    DirtyStat.total_block_mem_MB = 0;
    DirtyStat.dirty_rate = -1;
    DirtyStat.start_time = 0;
    DirtyStat.calc_time = 0;
}

static void update_dirtyrate_stat(struct RamblockDirtyInfo *info)
{
    DirtyStat.total_dirty_samples += info->sample_dirty_count;
    DirtyStat.total_sample_count += info->sample_pages_count;
    /* size of total pages in MB */
    DirtyStat.total_block_mem_MB += (info->ramblock_pages *
                                     TARGET_PAGE_SIZE) >> 20;
}

static void update_dirtyrate(uint64_t msec)
{
    uint64_t dirtyrate;
    uint64_t total_dirty_samples = DirtyStat.total_dirty_samples;
    uint64_t total_sample_count = DirtyStat.total_sample_count;
    uint64_t total_block_mem_MB = DirtyStat.total_block_mem_MB;

    dirtyrate = total_dirty_samples * total_block_mem_MB *
                1000 / (total_sample_count * msec);

    DirtyStat.dirty_rate = dirtyrate;
}

static void calculate_dirtyrate(struct DirtyRateConfig config)
{
    /* todo */
    return;
}

void *get_dirtyrate_thread(void *arg)
{
    struct DirtyRateConfig config = *(struct DirtyRateConfig *)arg;
    int ret;

    ret = dirtyrate_set_state(&CalculatingState, DIRTY_RATE_STATUS_UNSTARTED,
                              DIRTY_RATE_STATUS_MEASURING);
    if (ret == -1) {
        error_report("change dirtyrate state failed.");
        return NULL;
    }

    calculate_dirtyrate(config);

    ret = dirtyrate_set_state(&CalculatingState, DIRTY_RATE_STATUS_MEASURING,
                              DIRTY_RATE_STATUS_MEASURED);
    if (ret == -1) {
        error_report("change dirtyrate state failed.");
    }
    return NULL;
}
