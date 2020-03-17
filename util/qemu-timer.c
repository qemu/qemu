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
#include "qemu/main-loop.h"
#include "qemu/timer.h"
#include "qemu/lockable.h"
#include "sysemu/replay.h"
#include "sysemu/cpus.h"

#ifdef CONFIG_POSIX
#include <pthread.h>
#endif

#ifdef CONFIG_PPOLL
#include <poll.h>
#endif

#ifdef CONFIG_PRCTL_PR_SET_TIMERSLACK
#include <sys/prctl.h>
#endif

/***********************************************************/
/* timers */

typedef struct QEMUClock {
    /* We rely on BQL to protect the timerlists */
    QLIST_HEAD(, QEMUTimerList) timerlists;

    QEMUClockType type;
    bool enabled;
} QEMUClock;

QEMUTimerListGroup main_loop_tlg;
static QEMUClock qemu_clocks[QEMU_CLOCK_MAX];

/* A QEMUTimerList is a list of timers attached to a clock. More
 * than one QEMUTimerList can be attached to each clock, for instance
 * used by different AioContexts / threads. Each clock also has
 * a list of the QEMUTimerLists associated with it, in order that
 * reenabling the clock can call all the notifiers.
 */

struct QEMUTimerList {
    QEMUClock *clock;
    QemuMutex active_timers_lock;
    QEMUTimer *active_timers;
    QLIST_ENTRY(QEMUTimerList) list;
    QEMUTimerListNotifyCB *notify_cb;
    void *notify_opaque;

    /* lightweight method to mark the end of timerlist's running */
    QemuEvent timers_done_ev;
};

/**
 * qemu_clock_ptr:
 * @type: type of clock
 *
 * Translate a clock type into a pointer to QEMUClock object.
 *
 * Returns: a pointer to the QEMUClock object
 */
static inline QEMUClock *qemu_clock_ptr(QEMUClockType type)
{
    return &qemu_clocks[type];
}

static bool timer_expired_ns(QEMUTimer *timer_head, int64_t current_time)
{
    return timer_head && (timer_head->expire_time <= current_time);
}

QEMUTimerList *timerlist_new(QEMUClockType type,
                             QEMUTimerListNotifyCB *cb,
                             void *opaque)
{
    QEMUTimerList *timer_list;
    QEMUClock *clock = qemu_clock_ptr(type);

    timer_list = g_malloc0(sizeof(QEMUTimerList));
    qemu_event_init(&timer_list->timers_done_ev, true);
    timer_list->clock = clock;
    timer_list->notify_cb = cb;
    timer_list->notify_opaque = opaque;
    qemu_mutex_init(&timer_list->active_timers_lock);
    QLIST_INSERT_HEAD(&clock->timerlists, timer_list, list);
    return timer_list;
}

void timerlist_free(QEMUTimerList *timer_list)
{
    assert(!timerlist_has_timers(timer_list));
    if (timer_list->clock) {
        QLIST_REMOVE(timer_list, list);
    }
    qemu_mutex_destroy(&timer_list->active_timers_lock);
    g_free(timer_list);
}

static void qemu_clock_init(QEMUClockType type, QEMUTimerListNotifyCB *notify_cb)
{
    QEMUClock *clock = qemu_clock_ptr(type);

    /* Assert that the clock of type TYPE has not been initialized yet. */
    assert(main_loop_tlg.tl[type] == NULL);

    clock->type = type;
    clock->enabled = (type == QEMU_CLOCK_VIRTUAL ? false : true);
    QLIST_INIT(&clock->timerlists);
    main_loop_tlg.tl[type] = timerlist_new(type, notify_cb, NULL);
}

bool qemu_clock_use_for_deadline(QEMUClockType type)
{
    return !(use_icount && (type == QEMU_CLOCK_VIRTUAL));
}

void qemu_clock_notify(QEMUClockType type)
{
    QEMUTimerList *timer_list;
    QEMUClock *clock = qemu_clock_ptr(type);
    QLIST_FOREACH(timer_list, &clock->timerlists, list) {
        timerlist_notify(timer_list);
    }
}

/* Disabling the clock will wait for related timerlists to stop
 * executing qemu_run_timers.  Thus, this functions should not
 * be used from the callback of a timer that is based on @clock.
 * Doing so would cause a deadlock.
 *
 * Caller should hold BQL.
 */
void qemu_clock_enable(QEMUClockType type, bool enabled)
{
    QEMUClock *clock = qemu_clock_ptr(type);
    QEMUTimerList *tl;
    bool old = clock->enabled;
    clock->enabled = enabled;
    if (enabled && !old) {
        qemu_clock_notify(type);
    } else if (!enabled && old) {
        QLIST_FOREACH(tl, &clock->timerlists, list) {
            qemu_event_wait(&tl->timers_done_ev);
        }
    }
}

bool timerlist_has_timers(QEMUTimerList *timer_list)
{
    return !!atomic_read(&timer_list->active_timers);
}

bool qemu_clock_has_timers(QEMUClockType type)
{
    return timerlist_has_timers(
        main_loop_tlg.tl[type]);
}

bool timerlist_expired(QEMUTimerList *timer_list)
{
    int64_t expire_time;

    if (!atomic_read(&timer_list->active_timers)) {
        return false;
    }

    WITH_QEMU_LOCK_GUARD(&timer_list->active_timers_lock) {
        if (!timer_list->active_timers) {
            return false;
        }
        expire_time = timer_list->active_timers->expire_time;
    }

    return expire_time <= qemu_clock_get_ns(timer_list->clock->type);
}

bool qemu_clock_expired(QEMUClockType type)
{
    return timerlist_expired(
        main_loop_tlg.tl[type]);
}

/*
 * As above, but return -1 for no deadline, and do not cap to 2^32
 * as we know the result is always positive.
 */

int64_t timerlist_deadline_ns(QEMUTimerList *timer_list)
{
    int64_t delta;
    int64_t expire_time;

    if (!atomic_read(&timer_list->active_timers)) {
        return -1;
    }

    if (!timer_list->clock->enabled) {
        return -1;
    }

    /* The active timers list may be modified before the caller uses our return
     * value but ->notify_cb() is called when the deadline changes.  Therefore
     * the caller should notice the change and there is no race condition.
     */
    WITH_QEMU_LOCK_GUARD(&timer_list->active_timers_lock) {
        if (!timer_list->active_timers) {
            return -1;
        }
        expire_time = timer_list->active_timers->expire_time;
    }

    delta = expire_time - qemu_clock_get_ns(timer_list->clock->type);

    if (delta <= 0) {
        return 0;
    }

    return delta;
}

/* Calculate the soonest deadline across all timerlists attached
 * to the clock. This is used for the icount timeout so we
 * ignore whether or not the clock should be used in deadline
 * calculations.
 */
int64_t qemu_clock_deadline_ns_all(QEMUClockType type, int attr_mask)
{
    int64_t deadline = -1;
    int64_t delta;
    int64_t expire_time;
    QEMUTimer *ts;
    QEMUTimerList *timer_list;
    QEMUClock *clock = qemu_clock_ptr(type);

    if (!clock->enabled) {
        return -1;
    }

    QLIST_FOREACH(timer_list, &clock->timerlists, list) {
        qemu_mutex_lock(&timer_list->active_timers_lock);
        ts = timer_list->active_timers;
        /* Skip all external timers */
        while (ts && (ts->attributes & ~attr_mask)) {
            ts = ts->next;
        }
        if (!ts) {
            qemu_mutex_unlock(&timer_list->active_timers_lock);
            continue;
        }
        expire_time = ts->expire_time;
        qemu_mutex_unlock(&timer_list->active_timers_lock);

        delta = expire_time - qemu_clock_get_ns(type);
        if (delta <= 0) {
            delta = 0;
        }
        deadline = qemu_soonest_timeout(deadline, delta);
    }
    return deadline;
}

QEMUClockType timerlist_get_clock(QEMUTimerList *timer_list)
{
    return timer_list->clock->type;
}

QEMUTimerList *qemu_clock_get_main_loop_timerlist(QEMUClockType type)
{
    return main_loop_tlg.tl[type];
}

void timerlist_notify(QEMUTimerList *timer_list)
{
    if (timer_list->notify_cb) {
        timer_list->notify_cb(timer_list->notify_opaque, timer_list->clock->type);
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
    ms = DIV_ROUND_UP(ns, SCALE_MS);

    /* To avoid overflow problems, limit this to 2^31, i.e. approx 25 days */
    return MIN(ms, INT32_MAX);
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
        int64_t tvsec = timeout / 1000000000LL;
        /* Avoid possibly overflowing and specifying a negative number of
         * seconds, which would turn a very long timeout into a busy-wait.
         */
        if (tvsec > (int64_t)INT32_MAX) {
            tvsec = INT32_MAX;
        }
        ts.tv_sec = tvsec;
        ts.tv_nsec = timeout % 1000000000LL;
        return ppoll((struct pollfd *)fds, nfds, &ts, NULL);
    }
#else
    return g_poll(fds, nfds, qemu_timeout_ns_to_ms(timeout));
#endif
}


void timer_init_full(QEMUTimer *ts,
                     QEMUTimerListGroup *timer_list_group, QEMUClockType type,
                     int scale, int attributes,
                     QEMUTimerCB *cb, void *opaque)
{
    if (!timer_list_group) {
        timer_list_group = &main_loop_tlg;
    }
    ts->timer_list = timer_list_group->tl[type];
    ts->cb = cb;
    ts->opaque = opaque;
    ts->scale = scale;
    ts->attributes = attributes;
    ts->expire_time = -1;
}

void timer_deinit(QEMUTimer *ts)
{
    assert(ts->expire_time == -1);
    ts->timer_list = NULL;
}

static void timer_del_locked(QEMUTimerList *timer_list, QEMUTimer *ts)
{
    QEMUTimer **pt, *t;

    ts->expire_time = -1;
    pt = &timer_list->active_timers;
    for(;;) {
        t = *pt;
        if (!t)
            break;
        if (t == ts) {
            atomic_set(pt, t->next);
            break;
        }
        pt = &t->next;
    }
}

static bool timer_mod_ns_locked(QEMUTimerList *timer_list,
                                QEMUTimer *ts, int64_t expire_time)
{
    QEMUTimer **pt, *t;

    /* add the timer in the sorted list */
    pt = &timer_list->active_timers;
    for (;;) {
        t = *pt;
        if (!timer_expired_ns(t, expire_time)) {
            break;
        }
        pt = &t->next;
    }
    ts->expire_time = MAX(expire_time, 0);
    ts->next = *pt;
    atomic_set(pt, ts);

    return pt == &timer_list->active_timers;
}

static void timerlist_rearm(QEMUTimerList *timer_list)
{
    /* Interrupt execution to force deadline recalculation.  */
    if (timer_list->clock->type == QEMU_CLOCK_VIRTUAL) {
        qemu_start_warp_timer();
    }
    timerlist_notify(timer_list);
}

/* stop a timer, but do not dealloc it */
void timer_del(QEMUTimer *ts)
{
    QEMUTimerList *timer_list = ts->timer_list;

    if (timer_list) {
        qemu_mutex_lock(&timer_list->active_timers_lock);
        timer_del_locked(timer_list, ts);
        qemu_mutex_unlock(&timer_list->active_timers_lock);
    }
}

/* modify the current timer so that it will be fired when current_time
   >= expire_time. The corresponding callback will be called. */
void timer_mod_ns(QEMUTimer *ts, int64_t expire_time)
{
    QEMUTimerList *timer_list = ts->timer_list;
    bool rearm;

    qemu_mutex_lock(&timer_list->active_timers_lock);
    timer_del_locked(timer_list, ts);
    rearm = timer_mod_ns_locked(timer_list, ts, expire_time);
    qemu_mutex_unlock(&timer_list->active_timers_lock);

    if (rearm) {
        timerlist_rearm(timer_list);
    }
}

/* modify the current timer so that it will be fired when current_time
   >= expire_time or the current deadline, whichever comes earlier.
   The corresponding callback will be called. */
void timer_mod_anticipate_ns(QEMUTimer *ts, int64_t expire_time)
{
    QEMUTimerList *timer_list = ts->timer_list;
    bool rearm;

    qemu_mutex_lock(&timer_list->active_timers_lock);
    if (ts->expire_time == -1 || ts->expire_time > expire_time) {
        if (ts->expire_time != -1) {
            timer_del_locked(timer_list, ts);
        }
        rearm = timer_mod_ns_locked(timer_list, ts, expire_time);
    } else {
        rearm = false;
    }
    qemu_mutex_unlock(&timer_list->active_timers_lock);

    if (rearm) {
        timerlist_rearm(timer_list);
    }
}

void timer_mod(QEMUTimer *ts, int64_t expire_time)
{
    timer_mod_ns(ts, expire_time * ts->scale);
}

void timer_mod_anticipate(QEMUTimer *ts, int64_t expire_time)
{
    timer_mod_anticipate_ns(ts, expire_time * ts->scale);
}

bool timer_pending(QEMUTimer *ts)
{
    return ts->expire_time >= 0;
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
    QEMUTimerCB *cb;
    void *opaque;
    bool need_replay_checkpoint = false;

    if (!atomic_read(&timer_list->active_timers)) {
        return false;
    }

    qemu_event_reset(&timer_list->timers_done_ev);
    if (!timer_list->clock->enabled) {
        goto out;
    }

    switch (timer_list->clock->type) {
    case QEMU_CLOCK_REALTIME:
        break;
    default:
    case QEMU_CLOCK_VIRTUAL:
        if (replay_mode != REPLAY_MODE_NONE) {
            /* Checkpoint for virtual clock is redundant in cases where
             * it's being triggered with only non-EXTERNAL timers, because
             * these timers don't change guest state directly.
             * Since it has conditional dependence on specific timers, it is
             * subject to race conditions and requires special handling.
             * See below.
             */
            need_replay_checkpoint = true;
        }
        break;
    case QEMU_CLOCK_HOST:
        if (!replay_checkpoint(CHECKPOINT_CLOCK_HOST)) {
            goto out;
        }
        break;
    case QEMU_CLOCK_VIRTUAL_RT:
        if (!replay_checkpoint(CHECKPOINT_CLOCK_VIRTUAL_RT)) {
            goto out;
        }
        break;
    }

    /*
     * Extract expired timers from active timers list and and process them.
     *
     * In rr mode we need "filtered" checkpointing for virtual clock.  The
     * checkpoint must be recorded/replayed before processing any non-EXTERNAL timer,
     * and that must only be done once since the clock value stays the same. Because
     * non-EXTERNAL timers may appear in the timers list while it being processed,
     * the checkpoint can be issued at a time until no timers are left and we are
     * done".
     */
    current_time = qemu_clock_get_ns(timer_list->clock->type);
    qemu_mutex_lock(&timer_list->active_timers_lock);
    while ((ts = timer_list->active_timers)) {
        if (!timer_expired_ns(ts, current_time)) {
            /* No expired timers left.  The checkpoint can be skipped
             * if no timers fired or they were all external.
             */
            break;
        }
        if (need_replay_checkpoint
                && !(ts->attributes & QEMU_TIMER_ATTR_EXTERNAL)) {
            /* once we got here, checkpoint clock only once */
            need_replay_checkpoint = false;
            qemu_mutex_unlock(&timer_list->active_timers_lock);
            if (!replay_checkpoint(CHECKPOINT_CLOCK_VIRTUAL)) {
                goto out;
            }
            qemu_mutex_lock(&timer_list->active_timers_lock);
            /* The lock was released; start over again in case the list was
             * modified.
             */
            continue;
        }

        /* remove timer from the list before calling the callback */
        timer_list->active_timers = ts->next;
        ts->next = NULL;
        ts->expire_time = -1;
        cb = ts->cb;
        opaque = ts->opaque;

        /* run the callback (the timer list can be modified) */
        qemu_mutex_unlock(&timer_list->active_timers_lock);
        cb(opaque);
        qemu_mutex_lock(&timer_list->active_timers_lock);

        progress = true;
    }
    qemu_mutex_unlock(&timer_list->active_timers_lock);

out:
    qemu_event_set(&timer_list->timers_done_ev);
    return progress;
}

bool qemu_clock_run_timers(QEMUClockType type)
{
    return timerlist_run_timers(main_loop_tlg.tl[type]);
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
        if (qemu_clock_use_for_deadline(type)) {
            deadline = qemu_soonest_timeout(deadline,
                                            timerlist_deadline_ns(tlg->tl[type]));
        }
    }
    return deadline;
}

int64_t qemu_clock_get_ns(QEMUClockType type)
{
    switch (type) {
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
        return REPLAY_CLOCK(REPLAY_CLOCK_HOST, get_clock_realtime());
    case QEMU_CLOCK_VIRTUAL_RT:
        return REPLAY_CLOCK(REPLAY_CLOCK_VIRTUAL_RT, cpu_get_clock());
    }
}

void init_clocks(QEMUTimerListNotifyCB *notify_cb)
{
    QEMUClockType type;
    for (type = 0; type < QEMU_CLOCK_MAX; type++) {
        qemu_clock_init(type, notify_cb);
    }

#ifdef CONFIG_PRCTL_PR_SET_TIMERSLACK
    prctl(PR_SET_TIMERSLACK, 1, 0, 0, 0);
#endif
}

uint64_t timer_expire_time_ns(QEMUTimer *ts)
{
    return timer_pending(ts) ? ts->expire_time : -1;
}

bool qemu_clock_run_all_timers(void)
{
    bool progress = false;
    QEMUClockType type;

    for (type = 0; type < QEMU_CLOCK_MAX; type++) {
        if (qemu_clock_use_for_deadline(type)) {
            progress |= qemu_clock_run_timers(type);
        }
    }

    return progress;
}
