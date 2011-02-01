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
#include "exec-all.h"

#include "cpus.h"
#include "compatfd.h"
#ifdef CONFIG_LINUX
#include <sys/prctl.h>
#endif

#ifdef SIGRTMIN
#define SIG_IPI (SIGRTMIN+4)
#else
#define SIG_IPI SIGUSR1
#endif

#ifndef PR_MCE_KILL
#define PR_MCE_KILL 33
#endif

static CPUState *next_cpu;

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
    return !vm_running || env->stopped;
}

static void do_vm_stop(int reason)
{
    if (vm_running) {
        cpu_disable_ticks();
        vm_running = 0;
        pause_all_vcpus();
        vm_state_notify(0, reason);
        qemu_aio_flush();
        bdrv_flush_all();
        monitor_protocol_event(QEVENT_STOP, NULL);
    }
}

static int cpu_can_run(CPUState *env)
{
    if (env->stop)
        return 0;
    if (env->stopped || !vm_running)
        return 0;
    return 1;
}

static int cpu_has_work(CPUState *env)
{
    if (env->stop)
        return 1;
    if (env->queued_work_first)
        return 1;
    if (env->stopped || !vm_running)
        return 0;
    if (!env->halted)
        return 1;
    if (qemu_cpu_has_work(env))
        return 1;
    return 0;
}

static int any_cpu_has_work(void)
{
    CPUState *env;

    for (env = first_cpu; env != NULL; env = env->next_cpu)
        if (cpu_has_work(env))
            return 1;
    return 0;
}

static void cpu_debug_handler(CPUState *env)
{
    gdb_set_stop_cpu(env);
    debug_requested = EXCP_DEBUG;
    vm_stop(EXCP_DEBUG);
}

#ifndef _WIN32
static int io_thread_fd = -1;

static void qemu_event_increment(void)
{
    /* Write 8 bytes to be compatible with eventfd.  */
    static const uint64_t val = 1;
    ssize_t ret;

    if (io_thread_fd == -1)
        return;

    do {
        ret = write(io_thread_fd, &val, sizeof(val));
    } while (ret < 0 && errno == EINTR);

    /* EAGAIN is fine, a read must be pending.  */
    if (ret < 0 && errno != EAGAIN) {
        fprintf(stderr, "qemu_event_increment: write() filed: %s\n",
                strerror(errno));
        exit (1);
    }
}

static void qemu_event_read(void *opaque)
{
    int fd = (unsigned long)opaque;
    ssize_t len;
    char buffer[512];

    /* Drain the notify pipe.  For eventfd, only 8 bytes will be read.  */
    do {
        len = read(fd, buffer, sizeof(buffer));
    } while ((len == -1 && errno == EINTR) || len == sizeof(buffer));
}

static int qemu_event_init(void)
{
    int err;
    int fds[2];

    err = qemu_eventfd(fds);
    if (err == -1)
        return -errno;

    err = fcntl_setfl(fds[0], O_NONBLOCK);
    if (err < 0)
        goto fail;

    err = fcntl_setfl(fds[1], O_NONBLOCK);
    if (err < 0)
        goto fail;

    qemu_set_fd_handler2(fds[0], NULL, qemu_event_read, NULL,
                         (void *)(unsigned long)fds[0]);

    io_thread_fd = fds[1];
    return 0;

fail:
    close(fds[0]);
    close(fds[1]);
    return err;
}
#else
HANDLE qemu_event_handle;

static void dummy_event_handler(void *opaque)
{
}

static int qemu_event_init(void)
{
    qemu_event_handle = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!qemu_event_handle) {
        fprintf(stderr, "Failed CreateEvent: %ld\n", GetLastError());
        return -1;
    }
    qemu_add_wait_object(qemu_event_handle, dummy_event_handler, NULL);
    return 0;
}

static void qemu_event_increment(void)
{
    if (!SetEvent(qemu_event_handle)) {
        fprintf(stderr, "qemu_event_increment: SetEvent failed: %ld\n",
                GetLastError());
        exit (1);
    }
}
#endif

#ifndef CONFIG_IOTHREAD
int qemu_init_main_loop(void)
{
    cpu_set_debug_excp_handler(cpu_debug_handler);

    return qemu_event_init();
}

void qemu_main_loop_start(void)
{
}

void qemu_init_vcpu(void *_env)
{
    CPUState *env = _env;

    env->nr_cores = smp_cores;
    env->nr_threads = smp_threads;
    if (kvm_enabled())
        kvm_init_vcpu(env);
    return;
}

int qemu_cpu_self(void *env)
{
    return 1;
}

void run_on_cpu(CPUState *env, void (*func)(void *data), void *data)
{
    func(data);
}

void resume_all_vcpus(void)
{
}

void pause_all_vcpus(void)
{
}

void qemu_cpu_kick(void *env)
{
    return;
}

void qemu_notify_event(void)
{
    CPUState *env = cpu_single_env;

    qemu_event_increment ();
    if (env) {
        cpu_exit(env);
    }
    if (next_cpu && env != next_cpu) {
        cpu_exit(next_cpu);
    }
}

void qemu_mutex_lock_iothread(void) {}
void qemu_mutex_unlock_iothread(void) {}

void vm_stop(int reason)
{
    do_vm_stop(reason);
}

#else /* CONFIG_IOTHREAD */

#include "qemu-thread.h"

QemuMutex qemu_global_mutex;
static QemuMutex qemu_fair_mutex;

static QemuThread io_thread;

static QemuThread *tcg_cpu_thread;
static QemuCond *tcg_halt_cond;

static int qemu_system_ready;
/* cpu creation */
static QemuCond qemu_cpu_cond;
/* system init */
static QemuCond qemu_system_cond;
static QemuCond qemu_pause_cond;
static QemuCond qemu_work_cond;

static void tcg_init_ipi(void);
static void kvm_init_ipi(CPUState *env);
static sigset_t block_io_signals(void);

/* If we have signalfd, we mask out the signals we want to handle and then
 * use signalfd to listen for them.  We rely on whatever the current signal
 * handler is to dispatch the signals when we receive them.
 */
static void sigfd_handler(void *opaque)
{
    int fd = (unsigned long) opaque;
    struct qemu_signalfd_siginfo info;
    struct sigaction action;
    ssize_t len;

    while (1) {
        do {
            len = read(fd, &info, sizeof(info));
        } while (len == -1 && errno == EINTR);

        if (len == -1 && errno == EAGAIN) {
            break;
        }

        if (len != sizeof(info)) {
            printf("read from sigfd returned %zd: %m\n", len);
            return;
        }

        sigaction(info.ssi_signo, NULL, &action);
        if ((action.sa_flags & SA_SIGINFO) && action.sa_sigaction) {
            action.sa_sigaction(info.ssi_signo,
                                (siginfo_t *)&info, NULL);
        } else if (action.sa_handler) {
            action.sa_handler(info.ssi_signo);
        }
    }
}

static int qemu_signalfd_init(sigset_t mask)
{
    int sigfd;

    sigfd = qemu_signalfd(&mask);
    if (sigfd == -1) {
        fprintf(stderr, "failed to create signalfd\n");
        return -errno;
    }

    fcntl_setfl(sigfd, O_NONBLOCK);

    qemu_set_fd_handler2(sigfd, NULL, sigfd_handler, NULL,
                         (void *)(unsigned long) sigfd);

    return 0;
}

int qemu_init_main_loop(void)
{
    int ret;
    sigset_t blocked_signals;

    cpu_set_debug_excp_handler(cpu_debug_handler);

    blocked_signals = block_io_signals();

    ret = qemu_signalfd_init(blocked_signals);
    if (ret)
        return ret;

    /* Note eventfd must be drained before signalfd handlers run */
    ret = qemu_event_init();
    if (ret)
        return ret;

    qemu_cond_init(&qemu_pause_cond);
    qemu_cond_init(&qemu_system_cond);
    qemu_mutex_init(&qemu_fair_mutex);
    qemu_mutex_init(&qemu_global_mutex);
    qemu_mutex_lock(&qemu_global_mutex);

    qemu_thread_self(&io_thread);

    return 0;
}

void qemu_main_loop_start(void)
{
    qemu_system_ready = 1;
    qemu_cond_broadcast(&qemu_system_cond);
}

void run_on_cpu(CPUState *env, void (*func)(void *data), void *data)
{
    struct qemu_work_item wi;

    if (qemu_cpu_self(env)) {
        func(data);
        return;
    }

    wi.func = func;
    wi.data = data;
    if (!env->queued_work_first)
        env->queued_work_first = &wi;
    else
        env->queued_work_last->next = &wi;
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

    if (!env->queued_work_first)
        return;

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
}

static void qemu_tcg_wait_io_event(void)
{
    CPUState *env;

    while (!any_cpu_has_work())
        qemu_cond_timedwait(tcg_halt_cond, &qemu_global_mutex, 1000);

    qemu_mutex_unlock(&qemu_global_mutex);

    /*
     * Users of qemu_global_mutex can be starved, having no chance
     * to acquire it since this path will get to it first.
     * So use another lock to provide fairness.
     */
    qemu_mutex_lock(&qemu_fair_mutex);
    qemu_mutex_unlock(&qemu_fair_mutex);

    qemu_mutex_lock(&qemu_global_mutex);

    for (env = first_cpu; env != NULL; env = env->next_cpu) {
        qemu_wait_io_event_common(env);
    }
}

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
#if defined(TARGET_I386)
    if (kvm_on_sigbus(siginfo->ssi_code, (void *)(intptr_t)siginfo->ssi_addr))
#endif
        sigbus_reraise();
}

static void qemu_kvm_eat_signal(CPUState *env, int timeout)
{
    struct timespec ts;
    int r, e;
    siginfo_t siginfo;
    sigset_t waitset;
    sigset_t chkset;

    ts.tv_sec = timeout / 1000;
    ts.tv_nsec = (timeout % 1000) * 1000000;

    sigemptyset(&waitset);
    sigaddset(&waitset, SIG_IPI);
    sigaddset(&waitset, SIGBUS);

    do {
        qemu_mutex_unlock(&qemu_global_mutex);

        r = sigtimedwait(&waitset, &siginfo, &ts);
        e = errno;

        qemu_mutex_lock(&qemu_global_mutex);

        if (r == -1 && !(e == EAGAIN || e == EINTR)) {
            fprintf(stderr, "sigtimedwait: %s\n", strerror(e));
            exit(1);
        }

        switch (r) {
        case SIGBUS:
#ifdef TARGET_I386
            if (kvm_on_sigbus_vcpu(env, siginfo.si_code, siginfo.si_addr))
#endif
                sigbus_reraise();
            break;
        default:
            break;
        }

        r = sigpending(&chkset);
        if (r == -1) {
            fprintf(stderr, "sigpending: %s\n", strerror(e));
            exit(1);
        }
    } while (sigismember(&chkset, SIG_IPI) || sigismember(&chkset, SIGBUS));
}

static void qemu_kvm_wait_io_event(CPUState *env)
{
    while (!cpu_has_work(env))
        qemu_cond_timedwait(env->halt_cond, &qemu_global_mutex, 1000);

    qemu_kvm_eat_signal(env, 0);
    qemu_wait_io_event_common(env);
}

static int qemu_cpu_exec(CPUState *env);

static void *kvm_cpu_thread_fn(void *arg)
{
    CPUState *env = arg;

    qemu_mutex_lock(&qemu_global_mutex);
    qemu_thread_self(env->thread);
    if (kvm_enabled())
        kvm_init_vcpu(env);

    kvm_init_ipi(env);

    /* signal CPU creation */
    env->created = 1;
    qemu_cond_signal(&qemu_cpu_cond);

    /* and wait for machine initialization */
    while (!qemu_system_ready)
        qemu_cond_timedwait(&qemu_system_cond, &qemu_global_mutex, 100);

    while (1) {
        if (cpu_can_run(env))
            qemu_cpu_exec(env);
        qemu_kvm_wait_io_event(env);
    }

    return NULL;
}

static void *tcg_cpu_thread_fn(void *arg)
{
    CPUState *env = arg;

    tcg_init_ipi();
    qemu_thread_self(env->thread);

    /* signal CPU creation */
    qemu_mutex_lock(&qemu_global_mutex);
    for (env = first_cpu; env != NULL; env = env->next_cpu)
        env->created = 1;
    qemu_cond_signal(&qemu_cpu_cond);

    /* and wait for machine initialization */
    while (!qemu_system_ready)
        qemu_cond_timedwait(&qemu_system_cond, &qemu_global_mutex, 100);

    while (1) {
        cpu_exec_all();
        qemu_tcg_wait_io_event();
    }

    return NULL;
}

void qemu_cpu_kick(void *_env)
{
    CPUState *env = _env;
    qemu_cond_broadcast(env->halt_cond);
    qemu_thread_signal(env->thread, SIG_IPI);
}

int qemu_cpu_self(void *_env)
{
    CPUState *env = _env;
    QemuThread this;

    qemu_thread_self(&this);

    return qemu_thread_equal(&this, env->thread);
}

static void cpu_signal(int sig)
{
    if (cpu_single_env)
        cpu_exit(cpu_single_env);
    exit_request = 1;
}

static void tcg_init_ipi(void)
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

static void dummy_signal(int sig)
{
}

static void kvm_init_ipi(CPUState *env)
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
        fprintf(stderr, "kvm_set_signal_mask: %s\n", strerror(r));
        exit(1);
    }
}

static sigset_t block_io_signals(void)
{
    sigset_t set;
    struct sigaction action;

    /* SIGUSR2 used by posix-aio-compat.c */
    sigemptyset(&set);
    sigaddset(&set, SIGUSR2);
    pthread_sigmask(SIG_UNBLOCK, &set, NULL);

    sigemptyset(&set);
    sigaddset(&set, SIGIO);
    sigaddset(&set, SIGALRM);
    sigaddset(&set, SIG_IPI);
    sigaddset(&set, SIGBUS);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    memset(&action, 0, sizeof(action));
    action.sa_flags = SA_SIGINFO;
    action.sa_sigaction = (void (*)(int, siginfo_t*, void*))sigbus_handler;
    sigaction(SIGBUS, &action, NULL);
    prctl(PR_MCE_KILL, 1, 1, 0, 0);

    return set;
}

void qemu_mutex_lock_iothread(void)
{
    if (kvm_enabled()) {
        qemu_mutex_lock(&qemu_global_mutex);
    } else {
        qemu_mutex_lock(&qemu_fair_mutex);
        if (qemu_mutex_trylock(&qemu_global_mutex)) {
            qemu_thread_signal(tcg_cpu_thread, SIG_IPI);
            qemu_mutex_lock(&qemu_global_mutex);
        }
        qemu_mutex_unlock(&qemu_fair_mutex);
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
        if (!penv->stopped)
            return 0;
        penv = (CPUState *)penv->next_cpu;
    }

    return 1;
}

void pause_all_vcpus(void)
{
    CPUState *penv = first_cpu;

    while (penv) {
        penv->stop = 1;
        qemu_cpu_kick(penv);
        penv = (CPUState *)penv->next_cpu;
    }

    while (!all_vcpus_paused()) {
        qemu_cond_timedwait(&qemu_pause_cond, &qemu_global_mutex, 100);
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

    while (penv) {
        penv->stop = 0;
        penv->stopped = 0;
        qemu_cpu_kick(penv);
        penv = (CPUState *)penv->next_cpu;
    }
}

static void tcg_init_vcpu(void *_env)
{
    CPUState *env = _env;
    /* share a single thread for all cpus with TCG */
    if (!tcg_cpu_thread) {
        env->thread = qemu_mallocz(sizeof(QemuThread));
        env->halt_cond = qemu_mallocz(sizeof(QemuCond));
        qemu_cond_init(env->halt_cond);
        qemu_thread_create(env->thread, tcg_cpu_thread_fn, env);
        while (env->created == 0)
            qemu_cond_timedwait(&qemu_cpu_cond, &qemu_global_mutex, 100);
        tcg_cpu_thread = env->thread;
        tcg_halt_cond = env->halt_cond;
    } else {
        env->thread = tcg_cpu_thread;
        env->halt_cond = tcg_halt_cond;
    }
}

static void kvm_start_vcpu(CPUState *env)
{
    env->thread = qemu_mallocz(sizeof(QemuThread));
    env->halt_cond = qemu_mallocz(sizeof(QemuCond));
    qemu_cond_init(env->halt_cond);
    qemu_thread_create(env->thread, kvm_cpu_thread_fn, env);
    while (env->created == 0)
        qemu_cond_timedwait(&qemu_cpu_cond, &qemu_global_mutex, 100);
}

void qemu_init_vcpu(void *_env)
{
    CPUState *env = _env;

    env->nr_cores = smp_cores;
    env->nr_threads = smp_threads;
    if (kvm_enabled())
        kvm_start_vcpu(env);
    else
        tcg_init_vcpu(env);
}

void qemu_notify_event(void)
{
    qemu_event_increment();
}

static void qemu_system_vmstop_request(int reason)
{
    vmstop_requested = reason;
    qemu_notify_event();
}

void vm_stop(int reason)
{
    QemuThread me;
    qemu_thread_self(&me);

    if (!qemu_thread_equal(&me, &io_thread)) {
        qemu_system_vmstop_request(reason);
        /*
         * FIXME: should not return to device code in case
         * vm_stop() has been requested.
         */
        if (cpu_single_env) {
            cpu_exit(cpu_single_env);
            cpu_single_env->stop = 1;
        }
        return;
    }
    do_vm_stop(reason);
}

#endif

static int qemu_cpu_exec(CPUState *env)
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
        count = qemu_icount_round (qemu_next_deadline());
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

bool cpu_exec_all(void)
{
    if (next_cpu == NULL)
        next_cpu = first_cpu;
    for (; next_cpu != NULL && !exit_request; next_cpu = next_cpu->next_cpu) {
        CPUState *env = next_cpu;

        qemu_clock_enable(vm_clock,
                          (env->singlestep_enabled & SSTEP_NOTIMER) == 0);

        if (qemu_alarm_pending())
            break;
        if (cpu_can_run(env)) {
            if (qemu_cpu_exec(env) == EXCP_DEBUG) {
                break;
            }
        } else if (env->stop) {
            break;
        }
    }
    exit_request = 0;
    return any_cpu_has_work();
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

void list_cpus(FILE *f, fprintf_function cpu_fprintf, const char *optarg)
{
    /* XXX: implement xxx_cpu_list for targets that still miss it */
#if defined(cpu_list_id)
    cpu_list_id(f, cpu_fprintf, optarg);
#elif defined(cpu_list)
    cpu_list(f, cpu_fprintf); /* deprecated */
#endif
}
