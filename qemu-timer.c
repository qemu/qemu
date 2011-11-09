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

#include "sysemu.h"
#include "net.h"
#include "monitor.h"
#include "console.h"

#include "hw/hw.h"

#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <sys/time.h>
#include <signal.h>
#ifdef __FreeBSD__
#include <sys/param.h>
#endif

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#endif

#include "qemu-timer.h"

/***********************************************************/
/* timers */

#define QEMU_CLOCK_REALTIME 0
#define QEMU_CLOCK_VIRTUAL  1
#define QEMU_CLOCK_HOST     2

struct QEMUClock {
    int type;
    int enabled;

    QEMUTimer *active_timers;

    NotifierList reset_notifiers;
    int64_t last;
};

struct QEMUTimer {
    QEMUClock *clock;
    int64_t expire_time;	/* in nanoseconds */
    int scale;
    QEMUTimerCB *cb;
    void *opaque;
    struct QEMUTimer *next;
};

struct qemu_alarm_timer {
    char const *name;
    int (*start)(struct qemu_alarm_timer *t);
    void (*stop)(struct qemu_alarm_timer *t);
    void (*rearm)(struct qemu_alarm_timer *t, int64_t nearest_delta_ns);
#if defined(__linux__)
    int fd;
    timer_t timer;
#elif defined(_WIN32)
    HANDLE timer;
#endif
    char expired;
    char pending;
};

static struct qemu_alarm_timer *alarm_timer;

static bool qemu_timer_expired_ns(QEMUTimer *timer_head, int64_t current_time)
{
    return timer_head && (timer_head->expire_time <= current_time);
}

int qemu_alarm_pending(void)
{
    return alarm_timer->pending;
}

static inline int alarm_has_dynticks(struct qemu_alarm_timer *t)
{
    return !!t->rearm;
}

static int64_t qemu_next_alarm_deadline(void)
{
    int64_t delta;
    int64_t rtdelta;

    if (!use_icount && vm_clock->active_timers) {
        delta = vm_clock->active_timers->expire_time -
                     qemu_get_clock_ns(vm_clock);
    } else {
        delta = INT32_MAX;
    }
    if (host_clock->active_timers) {
        int64_t hdelta = host_clock->active_timers->expire_time -
                 qemu_get_clock_ns(host_clock);
        if (hdelta < delta) {
            delta = hdelta;
        }
    }
    if (rt_clock->active_timers) {
        rtdelta = (rt_clock->active_timers->expire_time -
                 qemu_get_clock_ns(rt_clock));
        if (rtdelta < delta) {
            delta = rtdelta;
        }
    }

    return delta;
}

static void qemu_rearm_alarm_timer(struct qemu_alarm_timer *t)
{
    int64_t nearest_delta_ns;
    assert(alarm_has_dynticks(t));
    if (!rt_clock->active_timers &&
        !vm_clock->active_timers &&
        !host_clock->active_timers) {
        return;
    }
    nearest_delta_ns = qemu_next_alarm_deadline();
    t->rearm(t, nearest_delta_ns);
}

/* TODO: MIN_TIMER_REARM_NS should be optimized */
#define MIN_TIMER_REARM_NS 250000

#ifdef _WIN32

static int mm_start_timer(struct qemu_alarm_timer *t);
static void mm_stop_timer(struct qemu_alarm_timer *t);
static void mm_rearm_timer(struct qemu_alarm_timer *t, int64_t delta);

static int win32_start_timer(struct qemu_alarm_timer *t);
static void win32_stop_timer(struct qemu_alarm_timer *t);
static void win32_rearm_timer(struct qemu_alarm_timer *t, int64_t delta);

#else

static int unix_start_timer(struct qemu_alarm_timer *t);
static void unix_stop_timer(struct qemu_alarm_timer *t);
static void unix_rearm_timer(struct qemu_alarm_timer *t, int64_t delta);

#ifdef __linux__

static int dynticks_start_timer(struct qemu_alarm_timer *t);
static void dynticks_stop_timer(struct qemu_alarm_timer *t);
static void dynticks_rearm_timer(struct qemu_alarm_timer *t, int64_t delta);

#endif /* __linux__ */

#endif /* _WIN32 */

static struct qemu_alarm_timer alarm_timers[] = {
#ifndef _WIN32
#ifdef __linux__
    {"dynticks", dynticks_start_timer,
     dynticks_stop_timer, dynticks_rearm_timer},
#endif
    {"unix", unix_start_timer, unix_stop_timer, unix_rearm_timer},
#else
    {"mmtimer", mm_start_timer, mm_stop_timer, mm_rearm_timer},
    {"dynticks", win32_start_timer, win32_stop_timer, win32_rearm_timer},
#endif
    {NULL, }
};

static void show_available_alarms(void)
{
    int i;

    printf("Available alarm timers, in order of precedence:\n");
    for (i = 0; alarm_timers[i].name; i++)
        printf("%s\n", alarm_timers[i].name);
}

void configure_alarms(char const *opt)
{
    int i;
    int cur = 0;
    int count = ARRAY_SIZE(alarm_timers) - 1;
    char *arg;
    char *name;
    struct qemu_alarm_timer tmp;

    if (!strcmp(opt, "?")) {
        show_available_alarms();
        exit(0);
    }

    arg = g_strdup(opt);

    /* Reorder the array */
    name = strtok(arg, ",");
    while (name) {
        for (i = 0; i < count && alarm_timers[i].name; i++) {
            if (!strcmp(alarm_timers[i].name, name))
                break;
        }

        if (i == count) {
            fprintf(stderr, "Unknown clock %s\n", name);
            goto next;
        }

        if (i < cur)
            /* Ignore */
            goto next;

	/* Swap */
        tmp = alarm_timers[i];
        alarm_timers[i] = alarm_timers[cur];
        alarm_timers[cur] = tmp;

        cur++;
next:
        name = strtok(NULL, ",");
    }

    g_free(arg);

    if (cur) {
        /* Disable remaining timers */
        for (i = cur; i < count; i++)
            alarm_timers[i].name = NULL;
    } else {
        show_available_alarms();
        exit(1);
    }
}

QEMUClock *rt_clock;
QEMUClock *vm_clock;
QEMUClock *host_clock;

static QEMUClock *qemu_new_clock(int type)
{
    QEMUClock *clock;

    clock = g_malloc0(sizeof(QEMUClock));
    clock->type = type;
    clock->enabled = 1;
    clock->last = INT64_MIN;
    notifier_list_init(&clock->reset_notifiers);
    return clock;
}

void qemu_clock_enable(QEMUClock *clock, int enabled)
{
    bool old = clock->enabled;
    clock->enabled = enabled;
    if (enabled && !old) {
        qemu_rearm_alarm_timer(alarm_timer);
    }
}

int64_t qemu_clock_has_timers(QEMUClock *clock)
{
    return !!clock->active_timers;
}

int64_t qemu_clock_expired(QEMUClock *clock)
{
    return (clock->active_timers &&
            clock->active_timers->expire_time < qemu_get_clock_ns(clock));
}

int64_t qemu_clock_deadline(QEMUClock *clock)
{
    /* To avoid problems with overflow limit this to 2^32.  */
    int64_t delta = INT32_MAX;

    if (clock->active_timers) {
        delta = clock->active_timers->expire_time - qemu_get_clock_ns(clock);
    }
    if (delta < 0) {
        delta = 0;
    }
    return delta;
}

QEMUTimer *qemu_new_timer(QEMUClock *clock, int scale,
                          QEMUTimerCB *cb, void *opaque)
{
    QEMUTimer *ts;

    ts = g_malloc0(sizeof(QEMUTimer));
    ts->clock = clock;
    ts->cb = cb;
    ts->opaque = opaque;
    ts->scale = scale;
    return ts;
}

void qemu_free_timer(QEMUTimer *ts)
{
    g_free(ts);
}

/* stop a timer, but do not dealloc it */
void qemu_del_timer(QEMUTimer *ts)
{
    QEMUTimer **pt, *t;

    /* NOTE: this code must be signal safe because
       qemu_timer_expired() can be called from a signal. */
    pt = &ts->clock->active_timers;
    for(;;) {
        t = *pt;
        if (!t)
            break;
        if (t == ts) {
            *pt = t->next;
            break;
        }
        pt = &t->next;
    }
}

/* modify the current timer so that it will be fired when current_time
   >= expire_time. The corresponding callback will be called. */
void qemu_mod_timer_ns(QEMUTimer *ts, int64_t expire_time)
{
    QEMUTimer **pt, *t;

    qemu_del_timer(ts);

    /* add the timer in the sorted list */
    /* NOTE: this code must be signal safe because
       qemu_timer_expired() can be called from a signal. */
    pt = &ts->clock->active_timers;
    for(;;) {
        t = *pt;
        if (!qemu_timer_expired_ns(t, expire_time)) {
            break;
        }
        pt = &t->next;
    }
    ts->expire_time = expire_time;
    ts->next = *pt;
    *pt = ts;

    /* Rearm if necessary  */
    if (pt == &ts->clock->active_timers) {
        if (!alarm_timer->pending) {
            qemu_rearm_alarm_timer(alarm_timer);
        }
        /* Interrupt execution to force deadline recalculation.  */
        qemu_clock_warp(ts->clock);
        if (use_icount) {
            qemu_notify_event();
        }
    }
}

void qemu_mod_timer(QEMUTimer *ts, int64_t expire_time)
{
    qemu_mod_timer_ns(ts, expire_time * ts->scale);
}

int qemu_timer_pending(QEMUTimer *ts)
{
    QEMUTimer *t;
    for (t = ts->clock->active_timers; t != NULL; t = t->next) {
        if (t == ts)
            return 1;
    }
    return 0;
}

int qemu_timer_expired(QEMUTimer *timer_head, int64_t current_time)
{
    return qemu_timer_expired_ns(timer_head, current_time * timer_head->scale);
}

static void qemu_run_timers(QEMUClock *clock)
{
    QEMUTimer **ptimer_head, *ts;
    int64_t current_time;
   
    if (!clock->enabled)
        return;

    current_time = qemu_get_clock_ns(clock);
    ptimer_head = &clock->active_timers;
    for(;;) {
        ts = *ptimer_head;
        if (!qemu_timer_expired_ns(ts, current_time)) {
            break;
        }
        /* remove timer from the list before calling the callback */
        *ptimer_head = ts->next;
        ts->next = NULL;

        /* run the callback (the timer list can be modified) */
        ts->cb(ts->opaque);
    }
}

int64_t qemu_get_clock_ns(QEMUClock *clock)
{
    int64_t now, last;

    switch(clock->type) {
    case QEMU_CLOCK_REALTIME:
        return get_clock();
    default:
    case QEMU_CLOCK_VIRTUAL:
        if (use_icount) {
            return cpu_get_icount();
        } else {
            return cpu_get_clock();
        }
    case QEMU_CLOCK_HOST:
        now = get_clock_realtime();
        last = clock->last;
        clock->last = now;
        if (now < last) {
            notifier_list_notify(&clock->reset_notifiers, &now);
        }
        return now;
    }
}

void qemu_register_clock_reset_notifier(QEMUClock *clock, Notifier *notifier)
{
    notifier_list_add(&clock->reset_notifiers, notifier);
}

void qemu_unregister_clock_reset_notifier(QEMUClock *clock, Notifier *notifier)
{
    notifier_list_remove(&clock->reset_notifiers, notifier);
}

void init_clocks(void)
{
    rt_clock = qemu_new_clock(QEMU_CLOCK_REALTIME);
    vm_clock = qemu_new_clock(QEMU_CLOCK_VIRTUAL);
    host_clock = qemu_new_clock(QEMU_CLOCK_HOST);
}

uint64_t qemu_timer_expire_time_ns(QEMUTimer *ts)
{
    return qemu_timer_pending(ts) ? ts->expire_time : -1;
}

void qemu_run_all_timers(void)
{
    alarm_timer->pending = 0;

    /* rearm timer, if not periodic */
    if (alarm_timer->expired) {
        alarm_timer->expired = 0;
        qemu_rearm_alarm_timer(alarm_timer);
    }

    /* vm time timers */
    qemu_run_timers(vm_clock);
    qemu_run_timers(rt_clock);
    qemu_run_timers(host_clock);
}

#ifdef _WIN32
static void CALLBACK host_alarm_handler(PVOID lpParam, BOOLEAN unused)
#else
static void host_alarm_handler(int host_signum)
#endif
{
    struct qemu_alarm_timer *t = alarm_timer;
    if (!t)
	return;

#if 0
#define DISP_FREQ 1000
    {
        static int64_t delta_min = INT64_MAX;
        static int64_t delta_max, delta_cum, last_clock, delta, ti;
        static int count;
        ti = qemu_get_clock_ns(vm_clock);
        if (last_clock != 0) {
            delta = ti - last_clock;
            if (delta < delta_min)
                delta_min = delta;
            if (delta > delta_max)
                delta_max = delta;
            delta_cum += delta;
            if (++count == DISP_FREQ) {
                printf("timer: min=%" PRId64 " us max=%" PRId64 " us avg=%" PRId64 " us avg_freq=%0.3f Hz\n",
                       muldiv64(delta_min, 1000000, get_ticks_per_sec()),
                       muldiv64(delta_max, 1000000, get_ticks_per_sec()),
                       muldiv64(delta_cum, 1000000 / DISP_FREQ, get_ticks_per_sec()),
                       (double)get_ticks_per_sec() / ((double)delta_cum / DISP_FREQ));
                count = 0;
                delta_min = INT64_MAX;
                delta_max = 0;
                delta_cum = 0;
            }
        }
        last_clock = ti;
    }
#endif
    if (alarm_has_dynticks(t) ||
        qemu_next_alarm_deadline () <= 0) {
        t->expired = alarm_has_dynticks(t);
        t->pending = 1;
        qemu_notify_event();
    }
}

#if defined(__linux__)

#include "compatfd.h"

static int dynticks_start_timer(struct qemu_alarm_timer *t)
{
    struct sigevent ev;
    timer_t host_timer;
    struct sigaction act;

    sigfillset(&act.sa_mask);
    act.sa_flags = 0;
    act.sa_handler = host_alarm_handler;

    sigaction(SIGALRM, &act, NULL);

    /* 
     * Initialize ev struct to 0 to avoid valgrind complaining
     * about uninitialized data in timer_create call
     */
    memset(&ev, 0, sizeof(ev));
    ev.sigev_value.sival_int = 0;
    ev.sigev_notify = SIGEV_SIGNAL;
#ifdef SIGEV_THREAD_ID
    if (qemu_signalfd_available()) {
        ev.sigev_notify = SIGEV_THREAD_ID;
        ev._sigev_un._tid = qemu_get_thread_id();
    }
#endif /* SIGEV_THREAD_ID */
    ev.sigev_signo = SIGALRM;

    if (timer_create(CLOCK_REALTIME, &ev, &host_timer)) {
        perror("timer_create");

        /* disable dynticks */
        fprintf(stderr, "Dynamic Ticks disabled\n");

        return -1;
    }

    t->timer = host_timer;

    return 0;
}

static void dynticks_stop_timer(struct qemu_alarm_timer *t)
{
    timer_t host_timer = t->timer;

    timer_delete(host_timer);
}

static void dynticks_rearm_timer(struct qemu_alarm_timer *t,
                                 int64_t nearest_delta_ns)
{
    timer_t host_timer = t->timer;
    struct itimerspec timeout;
    int64_t current_ns;

    if (nearest_delta_ns < MIN_TIMER_REARM_NS)
        nearest_delta_ns = MIN_TIMER_REARM_NS;

    /* check whether a timer is already running */
    if (timer_gettime(host_timer, &timeout)) {
        perror("gettime");
        fprintf(stderr, "Internal timer error: aborting\n");
        exit(1);
    }
    current_ns = timeout.it_value.tv_sec * 1000000000LL + timeout.it_value.tv_nsec;
    if (current_ns && current_ns <= nearest_delta_ns)
        return;

    timeout.it_interval.tv_sec = 0;
    timeout.it_interval.tv_nsec = 0; /* 0 for one-shot timer */
    timeout.it_value.tv_sec =  nearest_delta_ns / 1000000000;
    timeout.it_value.tv_nsec = nearest_delta_ns % 1000000000;
    if (timer_settime(host_timer, 0 /* RELATIVE */, &timeout, NULL)) {
        perror("settime");
        fprintf(stderr, "Internal timer error: aborting\n");
        exit(1);
    }
}

#endif /* defined(__linux__) */

#if !defined(_WIN32)

static int unix_start_timer(struct qemu_alarm_timer *t)
{
    struct sigaction act;

    /* timer signal */
    sigfillset(&act.sa_mask);
    act.sa_flags = 0;
    act.sa_handler = host_alarm_handler;

    sigaction(SIGALRM, &act, NULL);
    return 0;
}

static void unix_rearm_timer(struct qemu_alarm_timer *t,
                             int64_t nearest_delta_ns)
{
    struct itimerval itv;
    int err;

    if (nearest_delta_ns < MIN_TIMER_REARM_NS)
        nearest_delta_ns = MIN_TIMER_REARM_NS;

    itv.it_interval.tv_sec = 0;
    itv.it_interval.tv_usec = 0; /* 0 for one-shot timer */
    itv.it_value.tv_sec =  nearest_delta_ns / 1000000000;
    itv.it_value.tv_usec = (nearest_delta_ns % 1000000000) / 1000;
    err = setitimer(ITIMER_REAL, &itv, NULL);
    if (err) {
        perror("setitimer");
        fprintf(stderr, "Internal timer error: aborting\n");
        exit(1);
    }
}

static void unix_stop_timer(struct qemu_alarm_timer *t)
{
    struct itimerval itv;

    memset(&itv, 0, sizeof(itv));
    setitimer(ITIMER_REAL, &itv, NULL);
}

#endif /* !defined(_WIN32) */


#ifdef _WIN32

static MMRESULT mm_timer;
static unsigned mm_period;

static void CALLBACK mm_alarm_handler(UINT uTimerID, UINT uMsg,
                                      DWORD_PTR dwUser, DWORD_PTR dw1,
                                      DWORD_PTR dw2)
{
    struct qemu_alarm_timer *t = alarm_timer;
    if (!t) {
        return;
    }
    if (alarm_has_dynticks(t) || qemu_next_alarm_deadline() <= 0) {
        t->expired = alarm_has_dynticks(t);
        t->pending = 1;
        qemu_notify_event();
    }
}

static int mm_start_timer(struct qemu_alarm_timer *t)
{
    TIMECAPS tc;
    UINT flags;

    memset(&tc, 0, sizeof(tc));
    timeGetDevCaps(&tc, sizeof(tc));

    mm_period = tc.wPeriodMin;
    timeBeginPeriod(mm_period);

    flags = TIME_CALLBACK_FUNCTION;
    if (alarm_has_dynticks(t)) {
        flags |= TIME_ONESHOT;
    } else {
        flags |= TIME_PERIODIC;
    }

    mm_timer = timeSetEvent(1,                  /* interval (ms) */
                            mm_period,          /* resolution */
                            mm_alarm_handler,   /* function */
                            (DWORD_PTR)t,       /* parameter */
                            flags);

    if (!mm_timer) {
        fprintf(stderr, "Failed to initialize win32 alarm timer: %ld\n",
                GetLastError());
        timeEndPeriod(mm_period);
        return -1;
    }

    return 0;
}

static void mm_stop_timer(struct qemu_alarm_timer *t)
{
    timeKillEvent(mm_timer);
    timeEndPeriod(mm_period);
}

static void mm_rearm_timer(struct qemu_alarm_timer *t, int64_t delta)
{
    int nearest_delta_ms = (delta + 999999) / 1000000;
    if (nearest_delta_ms < 1) {
        nearest_delta_ms = 1;
    }

    timeKillEvent(mm_timer);
    mm_timer = timeSetEvent(nearest_delta_ms,
                            mm_period,
                            mm_alarm_handler,
                            (DWORD_PTR)t,
                            TIME_ONESHOT | TIME_CALLBACK_FUNCTION);

    if (!mm_timer) {
        fprintf(stderr, "Failed to re-arm win32 alarm timer %ld\n",
                GetLastError());

        timeEndPeriod(mm_period);
        exit(1);
    }
}

static int win32_start_timer(struct qemu_alarm_timer *t)
{
    HANDLE hTimer;
    BOOLEAN success;

    /* If you call ChangeTimerQueueTimer on a one-shot timer (its period
       is zero) that has already expired, the timer is not updated.  Since
       creating a new timer is relatively expensive, set a bogus one-hour
       interval in the dynticks case.  */
    success = CreateTimerQueueTimer(&hTimer,
                          NULL,
                          host_alarm_handler,
                          t,
                          1,
                          alarm_has_dynticks(t) ? 3600000 : 1,
                          WT_EXECUTEINTIMERTHREAD);

    if (!success) {
        fprintf(stderr, "Failed to initialize win32 alarm timer: %ld\n",
                GetLastError());
        return -1;
    }

    t->timer = hTimer;
    return 0;
}

static void win32_stop_timer(struct qemu_alarm_timer *t)
{
    HANDLE hTimer = t->timer;

    if (hTimer) {
        DeleteTimerQueueTimer(NULL, hTimer, NULL);
    }
}

static void win32_rearm_timer(struct qemu_alarm_timer *t,
                              int64_t nearest_delta_ns)
{
    HANDLE hTimer = t->timer;
    int nearest_delta_ms;
    BOOLEAN success;

    nearest_delta_ms = (nearest_delta_ns + 999999) / 1000000;
    if (nearest_delta_ms < 1) {
        nearest_delta_ms = 1;
    }
    success = ChangeTimerQueueTimer(NULL,
                                    hTimer,
                                    nearest_delta_ms,
                                    3600000);

    if (!success) {
        fprintf(stderr, "Failed to rearm win32 alarm timer: %ld\n",
                GetLastError());
        exit(-1);
    }

}

#endif /* _WIN32 */

static void quit_timers(void)
{
    struct qemu_alarm_timer *t = alarm_timer;
    alarm_timer = NULL;
    t->stop(t);
}

int init_timer_alarm(void)
{
    struct qemu_alarm_timer *t = NULL;
    int i, err = -1;

    for (i = 0; alarm_timers[i].name; i++) {
        t = &alarm_timers[i];

        err = t->start(t);
        if (!err)
            break;
    }

    if (err) {
        err = -ENOENT;
        goto fail;
    }

    /* first event is at time 0 */
    atexit(quit_timers);
    t->pending = 1;
    alarm_timer = t;

    return 0;

fail:
    return err;
}

int qemu_calculate_timeout(void)
{
    return 1000;
}

