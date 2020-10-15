/*
 * QEMU TCG Single Threaded vCPUs implementation
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2014 Red Hat Inc.
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
#include "qemu-common.h"
#include "sysemu/tcg.h"
#include "sysemu/replay.h"
#include "qemu/main-loop.h"
#include "qemu/guest-random.h"
#include "exec/exec-all.h"
#include "hw/boards.h"

#include "tcg-cpus.h"
#include "tcg-cpus-rr.h"
#include "tcg-cpus-icount.h"

/* Kick all RR vCPUs */
void qemu_cpu_kick_rr_cpus(CPUState *unused)
{
    CPUState *cpu;

    CPU_FOREACH(cpu) {
        cpu_exit(cpu);
    };
}

/*
 * TCG vCPU kick timer
 *
 * The kick timer is responsible for moving single threaded vCPU
 * emulation on to the next vCPU. If more than one vCPU is running a
 * timer event with force a cpu->exit so the next vCPU can get
 * scheduled.
 *
 * The timer is removed if all vCPUs are idle and restarted again once
 * idleness is complete.
 */

static QEMUTimer *tcg_kick_vcpu_timer;
static CPUState *tcg_current_rr_cpu;

#define TCG_KICK_PERIOD (NANOSECONDS_PER_SECOND / 10)

static inline int64_t qemu_tcg_next_kick(void)
{
    return qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + TCG_KICK_PERIOD;
}

/* Kick the currently round-robin scheduled vCPU to next */
static void qemu_cpu_kick_rr_next_cpu(void)
{
    CPUState *cpu;
    do {
        cpu = qatomic_mb_read(&tcg_current_rr_cpu);
        if (cpu) {
            cpu_exit(cpu);
        }
    } while (cpu != qatomic_mb_read(&tcg_current_rr_cpu));
}

static void kick_tcg_thread(void *opaque)
{
    timer_mod(tcg_kick_vcpu_timer, qemu_tcg_next_kick());
    qemu_cpu_kick_rr_next_cpu();
}

static void start_tcg_kick_timer(void)
{
    if (!tcg_kick_vcpu_timer && CPU_NEXT(first_cpu)) {
        tcg_kick_vcpu_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                           kick_tcg_thread, NULL);
    }
    if (tcg_kick_vcpu_timer && !timer_pending(tcg_kick_vcpu_timer)) {
        timer_mod(tcg_kick_vcpu_timer, qemu_tcg_next_kick());
    }
}

static void stop_tcg_kick_timer(void)
{
    if (tcg_kick_vcpu_timer && timer_pending(tcg_kick_vcpu_timer)) {
        timer_del(tcg_kick_vcpu_timer);
    }
}

static void qemu_tcg_rr_wait_io_event(void)
{
    CPUState *cpu;

    while (all_cpu_threads_idle()) {
        stop_tcg_kick_timer();
        qemu_cond_wait_iothread(first_cpu->halt_cond);
    }

    start_tcg_kick_timer();

    CPU_FOREACH(cpu) {
        qemu_wait_io_event_common(cpu);
    }
}

/*
 * Destroy any remaining vCPUs which have been unplugged and have
 * finished running
 */
static void deal_with_unplugged_cpus(void)
{
    CPUState *cpu;

    CPU_FOREACH(cpu) {
        if (cpu->unplug && !cpu_can_run(cpu)) {
            qemu_tcg_destroy_vcpu(cpu);
            break;
        }
    }
}

/*
 * In the single-threaded case each vCPU is simulated in turn. If
 * there is more than a single vCPU we create a simple timer to kick
 * the vCPU and ensure we don't get stuck in a tight loop in one vCPU.
 * This is done explicitly rather than relying on side-effects
 * elsewhere.
 */

static void *tcg_rr_cpu_thread_fn(void *arg)
{
    CPUState *cpu = arg;

    assert(tcg_enabled());
    rcu_register_thread();
    tcg_register_thread();

    qemu_mutex_lock_iothread();
    qemu_thread_get_self(cpu->thread);

    cpu->thread_id = qemu_get_thread_id();
    cpu->can_do_io = 1;
    cpu_thread_signal_created(cpu);
    qemu_guest_random_seed_thread_part2(cpu->random_seed);

    /* wait for initial kick-off after machine start */
    while (first_cpu->stopped) {
        qemu_cond_wait_iothread(first_cpu->halt_cond);

        /* process any pending work */
        CPU_FOREACH(cpu) {
            current_cpu = cpu;
            qemu_wait_io_event_common(cpu);
        }
    }

    start_tcg_kick_timer();

    cpu = first_cpu;

    /* process any pending work */
    cpu->exit_request = 1;

    while (1) {
        qemu_mutex_unlock_iothread();
        replay_mutex_lock();
        qemu_mutex_lock_iothread();

        if (icount_enabled()) {
            /* Account partial waits to QEMU_CLOCK_VIRTUAL.  */
            icount_account_warp_timer();
            /*
             * Run the timers here.  This is much more efficient than
             * waking up the I/O thread and waiting for completion.
             */
            handle_icount_deadline();
        }

        replay_mutex_unlock();

        if (!cpu) {
            cpu = first_cpu;
        }

        while (cpu && cpu_work_list_empty(cpu) && !cpu->exit_request) {

            qatomic_mb_set(&tcg_current_rr_cpu, cpu);
            current_cpu = cpu;

            qemu_clock_enable(QEMU_CLOCK_VIRTUAL,
                              (cpu->singlestep_enabled & SSTEP_NOTIMER) == 0);

            if (cpu_can_run(cpu)) {
                int r;

                qemu_mutex_unlock_iothread();
                if (icount_enabled()) {
                    prepare_icount_for_run(cpu);
                }
                r = tcg_cpu_exec(cpu);
                if (icount_enabled()) {
                    process_icount_data(cpu);
                }
                qemu_mutex_lock_iothread();

                if (r == EXCP_DEBUG) {
                    cpu_handle_guest_debug(cpu);
                    break;
                } else if (r == EXCP_ATOMIC) {
                    qemu_mutex_unlock_iothread();
                    cpu_exec_step_atomic(cpu);
                    qemu_mutex_lock_iothread();
                    break;
                }
            } else if (cpu->stop) {
                if (cpu->unplug) {
                    cpu = CPU_NEXT(cpu);
                }
                break;
            }

            cpu = CPU_NEXT(cpu);
        } /* while (cpu && !cpu->exit_request).. */

        /* Does not need qatomic_mb_set because a spurious wakeup is okay.  */
        qatomic_set(&tcg_current_rr_cpu, NULL);

        if (cpu && cpu->exit_request) {
            qatomic_mb_set(&cpu->exit_request, 0);
        }

        if (icount_enabled() && all_cpu_threads_idle()) {
            /*
             * When all cpus are sleeping (e.g in WFI), to avoid a deadlock
             * in the main_loop, wake it up in order to start the warp timer.
             */
            qemu_notify_event();
        }

        qemu_tcg_rr_wait_io_event();
        deal_with_unplugged_cpus();
    }

    rcu_unregister_thread();
    return NULL;
}

void rr_start_vcpu_thread(CPUState *cpu)
{
    char thread_name[VCPU_THREAD_NAME_SIZE];
    static QemuCond *single_tcg_halt_cond;
    static QemuThread *single_tcg_cpu_thread;

    g_assert(tcg_enabled());
    parallel_cpus = false;

    if (!single_tcg_cpu_thread) {
        cpu->thread = g_malloc0(sizeof(QemuThread));
        cpu->halt_cond = g_malloc0(sizeof(QemuCond));
        qemu_cond_init(cpu->halt_cond);

        /* share a single thread for all cpus with TCG */
        snprintf(thread_name, VCPU_THREAD_NAME_SIZE, "ALL CPUs/TCG");
        qemu_thread_create(cpu->thread, thread_name,
                           tcg_rr_cpu_thread_fn,
                           cpu, QEMU_THREAD_JOINABLE);

        single_tcg_halt_cond = cpu->halt_cond;
        single_tcg_cpu_thread = cpu->thread;
#ifdef _WIN32
        cpu->hThread = qemu_thread_get_handle(cpu->thread);
#endif
    } else {
        /* we share the thread */
        cpu->thread = single_tcg_cpu_thread;
        cpu->halt_cond = single_tcg_halt_cond;
        cpu->thread_id = first_cpu->thread_id;
        cpu->can_do_io = 1;
        cpu->created = true;
    }
}

const CpusAccel tcg_cpus_rr = {
    .create_vcpu_thread = rr_start_vcpu_thread,
    .kick_vcpu_thread = qemu_cpu_kick_rr_cpus,

    .handle_interrupt = tcg_handle_interrupt,
};
