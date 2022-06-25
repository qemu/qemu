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
#include "qapi/error.h"
#include "qemu/main-loop.h"
#include "qapi/qapi-commands-migration.h"
#include "sysemu/dirtyrate.h"
#include "sysemu/dirtylimit.h"
#include "exec/memory.h"
#include "hw/boards.h"

struct {
    VcpuStat stat;
    bool running;
    QemuThread thread;
} *vcpu_dirty_rate_stat;

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
    qemu_mutex_unlock_iothread();
    qemu_thread_join(&vcpu_dirty_rate_stat->thread);
    qemu_mutex_lock_iothread();
}

void vcpu_dirty_rate_stat_initialize(void)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    int max_cpus = ms->smp.max_cpus;

    vcpu_dirty_rate_stat =
        g_malloc0(sizeof(*vcpu_dirty_rate_stat));

    vcpu_dirty_rate_stat->stat.nvcpu = max_cpus;
    vcpu_dirty_rate_stat->stat.rates =
        g_malloc0(sizeof(DirtyRateVcpu) * max_cpus);

    vcpu_dirty_rate_stat->running = false;
}

void vcpu_dirty_rate_stat_finalize(void)
{
    free(vcpu_dirty_rate_stat->stat.rates);
    vcpu_dirty_rate_stat->stat.rates = NULL;

    free(vcpu_dirty_rate_stat);
    vcpu_dirty_rate_stat = NULL;
}
