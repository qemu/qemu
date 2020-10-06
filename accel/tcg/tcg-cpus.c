/*
 * QEMU System Emulator
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

/* Kick all RR vCPUs */
static void qemu_cpu_kick_rr_cpus(void)
{
    CPUState *cpu;

    CPU_FOREACH(cpu) {
        cpu_exit(cpu);
    };
}

static void tcg_kick_vcpu_thread(CPUState *cpu)
{
    if (qemu_tcg_mttcg_enabled()) {
        cpu_exit(cpu);
    } else {
        qemu_cpu_kick_rr_cpus();
    }
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
    assert(!mttcg_enabled);
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
    assert(!mttcg_enabled);
    if (tcg_kick_vcpu_timer && timer_pending(tcg_kick_vcpu_timer)) {
        timer_del(tcg_kick_vcpu_timer);
    }
}

static void qemu_tcg_destroy_vcpu(CPUState *cpu)
{
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

static int64_t tcg_get_icount_limit(void)
{
    int64_t deadline;

    if (replay_mode != REPLAY_MODE_PLAY) {
        /*
         * Include all the timers, because they may need an attention.
         * Too long CPU execution may create unnecessary delay in UI.
         */
        deadline = qemu_clock_deadline_ns_all(QEMU_CLOCK_VIRTUAL,
                                              QEMU_TIMER_ATTR_ALL);
        /* Check realtime timers, because they help with input processing */
        deadline = qemu_soonest_timeout(deadline,
                qemu_clock_deadline_ns_all(QEMU_CLOCK_REALTIME,
                                           QEMU_TIMER_ATTR_ALL));

        /*
         * Maintain prior (possibly buggy) behaviour where if no deadline
         * was set (as there is no QEMU_CLOCK_VIRTUAL timer) or it is more than
         * INT32_MAX nanoseconds ahead, we still use INT32_MAX
         * nanoseconds.
         */
        if ((deadline < 0) || (deadline > INT32_MAX)) {
            deadline = INT32_MAX;
        }

        return icount_round(deadline);
    } else {
        return replay_get_instructions();
    }
}

static void notify_aio_contexts(void)
{
    /* Wake up other AioContexts.  */
    qemu_clock_notify(QEMU_CLOCK_VIRTUAL);
    qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
}

static void handle_icount_deadline(void)
{
    assert(qemu_in_vcpu_thread());
    if (icount_enabled()) {
        int64_t deadline = qemu_clock_deadline_ns_all(QEMU_CLOCK_VIRTUAL,
                                                      QEMU_TIMER_ATTR_ALL);

        if (deadline == 0) {
            notify_aio_contexts();
        }
    }
}

static void prepare_icount_for_run(CPUState *cpu)
{
    if (icount_enabled()) {
        int insns_left;

        /*
         * These should always be cleared by process_icount_data after
         * each vCPU execution. However u16.high can be raised
         * asynchronously by cpu_exit/cpu_interrupt/tcg_handle_interrupt
         */
        g_assert(cpu_neg(cpu)->icount_decr.u16.low == 0);
        g_assert(cpu->icount_extra == 0);

        cpu->icount_budget = tcg_get_icount_limit();
        insns_left = MIN(0xffff, cpu->icount_budget);
        cpu_neg(cpu)->icount_decr.u16.low = insns_left;
        cpu->icount_extra = cpu->icount_budget - insns_left;

        replay_mutex_lock();

        if (cpu->icount_budget == 0 && replay_has_checkpoint()) {
            notify_aio_contexts();
        }
    }
}

static void process_icount_data(CPUState *cpu)
{
    if (icount_enabled()) {
        /* Account for executed instructions */
        icount_update(cpu);

        /* Reset the counters */
        cpu_neg(cpu)->icount_decr.u16.low = 0;
        cpu->icount_extra = 0;
        cpu->icount_budget = 0;

        replay_account_executed_instructions();

        replay_mutex_unlock();
    }
}

static int tcg_cpu_exec(CPUState *cpu)
{
    int ret;
#ifdef CONFIG_PROFILER
    int64_t ti;
#endif

    assert(tcg_enabled());
#ifdef CONFIG_PROFILER
    ti = profile_getclock();
#endif
    cpu_exec_start(cpu);
    ret = cpu_exec(cpu);
    cpu_exec_end(cpu);
#ifdef CONFIG_PROFILER
    qatomic_set(&tcg_ctx->prof.cpu_exec_time,
                tcg_ctx->prof.cpu_exec_time + profile_getclock() - ti);
#endif
    return ret;
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
            cpu_thread_signal_destroyed(cpu);
            break;
        }
    }
}

/*
 * Single-threaded TCG
 *
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
        /* Account partial waits to QEMU_CLOCK_VIRTUAL.  */
        icount_account_warp_timer();

        /*
         * Run the timers here.  This is much more efficient than
         * waking up the I/O thread and waiting for completion.
         */
        handle_icount_deadline();

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
                prepare_icount_for_run(cpu);

                r = tcg_cpu_exec(cpu);

                process_icount_data(cpu);
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

/*
 * Multi-threaded TCG
 *
 * In the multi-threaded case each vCPU has its own thread. The TLS
 * variable current_cpu can be used deep in the code to find the
 * current CPUState for a given thread.
 */

static void *tcg_cpu_thread_fn(void *arg)
{
    CPUState *cpu = arg;

    assert(tcg_enabled());
    g_assert(!icount_enabled());

    rcu_register_thread();
    tcg_register_thread();

    qemu_mutex_lock_iothread();
    qemu_thread_get_self(cpu->thread);

    cpu->thread_id = qemu_get_thread_id();
    cpu->can_do_io = 1;
    current_cpu = cpu;
    cpu_thread_signal_created(cpu);
    qemu_guest_random_seed_thread_part2(cpu->random_seed);

    /* process any pending work */
    cpu->exit_request = 1;

    do {
        if (cpu_can_run(cpu)) {
            int r;
            qemu_mutex_unlock_iothread();
            r = tcg_cpu_exec(cpu);
            qemu_mutex_lock_iothread();
            switch (r) {
            case EXCP_DEBUG:
                cpu_handle_guest_debug(cpu);
                break;
            case EXCP_HALTED:
                /*
                 * during start-up the vCPU is reset and the thread is
                 * kicked several times. If we don't ensure we go back
                 * to sleep in the halted state we won't cleanly
                 * start-up when the vCPU is enabled.
                 *
                 * cpu->halted should ensure we sleep in wait_io_event
                 */
                g_assert(cpu->halted);
                break;
            case EXCP_ATOMIC:
                qemu_mutex_unlock_iothread();
                cpu_exec_step_atomic(cpu);
                qemu_mutex_lock_iothread();
            default:
                /* Ignore everything else? */
                break;
            }
        }

        qatomic_mb_set(&cpu->exit_request, 0);
        qemu_wait_io_event(cpu);
    } while (!cpu->unplug || cpu_can_run(cpu));

    qemu_tcg_destroy_vcpu(cpu);
    cpu_thread_signal_destroyed(cpu);
    qemu_mutex_unlock_iothread();
    rcu_unregister_thread();
    return NULL;
}

static void tcg_start_vcpu_thread(CPUState *cpu)
{
    char thread_name[VCPU_THREAD_NAME_SIZE];
    static QemuCond *single_tcg_halt_cond;
    static QemuThread *single_tcg_cpu_thread;
    static int tcg_region_inited;

    assert(tcg_enabled());
    /*
     * Initialize TCG regions--once. Now is a good time, because:
     * (1) TCG's init context, prologue and target globals have been set up.
     * (2) qemu_tcg_mttcg_enabled() works now (TCG init code runs before the
     *     -accel flag is processed, so the check doesn't work then).
     */
    if (!tcg_region_inited) {
        tcg_region_inited = 1;
        tcg_region_init();
        parallel_cpus = qemu_tcg_mttcg_enabled() && current_machine->smp.max_cpus > 1;
    }

    if (qemu_tcg_mttcg_enabled() || !single_tcg_cpu_thread) {
        cpu->thread = g_malloc0(sizeof(QemuThread));
        cpu->halt_cond = g_malloc0(sizeof(QemuCond));
        qemu_cond_init(cpu->halt_cond);

        if (qemu_tcg_mttcg_enabled()) {
            /* create a thread per vCPU with TCG (MTTCG) */
            snprintf(thread_name, VCPU_THREAD_NAME_SIZE, "CPU %d/TCG",
                 cpu->cpu_index);

            qemu_thread_create(cpu->thread, thread_name, tcg_cpu_thread_fn,
                               cpu, QEMU_THREAD_JOINABLE);

        } else {
            /* share a single thread for all cpus with TCG */
            snprintf(thread_name, VCPU_THREAD_NAME_SIZE, "ALL CPUs/TCG");
            qemu_thread_create(cpu->thread, thread_name,
                               tcg_rr_cpu_thread_fn,
                               cpu, QEMU_THREAD_JOINABLE);

            single_tcg_halt_cond = cpu->halt_cond;
            single_tcg_cpu_thread = cpu->thread;
        }
#ifdef _WIN32
        cpu->hThread = qemu_thread_get_handle(cpu->thread);
#endif
    } else {
        /* For non-MTTCG cases we share the thread */
        cpu->thread = single_tcg_cpu_thread;
        cpu->halt_cond = single_tcg_halt_cond;
        cpu->thread_id = first_cpu->thread_id;
        cpu->can_do_io = 1;
        cpu->created = true;
    }
}

static int64_t tcg_get_virtual_clock(void)
{
    if (icount_enabled()) {
        return icount_get();
    }
    return cpu_get_clock();
}

static int64_t tcg_get_elapsed_ticks(void)
{
    if (icount_enabled()) {
        return icount_get();
    }
    return cpu_get_ticks();
}

/* mask must never be zero, except for A20 change call */
static void tcg_handle_interrupt(CPUState *cpu, int mask)
{
    int old_mask;
    g_assert(qemu_mutex_iothread_locked());

    old_mask = cpu->interrupt_request;
    cpu->interrupt_request |= mask;

    /*
     * If called from iothread context, wake the target cpu in
     * case its halted.
     */
    if (!qemu_cpu_is_self(cpu)) {
        qemu_cpu_kick(cpu);
    } else {
        qatomic_set(&cpu_neg(cpu)->icount_decr.u16.high, -1);
        if (icount_enabled() &&
            !cpu->can_do_io
            && (mask & ~old_mask) != 0) {
            cpu_abort(cpu, "Raised interrupt while not in I/O function");
        }
    }
}

const CpusAccel tcg_cpus = {
    .create_vcpu_thread = tcg_start_vcpu_thread,
    .kick_vcpu_thread = tcg_kick_vcpu_thread,

    .handle_interrupt = tcg_handle_interrupt,

    .get_virtual_clock = tcg_get_virtual_clock,
    .get_elapsed_ticks = tcg_get_elapsed_ticks,
};
