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
#include <zlib.h>
#include "qapi/error.h"
#include "cpu.h"
#include "exec/ramblock.h"
#include "exec/ram_addr.h"
#include "qemu/rcu_queue.h"
#include "qemu/main-loop.h"
#include "qapi/qapi-commands-migration.h"
#include "ram.h"
#include "trace.h"
#include "dirtyrate.h"
#include "monitor/hmp.h"
#include "monitor/monitor.h"
#include "qapi/qmp/qdict.h"
#include "sysemu/kvm.h"
#include "sysemu/runstate.h"
#include "exec/memory.h"

/*
 * total_dirty_pages is procted by BQL and is used
 * to stat dirty pages during the period of two
 * memory_global_dirty_log_sync
 */
uint64_t total_dirty_pages;

typedef struct DirtyPageRecord {
    uint64_t start_pages;
    uint64_t end_pages;
} DirtyPageRecord;

static int CalculatingState = DIRTY_RATE_STATUS_UNSTARTED;
static struct DirtyRateStat DirtyStat;
static DirtyRateMeasureMode dirtyrate_mode =
                DIRTY_RATE_MEASURE_MODE_PAGE_SAMPLING;

static int64_t dirty_stat_wait(int64_t msec, int64_t initial_time)
{
    int64_t current_time;

    current_time = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
    if ((current_time - initial_time) >= msec) {
        msec = current_time - initial_time;
    } else {
        g_usleep((msec + initial_time - current_time) * 1000);
    }

    return msec;
}

static inline void record_dirtypages(DirtyPageRecord *dirty_pages,
                                     CPUState *cpu, bool start)
{
    if (start) {
        dirty_pages[cpu->cpu_index].start_pages = cpu->dirty_pages;
    } else {
        dirty_pages[cpu->cpu_index].end_pages = cpu->dirty_pages;
    }
}

static int64_t do_calculate_dirtyrate(DirtyPageRecord dirty_pages,
                                      int64_t calc_time_ms)
{
    uint64_t memory_size_MB;
    uint64_t increased_dirty_pages =
        dirty_pages.end_pages - dirty_pages.start_pages;

    memory_size_MB = (increased_dirty_pages * TARGET_PAGE_SIZE) >> 20;

    return memory_size_MB * 1000 / calc_time_ms;
}

void global_dirty_log_change(unsigned int flag, bool start)
{
    qemu_mutex_lock_iothread();
    if (start) {
        memory_global_dirty_log_start(flag);
    } else {
        memory_global_dirty_log_stop(flag);
    }
    qemu_mutex_unlock_iothread();
}

/*
 * global_dirty_log_sync
 * 1. sync dirty log from kvm
 * 2. stop dirty tracking if needed.
 */
static void global_dirty_log_sync(unsigned int flag, bool one_shot)
{
    qemu_mutex_lock_iothread();
    memory_global_dirty_log_sync();
    if (one_shot) {
        memory_global_dirty_log_stop(flag);
    }
    qemu_mutex_unlock_iothread();
}

static DirtyPageRecord *vcpu_dirty_stat_alloc(VcpuStat *stat)
{
    CPUState *cpu;
    DirtyPageRecord *records;
    int nvcpu = 0;

    CPU_FOREACH(cpu) {
        nvcpu++;
    }

    stat->nvcpu = nvcpu;
    stat->rates = g_new0(DirtyRateVcpu, nvcpu);

    records = g_new0(DirtyPageRecord, nvcpu);

    return records;
}

static void vcpu_dirty_stat_collect(VcpuStat *stat,
                                    DirtyPageRecord *records,
                                    bool start)
{
    CPUState *cpu;

    CPU_FOREACH(cpu) {
        record_dirtypages(records, cpu, start);
    }
}

int64_t vcpu_calculate_dirtyrate(int64_t calc_time_ms,
                                 VcpuStat *stat,
                                 unsigned int flag,
                                 bool one_shot)
{
    DirtyPageRecord *records;
    int64_t init_time_ms;
    int64_t duration;
    int64_t dirtyrate;
    int i = 0;
    unsigned int gen_id;

retry:
    init_time_ms = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);

    cpu_list_lock();
    gen_id = cpu_list_generation_id_get();
    records = vcpu_dirty_stat_alloc(stat);
    vcpu_dirty_stat_collect(stat, records, true);
    cpu_list_unlock();

    duration = dirty_stat_wait(calc_time_ms, init_time_ms);

    global_dirty_log_sync(flag, one_shot);

    cpu_list_lock();
    if (gen_id != cpu_list_generation_id_get()) {
        g_free(records);
        g_free(stat->rates);
        cpu_list_unlock();
        goto retry;
    }
    vcpu_dirty_stat_collect(stat, records, false);
    cpu_list_unlock();

    for (i = 0; i < stat->nvcpu; i++) {
        dirtyrate = do_calculate_dirtyrate(records[i], duration);

        stat->rates[i].id = i;
        stat->rates[i].dirty_rate = dirtyrate;

        trace_dirtyrate_do_calculate_vcpu(i, dirtyrate);
    }

    g_free(records);

    return duration;
}

static bool is_sample_period_valid(int64_t sec)
{
    if (sec < MIN_FETCH_DIRTYRATE_TIME_SEC ||
        sec > MAX_FETCH_DIRTYRATE_TIME_SEC) {
        return false;
    }

    return true;
}

static bool is_sample_pages_valid(int64_t pages)
{
    return pages >= MIN_SAMPLE_PAGE_COUNT &&
           pages <= MAX_SAMPLE_PAGE_COUNT;
}

static int dirtyrate_set_state(int *state, int old_state, int new_state)
{
    assert(new_state < DIRTY_RATE_STATUS__MAX);
    trace_dirtyrate_set_state(DirtyRateStatus_str(new_state));
    if (qatomic_cmpxchg(state, old_state, new_state) == old_state) {
        return 0;
    } else {
        return -1;
    }
}

static struct DirtyRateInfo *query_dirty_rate_info(void)
{
    int i;
    int64_t dirty_rate = DirtyStat.dirty_rate;
    struct DirtyRateInfo *info = g_new0(DirtyRateInfo, 1);
    DirtyRateVcpuList *head = NULL, **tail = &head;

    info->status = CalculatingState;
    info->start_time = DirtyStat.start_time;
    info->calc_time = DirtyStat.calc_time;
    info->sample_pages = DirtyStat.sample_pages;
    info->mode = dirtyrate_mode;

    if (qatomic_read(&CalculatingState) == DIRTY_RATE_STATUS_MEASURED) {
        info->has_dirty_rate = true;
        info->dirty_rate = dirty_rate;

        if (dirtyrate_mode == DIRTY_RATE_MEASURE_MODE_DIRTY_RING) {
            /*
             * set sample_pages with 0 to indicate page sampling
             * isn't enabled
             **/
            info->sample_pages = 0;
            info->has_vcpu_dirty_rate = true;
            for (i = 0; i < DirtyStat.dirty_ring.nvcpu; i++) {
                DirtyRateVcpu *rate = g_new0(DirtyRateVcpu, 1);
                rate->id = DirtyStat.dirty_ring.rates[i].id;
                rate->dirty_rate = DirtyStat.dirty_ring.rates[i].dirty_rate;
                QAPI_LIST_APPEND(tail, rate);
            }
            info->vcpu_dirty_rate = head;
        }

        if (dirtyrate_mode == DIRTY_RATE_MEASURE_MODE_DIRTY_BITMAP) {
            info->sample_pages = 0;
        }
    }

    trace_query_dirty_rate_info(DirtyRateStatus_str(CalculatingState));

    return info;
}

static void init_dirtyrate_stat(int64_t start_time,
                                struct DirtyRateConfig config)
{
    DirtyStat.dirty_rate = -1;
    DirtyStat.start_time = start_time;
    DirtyStat.calc_time = config.sample_period_seconds;
    DirtyStat.sample_pages = config.sample_pages_per_gigabytes;

    switch (config.mode) {
    case DIRTY_RATE_MEASURE_MODE_PAGE_SAMPLING:
        DirtyStat.page_sampling.total_dirty_samples = 0;
        DirtyStat.page_sampling.total_sample_count = 0;
        DirtyStat.page_sampling.total_block_mem_MB = 0;
        break;
    case DIRTY_RATE_MEASURE_MODE_DIRTY_RING:
        DirtyStat.dirty_ring.nvcpu = -1;
        DirtyStat.dirty_ring.rates = NULL;
        break;
    default:
        break;
    }
}

static void cleanup_dirtyrate_stat(struct DirtyRateConfig config)
{
    /* last calc-dirty-rate qmp use dirty ring mode */
    if (dirtyrate_mode == DIRTY_RATE_MEASURE_MODE_DIRTY_RING) {
        free(DirtyStat.dirty_ring.rates);
        DirtyStat.dirty_ring.rates = NULL;
    }
}

static void update_dirtyrate_stat(struct RamblockDirtyInfo *info)
{
    DirtyStat.page_sampling.total_dirty_samples += info->sample_dirty_count;
    DirtyStat.page_sampling.total_sample_count += info->sample_pages_count;
    /* size of total pages in MB */
    DirtyStat.page_sampling.total_block_mem_MB += (info->ramblock_pages *
                                                   TARGET_PAGE_SIZE) >> 20;
}

static void update_dirtyrate(uint64_t msec)
{
    uint64_t dirtyrate;
    uint64_t total_dirty_samples = DirtyStat.page_sampling.total_dirty_samples;
    uint64_t total_sample_count = DirtyStat.page_sampling.total_sample_count;
    uint64_t total_block_mem_MB = DirtyStat.page_sampling.total_block_mem_MB;

    dirtyrate = total_dirty_samples * total_block_mem_MB *
                1000 / (total_sample_count * msec);

    DirtyStat.dirty_rate = dirtyrate;
}

/*
 * get hash result for the sampled memory with length of TARGET_PAGE_SIZE
 * in ramblock, which starts from ramblock base address.
 */
static uint32_t get_ramblock_vfn_hash(struct RamblockDirtyInfo *info,
                                      uint64_t vfn)
{
    uint32_t crc;

    crc = crc32(0, (info->ramblock_addr +
                vfn * TARGET_PAGE_SIZE), TARGET_PAGE_SIZE);

    trace_get_ramblock_vfn_hash(info->idstr, vfn, crc);
    return crc;
}

static bool save_ramblock_hash(struct RamblockDirtyInfo *info)
{
    unsigned int sample_pages_count;
    int i;
    GRand *rand;

    sample_pages_count = info->sample_pages_count;

    /* ramblock size less than one page, return success to skip this ramblock */
    if (unlikely(info->ramblock_pages == 0 || sample_pages_count == 0)) {
        return true;
    }

    info->hash_result = g_try_malloc0_n(sample_pages_count,
                                        sizeof(uint32_t));
    if (!info->hash_result) {
        return false;
    }

    info->sample_page_vfn = g_try_malloc0_n(sample_pages_count,
                                            sizeof(uint64_t));
    if (!info->sample_page_vfn) {
        g_free(info->hash_result);
        return false;
    }

    rand  = g_rand_new();
    for (i = 0; i < sample_pages_count; i++) {
        info->sample_page_vfn[i] = g_rand_int_range(rand, 0,
                                                    info->ramblock_pages - 1);
        info->hash_result[i] = get_ramblock_vfn_hash(info,
                                                     info->sample_page_vfn[i]);
    }
    g_rand_free(rand);

    return true;
}

static void get_ramblock_dirty_info(RAMBlock *block,
                                    struct RamblockDirtyInfo *info,
                                    struct DirtyRateConfig *config)
{
    uint64_t sample_pages_per_gigabytes = config->sample_pages_per_gigabytes;

    /* Right shift 30 bits to calc ramblock size in GB */
    info->sample_pages_count = (qemu_ram_get_used_length(block) *
                                sample_pages_per_gigabytes) >> 30;
    /* Right shift TARGET_PAGE_BITS to calc page count */
    info->ramblock_pages = qemu_ram_get_used_length(block) >>
                           TARGET_PAGE_BITS;
    info->ramblock_addr = qemu_ram_get_host_addr(block);
    strcpy(info->idstr, qemu_ram_get_idstr(block));
}

static void free_ramblock_dirty_info(struct RamblockDirtyInfo *infos, int count)
{
    int i;

    if (!infos) {
        return;
    }

    for (i = 0; i < count; i++) {
        g_free(infos[i].sample_page_vfn);
        g_free(infos[i].hash_result);
    }
    g_free(infos);
}

static bool skip_sample_ramblock(RAMBlock *block)
{
    /*
     * Sample only blocks larger than MIN_RAMBLOCK_SIZE.
     */
    if (qemu_ram_get_used_length(block) < (MIN_RAMBLOCK_SIZE << 10)) {
        trace_skip_sample_ramblock(block->idstr,
                                   qemu_ram_get_used_length(block));
        return true;
    }

    return false;
}

static bool record_ramblock_hash_info(struct RamblockDirtyInfo **block_dinfo,
                                      struct DirtyRateConfig config,
                                      int *block_count)
{
    struct RamblockDirtyInfo *info = NULL;
    struct RamblockDirtyInfo *dinfo = NULL;
    RAMBlock *block = NULL;
    int total_count = 0;
    int index = 0;
    bool ret = false;

    RAMBLOCK_FOREACH_MIGRATABLE(block) {
        if (skip_sample_ramblock(block)) {
            continue;
        }
        total_count++;
    }

    dinfo = g_try_malloc0_n(total_count, sizeof(struct RamblockDirtyInfo));
    if (dinfo == NULL) {
        goto out;
    }

    RAMBLOCK_FOREACH_MIGRATABLE(block) {
        if (skip_sample_ramblock(block)) {
            continue;
        }
        if (index >= total_count) {
            break;
        }
        info = &dinfo[index];
        get_ramblock_dirty_info(block, info, &config);
        if (!save_ramblock_hash(info)) {
            goto out;
        }
        index++;
    }
    ret = true;

out:
    *block_count = index;
    *block_dinfo = dinfo;
    return ret;
}

static void calc_page_dirty_rate(struct RamblockDirtyInfo *info)
{
    uint32_t crc;
    int i;

    for (i = 0; i < info->sample_pages_count; i++) {
        crc = get_ramblock_vfn_hash(info, info->sample_page_vfn[i]);
        if (crc != info->hash_result[i]) {
            trace_calc_page_dirty_rate(info->idstr, crc, info->hash_result[i]);
            info->sample_dirty_count++;
        }
    }
}

static struct RamblockDirtyInfo *
find_block_matched(RAMBlock *block, int count,
                  struct RamblockDirtyInfo *infos)
{
    int i;
    struct RamblockDirtyInfo *matched;

    for (i = 0; i < count; i++) {
        if (!strcmp(infos[i].idstr, qemu_ram_get_idstr(block))) {
            break;
        }
    }

    if (i == count) {
        return NULL;
    }

    if (infos[i].ramblock_addr != qemu_ram_get_host_addr(block) ||
        infos[i].ramblock_pages !=
            (qemu_ram_get_used_length(block) >> TARGET_PAGE_BITS)) {
        trace_find_page_matched(block->idstr);
        return NULL;
    }

    matched = &infos[i];

    return matched;
}

static bool compare_page_hash_info(struct RamblockDirtyInfo *info,
                                  int block_count)
{
    struct RamblockDirtyInfo *block_dinfo = NULL;
    RAMBlock *block = NULL;

    RAMBLOCK_FOREACH_MIGRATABLE(block) {
        if (skip_sample_ramblock(block)) {
            continue;
        }
        block_dinfo = find_block_matched(block, block_count, info);
        if (block_dinfo == NULL) {
            continue;
        }
        calc_page_dirty_rate(block_dinfo);
        update_dirtyrate_stat(block_dinfo);
    }

    if (DirtyStat.page_sampling.total_sample_count == 0) {
        return false;
    }

    return true;
}

static inline void record_dirtypages_bitmap(DirtyPageRecord *dirty_pages,
                                            bool start)
{
    if (start) {
        dirty_pages->start_pages = total_dirty_pages;
    } else {
        dirty_pages->end_pages = total_dirty_pages;
    }
}

static inline void dirtyrate_manual_reset_protect(void)
{
    RAMBlock *block = NULL;

    WITH_RCU_READ_LOCK_GUARD() {
        RAMBLOCK_FOREACH_MIGRATABLE(block) {
            memory_region_clear_dirty_bitmap(block->mr, 0,
                                             block->used_length);
        }
    }
}

static void calculate_dirtyrate_dirty_bitmap(struct DirtyRateConfig config)
{
    int64_t msec = 0;
    int64_t start_time;
    DirtyPageRecord dirty_pages;

    qemu_mutex_lock_iothread();
    memory_global_dirty_log_start(GLOBAL_DIRTY_DIRTY_RATE);

    /*
     * 1'round of log sync may return all 1 bits with
     * KVM_DIRTY_LOG_INITIALLY_SET enable
     * skip it unconditionally and start dirty tracking
     * from 2'round of log sync
     */
    memory_global_dirty_log_sync();

    /*
     * reset page protect manually and unconditionally.
     * this make sure kvm dirty log be cleared if
     * KVM_DIRTY_LOG_MANUAL_PROTECT_ENABLE cap is enabled.
     */
    dirtyrate_manual_reset_protect();
    qemu_mutex_unlock_iothread();

    record_dirtypages_bitmap(&dirty_pages, true);

    start_time = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
    DirtyStat.start_time = start_time / 1000;

    msec = config.sample_period_seconds * 1000;
    msec = dirty_stat_wait(msec, start_time);
    DirtyStat.calc_time = msec / 1000;

    /*
     * do two things.
     * 1. fetch dirty bitmap from kvm
     * 2. stop dirty tracking
     */
    global_dirty_log_sync(GLOBAL_DIRTY_DIRTY_RATE, true);

    record_dirtypages_bitmap(&dirty_pages, false);

    DirtyStat.dirty_rate = do_calculate_dirtyrate(dirty_pages, msec);
}

static void calculate_dirtyrate_dirty_ring(struct DirtyRateConfig config)
{
    int64_t duration;
    uint64_t dirtyrate = 0;
    uint64_t dirtyrate_sum = 0;
    int i = 0;

    /* start log sync */
    global_dirty_log_change(GLOBAL_DIRTY_DIRTY_RATE, true);

    DirtyStat.start_time = qemu_clock_get_ms(QEMU_CLOCK_REALTIME) / 1000;

    /* calculate vcpu dirtyrate */
    duration = vcpu_calculate_dirtyrate(config.sample_period_seconds * 1000,
                                        &DirtyStat.dirty_ring,
                                        GLOBAL_DIRTY_DIRTY_RATE,
                                        true);

    DirtyStat.calc_time = duration / 1000;

    /* calculate vm dirtyrate */
    for (i = 0; i < DirtyStat.dirty_ring.nvcpu; i++) {
        dirtyrate = DirtyStat.dirty_ring.rates[i].dirty_rate;
        DirtyStat.dirty_ring.rates[i].dirty_rate = dirtyrate;
        dirtyrate_sum += dirtyrate;
    }

    DirtyStat.dirty_rate = dirtyrate_sum;
}

static void calculate_dirtyrate_sample_vm(struct DirtyRateConfig config)
{
    struct RamblockDirtyInfo *block_dinfo = NULL;
    int block_count = 0;
    int64_t msec = 0;
    int64_t initial_time;

    rcu_read_lock();
    initial_time = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
    if (!record_ramblock_hash_info(&block_dinfo, config, &block_count)) {
        goto out;
    }
    rcu_read_unlock();

    msec = config.sample_period_seconds * 1000;
    msec = dirty_stat_wait(msec, initial_time);
    DirtyStat.start_time = initial_time / 1000;
    DirtyStat.calc_time = msec / 1000;

    rcu_read_lock();
    if (!compare_page_hash_info(block_dinfo, block_count)) {
        goto out;
    }

    update_dirtyrate(msec);

out:
    rcu_read_unlock();
    free_ramblock_dirty_info(block_dinfo, block_count);
}

static void calculate_dirtyrate(struct DirtyRateConfig config)
{
    if (config.mode == DIRTY_RATE_MEASURE_MODE_DIRTY_BITMAP) {
        calculate_dirtyrate_dirty_bitmap(config);
    } else if (config.mode == DIRTY_RATE_MEASURE_MODE_DIRTY_RING) {
        calculate_dirtyrate_dirty_ring(config);
    } else {
        calculate_dirtyrate_sample_vm(config);
    }

    trace_dirtyrate_calculate(DirtyStat.dirty_rate);
}

void *get_dirtyrate_thread(void *arg)
{
    struct DirtyRateConfig config = *(struct DirtyRateConfig *)arg;
    int ret;
    rcu_register_thread();

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

    rcu_unregister_thread();
    return NULL;
}

void qmp_calc_dirty_rate(int64_t calc_time,
                         bool has_sample_pages,
                         int64_t sample_pages,
                         bool has_mode,
                         DirtyRateMeasureMode mode,
                         Error **errp)
{
    static struct DirtyRateConfig config;
    QemuThread thread;
    int ret;
    int64_t start_time;

    /*
     * If the dirty rate is already being measured, don't attempt to start.
     */
    if (qatomic_read(&CalculatingState) == DIRTY_RATE_STATUS_MEASURING) {
        error_setg(errp, "the dirty rate is already being measured.");
        return;
    }

    if (!is_sample_period_valid(calc_time)) {
        error_setg(errp, "calc-time is out of range[%d, %d].",
                         MIN_FETCH_DIRTYRATE_TIME_SEC,
                         MAX_FETCH_DIRTYRATE_TIME_SEC);
        return;
    }

    if (!has_mode) {
        mode =  DIRTY_RATE_MEASURE_MODE_PAGE_SAMPLING;
    }

    if (has_sample_pages && mode == DIRTY_RATE_MEASURE_MODE_DIRTY_RING) {
        error_setg(errp, "either sample-pages or dirty-ring can be specified.");
        return;
    }

    if (has_sample_pages) {
        if (!is_sample_pages_valid(sample_pages)) {
            error_setg(errp, "sample-pages is out of range[%d, %d].",
                            MIN_SAMPLE_PAGE_COUNT,
                            MAX_SAMPLE_PAGE_COUNT);
            return;
        }
    } else {
        sample_pages = DIRTYRATE_DEFAULT_SAMPLE_PAGES;
    }

    /*
     * dirty ring mode only works when kvm dirty ring is enabled.
     * on the contrary, dirty bitmap mode is not.
     */
    if (((mode == DIRTY_RATE_MEASURE_MODE_DIRTY_RING) &&
        !kvm_dirty_ring_enabled()) ||
        ((mode == DIRTY_RATE_MEASURE_MODE_DIRTY_BITMAP) &&
         kvm_dirty_ring_enabled())) {
        error_setg(errp, "mode %s is not enabled, use other method instead.",
                         DirtyRateMeasureMode_str(mode));
         return;
    }

    /*
     * Init calculation state as unstarted.
     */
    ret = dirtyrate_set_state(&CalculatingState, CalculatingState,
                              DIRTY_RATE_STATUS_UNSTARTED);
    if (ret == -1) {
        error_setg(errp, "init dirty rate calculation state failed.");
        return;
    }

    config.sample_period_seconds = calc_time;
    config.sample_pages_per_gigabytes = sample_pages;
    config.mode = mode;

    cleanup_dirtyrate_stat(config);

    /*
     * update dirty rate mode so that we can figure out what mode has
     * been used in last calculation
     **/
    dirtyrate_mode = mode;

    start_time = qemu_clock_get_ms(QEMU_CLOCK_REALTIME) / 1000;
    init_dirtyrate_stat(start_time, config);

    qemu_thread_create(&thread, "get_dirtyrate", get_dirtyrate_thread,
                       (void *)&config, QEMU_THREAD_DETACHED);
}

struct DirtyRateInfo *qmp_query_dirty_rate(Error **errp)
{
    return query_dirty_rate_info();
}

void hmp_info_dirty_rate(Monitor *mon, const QDict *qdict)
{
    DirtyRateInfo *info = query_dirty_rate_info();

    monitor_printf(mon, "Status: %s\n",
                   DirtyRateStatus_str(info->status));
    monitor_printf(mon, "Start Time: %"PRIi64" (ms)\n",
                   info->start_time);
    monitor_printf(mon, "Sample Pages: %"PRIu64" (per GB)\n",
                   info->sample_pages);
    monitor_printf(mon, "Period: %"PRIi64" (sec)\n",
                   info->calc_time);
    monitor_printf(mon, "Mode: %s\n",
                   DirtyRateMeasureMode_str(info->mode));
    monitor_printf(mon, "Dirty rate: ");
    if (info->has_dirty_rate) {
        monitor_printf(mon, "%"PRIi64" (MB/s)\n", info->dirty_rate);
        if (info->has_vcpu_dirty_rate) {
            DirtyRateVcpuList *rate, *head = info->vcpu_dirty_rate;
            for (rate = head; rate != NULL; rate = rate->next) {
                monitor_printf(mon, "vcpu[%"PRIi64"], Dirty rate: %"PRIi64
                               " (MB/s)\n", rate->value->id,
                               rate->value->dirty_rate);
            }
        }
    } else {
        monitor_printf(mon, "(not ready)\n");
    }

    qapi_free_DirtyRateVcpuList(info->vcpu_dirty_rate);
    g_free(info);
}

void hmp_calc_dirty_rate(Monitor *mon, const QDict *qdict)
{
    int64_t sec = qdict_get_try_int(qdict, "second", 0);
    int64_t sample_pages = qdict_get_try_int(qdict, "sample_pages_per_GB", -1);
    bool has_sample_pages = (sample_pages != -1);
    bool dirty_ring = qdict_get_try_bool(qdict, "dirty_ring", false);
    bool dirty_bitmap = qdict_get_try_bool(qdict, "dirty_bitmap", false);
    DirtyRateMeasureMode mode = DIRTY_RATE_MEASURE_MODE_PAGE_SAMPLING;
    Error *err = NULL;

    if (!sec) {
        monitor_printf(mon, "Incorrect period length specified!\n");
        return;
    }

    if (dirty_ring && dirty_bitmap) {
        monitor_printf(mon, "Either dirty ring or dirty bitmap "
                       "can be specified!\n");
        return;
    }

    if (dirty_bitmap) {
        mode = DIRTY_RATE_MEASURE_MODE_DIRTY_BITMAP;
    } else if (dirty_ring) {
        mode = DIRTY_RATE_MEASURE_MODE_DIRTY_RING;
    }

    qmp_calc_dirty_rate(sec, has_sample_pages, sample_pages, true,
                        mode, &err);
    if (err) {
        hmp_handle_error(mon, err);
        return;
    }

    monitor_printf(mon, "Starting dirty rate measurement with period %"PRIi64
                   " seconds\n", sec);
    monitor_printf(mon, "[Please use 'info dirty_rate' to check results]\n");
}
