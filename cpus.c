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

/* Needed early for CONFIG_BSD etc. */
#include "config-host.h"

#include "monitor.h"
#include "sysemu.h"
#include "gdbstub.h"
#include "dma.h"
#include "kvm.h"
#include "qmp-commands.h"

#include "qemu-thread.h"
#include "cpus.h"
#include "qtest.h"
#include "main-loop.h"
#include "bitmap.h"

#ifndef _WIN32
#include "compatfd.h"
#endif

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

static CPUArchState *next_cpu;

static bool cpu_thread_is_idle(CPUArchState *env)
{
    CPUState *cpu = ENV_GET_CPU(env);

    if (cpu->stop || cpu->queued_work_first) {
        return false;
    }
    if (cpu->stopped || !runstate_is_running()) {
        return true;
    }
    if (!env->halted || qemu_cpu_has_work(cpu) ||
        kvm_async_interrupts_enabled()) {
        return false;
    }
    return true;
}

static bool all_cpu_threads_idle(void)
{
    CPUArchState *env;

    for (env = first_cpu; env != NULL; env = env->next_cpu) {
        if (!cpu_thread_is_idle(env)) {
            return false;
        }
    }
    return true;
}

/***********************************************************/
/* guest cycle counter */

/* Conversion factor from emulated instructions to virtual clock ticks.  */
static int icount_time_shift;
/* Arbitrarily pick 1MIPS as the minimum allowable speed.  */
#define MAX_ICOUNT_SHIFT 10
/* Compensate for varying guest execution speed.  */
static int64_t qemu_icount_bias;
static QEMUTimer *icount_rt_timer;
static QEMUTimer *icount_vm_timer;
static QEMUTimer *icount_warp_timer;
static int64_t vm_clock_warp_start;
static int64_t qemu_icount;

typedef struct TimersState {
    int64_t cpu_ticks_prev;
    int64_t cpu_ticks_offset;
    int64_t cpu_clock_offset;
    int32_t cpu_ticks_enabled;
    int64_t dummy;
} TimersState;

TimersState timers_state;

/* Return the virtual CPU time, based on the instruction counter.  */
int64_t cpu_get_icount(void)
{
    int64_t icount;
    CPUArchState *env = cpu_single_env;

    icount = qemu_icount;
    if (env) {
        if (!can_do_io(env)) {
            fprintf(stderr, "Bad clock read\n");
        }
        icount -= (env->icount_decr.u16.low + env->icount_extra);
    }
    return qemu_icount_bias + (icount << icount_time_shift);
}

/* return the host CPU cycle counter and handle stop/restart */
int64_t cpu_get_ticks(void)
{
    if (use_icount) {
        return cpu_get_icount();
    }
    if (!timers_state.cpu_ticks_enabled) {
        return timers_state.cpu_ticks_offset;
    } else {
        int64_t ticks;
        ticks = cpu_get_real_ticks();
        if (timers_state.cpu_ticks_prev > ticks) {
            /* Note: non increasing ticks may happen if the host uses
               software suspend */
            timers_state.cpu_ticks_offset += timers_state.cpu_ticks_prev - ticks;
        }
        timers_state.cpu_ticks_prev = ticks;
        return ticks + timers_state.cpu_ticks_offset;
    }
}

/* return the host CPU monotonic timer and handle stop/restart */
int64_t cpu_get_clock(void)
{
    int64_t ti;
    if (!timers_state.cpu_ticks_enabled) {
        return timers_state.cpu_clock_offset;
    } else {
        ti = get_clock();
        return ti + timers_state.cpu_clock_offset;
    }
}

/* enable cpu_get_ticks() */
void cpu_enable_ticks(void)
{
    if (!timers_state.cpu_ticks_enabled) {
        timers_state.cpu_ticks_offset -= cpu_get_real_ticks();
        timers_state.cpu_clock_offset -= get_clock();
        timers_state.cpu_ticks_enabled = 1;
    }
}

/* disable cpu_get_ticks() : the clock is stopped. You must not call
   cpu_get_ticks() after that.  */
void cpu_disable_ticks(void)
{
    if (timers_state.cpu_ticks_enabled) {
        timers_state.cpu_ticks_offset = cpu_get_ticks();
        timers_state.cpu_clock_offset = cpu_get_clock();
        timers_state.cpu_ticks_enabled = 0;
    }
}

/* Correlation between real and virtual time is always going to be
   fairly approximate, so ignore small variation.
   When the guest is idle real and virtual time will be aligned in
   the IO wait loop.  */
#define ICOUNT_WOBBLE (get_ticks_per_sec() / 10)

static void icount_adjust(void)
{
    int64_t cur_time;
    int64_t cur_icount;
    int64_t delta;
    static int64_t last_delta;
    /* If the VM is not running, then do nothing.  */
    if (!runstate_is_running()) {
        return;
    }
    cur_time = cpu_get_clock();
    cur_icount = qemu_get_clock_ns(vm_clock);
    delta = cur_icount - cur_time;
    /* FIXME: This is a very crude algorithm, somewhat prone to oscillation.  */
    if (delta > 0
        && last_delta + ICOUNT_WOBBLE < delta * 2
        && icount_time_shift > 0) {
        /* The guest is getting too far ahead.  Slow time down.  */
        icount_time_shift--;
    }
    if (delta < 0
        && last_delta - ICOUNT_WOBBLE > delta * 2
        && icount_time_shift < MAX_ICOUNT_SHIFT) {
        /* The guest is getting too far behind.  Speed time up.  */
        icount_time_shift++;
    }
    last_delta = delta;
    qemu_icount_bias = cur_icount - (qemu_icount << icount_time_shift);
}

static void icount_adjust_rt(void *opaque)
{
    qemu_mod_timer(icount_rt_timer,
                   qemu_get_clock_ms(rt_clock) + 1000);
    icount_adjust();
}

static void icount_adjust_vm(void *opaque)
{
    qemu_mod_timer(icount_vm_timer,
                   qemu_get_clock_ns(vm_clock) + get_ticks_per_sec() / 10);
    icount_adjust();
}

static int64_t qemu_icount_round(int64_t count)
{
    return (count + (1 << icount_time_shift) - 1) >> icount_time_shift;
}

static void icount_warp_rt(void *opaque)
{
    if (vm_clock_warp_start == -1) {
        return;
    }

    if (runstate_is_running()) {
        int64_t clock = qemu_get_clock_ns(rt_clock);
        int64_t warp_delta = clock - vm_clock_warp_start;
        if (use_icount == 1) {
            qemu_icount_bias += warp_delta;
        } else {
            /*
             * In adaptive mode, do not let the vm_clock run too
             * far ahead of real time.
             */
            int64_t cur_time = cpu_get_clock();
            int64_t cur_icount = qemu_get_clock_ns(vm_clock);
            int64_t delta = cur_time - cur_icount;
            qemu_icount_bias += MIN(warp_delta, delta);
        }
        if (qemu_clock_expired(vm_clock)) {
            qemu_notify_event();
        }
    }
    vm_clock_warp_start = -1;
}

void qtest_clock_warp(int64_t dest)
{
    int64_t clock = qemu_get_clock_ns(vm_clock);
    assert(qtest_enabled());
    while (clock < dest) {
        int64_t deadline = qemu_clock_deadline(vm_clock);
        int64_t warp = MIN(dest - clock, deadline);
        qemu_icount_bias += warp;
        qemu_run_timers(vm_clock);
        clock = qemu_get_clock_ns(vm_clock);
    }
    qemu_notify_event();
}

void qemu_clock_warp(QEMUClock *clock)
{
    int64_t deadline;

    /*
     * There are too many global variables to make the "warp" behavior
     * applicable to other clocks.  But a clock argument removes the
     * need for if statements all over the place.
     */
    if (clock != vm_clock || !use_icount) {
        return;
    }

    /*
     * If the CPUs have been sleeping, advance the vm_clock timer now.  This
     * ensures that the deadline for the timer is computed correctly below.
     * This also makes sure that the insn counter is synchronized before the
     * CPU starts running, in case the CPU is woken by an event other than
     * the earliest vm_clock timer.
     */
    icount_warp_rt(NULL);
    if (!all_cpu_threads_idle() || !qemu_clock_has_timers(vm_clock)) {
        qemu_del_timer(icount_warp_timer);
        return;
    }

    if (qtest_enabled()) {
        /* When testing, qtest commands advance icount.  */
	return;
    }

    vm_clock_warp_start = qemu_get_clock_ns(rt_clock);
    deadline = qemu_clock_deadline(vm_clock);
    if (deadline > 0) {
        /*
         * Ensure the vm_clock proceeds even when the virtual CPU goes to
         * sleep.  Otherwise, the CPU might be waiting for a future timer
         * interrupt to wake it up, but the interrupt never comes because
         * the vCPU isn't running any insns and thus doesn't advance the
         * vm_clock.
         *
         * An extreme solution for this problem would be to never let VCPUs
         * sleep in icount mode if there is a pending vm_clock timer; rather
         * time could just advance to the next vm_clock event.  Instead, we
         * do stop VCPUs and only advance vm_clock after some "real" time,
         * (related to the time left until the next event) has passed.  This
         * rt_clock timer will do this.  This avoids that the warps are too
         * visible externally---for example, you will not be sending network
         * packets continuously instead of every 100ms.
         */
        qemu_mod_timer(icount_warp_timer, vm_clock_warp_start + deadline);
    } else {
        qemu_notify_event();
    }
}

static const VMStateDescription vmstate_timers = {
    .name = "timer",
    .version_id = 2,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_INT64(cpu_ticks_offset, TimersState),
        VMSTATE_INT64(dummy, TimersState),
        VMSTATE_INT64_V(cpu_clock_offset, TimersState, 2),
        VMSTATE_END_OF_LIST()
    }
};

void configure_icount(const char *option)
{
    vmstate_register(NULL, 0, &vmstate_timers, &timers_state);
    if (!option) {
        return;
    }

    icount_warp_timer = qemu_new_timer_ns(rt_clock, icount_warp_rt, NULL);
    if (strcmp(option, "auto") != 0) {
        icount_time_shift = strtol(option, NULL, 0);
        use_icount = 1;
        return;
    }

    use_icount = 2;

    /* 125MIPS seems a reasonable initial guess at the guest speed.
       It will be corrected fairly quickly anyway.  */
    icount_time_shift = 3;

    /* Have both realtime and virtual time triggers for speed adjustment.
       The realtime trigger catches emulated time passing too slowly,
       the virtual time trigger catches emulated time passing too fast.
       Realtime triggers occur even when idle, so use them less frequently
       than VM triggers.  */
    icount_rt_timer = qemu_new_timer_ms(rt_clock, icount_adjust_rt, NULL);
    qemu_mod_timer(icount_rt_timer,
                   qemu_get_clock_ms(rt_clock) + 1000);
    icount_vm_timer = qemu_new_timer_ns(vm_clock, icount_adjust_vm, NULL);
    qemu_mod_timer(icount_vm_timer,
                   qemu_get_clock_ns(vm_clock) + get_ticks_per_sec() / 10);
}

/***********************************************************/
void hw_error(const char *fmt, ...)
{
    va_list ap;
    CPUArchState *env;

    va_start(ap, fmt);
    fprintf(stderr, "qemu: hardware error: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    for(env = first_cpu; env != NULL; env = env->next_cpu) {
        fprintf(stderr, "CPU #%d:\n", env->cpu_index);
        cpu_dump_state(env, stderr, fprintf, CPU_DUMP_FPU);
    }
    va_end(ap);
    abort();
}

void cpu_synchronize_all_states(void)
{
    CPUArchState *cpu;

    for (cpu = first_cpu; cpu; cpu = cpu->next_cpu) {
        cpu_synchronize_state(cpu);
    }
}

void cpu_synchronize_all_post_reset(void)
{
    CPUArchState *cpu;

    for (cpu = first_cpu; cpu; cpu = cpu->next_cpu) {
        cpu_synchronize_post_reset(cpu);
    }
}

void cpu_synchronize_all_post_init(void)
{
    CPUArchState *cpu;

    for (cpu = first_cpu; cpu; cpu = cpu->next_cpu) {
        cpu_synchronize_post_init(cpu);
    }
}

bool cpu_is_stopped(CPUState *cpu)
{
    return !runstate_is_running() || cpu->stopped;
}

static void do_vm_stop(RunState state)
{
    if (runstate_is_running()) {
        cpu_disable_ticks();
        pause_all_vcpus();
        runstate_set(state);
        vm_state_notify(0, state);
        bdrv_drain_all();
        bdrv_flush_all();
        monitor_protocol_event(QEVENT_STOP, NULL);
    }
}

static bool cpu_can_run(CPUState *cpu)
{
    if (cpu->stop) {
        return false;
    }
    if (cpu->stopped || !runstate_is_running()) {
        return false;
    }
    return true;
}

static void cpu_handle_guest_debug(CPUArchState *env)
{
    CPUState *cpu = ENV_GET_CPU(env);

    gdb_set_stop_cpu(env);
    qemu_system_debug_request();
    cpu->stopped = true;
}

static void cpu_signal(int sig)
{
    if (cpu_single_env) {
        cpu_exit(cpu_single_env);
    }
    exit_request = 1;
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
        sigprocmask(SIG_UNBLOCK, &set, NULL);
    }
    perror("Failed to re-raise SIGBUS!\n");
    abort();
}

static void sigbus_handler(int n, struct qemu_signalfd_siginfo *siginfo,
                           void *ctx)
{
    if (kvm_on_sigbus(siginfo->ssi_code,
                      (void *)(intptr_t)siginfo->ssi_addr)) {
        sigbus_reraise();
    }
}

static void qemu_init_sigbus(void)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_flags = SA_SIGINFO;
    action.sa_sigaction = (void (*)(int, siginfo_t*, void*))sigbus_handler;
    sigaction(SIGBUS, &action, NULL);

    prctl(PR_MCE_KILL, PR_MCE_KILL_SET, PR_MCE_KILL_EARLY, 0, 0);
}

static void qemu_kvm_eat_signals(CPUArchState *env)
{
    struct timespec ts = { 0, 0 };
    siginfo_t siginfo;
    sigset_t waitset;
    sigset_t chkset;
    int r;

    sigemptyset(&waitset);
    sigaddset(&waitset, SIG_IPI);
    sigaddset(&waitset, SIGBUS);

    do {
        r = sigtimedwait(&waitset, &siginfo, &ts);
        if (r == -1 && !(errno == EAGAIN || errno == EINTR)) {
            perror("sigtimedwait");
            exit(1);
        }

        switch (r) {
        case SIGBUS:
            if (kvm_on_sigbus_vcpu(env, siginfo.si_code, siginfo.si_addr)) {
                sigbus_reraise();
            }
            break;
        default:
            break;
        }

        r = sigpending(&chkset);
        if (r == -1) {
            perror("sigpending");
            exit(1);
        }
    } while (sigismember(&chkset, SIG_IPI) || sigismember(&chkset, SIGBUS));
}

#else /* !CONFIG_LINUX */

static void qemu_init_sigbus(void)
{
}

static void qemu_kvm_eat_signals(CPUArchState *env)
{
}
#endif /* !CONFIG_LINUX */

#ifndef _WIN32
static void dummy_signal(int sig)
{
}

static void qemu_kvm_init_cpu_signals(CPUArchState *env)
{
    int r;
    sigset_t set;
    struct sigaction sigact;

    memset(&sigact, 0, sizeof(sigact));
    sigact.sa_handler = dummy_signal;
    sigaction(SIG_IPI, &sigact, NULL);

    pthread_sigmask(SIG_BLOCK, NULL, &set);
    sigdelset(&set, SIG_IPI);
    sigdelset(&set, SIGBUS);
    r = kvm_set_signal_mask(env, &set);
    if (r) {
        fprintf(stderr, "kvm_set_signal_mask: %s\n", strerror(-r));
        exit(1);
    }
}

static void qemu_tcg_init_cpu_signals(void)
{
    sigset_t set;
    struct sigaction sigact;

    memset(&sigact, 0, sizeof(sigact));
    sigact.sa_handler = cpu_signal;
    sigaction(SIG_IPI, &sigact, NULL);

    sigemptyset(&set);
    sigaddset(&set, SIG_IPI);
    pthread_sigmask(SIG_UNBLOCK, &set, NULL);
}

#else /* _WIN32 */
static void qemu_kvm_init_cpu_signals(CPUArchState *env)
{
    abort();
}

static void qemu_tcg_init_cpu_signals(void)
{
}
#endif /* _WIN32 */

static QemuMutex qemu_global_mutex;
static QemuCond qemu_io_proceeded_cond;
static bool iothread_requesting_mutex;

static QemuThread io_thread;

static QemuThread *tcg_cpu_thread;
static QemuCond *tcg_halt_cond;

/* cpu creation */
static QemuCond qemu_cpu_cond;
/* system init */
static QemuCond qemu_pause_cond;
static QemuCond qemu_work_cond;

void qemu_init_cpu_loop(void)
{
    qemu_init_sigbus();
    qemu_cond_init(&qemu_cpu_cond);
    qemu_cond_init(&qemu_pause_cond);
    qemu_cond_init(&qemu_work_cond);
    qemu_cond_init(&qemu_io_proceeded_cond);
    qemu_mutex_init(&qemu_global_mutex);

    qemu_thread_get_self(&io_thread);
}

void run_on_cpu(CPUState *cpu, void (*func)(void *data), void *data)
{
    struct qemu_work_item wi;

    if (qemu_cpu_is_self(cpu)) {
        func(data);
        return;
    }

    wi.func = func;
    wi.data = data;
    if (cpu->queued_work_first == NULL) {
        cpu->queued_work_first = &wi;
    } else {
        cpu->queued_work_last->next = &wi;
    }
    cpu->queued_work_last = &wi;
    wi.next = NULL;
    wi.done = false;

    qemu_cpu_kick(cpu);
    while (!wi.done) {
        CPUArchState *self_env = cpu_single_env;

        qemu_cond_wait(&qemu_work_cond, &qemu_global_mutex);
        cpu_single_env = self_env;
    }
}

static void flush_queued_work(CPUState *cpu)
{
    struct qemu_work_item *wi;

    if (cpu->queued_work_first == NULL) {
        return;
    }

    while ((wi = cpu->queued_work_first)) {
        cpu->queued_work_first = wi->next;
        wi->func(wi->data);
        wi->done = true;
    }
    cpu->queued_work_last = NULL;
    qemu_cond_broadcast(&qemu_work_cond);
}

static void qemu_wait_io_event_common(CPUState *cpu)
{
    if (cpu->stop) {
        cpu->stop = false;
        cpu->stopped = true;
        qemu_cond_signal(&qemu_pause_cond);
    }
    flush_queued_work(cpu);
    cpu->thread_kicked = false;
}

static void qemu_tcg_wait_io_event(void)
{
    CPUArchState *env;

    while (all_cpu_threads_idle()) {
       /* Start accounting real time to the virtual clock if the CPUs
          are idle.  */
        qemu_clock_warp(vm_clock);
        qemu_cond_wait(tcg_halt_cond, &qemu_global_mutex);
    }

    while (iothread_requesting_mutex) {
        qemu_cond_wait(&qemu_io_proceeded_cond, &qemu_global_mutex);
    }

    for (env = first_cpu; env != NULL; env = env->next_cpu) {
        qemu_wait_io_event_common(ENV_GET_CPU(env));
    }
}

static void qemu_kvm_wait_io_event(CPUArchState *env)
{
    CPUState *cpu = ENV_GET_CPU(env);

    while (cpu_thread_is_idle(env)) {
        qemu_cond_wait(cpu->halt_cond, &qemu_global_mutex);
    }

    qemu_kvm_eat_signals(env);
    qemu_wait_io_event_common(cpu);
}

static void *qemu_kvm_cpu_thread_fn(void *arg)
{
    CPUArchState *env = arg;
    CPUState *cpu = ENV_GET_CPU(env);
    int r;

    qemu_mutex_lock(&qemu_global_mutex);
    qemu_thread_get_self(cpu->thread);
    cpu->thread_id = qemu_get_thread_id();
    cpu_single_env = env;

    r = kvm_init_vcpu(env);
    if (r < 0) {
        fprintf(stderr, "kvm_init_vcpu failed: %s\n", strerror(-r));
        exit(1);
    }

    qemu_kvm_init_cpu_signals(env);

    /* signal CPU creation */
    cpu->created = true;
    qemu_cond_signal(&qemu_cpu_cond);

    while (1) {
        if (cpu_can_run(cpu)) {
            r = kvm_cpu_exec(env);
            if (r == EXCP_DEBUG) {
                cpu_handle_guest_debug(env);
            }
        }
        qemu_kvm_wait_io_event(env);
    }

    return NULL;
}

static void *qemu_dummy_cpu_thread_fn(void *arg)
{
#ifdef _WIN32
    fprintf(stderr, "qtest is not supported under Windows\n");
    exit(1);
#else
    CPUArchState *env = arg;
    CPUState *cpu = ENV_GET_CPU(env);
    sigset_t waitset;
    int r;

    qemu_mutex_lock_iothread();
    qemu_thread_get_self(cpu->thread);
    cpu->thread_id = qemu_get_thread_id();

    sigemptyset(&waitset);
    sigaddset(&waitset, SIG_IPI);

    /* signal CPU creation */
    cpu->created = true;
    qemu_cond_signal(&qemu_cpu_cond);

    cpu_single_env = env;
    while (1) {
        cpu_single_env = NULL;
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
        cpu_single_env = env;
        qemu_wait_io_event_common(cpu);
    }

    return NULL;
#endif
}

static void tcg_exec_all(void);

static void *qemu_tcg_cpu_thread_fn(void *arg)
{
    CPUState *cpu = arg;
    CPUArchState *env;

    qemu_tcg_init_cpu_signals();
    qemu_thread_get_self(cpu->thread);

    /* signal CPU creation */
    qemu_mutex_lock(&qemu_global_mutex);
    for (env = first_cpu; env != NULL; env = env->next_cpu) {
        cpu = ENV_GET_CPU(env);
        cpu->thread_id = qemu_get_thread_id();
        cpu->created = true;
    }
    qemu_cond_signal(&qemu_cpu_cond);

    /* wait for initial kick-off after machine start */
    while (ENV_GET_CPU(first_cpu)->stopped) {
        qemu_cond_wait(tcg_halt_cond, &qemu_global_mutex);

        /* process any pending work */
        for (env = first_cpu; env != NULL; env = env->next_cpu) {
            qemu_wait_io_event_common(ENV_GET_CPU(env));
        }
    }

    while (1) {
        tcg_exec_all();
        if (use_icount && qemu_clock_deadline(vm_clock) <= 0) {
            qemu_notify_event();
        }
        qemu_tcg_wait_io_event();
    }

    return NULL;
}

static void qemu_cpu_kick_thread(CPUState *cpu)
{
#ifndef _WIN32
    int err;

    err = pthread_kill(cpu->thread->thread, SIG_IPI);
    if (err) {
        fprintf(stderr, "qemu:%s: %s", __func__, strerror(err));
        exit(1);
    }
#else /* _WIN32 */
    if (!qemu_cpu_is_self(cpu)) {
        SuspendThread(cpu->hThread);
        cpu_signal(0);
        ResumeThread(cpu->hThread);
    }
#endif
}

void qemu_cpu_kick(CPUState *cpu)
{
    qemu_cond_broadcast(cpu->halt_cond);
    if (!tcg_enabled() && !cpu->thread_kicked) {
        qemu_cpu_kick_thread(cpu);
        cpu->thread_kicked = true;
    }
}

void qemu_cpu_kick_self(void)
{
#ifndef _WIN32
    assert(cpu_single_env);
    CPUState *cpu_single_cpu = ENV_GET_CPU(cpu_single_env);

    if (!cpu_single_cpu->thread_kicked) {
        qemu_cpu_kick_thread(cpu_single_cpu);
        cpu_single_cpu->thread_kicked = true;
    }
#else
    abort();
#endif
}

bool qemu_cpu_is_self(CPUState *cpu)
{
    return qemu_thread_is_self(cpu->thread);
}

static bool qemu_in_vcpu_thread(void)
{
    return cpu_single_env && qemu_cpu_is_self(ENV_GET_CPU(cpu_single_env));
}

void qemu_mutex_lock_iothread(void)
{
    if (!tcg_enabled()) {
        qemu_mutex_lock(&qemu_global_mutex);
    } else {
        iothread_requesting_mutex = true;
        if (qemu_mutex_trylock(&qemu_global_mutex)) {
            qemu_cpu_kick_thread(ENV_GET_CPU(first_cpu));
            qemu_mutex_lock(&qemu_global_mutex);
        }
        iothread_requesting_mutex = false;
        qemu_cond_broadcast(&qemu_io_proceeded_cond);
    }
}

void qemu_mutex_unlock_iothread(void)
{
    qemu_mutex_unlock(&qemu_global_mutex);
}

static int all_vcpus_paused(void)
{
    CPUArchState *penv = first_cpu;

    while (penv) {
        CPUState *pcpu = ENV_GET_CPU(penv);
        if (!pcpu->stopped) {
            return 0;
        }
        penv = penv->next_cpu;
    }

    return 1;
}

void pause_all_vcpus(void)
{
    CPUArchState *penv = first_cpu;

    qemu_clock_enable(vm_clock, false);
    while (penv) {
        CPUState *pcpu = ENV_GET_CPU(penv);
        pcpu->stop = true;
        qemu_cpu_kick(pcpu);
        penv = penv->next_cpu;
    }

    if (qemu_in_vcpu_thread()) {
        cpu_stop_current();
        if (!kvm_enabled()) {
            while (penv) {
                CPUState *pcpu = ENV_GET_CPU(penv);
                pcpu->stop = 0;
                pcpu->stopped = true;
                penv = penv->next_cpu;
            }
            return;
        }
    }

    while (!all_vcpus_paused()) {
        qemu_cond_wait(&qemu_pause_cond, &qemu_global_mutex);
        penv = first_cpu;
        while (penv) {
            qemu_cpu_kick(ENV_GET_CPU(penv));
            penv = penv->next_cpu;
        }
    }
}

void resume_all_vcpus(void)
{
    CPUArchState *penv = first_cpu;

    qemu_clock_enable(vm_clock, true);
    while (penv) {
        CPUState *pcpu = ENV_GET_CPU(penv);
        pcpu->stop = false;
        pcpu->stopped = false;
        qemu_cpu_kick(pcpu);
        penv = penv->next_cpu;
    }
}

static void qemu_tcg_init_vcpu(CPUState *cpu)
{
    /* share a single thread for all cpus with TCG */
    if (!tcg_cpu_thread) {
        cpu->thread = g_malloc0(sizeof(QemuThread));
        cpu->halt_cond = g_malloc0(sizeof(QemuCond));
        qemu_cond_init(cpu->halt_cond);
        tcg_halt_cond = cpu->halt_cond;
        qemu_thread_create(cpu->thread, qemu_tcg_cpu_thread_fn, cpu,
                           QEMU_THREAD_JOINABLE);
#ifdef _WIN32
        cpu->hThread = qemu_thread_get_handle(cpu->thread);
#endif
        while (!cpu->created) {
            qemu_cond_wait(&qemu_cpu_cond, &qemu_global_mutex);
        }
        tcg_cpu_thread = cpu->thread;
    } else {
        cpu->thread = tcg_cpu_thread;
        cpu->halt_cond = tcg_halt_cond;
    }
}

static void qemu_kvm_start_vcpu(CPUArchState *env)
{
    CPUState *cpu = ENV_GET_CPU(env);

    cpu->thread = g_malloc0(sizeof(QemuThread));
    cpu->halt_cond = g_malloc0(sizeof(QemuCond));
    qemu_cond_init(cpu->halt_cond);
    qemu_thread_create(cpu->thread, qemu_kvm_cpu_thread_fn, env,
                       QEMU_THREAD_JOINABLE);
    while (!cpu->created) {
        qemu_cond_wait(&qemu_cpu_cond, &qemu_global_mutex);
    }
}

static void qemu_dummy_start_vcpu(CPUArchState *env)
{
    CPUState *cpu = ENV_GET_CPU(env);

    cpu->thread = g_malloc0(sizeof(QemuThread));
    cpu->halt_cond = g_malloc0(sizeof(QemuCond));
    qemu_cond_init(cpu->halt_cond);
    qemu_thread_create(cpu->thread, qemu_dummy_cpu_thread_fn, env,
                       QEMU_THREAD_JOINABLE);
    while (!cpu->created) {
        qemu_cond_wait(&qemu_cpu_cond, &qemu_global_mutex);
    }
}

void qemu_init_vcpu(void *_env)
{
    CPUArchState *env = _env;
    CPUState *cpu = ENV_GET_CPU(env);

    env->nr_cores = smp_cores;
    env->nr_threads = smp_threads;
    cpu->stopped = true;
    if (kvm_enabled()) {
        qemu_kvm_start_vcpu(env);
    } else if (tcg_enabled()) {
        qemu_tcg_init_vcpu(cpu);
    } else {
        qemu_dummy_start_vcpu(env);
    }
}

void cpu_stop_current(void)
{
    if (cpu_single_env) {
        CPUState *cpu_single_cpu = ENV_GET_CPU(cpu_single_env);
        cpu_single_cpu->stop = false;
        cpu_single_cpu->stopped = true;
        cpu_exit(cpu_single_env);
        qemu_cond_signal(&qemu_pause_cond);
    }
}

void vm_stop(RunState state)
{
    if (qemu_in_vcpu_thread()) {
        qemu_system_vmstop_request(state);
        /*
         * FIXME: should not return to device code in case
         * vm_stop() has been requested.
         */
        cpu_stop_current();
        return;
    }
    do_vm_stop(state);
}

/* does a state transition even if the VM is already stopped,
   current state is forgotten forever */
void vm_stop_force_state(RunState state)
{
    if (runstate_is_running()) {
        vm_stop(state);
    } else {
        runstate_set(state);
    }
}

static int tcg_cpu_exec(CPUArchState *env)
{
    int ret;
#ifdef CONFIG_PROFILER
    int64_t ti;
#endif

#ifdef CONFIG_PROFILER
    ti = profile_getclock();
#endif
    if (use_icount) {
        int64_t count;
        int decr;
        qemu_icount -= (env->icount_decr.u16.low + env->icount_extra);
        env->icount_decr.u16.low = 0;
        env->icount_extra = 0;
        count = qemu_icount_round(qemu_clock_deadline(vm_clock));
        qemu_icount += count;
        decr = (count > 0xffff) ? 0xffff : count;
        count -= decr;
        env->icount_decr.u16.low = decr;
        env->icount_extra = count;
    }
    ret = cpu_exec(env);
#ifdef CONFIG_PROFILER
    qemu_time += profile_getclock() - ti;
#endif
    if (use_icount) {
        /* Fold pending instructions back into the
           instruction counter, and clear the interrupt flag.  */
        qemu_icount -= (env->icount_decr.u16.low
                        + env->icount_extra);
        env->icount_decr.u32 = 0;
        env->icount_extra = 0;
    }
    return ret;
}

static void tcg_exec_all(void)
{
    int r;

    /* Account partial waits to the vm_clock.  */
    qemu_clock_warp(vm_clock);

    if (next_cpu == NULL) {
        next_cpu = first_cpu;
    }
    for (; next_cpu != NULL && !exit_request; next_cpu = next_cpu->next_cpu) {
        CPUArchState *env = next_cpu;
        CPUState *cpu = ENV_GET_CPU(env);

        qemu_clock_enable(vm_clock,
                          (env->singlestep_enabled & SSTEP_NOTIMER) == 0);

        if (cpu_can_run(cpu)) {
            r = tcg_cpu_exec(env);
            if (r == EXCP_DEBUG) {
                cpu_handle_guest_debug(env);
                break;
            }
        } else if (cpu->stop || cpu->stopped) {
            break;
        }
    }
    exit_request = 0;
}

void set_numa_modes(void)
{
    CPUArchState *env;
    int i;

    for (env = first_cpu; env != NULL; env = env->next_cpu) {
        for (i = 0; i < nb_numa_nodes; i++) {
            if (test_bit(env->cpu_index, node_cpumask[i])) {
                env->numa_node = i;
            }
        }
    }
}

void set_cpu_log(const char *optarg)
{
    int mask;
    const CPULogItem *item;

    mask = cpu_str_to_log_mask(optarg);
    if (!mask) {
        printf("Log items (comma separated):\n");
        for (item = cpu_log_items; item->mask != 0; item++) {
            printf("%-10s %s\n", item->name, item->help);
        }
        exit(1);
    }
    cpu_set_log(mask);
}

void set_cpu_log_filename(const char *optarg)
{
    cpu_set_log_filename(optarg);
}

void list_cpus(FILE *f, fprintf_function cpu_fprintf, const char *optarg)
{
    /* XXX: implement xxx_cpu_list for targets that still miss it */
#if defined(cpu_list)
    cpu_list(f, cpu_fprintf);
#endif
}

CpuInfoList *qmp_query_cpus(Error **errp)
{
    CpuInfoList *head = NULL, *cur_item = NULL;
    CPUArchState *env;

    for (env = first_cpu; env != NULL; env = env->next_cpu) {
        CPUState *cpu = ENV_GET_CPU(env);
        CpuInfoList *info;

        cpu_synchronize_state(env);

        info = g_malloc0(sizeof(*info));
        info->value = g_malloc0(sizeof(*info->value));
        info->value->CPU = env->cpu_index;
        info->value->current = (env == first_cpu);
        info->value->halted = env->halted;
        info->value->thread_id = cpu->thread_id;
#if defined(TARGET_I386)
        info->value->has_pc = true;
        info->value->pc = env->eip + env->segs[R_CS].base;
#elif defined(TARGET_PPC)
        info->value->has_nip = true;
        info->value->nip = env->nip;
#elif defined(TARGET_SPARC)
        info->value->has_pc = true;
        info->value->pc = env->pc;
        info->value->has_npc = true;
        info->value->npc = env->npc;
#elif defined(TARGET_MIPS)
        info->value->has_PC = true;
        info->value->PC = env->active_tc.PC;
#endif

        /* XXX: waiting for the qapi to support GSList */
        if (!cur_item) {
            head = cur_item = info;
        } else {
            cur_item->next = info;
            cur_item = info;
        }
    }

    return head;
}

void qmp_memsave(int64_t addr, int64_t size, const char *filename,
                 bool has_cpu, int64_t cpu_index, Error **errp)
{
    FILE *f;
    uint32_t l;
    CPUArchState *env;
    uint8_t buf[1024];

    if (!has_cpu) {
        cpu_index = 0;
    }

    for (env = first_cpu; env; env = env->next_cpu) {
        if (cpu_index == env->cpu_index) {
            break;
        }
    }

    if (env == NULL) {
        error_set(errp, QERR_INVALID_PARAMETER_VALUE, "cpu-index",
                  "a CPU number");
        return;
    }

    f = fopen(filename, "wb");
    if (!f) {
        error_set(errp, QERR_OPEN_FILE_FAILED, filename);
        return;
    }

    while (size != 0) {
        l = sizeof(buf);
        if (l > size)
            l = size;
        cpu_memory_rw_debug(env, addr, buf, l, 0);
        if (fwrite(buf, 1, l, f) != l) {
            error_set(errp, QERR_IO_ERROR);
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
        error_set(errp, QERR_OPEN_FILE_FAILED, filename);
        return;
    }

    while (size != 0) {
        l = sizeof(buf);
        if (l > size)
            l = size;
        cpu_physical_memory_rw(addr, buf, l, 0);
        if (fwrite(buf, 1, l, f) != l) {
            error_set(errp, QERR_IO_ERROR);
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
#if defined(TARGET_I386)
    CPUArchState *env;

    for (env = first_cpu; env != NULL; env = env->next_cpu) {
        if (!env->apic_state) {
            cpu_interrupt(env, CPU_INTERRUPT_NMI);
        } else {
            apic_deliver_nmi(env->apic_state);
        }
    }
#else
    error_set(errp, QERR_UNSUPPORTED);
#endif
}
