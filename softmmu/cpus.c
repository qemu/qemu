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
#include "qemu-common.h"
#include "qemu/config-file.h"
#include "qemu/cutils.h"
#include "migration/vmstate.h"
#include "monitor/monitor.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-misc.h"
#include "qapi/qapi-events-run-state.h"
#include "qapi/qmp/qerror.h"
#include "qemu/error-report.h"
#include "qemu/qemu-print.h"
#include "sysemu/tcg.h"
#include "sysemu/block-backend.h"
#include "exec/gdbstub.h"
#include "sysemu/dma.h"
#include "sysemu/hw_accel.h"
#include "sysemu/kvm.h"
#include "sysemu/hax.h"
#include "sysemu/hvf.h"
#include "sysemu/whpx.h"
#include "exec/exec-all.h"

#include "qemu/thread.h"
#include "qemu/plugin.h"
#include "sysemu/cpus.h"
#include "sysemu/qtest.h"
#include "qemu/main-loop.h"
#include "qemu/option.h"
#include "qemu/bitmap.h"
#include "qemu/seqlock.h"
#include "qemu/guest-random.h"
#include "tcg/tcg.h"
#include "hw/nmi.h"
#include "sysemu/replay.h"
#include "sysemu/runstate.h"
#include "sysemu/cpu-timers.h"
#include "hw/boards.h"
#include "hw/hw.h"

#ifdef CONFIG_LINUX

#include <sys/prctl.h>

#ifndef PR_MCE_KILL
#define PR_MCE_KILL 33
#endif

#ifndef PR_MCE_KILL_SET
#define PR_MCE_KILL_SET 1
#endif

#ifndef PR_MCE_KILL_EARLY
#define PR_MCE_KILL_EARLY 1
#endif

#endif /* CONFIG_LINUX */

static QemuMutex qemu_global_mutex;

bool cpu_is_stopped(CPUState *cpu)
{
    return cpu->stopped || !runstate_is_running();
}

bool cpu_work_list_empty(CPUState *cpu)
{
    bool ret;

    qemu_mutex_lock(&cpu->work_mutex);
    ret = QSIMPLEQ_EMPTY(&cpu->work_list);
    qemu_mutex_unlock(&cpu->work_mutex);
    return ret;
}

bool cpu_thread_is_idle(CPUState *cpu)
{
    if (cpu->stop || !cpu_work_list_empty(cpu)) {
        return false;
    }
    if (cpu_is_stopped(cpu)) {
        return true;
    }
    if (!cpu->halted || cpu_has_work(cpu) ||
        kvm_halt_in_kernel()) {
        return false;
    }
    return true;
}

bool all_cpu_threads_idle(void)
{
    CPUState *cpu;

    CPU_FOREACH(cpu) {
        if (!cpu_thread_is_idle(cpu)) {
            return false;
        }
    }
    return true;
}

bool mttcg_enabled;


/***********************************************************/
/* TCG vCPU kick timer
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

/* Kick all RR vCPUs */
static void qemu_cpu_kick_rr_cpus(void)
{
    CPUState *cpu;

    CPU_FOREACH(cpu) {
        cpu_exit(cpu);
    };
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

/***********************************************************/
void hw_error(const char *fmt, ...)
{
    va_list ap;
    CPUState *cpu;

    va_start(ap, fmt);
    fprintf(stderr, "qemu: hardware error: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    CPU_FOREACH(cpu) {
        fprintf(stderr, "CPU #%d:\n", cpu->cpu_index);
        cpu_dump_state(cpu, stderr, CPU_DUMP_FPU);
    }
    va_end(ap);
    abort();
}

/*
 * The chosen accelerator is supposed to register this.
 */
static const CpusAccel *cpus_accel;

void cpu_synchronize_all_states(void)
{
    CPUState *cpu;

    CPU_FOREACH(cpu) {
        cpu_synchronize_state(cpu);
    }
}

void cpu_synchronize_all_post_reset(void)
{
    CPUState *cpu;

    CPU_FOREACH(cpu) {
        cpu_synchronize_post_reset(cpu);
    }
}

void cpu_synchronize_all_post_init(void)
{
    CPUState *cpu;

    CPU_FOREACH(cpu) {
        cpu_synchronize_post_init(cpu);
    }
}

void cpu_synchronize_all_pre_loadvm(void)
{
    CPUState *cpu;

    CPU_FOREACH(cpu) {
        cpu_synchronize_pre_loadvm(cpu);
    }
}

void cpu_synchronize_state(CPUState *cpu)
{
    if (cpus_accel && cpus_accel->synchronize_state) {
        cpus_accel->synchronize_state(cpu);
    }
    if (kvm_enabled()) {
        kvm_cpu_synchronize_state(cpu);
    }
    if (hax_enabled()) {
        hax_cpu_synchronize_state(cpu);
    }
    if (whpx_enabled()) {
        whpx_cpu_synchronize_state(cpu);
    }
}

void cpu_synchronize_post_reset(CPUState *cpu)
{
    if (cpus_accel && cpus_accel->synchronize_post_reset) {
        cpus_accel->synchronize_post_reset(cpu);
    }
    if (kvm_enabled()) {
        kvm_cpu_synchronize_post_reset(cpu);
    }
    if (hax_enabled()) {
        hax_cpu_synchronize_post_reset(cpu);
    }
    if (whpx_enabled()) {
        whpx_cpu_synchronize_post_reset(cpu);
    }
}

void cpu_synchronize_post_init(CPUState *cpu)
{
    if (cpus_accel && cpus_accel->synchronize_post_init) {
        cpus_accel->synchronize_post_init(cpu);
    }
    if (kvm_enabled()) {
        kvm_cpu_synchronize_post_init(cpu);
    }
    if (hax_enabled()) {
        hax_cpu_synchronize_post_init(cpu);
    }
    if (whpx_enabled()) {
        whpx_cpu_synchronize_post_init(cpu);
    }
}

void cpu_synchronize_pre_loadvm(CPUState *cpu)
{
    if (cpus_accel && cpus_accel->synchronize_pre_loadvm) {
        cpus_accel->synchronize_pre_loadvm(cpu);
    }
    if (kvm_enabled()) {
        kvm_cpu_synchronize_pre_loadvm(cpu);
    }
    if (hax_enabled()) {
        hax_cpu_synchronize_pre_loadvm(cpu);
    }
    if (hvf_enabled()) {
        hvf_cpu_synchronize_pre_loadvm(cpu);
    }
    if (whpx_enabled()) {
        whpx_cpu_synchronize_pre_loadvm(cpu);
    }
}

int64_t cpus_get_virtual_clock(void)
{
    if (cpus_accel && cpus_accel->get_virtual_clock) {
        return cpus_accel->get_virtual_clock();
    }
    if (icount_enabled()) {
        return icount_get();
    } else if (qtest_enabled()) { /* for qtest_clock_warp */
        return qtest_get_virtual_clock();
    }
    return cpu_get_clock();
}

/*
 * return the time elapsed in VM between vm_start and vm_stop.  Unless
 * icount is active, cpus_get_elapsed_ticks() uses units of the host CPU cycle
 * counter.
 */
int64_t cpus_get_elapsed_ticks(void)
{
    if (cpus_accel && cpus_accel->get_elapsed_ticks) {
        return cpus_accel->get_elapsed_ticks();
    }
    if (icount_enabled()) {
        return icount_get();
    }
    return cpu_get_ticks();
}

static int do_vm_stop(RunState state, bool send_stop)
{
    int ret = 0;

    if (runstate_is_running()) {
        runstate_set(state);
        cpu_disable_ticks();
        pause_all_vcpus();
        vm_state_notify(0, state);
        if (send_stop) {
            qapi_event_send_stop();
        }
    }

    bdrv_drain_all();
    ret = bdrv_flush_all();

    return ret;
}

/* Special vm_stop() variant for terminating the process.  Historically clients
 * did not expect a QMP STOP event and so we need to retain compatibility.
 */
int vm_shutdown(void)
{
    return do_vm_stop(RUN_STATE_SHUTDOWN, false);
}

bool cpu_can_run(CPUState *cpu)
{
    if (cpu->stop) {
        return false;
    }
    if (cpu_is_stopped(cpu)) {
        return false;
    }
    return true;
}

void cpu_handle_guest_debug(CPUState *cpu)
{
    gdb_set_stop_cpu(cpu);
    qemu_system_debug_request();
    cpu->stopped = true;
}

#ifdef CONFIG_LINUX
static void sigbus_reraise(void)
{
    sigset_t set;
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = SIG_DFL;
    if (!sigaction(SIGBUS, &action, NULL)) {
        raise(SIGBUS);
        sigemptyset(&set);
        sigaddset(&set, SIGBUS);
        pthread_sigmask(SIG_UNBLOCK, &set, NULL);
    }
    perror("Failed to re-raise SIGBUS!\n");
    abort();
}

static void sigbus_handler(int n, siginfo_t *siginfo, void *ctx)
{
    if (siginfo->si_code != BUS_MCEERR_AO && siginfo->si_code != BUS_MCEERR_AR) {
        sigbus_reraise();
    }

    if (current_cpu) {
        /* Called asynchronously in VCPU thread.  */
        if (kvm_on_sigbus_vcpu(current_cpu, siginfo->si_code, siginfo->si_addr)) {
            sigbus_reraise();
        }
    } else {
        /* Called synchronously (via signalfd) in main thread.  */
        if (kvm_on_sigbus(siginfo->si_code, siginfo->si_addr)) {
            sigbus_reraise();
        }
    }
}

static void qemu_init_sigbus(void)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_flags = SA_SIGINFO;
    action.sa_sigaction = sigbus_handler;
    sigaction(SIGBUS, &action, NULL);

    prctl(PR_MCE_KILL, PR_MCE_KILL_SET, PR_MCE_KILL_EARLY, 0, 0);
}
#else /* !CONFIG_LINUX */
static void qemu_init_sigbus(void)
{
}
#endif /* !CONFIG_LINUX */

static QemuThread io_thread;

/* cpu creation */
static QemuCond qemu_cpu_cond;
/* system init */
static QemuCond qemu_pause_cond;

void qemu_init_cpu_loop(void)
{
    qemu_init_sigbus();
    qemu_cond_init(&qemu_cpu_cond);
    qemu_cond_init(&qemu_pause_cond);
    qemu_mutex_init(&qemu_global_mutex);

    qemu_thread_get_self(&io_thread);
}

void run_on_cpu(CPUState *cpu, run_on_cpu_func func, run_on_cpu_data data)
{
    do_run_on_cpu(cpu, func, data, &qemu_global_mutex);
}

static void qemu_kvm_destroy_vcpu(CPUState *cpu)
{
    if (kvm_destroy_vcpu(cpu) < 0) {
        error_report("kvm_destroy_vcpu failed");
        exit(EXIT_FAILURE);
    }
}

static void qemu_tcg_destroy_vcpu(CPUState *cpu)
{
}

static void qemu_cpu_stop(CPUState *cpu, bool exit)
{
    g_assert(qemu_cpu_is_self(cpu));
    cpu->stop = false;
    cpu->stopped = true;
    if (exit) {
        cpu_exit(cpu);
    }
    qemu_cond_broadcast(&qemu_pause_cond);
}

void qemu_wait_io_event_common(CPUState *cpu)
{
    qatomic_mb_set(&cpu->thread_kicked, false);
    if (cpu->stop) {
        qemu_cpu_stop(cpu, false);
    }
    process_queued_cpu_work(cpu);
}

static void qemu_tcg_rr_wait_io_event(void)
{
    CPUState *cpu;

    while (all_cpu_threads_idle()) {
        stop_tcg_kick_timer();
        qemu_cond_wait(first_cpu->halt_cond, &qemu_global_mutex);
    }

    start_tcg_kick_timer();

    CPU_FOREACH(cpu) {
        qemu_wait_io_event_common(cpu);
    }
}

void qemu_wait_io_event(CPUState *cpu)
{
    bool slept = false;

    while (cpu_thread_is_idle(cpu)) {
        if (!slept) {
            slept = true;
            qemu_plugin_vcpu_idle_cb(cpu);
        }
        qemu_cond_wait(cpu->halt_cond, &qemu_global_mutex);
    }
    if (slept) {
        qemu_plugin_vcpu_resume_cb(cpu);
    }

#ifdef _WIN32
    /* Eat dummy APC queued by cpus_kick_thread. */
    if (hax_enabled()) {
        SleepEx(0, TRUE);
    }
#endif
    qemu_wait_io_event_common(cpu);
}

static void *qemu_kvm_cpu_thread_fn(void *arg)
{
    CPUState *cpu = arg;
    int r;

    rcu_register_thread();

    qemu_mutex_lock_iothread();
    qemu_thread_get_self(cpu->thread);
    cpu->thread_id = qemu_get_thread_id();
    cpu->can_do_io = 1;
    current_cpu = cpu;

    r = kvm_init_vcpu(cpu);
    if (r < 0) {
        error_report("kvm_init_vcpu failed: %s", strerror(-r));
        exit(1);
    }

    kvm_init_cpu_signals(cpu);

    /* signal CPU creation */
    cpu_thread_signal_created(cpu);
    qemu_guest_random_seed_thread_part2(cpu->random_seed);

    do {
        if (cpu_can_run(cpu)) {
            r = kvm_cpu_exec(cpu);
            if (r == EXCP_DEBUG) {
                cpu_handle_guest_debug(cpu);
            }
        }
        qemu_wait_io_event(cpu);
    } while (!cpu->unplug || cpu_can_run(cpu));

    qemu_kvm_destroy_vcpu(cpu);
    cpu_thread_signal_destroyed(cpu);
    qemu_mutex_unlock_iothread();
    rcu_unregister_thread();
    return NULL;
}

static void *qemu_dummy_cpu_thread_fn(void *arg)
{
#ifdef _WIN32
    error_report("qtest is not supported under Windows");
    exit(1);
#else
    CPUState *cpu = arg;
    sigset_t waitset;
    int r;

    rcu_register_thread();

    qemu_mutex_lock_iothread();
    qemu_thread_get_self(cpu->thread);
    cpu->thread_id = qemu_get_thread_id();
    cpu->can_do_io = 1;
    current_cpu = cpu;

    sigemptyset(&waitset);
    sigaddset(&waitset, SIG_IPI);

    /* signal CPU creation */
    cpu_thread_signal_created(cpu);
    qemu_guest_random_seed_thread_part2(cpu->random_seed);

    do {
        qemu_mutex_unlock_iothread();
        do {
            int sig;
            r = sigwait(&waitset, &sig);
        } while (r == -1 && (errno == EAGAIN || errno == EINTR));
        if (r == -1) {
            perror("sigwait");
            exit(1);
        }
        qemu_mutex_lock_iothread();
        qemu_wait_io_event(cpu);
    } while (!cpu->unplug);

    qemu_mutex_unlock_iothread();
    rcu_unregister_thread();
    return NULL;
#endif
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

        /* Maintain prior (possibly buggy) behaviour where if no deadline
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

        /* These should always be cleared by process_icount_data after
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

/* Destroy any remaining vCPUs which have been unplugged and have
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

/* Single-threaded TCG
 *
 * In the single-threaded case each vCPU is simulated in turn. If
 * there is more than a single vCPU we create a simple timer to kick
 * the vCPU and ensure we don't get stuck in a tight loop in one vCPU.
 * This is done explicitly rather than relying on side-effects
 * elsewhere.
 */

static void *qemu_tcg_rr_cpu_thread_fn(void *arg)
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
        qemu_cond_wait(first_cpu->halt_cond, &qemu_global_mutex);

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

        /* Run the timers here.  This is much more efficient than
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

static void *qemu_hax_cpu_thread_fn(void *arg)
{
    CPUState *cpu = arg;
    int r;

    rcu_register_thread();
    qemu_mutex_lock_iothread();
    qemu_thread_get_self(cpu->thread);

    cpu->thread_id = qemu_get_thread_id();
    current_cpu = cpu;
    hax_init_vcpu(cpu);
    cpu_thread_signal_created(cpu);
    qemu_guest_random_seed_thread_part2(cpu->random_seed);

    do {
        if (cpu_can_run(cpu)) {
            r = hax_smp_cpu_exec(cpu);
            if (r == EXCP_DEBUG) {
                cpu_handle_guest_debug(cpu);
            }
        }

        qemu_wait_io_event(cpu);
    } while (!cpu->unplug || cpu_can_run(cpu));
    rcu_unregister_thread();
    return NULL;
}

/* The HVF-specific vCPU thread function. This one should only run when the host
 * CPU supports the VMX "unrestricted guest" feature. */
static void *qemu_hvf_cpu_thread_fn(void *arg)
{
    CPUState *cpu = arg;

    int r;

    assert(hvf_enabled());

    rcu_register_thread();

    qemu_mutex_lock_iothread();
    qemu_thread_get_self(cpu->thread);

    cpu->thread_id = qemu_get_thread_id();
    cpu->can_do_io = 1;
    current_cpu = cpu;

    hvf_init_vcpu(cpu);

    /* signal CPU creation */
    cpu_thread_signal_created(cpu);
    qemu_guest_random_seed_thread_part2(cpu->random_seed);

    do {
        if (cpu_can_run(cpu)) {
            r = hvf_vcpu_exec(cpu);
            if (r == EXCP_DEBUG) {
                cpu_handle_guest_debug(cpu);
            }
        }
        qemu_wait_io_event(cpu);
    } while (!cpu->unplug || cpu_can_run(cpu));

    hvf_vcpu_destroy(cpu);
    cpu_thread_signal_destroyed(cpu);
    qemu_mutex_unlock_iothread();
    rcu_unregister_thread();
    return NULL;
}

static void *qemu_whpx_cpu_thread_fn(void *arg)
{
    CPUState *cpu = arg;
    int r;

    rcu_register_thread();

    qemu_mutex_lock_iothread();
    qemu_thread_get_self(cpu->thread);
    cpu->thread_id = qemu_get_thread_id();
    current_cpu = cpu;

    r = whpx_init_vcpu(cpu);
    if (r < 0) {
        fprintf(stderr, "whpx_init_vcpu failed: %s\n", strerror(-r));
        exit(1);
    }

    /* signal CPU creation */
    cpu_thread_signal_created(cpu);
    qemu_guest_random_seed_thread_part2(cpu->random_seed);

    do {
        if (cpu_can_run(cpu)) {
            r = whpx_vcpu_exec(cpu);
            if (r == EXCP_DEBUG) {
                cpu_handle_guest_debug(cpu);
            }
        }
        while (cpu_thread_is_idle(cpu)) {
            qemu_cond_wait(cpu->halt_cond, &qemu_global_mutex);
        }
        qemu_wait_io_event_common(cpu);
    } while (!cpu->unplug || cpu_can_run(cpu));

    whpx_destroy_vcpu(cpu);
    cpu_thread_signal_destroyed(cpu);
    qemu_mutex_unlock_iothread();
    rcu_unregister_thread();
    return NULL;
}

#ifdef _WIN32
static void CALLBACK dummy_apc_func(ULONG_PTR unused)
{
}
#endif

/* Multi-threaded TCG
 *
 * In the multi-threaded case each vCPU has its own thread. The TLS
 * variable current_cpu can be used deep in the code to find the
 * current CPUState for a given thread.
 */

static void *qemu_tcg_cpu_thread_fn(void *arg)
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
                /* during start-up the vCPU is reset and the thread is
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

void cpus_kick_thread(CPUState *cpu)
{
#ifndef _WIN32
    int err;

    if (cpu->thread_kicked) {
        return;
    }
    cpu->thread_kicked = true;
    err = pthread_kill(cpu->thread->thread, SIG_IPI);
    if (err && err != ESRCH) {
        fprintf(stderr, "qemu:%s: %s", __func__, strerror(err));
        exit(1);
    }
#else /* _WIN32 */
    if (!qemu_cpu_is_self(cpu)) {
        if (whpx_enabled()) {
            whpx_vcpu_kick(cpu);
        } else if (!QueueUserAPC(dummy_apc_func, cpu->hThread, 0)) {
            fprintf(stderr, "%s: QueueUserAPC failed with error %lu\n",
                    __func__, GetLastError());
            exit(1);
        }
    }
#endif
}

void qemu_cpu_kick(CPUState *cpu)
{
    qemu_cond_broadcast(cpu->halt_cond);

    if (cpus_accel && cpus_accel->kick_vcpu_thread) {
        cpus_accel->kick_vcpu_thread(cpu);
    } else if (tcg_enabled()) {
        if (qemu_tcg_mttcg_enabled()) {
            cpu_exit(cpu);
        } else {
            qemu_cpu_kick_rr_cpus();
        }
    } else {
        if (hax_enabled()) {
            /*
             * FIXME: race condition with the exit_request check in
             * hax_vcpu_hax_exec
             */
            cpu->exit_request = 1;
        }
        cpus_kick_thread(cpu);
    }
}

void qemu_cpu_kick_self(void)
{
    assert(current_cpu);
    cpus_kick_thread(current_cpu);
}

bool qemu_cpu_is_self(CPUState *cpu)
{
    return qemu_thread_is_self(cpu->thread);
}

bool qemu_in_vcpu_thread(void)
{
    return current_cpu && qemu_cpu_is_self(current_cpu);
}

static __thread bool iothread_locked = false;

bool qemu_mutex_iothread_locked(void)
{
    return iothread_locked;
}

/*
 * The BQL is taken from so many places that it is worth profiling the
 * callers directly, instead of funneling them all through a single function.
 */
void qemu_mutex_lock_iothread_impl(const char *file, int line)
{
    QemuMutexLockFunc bql_lock = qatomic_read(&qemu_bql_mutex_lock_func);

    g_assert(!qemu_mutex_iothread_locked());
    bql_lock(&qemu_global_mutex, file, line);
    iothread_locked = true;
}

void qemu_mutex_unlock_iothread(void)
{
    g_assert(qemu_mutex_iothread_locked());
    iothread_locked = false;
    qemu_mutex_unlock(&qemu_global_mutex);
}

void qemu_cond_wait_iothread(QemuCond *cond)
{
    qemu_cond_wait(cond, &qemu_global_mutex);
}

void qemu_cond_timedwait_iothread(QemuCond *cond, int ms)
{
    qemu_cond_timedwait(cond, &qemu_global_mutex, ms);
}

/* signal CPU creation */
void cpu_thread_signal_created(CPUState *cpu)
{
    cpu->created = true;
    qemu_cond_signal(&qemu_cpu_cond);
}

/* signal CPU destruction */
void cpu_thread_signal_destroyed(CPUState *cpu)
{
    cpu->created = false;
    qemu_cond_signal(&qemu_cpu_cond);
}


static bool all_vcpus_paused(void)
{
    CPUState *cpu;

    CPU_FOREACH(cpu) {
        if (!cpu->stopped) {
            return false;
        }
    }

    return true;
}

void pause_all_vcpus(void)
{
    CPUState *cpu;

    qemu_clock_enable(QEMU_CLOCK_VIRTUAL, false);
    CPU_FOREACH(cpu) {
        if (qemu_cpu_is_self(cpu)) {
            qemu_cpu_stop(cpu, true);
        } else {
            cpu->stop = true;
            qemu_cpu_kick(cpu);
        }
    }

    /* We need to drop the replay_lock so any vCPU threads woken up
     * can finish their replay tasks
     */
    replay_mutex_unlock();

    while (!all_vcpus_paused()) {
        qemu_cond_wait(&qemu_pause_cond, &qemu_global_mutex);
        CPU_FOREACH(cpu) {
            qemu_cpu_kick(cpu);
        }
    }

    qemu_mutex_unlock_iothread();
    replay_mutex_lock();
    qemu_mutex_lock_iothread();
}

void cpu_resume(CPUState *cpu)
{
    cpu->stop = false;
    cpu->stopped = false;
    qemu_cpu_kick(cpu);
}

void resume_all_vcpus(void)
{
    CPUState *cpu;

    if (!runstate_is_running()) {
        return;
    }

    qemu_clock_enable(QEMU_CLOCK_VIRTUAL, true);
    CPU_FOREACH(cpu) {
        cpu_resume(cpu);
    }
}

void cpu_remove_sync(CPUState *cpu)
{
    cpu->stop = true;
    cpu->unplug = true;
    qemu_cpu_kick(cpu);
    qemu_mutex_unlock_iothread();
    qemu_thread_join(cpu->thread);
    qemu_mutex_lock_iothread();
}

static void qemu_tcg_init_vcpu(CPUState *cpu)
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
        /*
         * If MTTCG, and we will create multiple cpus,
         * then we will have cpus running in parallel.
         */
        if (qemu_tcg_mttcg_enabled()) {
            MachineState *ms = MACHINE(qdev_get_machine());
            if (ms->smp.max_cpus > 1) {
                parallel_cpus = true;
            }
        }
    }

    if (qemu_tcg_mttcg_enabled() || !single_tcg_cpu_thread) {
        cpu->thread = g_malloc0(sizeof(QemuThread));
        cpu->halt_cond = g_malloc0(sizeof(QemuCond));
        qemu_cond_init(cpu->halt_cond);

        if (qemu_tcg_mttcg_enabled()) {
            /* create a thread per vCPU with TCG (MTTCG) */
            snprintf(thread_name, VCPU_THREAD_NAME_SIZE, "CPU %d/TCG",
                 cpu->cpu_index);

            qemu_thread_create(cpu->thread, thread_name, qemu_tcg_cpu_thread_fn,
                               cpu, QEMU_THREAD_JOINABLE);

        } else {
            /* share a single thread for all cpus with TCG */
            snprintf(thread_name, VCPU_THREAD_NAME_SIZE, "ALL CPUs/TCG");
            qemu_thread_create(cpu->thread, thread_name,
                               qemu_tcg_rr_cpu_thread_fn,
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

static void qemu_hax_start_vcpu(CPUState *cpu)
{
    char thread_name[VCPU_THREAD_NAME_SIZE];

    cpu->thread = g_malloc0(sizeof(QemuThread));
    cpu->halt_cond = g_malloc0(sizeof(QemuCond));
    qemu_cond_init(cpu->halt_cond);

    snprintf(thread_name, VCPU_THREAD_NAME_SIZE, "CPU %d/HAX",
             cpu->cpu_index);
    qemu_thread_create(cpu->thread, thread_name, qemu_hax_cpu_thread_fn,
                       cpu, QEMU_THREAD_JOINABLE);
#ifdef _WIN32
    cpu->hThread = qemu_thread_get_handle(cpu->thread);
#endif
}

static void qemu_kvm_start_vcpu(CPUState *cpu)
{
    char thread_name[VCPU_THREAD_NAME_SIZE];

    cpu->thread = g_malloc0(sizeof(QemuThread));
    cpu->halt_cond = g_malloc0(sizeof(QemuCond));
    qemu_cond_init(cpu->halt_cond);
    snprintf(thread_name, VCPU_THREAD_NAME_SIZE, "CPU %d/KVM",
             cpu->cpu_index);
    qemu_thread_create(cpu->thread, thread_name, qemu_kvm_cpu_thread_fn,
                       cpu, QEMU_THREAD_JOINABLE);
}

static void qemu_hvf_start_vcpu(CPUState *cpu)
{
    char thread_name[VCPU_THREAD_NAME_SIZE];

    /* HVF currently does not support TCG, and only runs in
     * unrestricted-guest mode. */
    assert(hvf_enabled());

    cpu->thread = g_malloc0(sizeof(QemuThread));
    cpu->halt_cond = g_malloc0(sizeof(QemuCond));
    qemu_cond_init(cpu->halt_cond);

    snprintf(thread_name, VCPU_THREAD_NAME_SIZE, "CPU %d/HVF",
             cpu->cpu_index);
    qemu_thread_create(cpu->thread, thread_name, qemu_hvf_cpu_thread_fn,
                       cpu, QEMU_THREAD_JOINABLE);
}

static void qemu_whpx_start_vcpu(CPUState *cpu)
{
    char thread_name[VCPU_THREAD_NAME_SIZE];

    cpu->thread = g_malloc0(sizeof(QemuThread));
    cpu->halt_cond = g_malloc0(sizeof(QemuCond));
    qemu_cond_init(cpu->halt_cond);
    snprintf(thread_name, VCPU_THREAD_NAME_SIZE, "CPU %d/WHPX",
             cpu->cpu_index);
    qemu_thread_create(cpu->thread, thread_name, qemu_whpx_cpu_thread_fn,
                       cpu, QEMU_THREAD_JOINABLE);
#ifdef _WIN32
    cpu->hThread = qemu_thread_get_handle(cpu->thread);
#endif
}

void cpus_register_accel(const CpusAccel *ca)
{
    assert(ca != NULL);
    assert(ca->create_vcpu_thread != NULL); /* mandatory */
    cpus_accel = ca;
}

static void qemu_dummy_start_vcpu(CPUState *cpu)
{
    char thread_name[VCPU_THREAD_NAME_SIZE];

    cpu->thread = g_malloc0(sizeof(QemuThread));
    cpu->halt_cond = g_malloc0(sizeof(QemuCond));
    qemu_cond_init(cpu->halt_cond);
    snprintf(thread_name, VCPU_THREAD_NAME_SIZE, "CPU %d/DUMMY",
             cpu->cpu_index);
    qemu_thread_create(cpu->thread, thread_name, qemu_dummy_cpu_thread_fn, cpu,
                       QEMU_THREAD_JOINABLE);
}

void qemu_init_vcpu(CPUState *cpu)
{
    MachineState *ms = MACHINE(qdev_get_machine());

    cpu->nr_cores = ms->smp.cores;
    cpu->nr_threads =  ms->smp.threads;
    cpu->stopped = true;
    cpu->random_seed = qemu_guest_random_seed_thread_part1();

    if (!cpu->as) {
        /* If the target cpu hasn't set up any address spaces itself,
         * give it the default one.
         */
        cpu->num_ases = 1;
        cpu_address_space_init(cpu, 0, "cpu-memory", cpu->memory);
    }

    if (cpus_accel) {
        /* accelerator already implements the CpusAccel interface */
        cpus_accel->create_vcpu_thread(cpu);
    } else if (kvm_enabled()) {
        qemu_kvm_start_vcpu(cpu);
    } else if (hax_enabled()) {
        qemu_hax_start_vcpu(cpu);
    } else if (hvf_enabled()) {
        qemu_hvf_start_vcpu(cpu);
    } else if (tcg_enabled()) {
        qemu_tcg_init_vcpu(cpu);
    } else if (whpx_enabled()) {
        qemu_whpx_start_vcpu(cpu);
    } else {
        qemu_dummy_start_vcpu(cpu);
    }

    while (!cpu->created) {
        qemu_cond_wait(&qemu_cpu_cond, &qemu_global_mutex);
    }
}

void cpu_stop_current(void)
{
    if (current_cpu) {
        current_cpu->stop = true;
        cpu_exit(current_cpu);
    }
}

int vm_stop(RunState state)
{
    if (qemu_in_vcpu_thread()) {
        qemu_system_vmstop_request_prepare();
        qemu_system_vmstop_request(state);
        /*
         * FIXME: should not return to device code in case
         * vm_stop() has been requested.
         */
        cpu_stop_current();
        return 0;
    }

    return do_vm_stop(state, true);
}

/**
 * Prepare for (re)starting the VM.
 * Returns -1 if the vCPUs are not to be restarted (e.g. if they are already
 * running or in case of an error condition), 0 otherwise.
 */
int vm_prepare_start(void)
{
    RunState requested;

    qemu_vmstop_requested(&requested);
    if (runstate_is_running() && requested == RUN_STATE__MAX) {
        return -1;
    }

    /* Ensure that a STOP/RESUME pair of events is emitted if a
     * vmstop request was pending.  The BLOCK_IO_ERROR event, for
     * example, according to documentation is always followed by
     * the STOP event.
     */
    if (runstate_is_running()) {
        qapi_event_send_stop();
        qapi_event_send_resume();
        return -1;
    }

    /* We are sending this now, but the CPUs will be resumed shortly later */
    qapi_event_send_resume();

    cpu_enable_ticks();
    runstate_set(RUN_STATE_RUNNING);
    vm_state_notify(1, RUN_STATE_RUNNING);
    return 0;
}

void vm_start(void)
{
    if (!vm_prepare_start()) {
        resume_all_vcpus();
    }
}

/* does a state transition even if the VM is already stopped,
   current state is forgotten forever */
int vm_stop_force_state(RunState state)
{
    if (runstate_is_running()) {
        return vm_stop(state);
    } else {
        runstate_set(state);

        bdrv_drain_all();
        /* Make sure to return an error if the flush in a previous vm_stop()
         * failed. */
        return bdrv_flush_all();
    }
}

void list_cpus(const char *optarg)
{
    /* XXX: implement xxx_cpu_list for targets that still miss it */
#if defined(cpu_list)
    cpu_list();
#endif
}

void qmp_memsave(int64_t addr, int64_t size, const char *filename,
                 bool has_cpu, int64_t cpu_index, Error **errp)
{
    FILE *f;
    uint32_t l;
    CPUState *cpu;
    uint8_t buf[1024];
    int64_t orig_addr = addr, orig_size = size;

    if (!has_cpu) {
        cpu_index = 0;
    }

    cpu = qemu_get_cpu(cpu_index);
    if (cpu == NULL) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "cpu-index",
                   "a CPU number");
        return;
    }

    f = fopen(filename, "wb");
    if (!f) {
        error_setg_file_open(errp, errno, filename);
        return;
    }

    while (size != 0) {
        l = sizeof(buf);
        if (l > size)
            l = size;
        if (cpu_memory_rw_debug(cpu, addr, buf, l, 0) != 0) {
            error_setg(errp, "Invalid addr 0x%016" PRIx64 "/size %" PRId64
                             " specified", orig_addr, orig_size);
            goto exit;
        }
        if (fwrite(buf, 1, l, f) != l) {
            error_setg(errp, QERR_IO_ERROR);
            goto exit;
        }
        addr += l;
        size -= l;
    }

exit:
    fclose(f);
}

void qmp_pmemsave(int64_t addr, int64_t size, const char *filename,
                  Error **errp)
{
    FILE *f;
    uint32_t l;
    uint8_t buf[1024];

    f = fopen(filename, "wb");
    if (!f) {
        error_setg_file_open(errp, errno, filename);
        return;
    }

    while (size != 0) {
        l = sizeof(buf);
        if (l > size)
            l = size;
        cpu_physical_memory_read(addr, buf, l);
        if (fwrite(buf, 1, l, f) != l) {
            error_setg(errp, QERR_IO_ERROR);
            goto exit;
        }
        addr += l;
        size -= l;
    }

exit:
    fclose(f);
}

void qmp_inject_nmi(Error **errp)
{
    nmi_monitor_handle(monitor_get_cpu_index(), errp);
}

