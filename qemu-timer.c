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

#include "sysemu/sysemu.h"
#include "monitor/monitor.h"
#include "ui/console.h"

#include "hw/hw.h"

#include "qemu/timer.h"
#ifdef CONFIG_POSIX
#include <pthread.h>
#endif

#ifdef _WIN32
#include <mmsystem.h>
#endif

#ifdef CONFIG_PPOLL
#include <poll.h>
#endif

#ifdef CONFIG_PRCTL_PR_SET_TIMERSLACK
#include <sys/prctl.h>
#endif

/***********************************************************/
/* timers */

struct QEMUClock {
    QEMUTimerList *main_loop_timerlist;
    QLIST_HEAD(, QEMUTimerList) timerlists;

    NotifierList reset_notifiers;
    int64_t last;

    QEMUClockType type;
    bool enabled;
};

QEMUTimerListGroup main_loop_tlg;
QEMUClock *qemu_clocks[QEMU_CLOCK_MAX];

/* A QEMUTimerList is a list of timers attached to a clock. More
 * than one QEMUTimerList can be attached to each clock, for instance
 * used by different AioContexts / threads. Each clock also has
 * a list of the QEMUTimerLists associated with it, in order that
 * reenabling the clock can call all the notifiers.
 */

struct QEMUTimerList {
    QEMUClock *clock;
    QEMUTimer *active_timers;
    QLIST_ENTRY(QEMUTimerList) list;
    QEMUTimerListNotifyCB *notify_cb;
    void *notify_opaque;
};

struct qemu_alarm_timer {
    char const *name;
    int (*start)(struct qemu_alarm_timer *t);
    void (*stop)(struct qemu_alarm_timer *t);
    void (*rearm)(struct qemu_alarm_timer *t, int64_t nearest_delta_ns);
#if defined(__linux__)
    timer_t timer;
    int fd;
#elif defined(_WIN32)
    HANDLE timer;
#endif
    bool expired;
    bool pending;
};

static struct qemu_alarm_timer *alarm_timer;

static bool timer_expired_ns(QEMUTimer *timer_head, int64_t current_time)
{
    return timer_head && (timer_head->expire_time <= current_time);
}

static int64_t qemu_next_alarm_deadline(void)
{
    int64_t delta = INT64_MAX;
    int64_t rtdelta;
    int64_t hdelta;

    if (!use_icount && vm_clock->enabled &&
        vm_clock->main_loop_timerlist->active_timers) {
        delta = vm_clock->main_loop_timerlist->active_timers->expire_time -
            qemu_get_clock_ns(vm_clock);
    }
    if (host_clock->enabled &&
        host_clock->main_loop_timerlist->active_timers) {
        hdelta = host_clock->main_loop_timerlist->active_timers->expire_time -
            qemu_get_clock_ns(host_clock);
        if (hdelta < delta) {
            delta = hdelta;
        }
    }
    if (rt_clock->enabled &&
        rt_clock->main_loop_timerlist->active_timers) {
        rtdelta = (rt_clock->main_loop_timerlist->active_timers->expire_time -
                   qemu_get_clock_ns(rt_clock));
        if (rtdelta < delta) {
            delta = rtdelta;
        }
    }

    return delta;
}

static void qemu_rearm_alarm_timer(struct qemu_alarm_timer *t)
{
    int64_t nearest_delta_ns = qemu_next_alarm_deadline();
    if (nearest_delta_ns < INT64_MAX) {
        t->rearm(t, nearest_delta_ns);
    }
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

    if (is_help_option(opt)) {
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

static QEMUTimerList *timerlist_new_from_clock(QEMUClock *clock,
                                               QEMUTimerListNotifyCB *cb,
                                               void *opaque)
{
    QEMUTimerList *timer_list;

    /* Assert if we do not have a clock. If you see this
     * assertion in means that the clocks have not been
     * initialised before a timerlist is needed. This
     * normally happens if an AioContext is used before
     * init_clocks() is called within main().
     */
    assert(clock);

    timer_list = g_malloc0(sizeof(QEMUTimerList));
    timer_list->clock = clock;
    timer_list->notify_cb = cb;
    timer_list->notify_opaque = opaque;
    QLIST_INSERT_HEAD(&clock->timerlists, timer_list, list);
    return timer_list;
}

QEMUTimerList *timerlist_new(QEMUClockType type,
                             QEMUTimerListNotifyCB *cb, void *opaque)
{
    return timerlist_new_from_clock(qemu_clock_ptr(type), cb, opaque);
}

void timerlist_free(QEMUTimerList *timer_list)
{
    assert(!timerlist_has_timers(timer_list));
    if (timer_list->clock) {
        QLIST_REMOVE(timer_list, list);
        if (timer_list->clock->main_loop_timerlist == timer_list) {
            timer_list->clock->main_loop_timerlist = NULL;
        }
    }
    g_free(timer_list);
}

static QEMUClock *qemu_clock_new(QEMUClockType type)
{
    QEMUClock *clock;

    clock = g_malloc0(sizeof(QEMUClock));
    clock->type = type;
    clock->enabled = true;
    clock->last = INT64_MIN;
    QLIST_INIT(&clock->timerlists);
    notifier_list_init(&clock->reset_notifiers);
    clock->main_loop_timerlist = timerlist_new_from_clock(clock, NULL, NULL);
    return clock;
}

bool qemu_clock_use_for_deadline(QEMUClock *clock)
{
    return !(use_icount && (clock->type == QEMU_CLOCK_VIRTUAL));
}

void qemu_clock_enable(QEMUClock *clock, bool enabled)
{
    bool old = clock->enabled;
    clock->enabled = enabled;
    if (enabled && !old) {
        qemu_rearm_alarm_timer(alarm_timer);
    }
}

bool timerlist_has_timers(QEMUTimerList *timer_list)
{
    return !!timer_list->active_timers;
}

bool qemu_clock_has_timers(QEMUClock *clock)
{
    return timerlist_has_timers(clock->main_loop_timerlist);
}

bool timerlist_expired(QEMUTimerList *timer_list)
{
    return (timer_list->active_timers &&
            timer_list->active_timers->expire_time <
            qemu_get_clock_ns(timer_list->clock));
}

bool qemu_clock_expired(QEMUClock *clock)
{
    return timerlist_expired(clock->main_loop_timerlist);
}

int64_t timerlist_deadline(QEMUTimerList *timer_list)
{
    /* To avoid problems with overflow limit this to 2^32.  */
    int64_t delta = INT32_MAX;

    if (timer_list->clock->enabled && timer_list->active_timers) {
        delta = timer_list->active_timers->expire_time -
            qemu_get_clock_ns(timer_list->clock);
    }
    if (delta < 0) {
        delta = 0;
    }
    return delta;
}

int64_t qemu_clock_deadline(QEMUClock *clock)
{
    return timerlist_deadline(clock->main_loop_timerlist);
}

/*
 * As above, but return -1 for no deadline, and do not cap to 2^32
 * as we know the result is always positive.
 */

int64_t timerlist_deadline_ns(QEMUTimerList *timer_list)
{
    int64_t delta;

    if (!timer_list->clock->enabled || !timer_list->active_timers) {
        return -1;
    }

    delta = timer_list->active_timers->expire_time -
        qemu_get_clock_ns(timer_list->clock);

    if (delta <= 0) {
        return 0;
    }

    return delta;
}

int64_t qemu_clock_deadline_ns(QEMUClock *clock)
{
    return timerlist_deadline_ns(clock->main_loop_timerlist);
}

QEMUClock *timerlist_get_clock(QEMUTimerList *timer_list)
{
    return timer_list->clock;
}

QEMUTimerList *qemu_clock_get_main_loop_timerlist(QEMUClock *clock)
{
    return clock->main_loop_timerlist;
}

void timerlist_notify(QEMUTimerList *timer_list)
{
    if (timer_list->notify_cb) {
        timer_list->notify_cb(timer_list->notify_opaque);
    } else {
        qemu_notify_event();
    }
}

/* Transition function to convert a nanosecond timeout to ms
 * This is used where a system does not support ppoll
 */
int qemu_timeout_ns_to_ms(int64_t ns)
{
    int64_t ms;
    if (ns < 0) {
        return -1;
    }

    if (!ns) {
        return 0;
    }

    /* Always round up, because it's better to wait too long than to wait too
     * little and effectively busy-wait
     */
    ms = (ns + SCALE_MS - 1) / SCALE_MS;

    /* To avoid overflow problems, limit this to 2^31, i.e. approx 25 days */
    if (ms > (int64_t) INT32_MAX) {
        ms = INT32_MAX;
    }

    return (int) ms;
}


/* qemu implementation of g_poll which uses a nanosecond timeout but is
 * otherwise identical to g_poll
 */
int qemu_poll_ns(GPollFD *fds, guint nfds, int64_t timeout)
{
#ifdef CONFIG_PPOLL
    if (timeout < 0) {
        return ppoll((struct pollfd *)fds, nfds, NULL, NULL);
    } else {
        struct timespec ts;
        ts.tv_sec = timeout / 1000000000LL;
        ts.tv_nsec = timeout % 1000000000LL;
        return ppoll((struct pollfd *)fds, nfds, &ts, NULL);
    }
#else
    return g_poll(fds, nfds, qemu_timeout_ns_to_ms(timeout));
#endif
}


void timer_init(QEMUTimer *ts,
                QEMUTimerList *timer_list, int scale,
                QEMUTimerCB *cb, void *opaque)
{
    ts->timer_list = timer_list;
    ts->cb = cb;
    ts->opaque = opaque;
    ts->scale = scale;
}

QEMUTimer *qemu_new_timer(QEMUClock *clock, int scale,
                          QEMUTimerCB *cb, void *opaque)
{
    return timer_new_tl(clock->main_loop_timerlist,
                     scale, cb, opaque);
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
       timer_expired() can be called from a signal. */
    pt = &ts->timer_list->active_timers;
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
       timer_expired() can be called from a signal. */
    pt = &ts->timer_list->active_timers;
    for(;;) {
        t = *pt;
        if (!timer_expired_ns(t, expire_time)) {
            break;
        }
        pt = &t->next;
    }
    ts->expire_time = expire_time;
    ts->next = *pt;
    *pt = ts;

    /* Rearm if necessary  */
    if (pt == &ts->timer_list->active_timers) {
        if (!alarm_timer->pending) {
            qemu_rearm_alarm_timer(alarm_timer);
        }
        /* Interrupt execution to force deadline recalculation.  */
        qemu_clock_warp(ts->timer_list->clock);
        if (use_icount) {
            timerlist_notify(ts->timer_list);
        }
    }
}

void qemu_mod_timer(QEMUTimer *ts, int64_t expire_time)
{
    qemu_mod_timer_ns(ts, expire_time * ts->scale);
}

bool timer_pending(QEMUTimer *ts)
{
    QEMUTimer *t;
    for (t = ts->timer_list->active_timers; t != NULL; t = t->next) {
        if (t == ts) {
            return true;
        }
    }
    return false;
}

bool timer_expired(QEMUTimer *timer_head, int64_t current_time)
{
    return timer_expired_ns(timer_head, current_time * timer_head->scale);
}

bool timerlist_run_timers(QEMUTimerList *timer_list)
{
    QEMUTimer *ts;
    int64_t current_time;
    bool progress = false;
   
    if (!timer_list->clock->enabled) {
        return progress;
    }

    current_time = qemu_get_clock_ns(timer_list->clock);
    for(;;) {
        ts = timer_list->active_timers;
        if (!timer_expired_ns(ts, current_time)) {
            break;
        }
        /* remove timer from the list before calling the callback */
        timer_list->active_timers = ts->next;
        ts->next = NULL;

        /* run the callback (the timer list can be modified) */
        ts->cb(ts->opaque);
        progress = true;
    }
    return progress;
}

bool qemu_run_timers(QEMUClock *clock)
{
    return timerlist_run_timers(clock->main_loop_timerlist);
}

void timerlistgroup_init(QEMUTimerListGroup *tlg,
                         QEMUTimerListNotifyCB *cb, void *opaque)
{
    QEMUClockType type;
    for (type = 0; type < QEMU_CLOCK_MAX; type++) {
        tlg->tl[type] = timerlist_new(type, cb, opaque);
    }
}

void timerlistgroup_deinit(QEMUTimerListGroup *tlg)
{
    QEMUClockType type;
    for (type = 0; type < QEMU_CLOCK_MAX; type++) {
        timerlist_free(tlg->tl[type]);
    }
}

bool timerlistgroup_run_timers(QEMUTimerListGroup *tlg)
{
    QEMUClockType type;
    bool progress = false;
    for (type = 0; type < QEMU_CLOCK_MAX; type++) {
        progress |= timerlist_run_timers(tlg->tl[type]);
    }
    return progress;
}

int64_t timerlistgroup_deadline_ns(QEMUTimerListGroup *tlg)
{
    int64_t deadline = -1;
    QEMUClockType type;
    for (type = 0; type < QEMU_CLOCK_MAX; type++) {
        if (qemu_clock_use_for_deadline(tlg->tl[type]->clock)) {
            deadline = qemu_soonest_timeout(deadline,
                                            timerlist_deadline_ns(
                                                tlg->tl[type]));
        }
    }
    return deadline;
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
    notifier_remove(notifier);
}

void init_clocks(void)
{
    QEMUClockType type;
    for (type = 0; type < QEMU_CLOCK_MAX; type++) {
        if (!qemu_clocks[type]) {
            qemu_clocks[type] = qemu_clock_new(type);
            main_loop_tlg.tl[type] = qemu_clocks[type]->main_loop_timerlist;
        }
    }

#ifdef CONFIG_PRCTL_PR_SET_TIMERSLACK
    prctl(PR_SET_TIMERSLACK, 1, 0, 0, 0);
#endif
}

uint64_t timer_expire_time_ns(QEMUTimer *ts)
{
    return timer_pending(ts) ? ts->expire_time : -1;
}

bool qemu_run_all_timers(void)
{
    bool progress = false;
    alarm_timer->pending = false;

    /* vm time timers */
    QEMUClockType type;
    for (type = 0; type < QEMU_CLOCK_MAX; type++) {
        progress |= qemu_run_timers(qemu_clock_ptr(type));
    }

    /* rearm timer, if not periodic */
    if (alarm_timer->expired) {
        alarm_timer->expired = false;
        qemu_rearm_alarm_timer(alarm_timer);
    }

    return progress;
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

    t->expired = true;
    t->pending = true;
    qemu_notify_event();
}

#if defined(__linux__)

#include "qemu/compatfd.h"

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
#ifdef CONFIG_SIGEV_THREAD_ID
    if (qemu_signalfd_available()) {
        ev.sigev_notify = SIGEV_THREAD_ID;
        ev._sigev_un._tid = qemu_get_thread_id();
    }
#endif /* CONFIG_SIGEV_THREAD_ID */
    ev.sigev_signo = SIGALRM;

    if (timer_create(CLOCK_REALTIME, &ev, &host_timer)) {
        perror("timer_create");
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
static TIMECAPS mm_tc;

static void CALLBACK mm_alarm_handler(UINT uTimerID, UINT uMsg,
                                      DWORD_PTR dwUser, DWORD_PTR dw1,
                                      DWORD_PTR dw2)
{
    struct qemu_alarm_timer *t = alarm_timer;
    if (!t) {
        return;
    }
    t->expired = true;
    t->pending = true;
    qemu_notify_event();
}

static int mm_start_timer(struct qemu_alarm_timer *t)
{
    timeGetDevCaps(&mm_tc, sizeof(mm_tc));
    return 0;
}

static void mm_stop_timer(struct qemu_alarm_timer *t)
{
    if (mm_timer) {
        timeKillEvent(mm_timer);
    }
}

static void mm_rearm_timer(struct qemu_alarm_timer *t, int64_t delta)
{
    int64_t nearest_delta_ms = delta / 1000000;
    if (nearest_delta_ms < mm_tc.wPeriodMin) {
        nearest_delta_ms = mm_tc.wPeriodMin;
    } else if (nearest_delta_ms > mm_tc.wPeriodMax) {
        nearest_delta_ms = mm_tc.wPeriodMax;
    }

    if (mm_timer) {
        timeKillEvent(mm_timer);
    }
    mm_timer = timeSetEvent((UINT)nearest_delta_ms,
                            mm_tc.wPeriodMin,
                            mm_alarm_handler,
                            (DWORD_PTR)t,
                            TIME_ONESHOT | TIME_CALLBACK_FUNCTION);

    if (!mm_timer) {
        fprintf(stderr, "Failed to re-arm win32 alarm timer\n");
        timeEndPeriod(mm_tc.wPeriodMin);
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
                          3600000,
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
    int64_t nearest_delta_ms;
    BOOLEAN success;

    nearest_delta_ms = nearest_delta_ns / 1000000;
    if (nearest_delta_ms < 1) {
        nearest_delta_ms = 1;
    }
    /* ULONG_MAX can be 32 bit */
    if (nearest_delta_ms > ULONG_MAX) {
        nearest_delta_ms = ULONG_MAX;
    }
    success = ChangeTimerQueueTimer(NULL,
                                    hTimer,
                                    (unsigned long) nearest_delta_ms,
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

#ifdef CONFIG_POSIX
static void reinit_timers(void)
{
    struct qemu_alarm_timer *t = alarm_timer;
    t->stop(t);
    if (t->start(t)) {
        fprintf(stderr, "Internal timer error: aborting\n");
        exit(1);
    }
    qemu_rearm_alarm_timer(t);
}
#endif /* CONFIG_POSIX */

int init_timer_alarm(void)
{
    struct qemu_alarm_timer *t = NULL;
    int i, err = -1;

    if (alarm_timer) {
        return 0;
    }

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

    atexit(quit_timers);
#ifdef CONFIG_POSIX
    pthread_atfork(NULL, NULL, reinit_timers);
#endif
    alarm_timer = t;
    return 0;

fail:
    return err;
}

