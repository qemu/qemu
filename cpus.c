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
#include "main-loop.h"

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

static CPUState *next_cpu;

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
    CPUState *env = cpu_single_env;;

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
         * packets continously instead of every 100ms.
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
    CPUState *env;

    va_start(ap, fmt);
    fprintf(stderr, "qemu: hardware error: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    for(env = first_cpu; env != NULL; env = env->next_cpu) {
        fprintf(stderr, "CPU #%d:\n", env->cpu_index);
#ifdef TARGET_I386
        cpu_dump_state(env, stderr, fprintf, X86_DUMP_FPU);
#else
        cpu_dump_state(env, stderr, fprintf, 0);
#endif
    }
    va_end(ap);
    abort();
}

void cpu_synchronize_all_states(void)
{
    CPUState *cpu;

    for (cpu = first_cpu; cpu; cpu = cpu->next_cpu) {
        cpu_synchronize_state(cpu);
    }
}

void cpu_synchronize_all_post_reset(void)
{
    CPUState *cpu;

    for (cpu = first_cpu; cpu; cpu = cpu->next_cpu) {
        cpu_synchronize_post_reset(cpu);
    }
}

void cpu_synchronize_all_post_init(void)
{
    CPUState *cpu;

    for (cpu = first_cpu; cpu; cpu = cpu->next_cpu) {
        cpu_synchronize_post_init(cpu);
    }
}

int cpu_is_stopped(CPUState *env)
{
    return !runstate_is_running() || env->stopped;
}

static void do_vm_stop(RunState state)
{
    if (runstate_is_running()) {
        cpu_disable_ticks();
        pause_all_vcpus();
        runstate_set(state);
        vm_state_notify(0, state);
        qemu_aio_flush();
        bdrv_flush_all();
        monitor_protocol_event(QEVENT_STOP, NULL);
    }
}

static int cpu_can_run(CPUState *env)
{
    if (env->stop) {
        return 0;
    }
    if (env->stopped || !runstate_is_running()) {
        return 0;
    }
    return 1;
}

static bool cpu_thread_is_idle(CPUState *env)
{
    if (env->stop || env->queued_work_first) {
        return false;
    }
    if (env->stopped || !runstate_is_running()) {
        return true;
    }
    if (!env->halted || qemu_cpu_has_work(env) ||
        (kvm_enabled() && kvm_irqchip_in_kernel())) {
        return false;
    }
    return true;
}

bool all_cpu_threads_idle(void)
{
    CPUState *env;

    for (env = first_cpu; env != NULL; env = env->next_cpu) {
        if (!cpu_thread_is_idle(env)) {
            return false;
        }
    }
    return true;
}

static void cpu_handle_guest_debug(CPUState *env)
{
    gdb_set_stop_cpu(env);
    qemu_system_debug_request();
    env->stopped = 1;
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

static void qemu_kvm_eat_signals(CPUState *env)
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

static void qemu_kvm_eat_signals(CPUState *env)
{
}
#endif /* !CONFIG_LINUX */

#ifndef _WIN32
static void dummy_signal(int sig)
{
}

static void qemu_kvm_init_cpu_signals(CPUState *env)
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
static void qemu_kvm_init_cpu_signals(CPUState *env)
{
    abort();
}

static void qemu_tcg_init_cpu_signals(void)
{
}
#endif /* _WIN32 */

QemuMutex qemu_global_mutex;
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

void run_on_cpu(CPUState *env, void (*func)(void *data), void *data)
{
    struct qemu_work_item wi;

    if (qemu_cpu_is_self(env)) {
        func(data);
        return;
    }

    wi.func = func;
    wi.data = data;
    if (!env->queued_work_first) {
        env->queued_work_first = &wi;
    } else {
        env->queued_work_last->next = &wi;
    }
    env->queued_work_last = &wi;
    wi.next = NULL;
    wi.done = false;

    qemu_cpu_kick(env);
    while (!wi.done) {
        CPUState *self_env = cpu_single_env;

        qemu_cond_wait(&qemu_work_cond, &qemu_global_mutex);
        cpu_single_env = self_env;
    }
}

static void flush_queued_work(CPUState *env)
{
    struct qemu_work_item *wi;

    if (!env->queued_work_first) {
        return;
    }

    while ((wi = env->queued_work_first)) {
        env->queued_work_first = wi->next;
        wi->func(wi->data);
        wi->done = true;
    }
    env->queued_work_last = NULL;
    qemu_cond_broadcast(&qemu_work_cond);
}

static void qemu_wait_io_event_common(CPUState *env)
{
    if (env->stop) {
        env->stop = 0;
        env->stopped = 1;
        qemu_cond_signal(&qemu_pause_cond);
    }
    flush_queued_work(env);
    env->thread_kicked = false;
}

static void qemu_tcg_wait_io_event(void)
{
    CPUState *env;

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
        qemu_wait_io_event_common(env);
    }
}

static void qemu_kvm_wait_io_event(CPUState *env)
{
    while (cpu_thread_is_idle(env)) {
        qemu_cond_wait(env->halt_cond, &qemu_global_mutex);
    }

    qemu_kvm_eat_signals(env);
    qemu_wait_io_event_common(env);
}

static void *qemu_kvm_cpu_thread_fn(void *arg)
{
    CPUState *env = arg;
    int r;

    qemu_mutex_lock(&qemu_global_mutex);
    qemu_thread_get_self(env->thread);
    env->thread_id = qemu_get_thread_id();

    r = kvm_init_vcpu(env);
    if (r < 0) {
        fprintf(stderr, "kvm_init_vcpu failed: %s\n", strerror(-r));
        exit(1);
    }

    qemu_kvm_init_cpu_signals(env);

    /* signal CPU creation */
    env->created = 1;
    qemu_cond_signal(&qemu_cpu_cond);

    while (1) {
        if (cpu_can_run(env)) {
            r = kvm_cpu_exec(env);
            if (r == EXCP_DEBUG) {
                cpu_handle_guest_debug(env);
            }
        }
        qemu_kvm_wait_io_event(env);
    }

    return NULL;
}

static void tcg_exec_all(void);

static void *qemu_tcg_cpu_thread_fn(void *arg)
{
    CPUState *env = arg;

    qemu_tcg_init_cpu_signals();
    qemu_thread_get_self(env->thread);

    /* signal CPU creation */
    qemu_mutex_lock(&qemu_global_mutex);
    for (env = first_cpu; env != NULL; env = env->next_cpu) {
        env->thread_id = qemu_get_thread_id();
        env->created = 1;
    }
    qemu_cond_signal(&qemu_cpu_cond);

    /* wait for initial kick-off after machine start */
    while (first_cpu->stopped) {
        qemu_cond_wait(tcg_halt_cond, &qemu_global_mutex);
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

static void qemu_cpu_kick_thread(CPUState *env)
{
#ifndef _WIN32
    int err;

    err = pthread_kill(env->thread->thread, SIG_IPI);
    if (err) {
        fprintf(stderr, "qemu:%s: %s", __func__, strerror(err));
        exit(1);
    }
#else /* _WIN32 */
    if (!qemu_cpu_is_self(env)) {
        SuspendThread(env->thread->thread);
        cpu_signal(0);
        ResumeThread(env->thread->thread);
    }
#endif
}

void qemu_cpu_kick(void *_env)
{
    CPUState *env = _env;

    qemu_cond_broadcast(env->halt_cond);
    if (kvm_enabled() && !env->thread_kicked) {
        qemu_cpu_kick_thread(env);
        env->thread_kicked = true;
    }
}

void qemu_cpu_kick_self(void)
{
#ifndef _WIN32
    assert(cpu_single_env);

    if (!cpu_single_env->thread_kicked) {
        qemu_cpu_kick_thread(cpu_single_env);
        cpu_single_env->thread_kicked = true;
    }
#else
    abort();
#endif
}

int qemu_cpu_is_self(void *_env)
{
    CPUState *env = _env;

    return qemu_thread_is_self(env->thread);
}

void qemu_mutex_lock_iothread(void)
{
    if (kvm_enabled()) {
        qemu_mutex_lock(&qemu_global_mutex);
    } else {
        iothread_requesting_mutex = true;
        if (qemu_mutex_trylock(&qemu_global_mutex)) {
            qemu_cpu_kick_thread(first_cpu);
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
    CPUState *penv = first_cpu;

    while (penv) {
        if (!penv->stopped) {
            return 0;
        }
        penv = (CPUState *)penv->next_cpu;
    }

    return 1;
}

void pause_all_vcpus(void)
{
    CPUState *penv = first_cpu;

    qemu_clock_enable(vm_clock, false);
    while (penv) {
        penv->stop = 1;
        qemu_cpu_kick(penv);
        penv = (CPUState *)penv->next_cpu;
    }

    while (!all_vcpus_paused()) {
        qemu_cond_wait(&qemu_pause_cond, &qemu_global_mutex);
        penv = first_cpu;
        while (penv) {
            qemu_cpu_kick(penv);
            penv = (CPUState *)penv->next_cpu;
        }
    }
}

void resume_all_vcpus(void)
{
    CPUState *penv = first_cpu;

    qemu_clock_enable(vm_clock, true);
    while (penv) {
        penv->stop = 0;
        penv->stopped = 0;
        qemu_cpu_kick(penv);
        penv = (CPUState *)penv->next_cpu;
    }
}

static void qemu_tcg_init_vcpu(void *_env)
{
    CPUState *env = _env;

    /* share a single thread for all cpus with TCG */
    if (!tcg_cpu_thread) {
        env->thread = g_malloc0(sizeof(QemuThread));
        env->halt_cond = g_malloc0(sizeof(QemuCond));
        qemu_cond_init(env->halt_cond);
        tcg_halt_cond = env->halt_cond;
        qemu_thread_create(env->thread, qemu_tcg_cpu_thread_fn, env);
        while (env->created == 0) {
            qemu_cond_wait(&qemu_cpu_cond, &qemu_global_mutex);
        }
        tcg_cpu_thread = env->thread;
    } else {
        env->thread = tcg_cpu_thread;
        env->halt_cond = tcg_halt_cond;
    }
}

static void qemu_kvm_start_vcpu(CPUState *env)
{
    env->thread = g_malloc0(sizeof(QemuThread));
    env->halt_cond = g_malloc0(sizeof(QemuCond));
    qemu_cond_init(env->halt_cond);
    qemu_thread_create(env->thread, qemu_kvm_cpu_thread_fn, env);
    while (env->created == 0) {
        qemu_cond_wait(&qemu_cpu_cond, &qemu_global_mutex);
    }
}

void qemu_init_vcpu(void *_env)
{
    CPUState *env = _env;

    env->nr_cores = smp_cores;
    env->nr_threads = smp_threads;
    env->stopped = 1;
    if (kvm_enabled()) {
        qemu_kvm_start_vcpu(env);
    } else {
        qemu_tcg_init_vcpu(env);
    }
}

void cpu_stop_current(void)
{
    if (cpu_single_env) {
        cpu_single_env->stop = 0;
        cpu_single_env->stopped = 1;
        cpu_exit(cpu_single_env);
        qemu_cond_signal(&qemu_pause_cond);
    }
}

void vm_stop(RunState state)
{
    if (!qemu_thread_is_self(&io_thread)) {
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

static int tcg_cpu_exec(CPUState *env)
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
        CPUState *env = next_cpu;

        qemu_clock_enable(vm_clock,
                          (env->singlestep_enabled & SSTEP_NOTIMER) == 0);

        if (cpu_can_run(env)) {
            r = tcg_cpu_exec(env);
            if (r == EXCP_DEBUG) {
                cpu_handle_guest_debug(env);
                break;
            }
        } else if (env->stop || env->stopped) {
            break;
        }
    }
    exit_request = 0;
}

void set_numa_modes(void)
{
    CPUState *env;
    int i;

    for (env = first_cpu; env != NULL; env = env->next_cpu) {
        for (i = 0; i < nb_numa_nodes; i++) {
            if (node_cpumask[i] & (1 << env->cpu_index)) {
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
#if defined(cpu_list_id)
    cpu_list_id(f, cpu_fprintf, optarg);
#elif defined(cpu_list)
    cpu_list(f, cpu_fprintf); /* deprecated */
#endif
}

CpuInfoList *qmp_query_cpus(Error **errp)
{
    CpuInfoList *head = NULL, *cur_item = NULL;
    CPUState *env;

    for(env = first_cpu; env != NULL; env = env->next_cpu) {
        CpuInfoList *info;

        cpu_synchronize_state(env);

        info = g_malloc0(sizeof(*info));
        info->value = g_malloc0(sizeof(*info->value));
        info->value->CPU = env->cpu_index;
        info->value->current = (env == first_cpu);
        info->value->halted = env->halted;
        info->value->thread_id = env->thread_id;
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
