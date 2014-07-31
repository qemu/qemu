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

#include "monitor/monitor.h"
#include "qapi/qmp/qerror.h"
#include "sysemu/sysemu.h"
#include "exec/gdbstub.h"
#include "sysemu/dma.h"
#include "sysemu/kvm.h"
#include "qmp-commands.h"

#include "qemu/thread.h"
#include "sysemu/cpus.h"
#include "sysemu/qtest.h"
#include "qemu/main-loop.h"
#include "qemu/bitmap.h"
#include "qemu/seqlock.h"
#include "qapi-event.h"

#ifndef _WIN32
#include "qemu/compatfd.h"
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

bool cpu_is_stopped(CPUState *cpu)
{
    return cpu->stopped || !runstate_is_running();
}

static bool cpu_thread_is_idle(CPUState *cpu)
{
    if (cpu->stop || cpu->queued_work_first) {
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

static bool all_cpu_threads_idle(void)
{
    CPUState *cpu;

    CPU_FOREACH(cpu) {
        if (!cpu_thread_is_idle(cpu)) {
            return false;
        }
    }
    return true;
}

/***********************************************************/
/* guest cycle counter */

/* Protected by TimersState seqlock */

static int64_t vm_clock_warp_start;
/* Conversion factor from emulated instructions to virtual clock ticks.  */
static int icount_time_shift;
/* Arbitrarily pick 1MIPS as the minimum allowable speed.  */
#define MAX_ICOUNT_SHIFT 10

static QEMUTimer *icount_rt_timer;
static QEMUTimer *icount_vm_timer;
static QEMUTimer *icount_warp_timer;

typedef struct TimersState {
    /* Protected by BQL.  */
    int64_t cpu_ticks_prev;
    int64_t cpu_ticks_offset;

    /* cpu_clock_offset can be read out of BQL, so protect it with
     * this lock.
     */
    QemuSeqLock vm_clock_seqlock;
    int64_t cpu_clock_offset;
    int32_t cpu_ticks_enabled;
    int64_t dummy;

    /* Compensate for varying guest execution speed.  */
    int64_t qemu_icount_bias;
    /* Only written by TCG thread */
    int64_t qemu_icount;
} TimersState;

static TimersState timers_state;

/* Return the virtual CPU time, based on the instruction counter.  */
static int64_t cpu_get_icount_locked(void)
{
    int64_t icount;
    CPUState *cpu = current_cpu;

    icount = timers_state.qemu_icount;
    if (cpu) {
        if (!cpu_can_do_io(cpu)) {
            fprintf(stderr, "Bad clock read\n");
        }
        icount -= (cpu->icount_decr.u16.low + cpu->icount_extra);
    }
    return timers_state.qemu_icount_bias + cpu_icount_to_ns(icount);
}

int64_t cpu_get_icount(void)
{
    int64_t icount;
    unsigned start;

    do {
        start = seqlock_read_begin(&timers_state.vm_clock_seqlock);
        icount = cpu_get_icount_locked();
    } while (seqlock_read_retry(&timers_state.vm_clock_seqlock, start));

    return icount;
}

int64_t cpu_icount_to_ns(int64_t icount)
{
    return icount << icount_time_shift;
}

/* return the host CPU cycle counter and handle stop/restart */
/* Caller must hold the BQL */
int64_t cpu_get_ticks(void)
{
    int64_t ticks;

    if (use_icount) {
        return cpu_get_icount();
    }

    ticks = timers_state.cpu_ticks_offset;
    if (timers_state.cpu_ticks_enabled) {
        ticks += cpu_get_real_ticks();
    }

    if (timers_state.cpu_ticks_prev > ticks) {
        /* Note: non increasing ticks may happen if the host uses
           software suspend */
        timers_state.cpu_ticks_offset += timers_state.cpu_ticks_prev - ticks;
        ticks = timers_state.cpu_ticks_prev;
    }

    timers_state.cpu_ticks_prev = ticks;
    return ticks;
}

static int64_t cpu_get_clock_locked(void)
{
    int64_t ticks;

    ticks = timers_state.cpu_clock_offset;
    if (timers_state.cpu_ticks_enabled) {
        ticks += get_clock();
    }

    return ticks;
}

/* return the host CPU monotonic timer and handle stop/restart */
int64_t cpu_get_clock(void)
{
    int64_t ti;
    unsigned start;

    do {
        start = seqlock_read_begin(&timers_state.vm_clock_seqlock);
        ti = cpu_get_clock_locked();
    } while (seqlock_read_retry(&timers_state.vm_clock_seqlock, start));

    return ti;
}

/* enable cpu_get_ticks()
 * Caller must hold BQL which server as mutex for vm_clock_seqlock.
 */
void cpu_enable_ticks(void)
{
    /* Here, the really thing protected by seqlock is cpu_clock_offset. */
    seqlock_write_lock(&timers_state.vm_clock_seqlock);
    if (!timers_state.cpu_ticks_enabled) {
        timers_state.cpu_ticks_offset -= cpu_get_real_ticks();
        timers_state.cpu_clock_offset -= get_clock();
        timers_state.cpu_ticks_enabled = 1;
    }
    seqlock_write_unlock(&timers_state.vm_clock_seqlock);
}

/* disable cpu_get_ticks() : the clock is stopped. You must not call
 * cpu_get_ticks() after that.
 * Caller must hold BQL which server as mutex for vm_clock_seqlock.
 */
void cpu_disable_ticks(void)
{
    /* Here, the really thing protected by seqlock is cpu_clock_offset. */
    seqlock_write_lock(&timers_state.vm_clock_seqlock);
    if (timers_state.cpu_ticks_enabled) {
        timers_state.cpu_ticks_offset += cpu_get_real_ticks();
        timers_state.cpu_clock_offset = cpu_get_clock_locked();
        timers_state.cpu_ticks_enabled = 0;
    }
    seqlock_write_unlock(&timers_state.vm_clock_seqlock);
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

    /* Protected by TimersState mutex.  */
    static int64_t last_delta;

    /* If the VM is not running, then do nothing.  */
    if (!runstate_is_running()) {
        return;
    }

    seqlock_write_lock(&timers_state.vm_clock_seqlock);
    cur_time = cpu_get_clock_locked();
    cur_icount = cpu_get_icount_locked();

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
    timers_state.qemu_icount_bias = cur_icount
                              - (timers_state.qemu_icount << icount_time_shift);
    seqlock_write_unlock(&timers_state.vm_clock_seqlock);
}

static void icount_adjust_rt(void *opaque)
{
    timer_mod(icount_rt_timer,
                   qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + 1000);
    icount_adjust();
}

static void icount_adjust_vm(void *opaque)
{
    timer_mod(icount_vm_timer,
                   qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                   get_ticks_per_sec() / 10);
    icount_adjust();
}

static int64_t qemu_icount_round(int64_t count)
{
    return (count + (1 << icount_time_shift) - 1) >> icount_time_shift;
}

static void icount_warp_rt(void *opaque)
{
    /* The icount_warp_timer is rescheduled soon after vm_clock_warp_start
     * changes from -1 to another value, so the race here is okay.
     */
    if (atomic_read(&vm_clock_warp_start) == -1) {
        return;
    }

    seqlock_write_lock(&timers_state.vm_clock_seqlock);
    if (runstate_is_running()) {
        int64_t clock = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
        int64_t warp_delta;

        warp_delta = clock - vm_clock_warp_start;
        if (use_icount == 2) {
            /*
             * In adaptive mode, do not let QEMU_CLOCK_VIRTUAL run too
             * far ahead of real time.
             */
            int64_t cur_time = cpu_get_clock_locked();
            int64_t cur_icount = cpu_get_icount_locked();
            int64_t delta = cur_time - cur_icount;
            warp_delta = MIN(warp_delta, delta);
        }
        timers_state.qemu_icount_bias += warp_delta;
    }
    vm_clock_warp_start = -1;
    seqlock_write_unlock(&timers_state.vm_clock_seqlock);

    if (qemu_clock_expired(QEMU_CLOCK_VIRTUAL)) {
        qemu_clock_notify(QEMU_CLOCK_VIRTUAL);
    }
}

void qtest_clock_warp(int64_t dest)
{
    int64_t clock = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    assert(qtest_enabled());
    while (clock < dest) {
        int64_t deadline = qemu_clock_deadline_ns_all(QEMU_CLOCK_VIRTUAL);
        int64_t warp = qemu_soonest_timeout(dest - clock, deadline);
        seqlock_write_lock(&timers_state.vm_clock_seqlock);
        timers_state.qemu_icount_bias += warp;
        seqlock_write_unlock(&timers_state.vm_clock_seqlock);

        qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
        clock = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    }
    qemu_clock_notify(QEMU_CLOCK_VIRTUAL);
}

void qemu_clock_warp(QEMUClockType type)
{
    int64_t clock;
    int64_t deadline;

    /*
     * There are too many global variables to make the "warp" behavior
     * applicable to other clocks.  But a clock argument removes the
     * need for if statements all over the place.
     */
    if (type != QEMU_CLOCK_VIRTUAL || !use_icount) {
        return;
    }

    /*
     * If the CPUs have been sleeping, advance QEMU_CLOCK_VIRTUAL timer now.
     * This ensures that the deadline for the timer is computed correctly below.
     * This also makes sure that the insn counter is synchronized before the
     * CPU starts running, in case the CPU is woken by an event other than
     * the earliest QEMU_CLOCK_VIRTUAL timer.
     */
    icount_warp_rt(NULL);
    timer_del(icount_warp_timer);
    if (!all_cpu_threads_idle()) {
        return;
    }

    if (qtest_enabled()) {
        /* When testing, qtest commands advance icount.  */
	return;
    }

    /* We want to use the earliest deadline from ALL vm_clocks */
    clock = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    deadline = qemu_clock_deadline_ns_all(QEMU_CLOCK_VIRTUAL);
    if (deadline < 0) {
        return;
    }

    if (deadline > 0) {
        /*
         * Ensure QEMU_CLOCK_VIRTUAL proceeds even when the virtual CPU goes to
         * sleep.  Otherwise, the CPU might be waiting for a future timer
         * interrupt to wake it up, but the interrupt never comes because
         * the vCPU isn't running any insns and thus doesn't advance the
         * QEMU_CLOCK_VIRTUAL.
         *
         * An extreme solution for this problem would be to never let VCPUs
         * sleep in icount mode if there is a pending QEMU_CLOCK_VIRTUAL
         * timer; rather time could just advance to the next QEMU_CLOCK_VIRTUAL
         * event.  Instead, we do stop VCPUs and only advance QEMU_CLOCK_VIRTUAL
         * after some e"real" time, (related to the time left until the next
         * event) has passed. The QEMU_CLOCK_REALTIME timer will do this.
         * This avoids that the warps are visible externally; for example,
         * you will not be sending network packets continuously instead of
         * every 100ms.
         */
        seqlock_write_lock(&timers_state.vm_clock_seqlock);
        if (vm_clock_warp_start == -1 || vm_clock_warp_start > clock) {
            vm_clock_warp_start = clock;
        }
        seqlock_write_unlock(&timers_state.vm_clock_seqlock);
        timer_mod_anticipate(icount_warp_timer, clock + deadline);
    } else if (deadline == 0) {
        qemu_clock_notify(QEMU_CLOCK_VIRTUAL);
    }
}

static bool icount_state_needed(void *opaque)
{
    return use_icount;
}

/*
 * This is a subsection for icount migration.
 */
static const VMStateDescription icount_vmstate_timers = {
    .name = "timer/icount",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_INT64(qemu_icount_bias, TimersState),
        VMSTATE_INT64(qemu_icount, TimersState),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_timers = {
    .name = "timer",
    .version_id = 2,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_INT64(cpu_ticks_offset, TimersState),
        VMSTATE_INT64(dummy, TimersState),
        VMSTATE_INT64_V(cpu_clock_offset, TimersState, 2),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (VMStateSubsection[]) {
        {
            .vmsd = &icount_vmstate_timers,
            .needed = icount_state_needed,
        }, {
            /* empty */
        }
    }
};

void configure_icount(const char *option)
{
    seqlock_init(&timers_state.vm_clock_seqlock, NULL);
    vmstate_register(NULL, 0, &vmstate_timers, &timers_state);
    if (!option) {
        return;
    }

    icount_warp_timer = timer_new_ns(QEMU_CLOCK_REALTIME,
                                          icount_warp_rt, NULL);
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
    icount_rt_timer = timer_new_ms(QEMU_CLOCK_REALTIME,
                                        icount_adjust_rt, NULL);
    timer_mod(icount_rt_timer,
                   qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + 1000);
    icount_vm_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                        icount_adjust_vm, NULL);
    timer_mod(icount_vm_timer,
                   qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                   get_ticks_per_sec() / 10);
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
        cpu_dump_state(cpu, stderr, fprintf, CPU_DUMP_FPU);
    }
    va_end(ap);
    abort();
}

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

static int do_vm_stop(RunState state)
{
    int ret = 0;

    if (runstate_is_running()) {
        cpu_disable_ticks();
        pause_all_vcpus();
        runstate_set(state);
        vm_state_notify(0, state);
        qapi_event_send_stop(&error_abort);
    }

    bdrv_drain_all();
    ret = bdrv_flush_all();

    return ret;
}

static bool cpu_can_run(CPUState *cpu)
{
    if (cpu->stop) {
        return false;
    }
    if (cpu_is_stopped(cpu)) {
        return false;
    }
    return true;
}

static void cpu_handle_guest_debug(CPUState *cpu)
{
    gdb_set_stop_cpu(cpu);
    qemu_system_debug_request();
    cpu->stopped = true;
}

static void cpu_signal(int sig)
{
    if (current_cpu) {
        cpu_exit(current_cpu);
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

static void qemu_kvm_eat_signals(CPUState *cpu)
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
            if (kvm_on_sigbus_vcpu(cpu, siginfo.si_code, siginfo.si_addr)) {
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

static void qemu_kvm_eat_signals(CPUState *cpu)
{
}
#endif /* !CONFIG_LINUX */

#ifndef _WIN32
static void dummy_signal(int sig)
{
}

static void qemu_kvm_init_cpu_signals(CPUState *cpu)
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
    r = kvm_set_signal_mask(cpu, &set);
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
static void qemu_kvm_init_cpu_signals(CPUState *cpu)
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
    wi.free = false;
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
        CPUState *self_cpu = current_cpu;

        qemu_cond_wait(&qemu_work_cond, &qemu_global_mutex);
        current_cpu = self_cpu;
    }
}

void async_run_on_cpu(CPUState *cpu, void (*func)(void *data), void *data)
{
    struct qemu_work_item *wi;

    if (qemu_cpu_is_self(cpu)) {
        func(data);
        return;
    }

    wi = g_malloc0(sizeof(struct qemu_work_item));
    wi->func = func;
    wi->data = data;
    wi->free = true;
    if (cpu->queued_work_first == NULL) {
        cpu->queued_work_first = wi;
    } else {
        cpu->queued_work_last->next = wi;
    }
    cpu->queued_work_last = wi;
    wi->next = NULL;
    wi->done = false;

    qemu_cpu_kick(cpu);
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
        if (wi->free) {
            g_free(wi);
        }
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
    CPUState *cpu;

    while (all_cpu_threads_idle()) {
       /* Start accounting real time to the virtual clock if the CPUs
          are idle.  */
        qemu_clock_warp(QEMU_CLOCK_VIRTUAL);
        qemu_cond_wait(tcg_halt_cond, &qemu_global_mutex);
    }

    while (iothread_requesting_mutex) {
        qemu_cond_wait(&qemu_io_proceeded_cond, &qemu_global_mutex);
    }

    CPU_FOREACH(cpu) {
        qemu_wait_io_event_common(cpu);
    }
}

static void qemu_kvm_wait_io_event(CPUState *cpu)
{
    while (cpu_thread_is_idle(cpu)) {
        qemu_cond_wait(cpu->halt_cond, &qemu_global_mutex);
    }

    qemu_kvm_eat_signals(cpu);
    qemu_wait_io_event_common(cpu);
}

static void *qemu_kvm_cpu_thread_fn(void *arg)
{
    CPUState *cpu = arg;
    int r;

    qemu_mutex_lock(&qemu_global_mutex);
    qemu_thread_get_self(cpu->thread);
    cpu->thread_id = qemu_get_thread_id();
    current_cpu = cpu;

    r = kvm_init_vcpu(cpu);
    if (r < 0) {
        fprintf(stderr, "kvm_init_vcpu failed: %s\n", strerror(-r));
        exit(1);
    }

    qemu_kvm_init_cpu_signals(cpu);

    /* signal CPU creation */
    cpu->created = true;
    qemu_cond_signal(&qemu_cpu_cond);

    while (1) {
        if (cpu_can_run(cpu)) {
            r = kvm_cpu_exec(cpu);
            if (r == EXCP_DEBUG) {
                cpu_handle_guest_debug(cpu);
            }
        }
        qemu_kvm_wait_io_event(cpu);
    }

    return NULL;
}

static void *qemu_dummy_cpu_thread_fn(void *arg)
{
#ifdef _WIN32
    fprintf(stderr, "qtest is not supported under Windows\n");
    exit(1);
#else
    CPUState *cpu = arg;
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

    current_cpu = cpu;
    while (1) {
        current_cpu = NULL;
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
        current_cpu = cpu;
        qemu_wait_io_event_common(cpu);
    }

    return NULL;
#endif
}

static void tcg_exec_all(void);

static void *qemu_tcg_cpu_thread_fn(void *arg)
{
    CPUState *cpu = arg;

    qemu_tcg_init_cpu_signals();
    qemu_thread_get_self(cpu->thread);

    qemu_mutex_lock(&qemu_global_mutex);
    CPU_FOREACH(cpu) {
        cpu->thread_id = qemu_get_thread_id();
        cpu->created = true;
    }
    qemu_cond_signal(&qemu_cpu_cond);

    /* wait for initial kick-off after machine start */
    while (QTAILQ_FIRST(&cpus)->stopped) {
        qemu_cond_wait(tcg_halt_cond, &qemu_global_mutex);

        /* process any pending work */
        CPU_FOREACH(cpu) {
            qemu_wait_io_event_common(cpu);
        }
    }

    while (1) {
        tcg_exec_all();

        if (use_icount) {
            int64_t deadline = qemu_clock_deadline_ns_all(QEMU_CLOCK_VIRTUAL);

            if (deadline == 0) {
                qemu_clock_notify(QEMU_CLOCK_VIRTUAL);
            }
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
        CONTEXT tcgContext;

        if (SuspendThread(cpu->hThread) == (DWORD)-1) {
            fprintf(stderr, "qemu:%s: GetLastError:%lu\n", __func__,
                    GetLastError());
            exit(1);
        }

        /* On multi-core systems, we are not sure that the thread is actually
         * suspended until we can get the context.
         */
        tcgContext.ContextFlags = CONTEXT_CONTROL;
        while (GetThreadContext(cpu->hThread, &tcgContext) != 0) {
            continue;
        }

        cpu_signal(0);

        if (ResumeThread(cpu->hThread) == (DWORD)-1) {
            fprintf(stderr, "qemu:%s: GetLastError:%lu\n", __func__,
                    GetLastError());
            exit(1);
        }
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
    assert(current_cpu);

    if (!current_cpu->thread_kicked) {
        qemu_cpu_kick_thread(current_cpu);
        current_cpu->thread_kicked = true;
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
    return current_cpu && qemu_cpu_is_self(current_cpu);
}

void qemu_mutex_lock_iothread(void)
{
    if (!tcg_enabled()) {
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
    CPUState *cpu;

    CPU_FOREACH(cpu) {
        if (!cpu->stopped) {
            return 0;
        }
    }

    return 1;
}

void pause_all_vcpus(void)
{
    CPUState *cpu;

    qemu_clock_enable(QEMU_CLOCK_VIRTUAL, false);
    CPU_FOREACH(cpu) {
        cpu->stop = true;
        qemu_cpu_kick(cpu);
    }

    if (qemu_in_vcpu_thread()) {
        cpu_stop_current();
        if (!kvm_enabled()) {
            CPU_FOREACH(cpu) {
                cpu->stop = false;
                cpu->stopped = true;
            }
            return;
        }
    }

    while (!all_vcpus_paused()) {
        qemu_cond_wait(&qemu_pause_cond, &qemu_global_mutex);
        CPU_FOREACH(cpu) {
            qemu_cpu_kick(cpu);
        }
    }
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

    qemu_clock_enable(QEMU_CLOCK_VIRTUAL, true);
    CPU_FOREACH(cpu) {
        cpu_resume(cpu);
    }
}

/* For temporary buffers for forming a name */
#define VCPU_THREAD_NAME_SIZE 16

static void qemu_tcg_init_vcpu(CPUState *cpu)
{
    char thread_name[VCPU_THREAD_NAME_SIZE];

    tcg_cpu_address_space_init(cpu, cpu->as);

    /* share a single thread for all cpus with TCG */
    if (!tcg_cpu_thread) {
        cpu->thread = g_malloc0(sizeof(QemuThread));
        cpu->halt_cond = g_malloc0(sizeof(QemuCond));
        qemu_cond_init(cpu->halt_cond);
        tcg_halt_cond = cpu->halt_cond;
        snprintf(thread_name, VCPU_THREAD_NAME_SIZE, "CPU %d/TCG",
                 cpu->cpu_index);
        qemu_thread_create(cpu->thread, thread_name, qemu_tcg_cpu_thread_fn,
                           cpu, QEMU_THREAD_JOINABLE);
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
    while (!cpu->created) {
        qemu_cond_wait(&qemu_cpu_cond, &qemu_global_mutex);
    }
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
    while (!cpu->created) {
        qemu_cond_wait(&qemu_cpu_cond, &qemu_global_mutex);
    }
}

void qemu_init_vcpu(CPUState *cpu)
{
    cpu->nr_cores = smp_cores;
    cpu->nr_threads = smp_threads;
    cpu->stopped = true;
    if (kvm_enabled()) {
        qemu_kvm_start_vcpu(cpu);
    } else if (tcg_enabled()) {
        qemu_tcg_init_vcpu(cpu);
    } else {
        qemu_dummy_start_vcpu(cpu);
    }
}

void cpu_stop_current(void)
{
    if (current_cpu) {
        current_cpu->stop = false;
        current_cpu->stopped = true;
        cpu_exit(current_cpu);
        qemu_cond_signal(&qemu_pause_cond);
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

    return do_vm_stop(state);
}

/* does a state transition even if the VM is already stopped,
   current state is forgotten forever */
int vm_stop_force_state(RunState state)
{
    if (runstate_is_running()) {
        return vm_stop(state);
    } else {
        runstate_set(state);
        /* Make sure to return an error if the flush in a previous vm_stop()
         * failed. */
        return bdrv_flush_all();
    }
}

static int tcg_cpu_exec(CPUArchState *env)
{
    CPUState *cpu = ENV_GET_CPU(env);
    int ret;
#ifdef CONFIG_PROFILER
    int64_t ti;
#endif

#ifdef CONFIG_PROFILER
    ti = profile_getclock();
#endif
    if (use_icount) {
        int64_t count;
        int64_t deadline;
        int decr;
        timers_state.qemu_icount -= (cpu->icount_decr.u16.low
                                    + cpu->icount_extra);
        cpu->icount_decr.u16.low = 0;
        cpu->icount_extra = 0;
        deadline = qemu_clock_deadline_ns_all(QEMU_CLOCK_VIRTUAL);

        /* Maintain prior (possibly buggy) behaviour where if no deadline
         * was set (as there is no QEMU_CLOCK_VIRTUAL timer) or it is more than
         * INT32_MAX nanoseconds ahead, we still use INT32_MAX
         * nanoseconds.
         */
        if ((deadline < 0) || (deadline > INT32_MAX)) {
            deadline = INT32_MAX;
        }

        count = qemu_icount_round(deadline);
        timers_state.qemu_icount += count;
        decr = (count > 0xffff) ? 0xffff : count;
        count -= decr;
        cpu->icount_decr.u16.low = decr;
        cpu->icount_extra = count;
    }
    ret = cpu_exec(env);
#ifdef CONFIG_PROFILER
    qemu_time += profile_getclock() - ti;
#endif
    if (use_icount) {
        /* Fold pending instructions back into the
           instruction counter, and clear the interrupt flag.  */
        timers_state.qemu_icount -= (cpu->icount_decr.u16.low
                        + cpu->icount_extra);
        cpu->icount_decr.u32 = 0;
        cpu->icount_extra = 0;
    }
    return ret;
}

static void tcg_exec_all(void)
{
    int r;

    /* Account partial waits to QEMU_CLOCK_VIRTUAL.  */
    qemu_clock_warp(QEMU_CLOCK_VIRTUAL);

    if (next_cpu == NULL) {
        next_cpu = first_cpu;
    }
    for (; next_cpu != NULL && !exit_request; next_cpu = CPU_NEXT(next_cpu)) {
        CPUState *cpu = next_cpu;
        CPUArchState *env = cpu->env_ptr;

        qemu_clock_enable(QEMU_CLOCK_VIRTUAL,
                          (cpu->singlestep_enabled & SSTEP_NOTIMER) == 0);

        if (cpu_can_run(cpu)) {
            r = tcg_cpu_exec(env);
            if (r == EXCP_DEBUG) {
                cpu_handle_guest_debug(cpu);
                break;
            }
        } else if (cpu->stop || cpu->stopped) {
            break;
        }
    }
    exit_request = 0;
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
    CPUState *cpu;

    CPU_FOREACH(cpu) {
        CpuInfoList *info;
#if defined(TARGET_I386)
        X86CPU *x86_cpu = X86_CPU(cpu);
        CPUX86State *env = &x86_cpu->env;
#elif defined(TARGET_PPC)
        PowerPCCPU *ppc_cpu = POWERPC_CPU(cpu);
        CPUPPCState *env = &ppc_cpu->env;
#elif defined(TARGET_SPARC)
        SPARCCPU *sparc_cpu = SPARC_CPU(cpu);
        CPUSPARCState *env = &sparc_cpu->env;
#elif defined(TARGET_MIPS)
        MIPSCPU *mips_cpu = MIPS_CPU(cpu);
        CPUMIPSState *env = &mips_cpu->env;
#endif

        cpu_synchronize_state(cpu);

        info = g_malloc0(sizeof(*info));
        info->value = g_malloc0(sizeof(*info->value));
        info->value->CPU = cpu->cpu_index;
        info->value->current = (cpu == first_cpu);
        info->value->halted = cpu->halted;
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
    CPUState *cpu;
    uint8_t buf[1024];

    if (!has_cpu) {
        cpu_index = 0;
    }

    cpu = qemu_get_cpu(cpu_index);
    if (cpu == NULL) {
        error_set(errp, QERR_INVALID_PARAMETER_VALUE, "cpu-index",
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
            error_setg(errp, "Invalid addr 0x%016" PRIx64 "specified", addr);
            goto exit;
        }
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
        error_setg_file_open(errp, errno, filename);
        return;
    }

    while (size != 0) {
        l = sizeof(buf);
        if (l > size)
            l = size;
        cpu_physical_memory_read(addr, buf, l);
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
    CPUState *cs;

    CPU_FOREACH(cs) {
        X86CPU *cpu = X86_CPU(cs);

        if (!cpu->apic_state) {
            cpu_interrupt(cs, CPU_INTERRUPT_NMI);
        } else {
            apic_deliver_nmi(cpu->apic_state);
        }
    }
#elif defined(TARGET_S390X)
    CPUState *cs;
    S390CPU *cpu;

    CPU_FOREACH(cs) {
        cpu = S390_CPU(cs);
        if (cpu->env.cpu_num == monitor_get_cpu_index()) {
            if (s390_cpu_restart(S390_CPU(cs)) == -1) {
                error_set(errp, QERR_UNSUPPORTED);
                return;
            }
            break;
        }
    }
#else
    error_set(errp, QERR_UNSUPPORTED);
#endif
}
