/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/thread.h"
#include "hw/core/cpu.h"
#include "qemu/main-loop.h"
#include "system/cpus.h"
#include "system/cpu-throttle.h"
#include "migration.h"
#include "migration-stats.h"
#include "trace.h"

/* vcpu throttling controls */
static QEMUTimer *throttle_timer, *throttle_dirty_sync_timer;
static unsigned int throttle_percentage;
static bool throttle_dirty_sync_timer_active;
static uint64_t throttle_dirty_sync_count_prev;

#define CPU_THROTTLE_PCT_MIN 1
#define CPU_THROTTLE_PCT_MAX 99
#define CPU_THROTTLE_TIMESLICE_NS 10000000

/* Making sure RAMBlock dirty bitmap is synchronized every five seconds */
#define CPU_THROTTLE_DIRTY_SYNC_TIMESLICE_MS 5000

static void cpu_throttle_thread(CPUState *cpu, run_on_cpu_data opaque)
{
    double pct;
    double throttle_ratio;
    int64_t sleeptime_ns, endtime_ns;

    if (!cpu_throttle_get_percentage()) {
        return;
    }

    pct = (double)cpu_throttle_get_percentage() / 100;
    throttle_ratio = pct / (1 - pct);
    /* Add 1ns to fix double's rounding error (like 0.9999999...) */
    sleeptime_ns = (int64_t)(throttle_ratio * CPU_THROTTLE_TIMESLICE_NS + 1);
    endtime_ns = qemu_clock_get_ns(QEMU_CLOCK_REALTIME) + sleeptime_ns;
    while (sleeptime_ns > 0 && !cpu->stop) {
        if (sleeptime_ns > SCALE_MS) {
            qemu_cond_timedwait_bql(cpu->halt_cond,
                                         sleeptime_ns / SCALE_MS);
        } else {
            bql_unlock();
            g_usleep(sleeptime_ns / SCALE_US);
            bql_lock();
        }
        sleeptime_ns = endtime_ns - qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    }
    qatomic_set(&cpu->throttle_thread_scheduled, 0);
}

static void cpu_throttle_timer_tick(void *opaque)
{
    CPUState *cpu;
    double pct;

    /* Stop the timer if needed */
    if (!cpu_throttle_get_percentage()) {
        return;
    }
    CPU_FOREACH(cpu) {
        if (!qatomic_xchg(&cpu->throttle_thread_scheduled, 1)) {
            async_run_on_cpu(cpu, cpu_throttle_thread,
                             RUN_ON_CPU_NULL);
        }
    }

    pct = (double)cpu_throttle_get_percentage() / 100;
    timer_mod(throttle_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL_RT) +
                                   CPU_THROTTLE_TIMESLICE_NS / (1 - pct));
}

void cpu_throttle_set(int new_throttle_pct)
{
    /*
     * boolean to store whether throttle is already active or not,
     * before modifying throttle_percentage
     */
    bool throttle_active = cpu_throttle_active();

    trace_cpu_throttle_set(new_throttle_pct);

    /* Ensure throttle percentage is within valid range */
    new_throttle_pct = MIN(new_throttle_pct, CPU_THROTTLE_PCT_MAX);
    new_throttle_pct = MAX(new_throttle_pct, CPU_THROTTLE_PCT_MIN);

    qatomic_set(&throttle_percentage, new_throttle_pct);

    if (!throttle_active) {
        cpu_throttle_timer_tick(NULL);
    }
}

void cpu_throttle_stop(void)
{
    qatomic_set(&throttle_percentage, 0);
    cpu_throttle_dirty_sync_timer(false);
}

bool cpu_throttle_active(void)
{
    return (cpu_throttle_get_percentage() != 0);
}

int cpu_throttle_get_percentage(void)
{
    return qatomic_read(&throttle_percentage);
}

void cpu_throttle_dirty_sync_timer_tick(void *opaque)
{
    uint64_t sync_cnt = stat64_get(&mig_stats.dirty_sync_count);

    /*
     * The first iteration copies all memory anyhow and has no
     * effect on guest performance, therefore omit it to avoid
     * paying extra for the sync penalty.
     */
    if (sync_cnt <= 1) {
        goto end;
    }

    if (sync_cnt == throttle_dirty_sync_count_prev) {
        trace_cpu_throttle_dirty_sync();
        WITH_RCU_READ_LOCK_GUARD() {
            migration_bitmap_sync_precopy(false);
        }
    }

end:
    throttle_dirty_sync_count_prev = stat64_get(&mig_stats.dirty_sync_count);

    timer_mod(throttle_dirty_sync_timer,
        qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL_RT) +
            CPU_THROTTLE_DIRTY_SYNC_TIMESLICE_MS);
}

static bool cpu_throttle_dirty_sync_active(void)
{
    return qatomic_read(&throttle_dirty_sync_timer_active);
}

void cpu_throttle_dirty_sync_timer(bool enable)
{
    assert(throttle_dirty_sync_timer);

    if (enable) {
        if (!cpu_throttle_dirty_sync_active()) {
            /*
             * Always reset the dirty sync count cache, in case migration
             * was cancelled once.
             */
            throttle_dirty_sync_count_prev = 0;
            timer_mod(throttle_dirty_sync_timer,
                qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL_RT) +
                    CPU_THROTTLE_DIRTY_SYNC_TIMESLICE_MS);
            qatomic_set(&throttle_dirty_sync_timer_active, 1);
        }
    } else {
        if (cpu_throttle_dirty_sync_active()) {
            timer_del(throttle_dirty_sync_timer);
            qatomic_set(&throttle_dirty_sync_timer_active, 0);
        }
    }
}

void cpu_throttle_init(void)
{
    throttle_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL_RT,
                                  cpu_throttle_timer_tick, NULL);
    throttle_dirty_sync_timer =
        timer_new_ms(QEMU_CLOCK_VIRTUAL_RT,
                     cpu_throttle_dirty_sync_timer_tick, NULL);
}
