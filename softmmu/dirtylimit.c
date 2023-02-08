/*
 * Dirty page rate limit implementation code
 *
 * Copyright (c) 2022 CHINA TELECOM CO.,LTD.
 *
 * Authors:
 *  Hyman Huang(黄勇) <huangy81@chinatelecom.cn>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qapi/qapi-commands-migration.h"
#include "qapi/qmp/qdict.h"
#include "qapi/error.h"
#include "sysemu/dirtyrate.h"
#include "sysemu/dirtylimit.h"
#include "monitor/hmp.h"
#include "monitor/monitor.h"
#include "exec/memory.h"
#include "hw/boards.h"
#include "sysemu/kvm.h"
#include "trace.h"

/*
 * Dirtylimit stop working if dirty page rate error
 * value less than DIRTYLIMIT_TOLERANCE_RANGE
 */
#define DIRTYLIMIT_TOLERANCE_RANGE  25  /* MB/s */
/*
 * Plus or minus vcpu sleep time linearly if dirty
 * page rate error value percentage over
 * DIRTYLIMIT_LINEAR_ADJUSTMENT_PCT.
 * Otherwise, plus or minus a fixed vcpu sleep time.
 */
#define DIRTYLIMIT_LINEAR_ADJUSTMENT_PCT     50
/*
 * Max vcpu sleep time percentage during a cycle
 * composed of dirty ring full and sleep time.
 */
#define DIRTYLIMIT_THROTTLE_PCT_MAX 99

struct {
    VcpuStat stat;
    bool running;
    QemuThread thread;
} *vcpu_dirty_rate_stat;

typedef struct VcpuDirtyLimitState {
    int cpu_index;
    bool enabled;
    /*
     * Quota dirty page rate, unit is MB/s
     * zero if not enabled.
     */
    uint64_t quota;
} VcpuDirtyLimitState;

struct {
    VcpuDirtyLimitState *states;
    /* Max cpus number configured by user */
    int max_cpus;
    /* Number of vcpu under dirtylimit */
    int limited_nvcpu;
} *dirtylimit_state;

/* protect dirtylimit_state */
static QemuMutex dirtylimit_mutex;

/* dirtylimit thread quit if dirtylimit_quit is true */
static bool dirtylimit_quit;

static void vcpu_dirty_rate_stat_collect(void)
{
    VcpuStat stat;
    int i = 0;

    /* calculate vcpu dirtyrate */
    vcpu_calculate_dirtyrate(DIRTYLIMIT_CALC_TIME_MS,
                             &stat,
                             GLOBAL_DIRTY_LIMIT,
                             false);

    for (i = 0; i < stat.nvcpu; i++) {
        vcpu_dirty_rate_stat->stat.rates[i].id = i;
        vcpu_dirty_rate_stat->stat.rates[i].dirty_rate =
            stat.rates[i].dirty_rate;
    }

    free(stat.rates);
}

static void *vcpu_dirty_rate_stat_thread(void *opaque)
{
    rcu_register_thread();

    /* start log sync */
    global_dirty_log_change(GLOBAL_DIRTY_LIMIT, true);

    while (qatomic_read(&vcpu_dirty_rate_stat->running)) {
        vcpu_dirty_rate_stat_collect();
        if (dirtylimit_in_service()) {
            dirtylimit_process();
        }
    }

    /* stop log sync */
    global_dirty_log_change(GLOBAL_DIRTY_LIMIT, false);

    rcu_unregister_thread();
    return NULL;
}

int64_t vcpu_dirty_rate_get(int cpu_index)
{
    DirtyRateVcpu *rates = vcpu_dirty_rate_stat->stat.rates;
    return qatomic_read_i64(&rates[cpu_index].dirty_rate);
}

void vcpu_dirty_rate_stat_start(void)
{
    if (qatomic_read(&vcpu_dirty_rate_stat->running)) {
        return;
    }

    qatomic_set(&vcpu_dirty_rate_stat->running, 1);
    qemu_thread_create(&vcpu_dirty_rate_stat->thread,
                       "dirtyrate-stat",
                       vcpu_dirty_rate_stat_thread,
                       NULL,
                       QEMU_THREAD_JOINABLE);
}

void vcpu_dirty_rate_stat_stop(void)
{
    qatomic_set(&vcpu_dirty_rate_stat->running, 0);
    dirtylimit_state_unlock();
    qemu_mutex_unlock_iothread();
    qemu_thread_join(&vcpu_dirty_rate_stat->thread);
    qemu_mutex_lock_iothread();
    dirtylimit_state_lock();
}

void vcpu_dirty_rate_stat_initialize(void)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    int max_cpus = ms->smp.max_cpus;

    vcpu_dirty_rate_stat =
        g_malloc0(sizeof(*vcpu_dirty_rate_stat));

    vcpu_dirty_rate_stat->stat.nvcpu = max_cpus;
    vcpu_dirty_rate_stat->stat.rates =
        g_new0(DirtyRateVcpu, max_cpus);

    vcpu_dirty_rate_stat->running = false;
}

void vcpu_dirty_rate_stat_finalize(void)
{
    free(vcpu_dirty_rate_stat->stat.rates);
    vcpu_dirty_rate_stat->stat.rates = NULL;

    free(vcpu_dirty_rate_stat);
    vcpu_dirty_rate_stat = NULL;
}

void dirtylimit_state_lock(void)
{
    qemu_mutex_lock(&dirtylimit_mutex);
}

void dirtylimit_state_unlock(void)
{
    qemu_mutex_unlock(&dirtylimit_mutex);
}

static void
__attribute__((__constructor__)) dirtylimit_mutex_init(void)
{
    qemu_mutex_init(&dirtylimit_mutex);
}

static inline VcpuDirtyLimitState *dirtylimit_vcpu_get_state(int cpu_index)
{
    return &dirtylimit_state->states[cpu_index];
}

void dirtylimit_state_initialize(void)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    int max_cpus = ms->smp.max_cpus;
    int i;

    dirtylimit_state = g_malloc0(sizeof(*dirtylimit_state));

    dirtylimit_state->states =
            g_new0(VcpuDirtyLimitState, max_cpus);

    for (i = 0; i < max_cpus; i++) {
        dirtylimit_state->states[i].cpu_index = i;
    }

    dirtylimit_state->max_cpus = max_cpus;
    trace_dirtylimit_state_initialize(max_cpus);
}

void dirtylimit_state_finalize(void)
{
    free(dirtylimit_state->states);
    dirtylimit_state->states = NULL;

    free(dirtylimit_state);
    dirtylimit_state = NULL;

    trace_dirtylimit_state_finalize();
}

bool dirtylimit_in_service(void)
{
    return !!dirtylimit_state;
}

bool dirtylimit_vcpu_index_valid(int cpu_index)
{
    MachineState *ms = MACHINE(qdev_get_machine());

    return !(cpu_index < 0 ||
             cpu_index >= ms->smp.max_cpus);
}

static inline int64_t dirtylimit_dirty_ring_full_time(uint64_t dirtyrate)
{
    static uint64_t max_dirtyrate;
    uint32_t dirty_ring_size = kvm_dirty_ring_size();
    uint64_t dirty_ring_size_meory_MB =
        dirty_ring_size * TARGET_PAGE_SIZE >> 20;

    if (max_dirtyrate < dirtyrate) {
        max_dirtyrate = dirtyrate;
    }

    return dirty_ring_size_meory_MB * 1000000 / max_dirtyrate;
}

static inline bool dirtylimit_done(uint64_t quota,
                                   uint64_t current)
{
    uint64_t min, max;

    min = MIN(quota, current);
    max = MAX(quota, current);

    return ((max - min) <= DIRTYLIMIT_TOLERANCE_RANGE) ? true : false;
}

static inline bool
dirtylimit_need_linear_adjustment(uint64_t quota,
                                  uint64_t current)
{
    uint64_t min, max;

    min = MIN(quota, current);
    max = MAX(quota, current);

    return ((max - min) * 100 / max) > DIRTYLIMIT_LINEAR_ADJUSTMENT_PCT;
}

static void dirtylimit_set_throttle(CPUState *cpu,
                                    uint64_t quota,
                                    uint64_t current)
{
    int64_t ring_full_time_us = 0;
    uint64_t sleep_pct = 0;
    uint64_t throttle_us = 0;

    if (current == 0) {
        cpu->throttle_us_per_full = 0;
        return;
    }

    ring_full_time_us = dirtylimit_dirty_ring_full_time(current);

    if (dirtylimit_need_linear_adjustment(quota, current)) {
        if (quota < current) {
            sleep_pct = (current - quota) * 100 / current;
            throttle_us =
                ring_full_time_us * sleep_pct / (double)(100 - sleep_pct);
            cpu->throttle_us_per_full += throttle_us;
        } else {
            sleep_pct = (quota - current) * 100 / quota;
            throttle_us =
                ring_full_time_us * sleep_pct / (double)(100 - sleep_pct);
            cpu->throttle_us_per_full -= throttle_us;
        }

        trace_dirtylimit_throttle_pct(cpu->cpu_index,
                                      sleep_pct,
                                      throttle_us);
    } else {
        if (quota < current) {
            cpu->throttle_us_per_full += ring_full_time_us / 10;
        } else {
            cpu->throttle_us_per_full -= ring_full_time_us / 10;
        }
    }

    /*
     * TODO: in the big kvm_dirty_ring_size case (eg: 65536, or other scenario),
     *       current dirty page rate may never reach the quota, we should stop
     *       increasing sleep time?
     */
    cpu->throttle_us_per_full = MIN(cpu->throttle_us_per_full,
        ring_full_time_us * DIRTYLIMIT_THROTTLE_PCT_MAX);

    cpu->throttle_us_per_full = MAX(cpu->throttle_us_per_full, 0);
}

static void dirtylimit_adjust_throttle(CPUState *cpu)
{
    uint64_t quota = 0;
    uint64_t current = 0;
    int cpu_index = cpu->cpu_index;

    quota = dirtylimit_vcpu_get_state(cpu_index)->quota;
    current = vcpu_dirty_rate_get(cpu_index);

    if (!dirtylimit_done(quota, current)) {
        dirtylimit_set_throttle(cpu, quota, current);
    }

    return;
}

void dirtylimit_process(void)
{
    CPUState *cpu;

    if (!qatomic_read(&dirtylimit_quit)) {
        dirtylimit_state_lock();

        if (!dirtylimit_in_service()) {
            dirtylimit_state_unlock();
            return;
        }

        CPU_FOREACH(cpu) {
            if (!dirtylimit_vcpu_get_state(cpu->cpu_index)->enabled) {
                continue;
            }
            dirtylimit_adjust_throttle(cpu);
        }
        dirtylimit_state_unlock();
    }
}

void dirtylimit_change(bool start)
{
    if (start) {
        qatomic_set(&dirtylimit_quit, 0);
    } else {
        qatomic_set(&dirtylimit_quit, 1);
    }
}

void dirtylimit_set_vcpu(int cpu_index,
                         uint64_t quota,
                         bool enable)
{
    trace_dirtylimit_set_vcpu(cpu_index, quota);

    if (enable) {
        dirtylimit_state->states[cpu_index].quota = quota;
        if (!dirtylimit_vcpu_get_state(cpu_index)->enabled) {
            dirtylimit_state->limited_nvcpu++;
        }
    } else {
        dirtylimit_state->states[cpu_index].quota = 0;
        if (dirtylimit_state->states[cpu_index].enabled) {
            dirtylimit_state->limited_nvcpu--;
        }
    }

    dirtylimit_state->states[cpu_index].enabled = enable;
}

void dirtylimit_set_all(uint64_t quota,
                        bool enable)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    int max_cpus = ms->smp.max_cpus;
    int i;

    for (i = 0; i < max_cpus; i++) {
        dirtylimit_set_vcpu(i, quota, enable);
    }
}

void dirtylimit_vcpu_execute(CPUState *cpu)
{
    if (dirtylimit_in_service() &&
        dirtylimit_vcpu_get_state(cpu->cpu_index)->enabled &&
        cpu->throttle_us_per_full) {
        trace_dirtylimit_vcpu_execute(cpu->cpu_index,
                cpu->throttle_us_per_full);
        usleep(cpu->throttle_us_per_full);
    }
}

static void dirtylimit_init(void)
{
    dirtylimit_state_initialize();
    dirtylimit_change(true);
    vcpu_dirty_rate_stat_initialize();
    vcpu_dirty_rate_stat_start();
}

static void dirtylimit_cleanup(void)
{
    vcpu_dirty_rate_stat_stop();
    vcpu_dirty_rate_stat_finalize();
    dirtylimit_change(false);
    dirtylimit_state_finalize();
}

void qmp_cancel_vcpu_dirty_limit(bool has_cpu_index,
                                 int64_t cpu_index,
                                 Error **errp)
{
    if (!kvm_enabled() || !kvm_dirty_ring_enabled()) {
        return;
    }

    if (has_cpu_index && !dirtylimit_vcpu_index_valid(cpu_index)) {
        error_setg(errp, "incorrect cpu index specified");
        return;
    }

    if (!dirtylimit_in_service()) {
        return;
    }

    dirtylimit_state_lock();

    if (has_cpu_index) {
        dirtylimit_set_vcpu(cpu_index, 0, false);
    } else {
        dirtylimit_set_all(0, false);
    }

    if (!dirtylimit_state->limited_nvcpu) {
        dirtylimit_cleanup();
    }

    dirtylimit_state_unlock();
}

void hmp_cancel_vcpu_dirty_limit(Monitor *mon, const QDict *qdict)
{
    int64_t cpu_index = qdict_get_try_int(qdict, "cpu_index", -1);
    Error *err = NULL;

    qmp_cancel_vcpu_dirty_limit(!!(cpu_index != -1), cpu_index, &err);
    if (err) {
        hmp_handle_error(mon, err);
        return;
    }

    monitor_printf(mon, "[Please use 'info vcpu_dirty_limit' to query "
                   "dirty limit for virtual CPU]\n");
}

void qmp_set_vcpu_dirty_limit(bool has_cpu_index,
                              int64_t cpu_index,
                              uint64_t dirty_rate,
                              Error **errp)
{
    if (!kvm_enabled() || !kvm_dirty_ring_enabled()) {
        error_setg(errp, "dirty page limit feature requires KVM with"
                   " accelerator property 'dirty-ring-size' set'");
        return;
    }

    if (has_cpu_index && !dirtylimit_vcpu_index_valid(cpu_index)) {
        error_setg(errp, "incorrect cpu index specified");
        return;
    }

    if (!dirty_rate) {
        qmp_cancel_vcpu_dirty_limit(has_cpu_index, cpu_index, errp);
        return;
    }

    dirtylimit_state_lock();

    if (!dirtylimit_in_service()) {
        dirtylimit_init();
    }

    if (has_cpu_index) {
        dirtylimit_set_vcpu(cpu_index, dirty_rate, true);
    } else {
        dirtylimit_set_all(dirty_rate, true);
    }

    dirtylimit_state_unlock();
}

void hmp_set_vcpu_dirty_limit(Monitor *mon, const QDict *qdict)
{
    int64_t dirty_rate = qdict_get_int(qdict, "dirty_rate");
    int64_t cpu_index = qdict_get_try_int(qdict, "cpu_index", -1);
    Error *err = NULL;

    qmp_set_vcpu_dirty_limit(!!(cpu_index != -1), cpu_index, dirty_rate, &err);
    if (err) {
        hmp_handle_error(mon, err);
        return;
    }

    monitor_printf(mon, "[Please use 'info vcpu_dirty_limit' to query "
                   "dirty limit for virtual CPU]\n");
}

static struct DirtyLimitInfo *dirtylimit_query_vcpu(int cpu_index)
{
    DirtyLimitInfo *info = NULL;

    info = g_malloc0(sizeof(*info));
    info->cpu_index = cpu_index;
    info->limit_rate = dirtylimit_vcpu_get_state(cpu_index)->quota;
    info->current_rate = vcpu_dirty_rate_get(cpu_index);

    return info;
}

static struct DirtyLimitInfoList *dirtylimit_query_all(void)
{
    int i, index;
    DirtyLimitInfo *info = NULL;
    DirtyLimitInfoList *head = NULL, **tail = &head;

    dirtylimit_state_lock();

    if (!dirtylimit_in_service()) {
        dirtylimit_state_unlock();
        return NULL;
    }

    for (i = 0; i < dirtylimit_state->max_cpus; i++) {
        index = dirtylimit_state->states[i].cpu_index;
        if (dirtylimit_vcpu_get_state(index)->enabled) {
            info = dirtylimit_query_vcpu(index);
            QAPI_LIST_APPEND(tail, info);
        }
    }

    dirtylimit_state_unlock();

    return head;
}

struct DirtyLimitInfoList *qmp_query_vcpu_dirty_limit(Error **errp)
{
    if (!dirtylimit_in_service()) {
        return NULL;
    }

    return dirtylimit_query_all();
}

void hmp_info_vcpu_dirty_limit(Monitor *mon, const QDict *qdict)
{
    DirtyLimitInfoList *limit, *head, *info = NULL;
    Error *err = NULL;

    if (!dirtylimit_in_service()) {
        monitor_printf(mon, "Dirty page limit not enabled!\n");
        return;
    }

    info = qmp_query_vcpu_dirty_limit(&err);
    if (err) {
        hmp_handle_error(mon, err);
        return;
    }

    head = info;
    for (limit = head; limit != NULL; limit = limit->next) {
        monitor_printf(mon, "vcpu[%"PRIi64"], limit rate %"PRIi64 " (MB/s),"
                            " current rate %"PRIi64 " (MB/s)\n",
                            limit->value->cpu_index,
                            limit->value->limit_rate,
                            limit->value->current_rate);
    }

    g_free(info);
}
