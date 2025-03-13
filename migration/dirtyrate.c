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
#include "qemu/error-report.h"
#include "hw/core/cpu.h"
#include "qapi/error.h"
#include "system/ramblock.h"
#include "exec/target_page.h"
#include "qemu/rcu_queue.h"
#include "qemu/main-loop.h"
#include "qapi/qapi-commands-migration.h"
#include "ram.h"
#include "trace.h"
#include "dirtyrate.h"
#include "monitor/hmp.h"
#include "monitor/monitor.h"
#include "qobject/qdict.h"
#include "system/kvm.h"
#include "system/runstate.h"
#include "system/memory.h"
#include "qemu/xxhash.h"
#include "migration.h"

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
        /* g_usleep may overshoot */
        msec = qemu_clock_get_ms(QEMU_CLOCK_REALTIME) - initial_time;
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
    uint64_t increased_dirty_pages =
        dirty_pages.end_pages - dirty_pages.start_pages;

    /*
     * multiply by 1000ms/s _before_ converting down to megabytes
     * to avoid losing precision
     */
    return qemu_target_pages_to_MiB(increased_dirty_pages * 1000) /
        calc_time_ms;
}

void global_dirty_log_change(unsigned int flag, bool start)
{
    Error *local_err = NULL;
    bool ret;

    bql_lock();
    if (start) {
        ret = memory_global_dirty_log_start(flag, &local_err);
        if (!ret) {
            error_report_err(local_err);
        }
    } else {
        memory_global_dirty_log_stop(flag);
    }
    bql_unlock();
}

/*
 * global_dirty_log_sync
 * 1. sync dirty log from kvm
 * 2. stop dirty tracking if needed.
 */
static void global_dirty_log_sync(unsigned int flag, bool one_shot)
{
    bql_lock();
    memory_global_dirty_log_sync(false);
    if (one_shot) {
        memory_global_dirty_log_stop(flag);
    }
    bql_unlock();
}

static DirtyPageRecord *vcpu_dirty_stat_alloc(VcpuStat *stat)
{
    CPUState *cpu;
    int nvcpu = 0;

    CPU_FOREACH(cpu) {
        nvcpu++;
    }

    stat->nvcpu = nvcpu;
    stat->rates = g_new0(DirtyRateVcpu, nvcpu);

    return g_new0(DirtyPageRecord, nvcpu);
}

static void vcpu_dirty_stat_collect(DirtyPageRecord *records,
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
    DirtyPageRecord *records = NULL;
    int64_t init_time_ms;
    int64_t duration;
    int64_t dirtyrate;
    int i = 0;
    unsigned int gen_id = 0;

retry:
    init_time_ms = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);

    WITH_QEMU_LOCK_GUARD(&qemu_cpu_list_lock) {
        gen_id = cpu_list_generation_id_get();
        records = vcpu_dirty_stat_alloc(stat);
        vcpu_dirty_stat_collect(records, true);
    }

    duration = dirty_stat_wait(calc_time_ms, init_time_ms);

    global_dirty_log_sync(flag, one_shot);

    WITH_QEMU_LOCK_GUARD(&qemu_cpu_list_lock) {
        if (gen_id != cpu_list_generation_id_get()) {
            g_free(records);
            g_free(stat->rates);
            cpu_list_unlock();
            goto retry;
        }
        vcpu_dirty_stat_collect(records, false);
    }

    for (i = 0; i < stat->nvcpu; i++) {
        dirtyrate = do_calculate_dirtyrate(records[i], duration);

        stat->rates[i].id = i;
        stat->rates[i].dirty_rate = dirtyrate;

        trace_dirtyrate_do_calculate_vcpu(i, dirtyrate);
    }

    g_free(records);

    return duration;
}

static bool is_calc_time_valid(int64_t msec)
{
    if ((msec < MIN_CALC_TIME_MS) || (msec > MAX_CALC_TIME_MS)) {
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

/* Decimal power of given time unit relative to one second */
static int time_unit_to_power(TimeUnit time_unit)
{
    switch (time_unit) {
    case TIME_UNIT_SECOND:
        return 0;
    case TIME_UNIT_MILLISECOND:
        return -3;
    default:
        g_assert_not_reached();
    }
}

static int64_t convert_time_unit(int64_t value, TimeUnit unit_from,
                                 TimeUnit unit_to)
{
    int power = time_unit_to_power(unit_from) -
                time_unit_to_power(unit_to);
    while (power < 0) {
        value /= 10;
        power += 1;
    }
    while (power > 0) {
        value *= 10;
        power -= 1;
    }
    return value;
}


static struct DirtyRateInfo *
query_dirty_rate_info(TimeUnit calc_time_unit)
{
    int i;
    int64_t dirty_rate = DirtyStat.dirty_rate;
    struct DirtyRateInfo *info = g_new0(DirtyRateInfo, 1);
    DirtyRateVcpuList *head = NULL, **tail = &head;

    info->status = CalculatingState;
    info->start_time = DirtyStat.start_time;
    info->calc_time = convert_time_unit(DirtyStat.calc_time_ms,
                                        TIME_UNIT_MILLISECOND,
                                        calc_time_unit);
    info->calc_time_unit = calc_time_unit;
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

static void init_dirtyrate_stat(struct DirtyRateConfig config)
{
    DirtyStat.dirty_rate = -1;
    DirtyStat.start_time = qemu_clock_get_ms(QEMU_CLOCK_HOST) / 1000;
    DirtyStat.calc_time_ms = config.calc_time_ms;
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
    DirtyStat.page_sampling.total_block_mem_MB +=
        qemu_target_pages_to_MiB(info->ramblock_pages);
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
 * Compute hash of a single page of size TARGET_PAGE_SIZE.
 */
static uint32_t compute_page_hash(void *ptr)
{
    size_t page_size = qemu_target_page_size();
    uint32_t i;
    uint64_t v1, v2, v3, v4;
    uint64_t res;
    const uint64_t *p = ptr;

    v1 = QEMU_XXHASH_SEED + XXH_PRIME64_1 + XXH_PRIME64_2;
    v2 = QEMU_XXHASH_SEED + XXH_PRIME64_2;
    v3 = QEMU_XXHASH_SEED + 0;
    v4 = QEMU_XXHASH_SEED - XXH_PRIME64_1;
    for (i = 0; i < page_size / 8; i += 4) {
        v1 = XXH64_round(v1, p[i + 0]);
        v2 = XXH64_round(v2, p[i + 1]);
        v3 = XXH64_round(v3, p[i + 2]);
        v4 = XXH64_round(v4, p[i + 3]);
    }
    res = XXH64_mergerounds(v1, v2, v3, v4);
    res += page_size;
    res = XXH64_avalanche(res);
    return (uint32_t)(res & UINT32_MAX);
}


/*
 * get hash result for the sampled memory with length of TARGET_PAGE_SIZE
 * in ramblock, which starts from ramblock base address.
 */
static uint32_t get_ramblock_vfn_hash(struct RamblockDirtyInfo *info,
                                      uint64_t vfn)
{
    uint32_t hash;

    hash = compute_page_hash(info->ramblock_addr +
                             vfn * qemu_target_page_size());

    trace_get_ramblock_vfn_hash(info->idstr, vfn, hash);
    return hash;
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
    gsize len;

    /* Right shift 30 bits to calc ramblock size in GB */
    info->sample_pages_count = (qemu_ram_get_used_length(block) *
                                sample_pages_per_gigabytes) >> 30;
    /* Right shift TARGET_PAGE_BITS to calc page count */
    info->ramblock_pages = qemu_ram_get_used_length(block) >>
                           qemu_target_page_bits();
    info->ramblock_addr = qemu_ram_get_host_addr(block);
    len = g_strlcpy(info->idstr, qemu_ram_get_idstr(block),
                    sizeof(info->idstr));
    g_assert(len < sizeof(info->idstr));
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
    uint32_t hash;
    int i;

    for (i = 0; i < info->sample_pages_count; i++) {
        hash = get_ramblock_vfn_hash(info, info->sample_page_vfn[i]);
        if (hash != info->hash_result[i]) {
            trace_calc_page_dirty_rate(info->idstr, hash, info->hash_result[i]);
            info->sample_dirty_count++;
        }
    }
}

static struct RamblockDirtyInfo *
find_block_matched(RAMBlock *block, int count,
                  struct RamblockDirtyInfo *infos)
{
    int i;

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
            (qemu_ram_get_used_length(block) >> qemu_target_page_bits())) {
        trace_find_page_matched(block->idstr);
        return NULL;
    }

    return &infos[i];
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
    int64_t start_time;
    DirtyPageRecord dirty_pages;
    Error *local_err = NULL;

    bql_lock();
    if (!memory_global_dirty_log_start(GLOBAL_DIRTY_DIRTY_RATE, &local_err)) {
        error_report_err(local_err);
    }

    /*
     * 1'round of log sync may return all 1 bits with
     * KVM_DIRTY_LOG_INITIALLY_SET enable
     * skip it unconditionally and start dirty tracking
     * from 2'round of log sync
     */
    memory_global_dirty_log_sync(false);

    /*
     * reset page protect manually and unconditionally.
     * this make sure kvm dirty log be cleared if
     * KVM_DIRTY_LOG_MANUAL_PROTECT_ENABLE cap is enabled.
     */
    dirtyrate_manual_reset_protect();
    bql_unlock();

    record_dirtypages_bitmap(&dirty_pages, true);

    start_time = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
    DirtyStat.start_time = qemu_clock_get_ms(QEMU_CLOCK_HOST) / 1000;

    DirtyStat.calc_time_ms = dirty_stat_wait(config.calc_time_ms, start_time);

    /*
     * do two things.
     * 1. fetch dirty bitmap from kvm
     * 2. stop dirty tracking
     */
    global_dirty_log_sync(GLOBAL_DIRTY_DIRTY_RATE, true);

    record_dirtypages_bitmap(&dirty_pages, false);

    DirtyStat.dirty_rate = do_calculate_dirtyrate(dirty_pages,
                                                  DirtyStat.calc_time_ms);
}

static void calculate_dirtyrate_dirty_ring(struct DirtyRateConfig config)
{
    uint64_t dirtyrate = 0;
    uint64_t dirtyrate_sum = 0;
    int i = 0;

    /* start log sync */
    global_dirty_log_change(GLOBAL_DIRTY_DIRTY_RATE, true);

    DirtyStat.start_time = qemu_clock_get_ms(QEMU_CLOCK_HOST) / 1000;

    /* calculate vcpu dirtyrate */
    DirtyStat.calc_time_ms = vcpu_calculate_dirtyrate(config.calc_time_ms,
                                                      &DirtyStat.dirty_ring,
                                                      GLOBAL_DIRTY_DIRTY_RATE,
                                                      true);

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
    int64_t initial_time;

    rcu_read_lock();
    initial_time = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
    DirtyStat.start_time = qemu_clock_get_ms(QEMU_CLOCK_HOST) / 1000;
    if (!record_ramblock_hash_info(&block_dinfo, config, &block_count)) {
        goto out;
    }
    rcu_read_unlock();

    DirtyStat.calc_time_ms = dirty_stat_wait(config.calc_time_ms,
                                             initial_time);

    rcu_read_lock();
    if (!compare_page_hash_info(block_dinfo, block_count)) {
        goto out;
    }

    update_dirtyrate(DirtyStat.calc_time_ms);

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
                         bool has_calc_time_unit,
                         TimeUnit calc_time_unit,
                         bool has_sample_pages,
                         int64_t sample_pages,
                         bool has_mode,
                         DirtyRateMeasureMode mode,
                         Error **errp)
{
    static struct DirtyRateConfig config;
    QemuThread thread;
    int ret;

    /*
     * If the dirty rate is already being measured, don't attempt to start.
     */
    if (qatomic_read(&CalculatingState) == DIRTY_RATE_STATUS_MEASURING) {
        error_setg(errp, "the dirty rate is already being measured.");
        return;
    }

    int64_t calc_time_ms = convert_time_unit(
        calc_time,
        has_calc_time_unit ? calc_time_unit : TIME_UNIT_SECOND,
        TIME_UNIT_MILLISECOND
    );

    if (!is_calc_time_valid(calc_time_ms)) {
        error_setg(errp, "Calculation time is out of range [%dms, %dms].",
                         MIN_CALC_TIME_MS, MAX_CALC_TIME_MS);
        return;
    }

    if (!has_mode) {
        mode =  DIRTY_RATE_MEASURE_MODE_PAGE_SAMPLING;
    }

    if (has_sample_pages && mode != DIRTY_RATE_MEASURE_MODE_PAGE_SAMPLING) {
        error_setg(errp, "sample-pages is used only in page-sampling mode");
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

    config.calc_time_ms = calc_time_ms;
    config.sample_pages_per_gigabytes = sample_pages;
    config.mode = mode;

    cleanup_dirtyrate_stat(config);

    /*
     * update dirty rate mode so that we can figure out what mode has
     * been used in last calculation
     **/
    dirtyrate_mode = mode;

    init_dirtyrate_stat(config);

    qemu_thread_create(&thread, MIGRATION_THREAD_DIRTY_RATE,
                       get_dirtyrate_thread, (void *)&config,
                       QEMU_THREAD_DETACHED);
}


struct DirtyRateInfo *qmp_query_dirty_rate(bool has_calc_time_unit,
                                           TimeUnit calc_time_unit,
                                           Error **errp)
{
    return query_dirty_rate_info(
        has_calc_time_unit ? calc_time_unit : TIME_UNIT_SECOND);
}

void hmp_info_dirty_rate(Monitor *mon, const QDict *qdict)
{
    DirtyRateInfo *info = query_dirty_rate_info(TIME_UNIT_SECOND);

    monitor_printf(mon, "Status: %s\n",
                   DirtyRateStatus_str(info->status));
    monitor_printf(mon, "Start Time: %"PRIi64" (ms)\n",
                   info->start_time);
    if (info->mode == DIRTY_RATE_MEASURE_MODE_PAGE_SAMPLING) {
        monitor_printf(mon, "Sample Pages: %"PRIu64" (per GB)\n",
                       info->sample_pages);
    }
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

    qmp_calc_dirty_rate(sec, /* calc-time */
                        false, TIME_UNIT_SECOND, /* calc-time-unit */
                        has_sample_pages, sample_pages,
                        true, mode,
                        &err);
    if (err) {
        hmp_handle_error(mon, err);
        return;
    }

    monitor_printf(mon, "Starting dirty rate measurement with period %"PRIi64
                   " seconds\n", sec);
    monitor_printf(mon, "[Please use 'info dirty_rate' to check results]\n");
}
