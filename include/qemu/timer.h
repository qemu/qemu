#ifndef QEMU_TIMER_H
#define QEMU_TIMER_H

#include "qemu/typedefs.h"
#include "qemu-common.h"
#include "qemu/notify.h"

/* timers */

#define SCALE_MS 1000000
#define SCALE_US 1000
#define SCALE_NS 1

/**
 * QEMUClockType:
 *
 * The following clock types are available:
 *
 * @QEMU_CLOCK_REALTIME: Real time clock
 *
 * The real time clock should be used only for stuff which does not
 * change the virtual machine state, as it is run even if the virtual
 * machine is stopped. The real time clock has a frequency of 1000
 * Hz.
 *
 * Formerly rt_clock
 *
 * @QEMU_CLOCK_VIRTUAL: virtual clock
 *
 * The virtual clock is only run during the emulation. It is stopped
 * when the virtual machine is stopped. Virtual timers use a high
 * precision clock, usually cpu cycles (use ticks_per_sec).
 *
 * Formerly vm_clock
 *
 * @QEMU_CLOCK_HOST: host clock
 *
 * The host clock should be use for device models that emulate accurate
 * real time sources. It will continue to run when the virtual machine
 * is suspended, and it will reflect system time changes the host may
 * undergo (e.g. due to NTP). The host clock has the same precision as
 * the virtual clock.
 *
 * Formerly host_clock
 */

typedef enum {
    QEMU_CLOCK_REALTIME = 0,
    QEMU_CLOCK_VIRTUAL = 1,
    QEMU_CLOCK_HOST = 2,
    QEMU_CLOCK_MAX
} QEMUClockType;

typedef struct QEMUClock QEMUClock;
typedef struct QEMUTimerList QEMUTimerList;

struct QEMUTimerListGroup {
    QEMUTimerList *tl[QEMU_CLOCK_MAX];
};

typedef void QEMUTimerCB(void *opaque);
typedef void QEMUTimerListNotifyCB(void *opaque);

struct QEMUTimer {
    int64_t expire_time;        /* in nanoseconds */
    QEMUTimerList *timer_list;
    QEMUTimerCB *cb;
    void *opaque;
    QEMUTimer *next;
    int scale;
};

extern QEMUTimerListGroup main_loop_tlg;
extern QEMUClock *qemu_clocks[QEMU_CLOCK_MAX];

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
    return qemu_clocks[type];
}

/* These three clocks are maintained here with separate variable
 * names for compatibility only.
 */
#define rt_clock (qemu_clock_ptr(QEMU_CLOCK_REALTIME))
#define vm_clock (qemu_clock_ptr(QEMU_CLOCK_VIRTUAL))
#define host_clock (qemu_clock_ptr(QEMU_CLOCK_HOST))

/**
 * qemu_get_clock_ns:
 * @clock: the clock to operate on
 *
 * Get the nanosecond value of a clock
 *
 * Returns: the clock value in nanoseconds
 */
int64_t qemu_get_clock_ns(QEMUClock *clock);

/**
 * qemu_clock_get_ns;
 * @type: the clock type
 *
 * Get the nanosecond value of a clock with
 * type @type
 *
 * Returns: the clock value in nanoseconds
 */
static inline int64_t qemu_clock_get_ns(QEMUClockType type)
{
    return qemu_get_clock_ns(qemu_clock_ptr(type));
}

/**
 * qemu_clock_has_timers:
 * @clock: the clock to operate on
 *
 * Determines whether a clock's default timer list
 * has timers attached
 *
 * Returns: true if the clock's default timer list
 * has timers attached
 */
bool qemu_clock_has_timers(QEMUClock *clock);

/**
 * qemu_clock_expired:
 * @clock: the clock to operate on
 *
 * Determines whether a clock's default timer list
 * has an expired clock.
 *
 * Returns: true if the clock's default timer list has
 * an expired timer
 */
bool qemu_clock_expired(QEMUClock *clock);
int64_t qemu_clock_deadline(QEMUClock *clock);

/**
 * qemu_clock_deadline_ns:
 * @clock: the clock to operate on
 *
 * Calculate the timeout of the earliest expiring timer
 * on the default timer list associated with the clock
 * in nanoseconds, or -1 if no timer is set to expire.
 *
 * Returns: time until expiry in nanoseconds or -1
 */
int64_t qemu_clock_deadline_ns(QEMUClock *clock);

/**
 * qemu_clock_use_for_deadline:
 * @clock: the clock to operate on
 *
 * Determine whether a clock should be used for deadline
 * calculations. Some clocks, for instance vm_clock with
 * use_icount set, do not count in nanoseconds. Such clocks
 * are not used for deadline calculations, and are presumed
 * to interrupt any poll using qemu_notify/aio_notify
 * etc.
 *
 * Returns: true if the clock runs in nanoseconds and
 * should be used for a deadline.
 */
bool qemu_clock_use_for_deadline(QEMUClock *clock);

/**
 * qemu_clock_use_for_deadline:
 * @clock: the clock to operate on
 *
 * Calculate the deadline across all timer lists associated
 * with a clock (as opposed to just the default one)
 * in nanoseconds, or -1 if no timer is set to expire.
 *
 * Returns: time until expiry in nanoseconds or -1
 */
int64_t qemu_clock_deadline_ns_all(QEMUClock *clock);

/**
 * qemu_clock_get_main_loop_timerlist:
 * @clock: the clock to operate on
 *
 * Return the default timer list assocatiated with a clock.
 *
 * Returns: the default timer list
 */
QEMUTimerList *qemu_clock_get_main_loop_timerlist(QEMUClock *clock);

/**
 * qemu_clock_nofify:
 * @clock: the clock to operate on
 *
 * Call the notifier callback connected with the default timer
 * list linked to the clock, or qemu_notify() if none.
 */
void qemu_clock_notify(QEMUClock *clock);

/**
 * timerlist_new:
 * @type: the clock type to associate with the timerlist
 * @cb: the callback to call on notification
 * @opaque: the opaque pointer to pass to the callback
 *
 * Create a new timerlist associated with the clock of
 * type @type.
 *
 * Returns: a pointer to the QEMUTimerList created
 */
QEMUTimerList *timerlist_new(QEMUClockType type,
                             QEMUTimerListNotifyCB *cb, void *opaque);

/**
 * timerlist_free:
 * @timer_list: the timer list to free
 *
 * Frees a timer_list. It must have no active timers.
 */
void timerlist_free(QEMUTimerList *timer_list);

/**
 * timerlist_has_timers:
 * @timer_list: the timer list to operate on
 *
 * Determine whether a timer list has active timers
 *
 * Returns: true if the timer list has timers.
 */
bool timerlist_has_timers(QEMUTimerList *timer_list);

/**
 * timerlist_expired:
 * @timer_list: the timer list to operate on
 *
 * Determine whether a timer list has any timers which
 * are expired.
 *
 * Returns: true if the timer list has timers which
 * have expired.
 */
bool timerlist_expired(QEMUTimerList *timer_list);

/**
 * timerlist_deadline:
 * @timer_list: the timer list to operate on
 *
 * Determine the deadline for a timer_list. This is
 * a legacy function which returns INT32_MAX if the
 * timer list has no timers or if the earliest timer
 * expires later than INT32_MAX nanoseconds away.
 *
 * Returns: the number of nanoseconds until the earliest
 * timer expires or INT32_MAX in the situations listed
 * above
 */
int64_t timerlist_deadline(QEMUTimerList *timer_list);

/**
 * timerlist_deadline_ns:
 * @timer_list: the timer list to operate on
 *
 * Determine the deadline for a timer_list, i.e.
 * the number of nanoseconds until the first timer
 * expires. Return -1 if there are no timers.
 *
 * Returns: the number of nanoseconds until the earliest
 * timer expires -1 if none
 */
int64_t timerlist_deadline_ns(QEMUTimerList *timer_list);

/**
 * timerlist_getclock:
 * @timer_list: the timer list to operate on
 *
 * Determine the clock associated with a timer list.
 *
 * Returns: the clock associated with the timer list.
 */
QEMUClock *timerlist_get_clock(QEMUTimerList *timer_list);

/**
 * timerlist_run_timers:
 * @timer_list: the timer list to use
 *
 * Call all expired timers associated with the timer list.
 *
 * Returns: true if any timer expired
 */
bool timerlist_run_timers(QEMUTimerList *timer_list);

/**
 * timerlist_notify:
 * @timer_list: the timer list to use
 *
 * call the notifier callback associated with the timer list.
 */
void timerlist_notify(QEMUTimerList *timer_list);

/**
 * timerlistgroup_init:
 * @tlg: the timer list group
 * @cb: the callback to call when a notify is required
 * @opaque: the opaque pointer to be passed to the callback.
 *
 * Initialise a timer list group. This must already be
 * allocated in memory and zeroed. The notifier callback is
 * called whenever a clock in the timer list group is
 * reenabled or whenever a timer associated with any timer
 * list is modified. If @cb is specified as null, qemu_notify()
 * is used instead.
 */
void timerlistgroup_init(QEMUTimerListGroup *tlg,
                         QEMUTimerListNotifyCB *cb, void *opaque);

/**
 * timerlistgroup_deinit:
 * @tlg: the timer list group
 *
 * Deinitialise a timer list group. This must already be
 * initialised. Note the memory is not freed.
 */
void timerlistgroup_deinit(QEMUTimerListGroup *tlg);

/**
 * timerlistgroup_run_timers:
 * @tlg: the timer list group
 *
 * Run the timers associated with a timer list group.
 * This will run timers on multiple clocks.
 *
 * Returns: true if any timer callback ran
 */
bool timerlistgroup_run_timers(QEMUTimerListGroup *tlg);

/**
 * timerlistgroup_deadline_ns:
 * @tlg: the timer list group
 *
 * Determine the deadline of the soonest timer to
 * expire associated with any timer list linked to
 * the timer list group. Only clocks suitable for
 * deadline calculation are included.
 *
 * Returns: the deadline in nanoseconds or -1 if no
 * timers are to expire.
 */
int64_t timerlistgroup_deadline_ns(QEMUTimerListGroup *tlg);

/**
 * qemu_timeout_ns_to_ms:
 * @ns: nanosecond timeout value
 *
 * Convert a nanosecond timeout value (or -1) to
 * a millisecond value (or -1), always rounding up.
 *
 * Returns: millisecond timeout value
 */
int qemu_timeout_ns_to_ms(int64_t ns);

/**
 * qemu_poll_ns:
 * @fds: Array of file descriptors
 * @nfds: number of file descriptors
 * @timeout: timeout in nanoseconds
 *
 * Perform a poll like g_poll but with a timeout in nanoseconds.
 * See g_poll documentation for further details.
 *
 * Returns: number of fds ready
 */
int qemu_poll_ns(GPollFD *fds, guint nfds, int64_t timeout);

/**
 * qemu_clock_enable:
 * @clock: the clock to operate on
 * @enabled: true to enable, false to disable
 *
 * Enable or disable a clock
 */
void qemu_clock_enable(QEMUClock *clock, bool enabled);

/**
 * qemu_clock_warp:
 * @clock: the clock to operate on
 *
 * Warp a clock to a new value
 */
void qemu_clock_warp(QEMUClock *clock);

/**
 * qemu_register_clock_reset_notifier:
 * @clock: the clock to operate on
 * @notifier: the notifier function
 *
 * Register a notifier function to call when the clock
 * concerned is reset.
 */
void qemu_register_clock_reset_notifier(QEMUClock *clock, Notifier *notifier);

/**
 * qemu_unregister_clock_reset_notifier:
 * @clock: the clock to operate on
 * @notifier: the notifier function
 *
 * Unregister a notifier function to call when the clock
 * concerned is reset.
 */
void qemu_unregister_clock_reset_notifier(QEMUClock *clock,
                                          Notifier *notifier);

/**
 * qemu_new_timer:
 * @clock: the clock to operate on
 * @scale: the scale of the clock
 * @cb: the callback function to call when the timer expires
 * @opaque: an opaque pointer to pass to the callback
 *
 * Produce a new timer attached to clock @clock. This is a legacy
 * function. Use timer_new instead.
 *
 * Returns: a pointer to the new timer allocated.
 */
QEMUTimer *qemu_new_timer(QEMUClock *clock, int scale,
                          QEMUTimerCB *cb, void *opaque);

/**
 * timer_init:
 * @ts: the timer to be initialised
 * @timer_list: the timer list to attach the timer to
 * @scale: the scale value for the tiemr
 * @cb: the callback to be called when the timer expires
 * @opaque: the opaque pointer to be passed to the callback
 *
 * Initialise a new timer and associate it with @timer_list.
 * The caller is responsible for allocating the memory.
 *
 * You need not call an explicit deinit call. Simply make
 * sure it is not on a list with timer_del.
 */
void timer_init(QEMUTimer *ts,
                QEMUTimerList *timer_list, int scale,
                QEMUTimerCB *cb, void *opaque);

/**
 * timer_new_tl:
 * @timer_list: the timer list to attach the timer to
 * @scale: the scale value for the tiemr
 * @cb: the callback to be called when the timer expires
 * @opaque: the opaque pointer to be passed to the callback
 *
 * Creeate a new timer and associate it with @timer_list.
 * The memory is allocated by the function.
 *
 * This is not the preferred interface unless you know you
 * are going to call timer_free. Use timer_init instead.
 *
 * Returns: a pointer to the timer
 */
static inline QEMUTimer *timer_new_tl(QEMUTimerList *timer_list,
                                      int scale,
                                      QEMUTimerCB *cb,
                                      void *opaque)
{
    QEMUTimer *ts = g_malloc0(sizeof(QEMUTimer));
    timer_init(ts, timer_list, scale, cb, opaque);
    return ts;
}

/**
 * timer_new:
 * @type: the clock type to use
 * @scale: the scale value for the tiemr
 * @cb: the callback to be called when the timer expires
 * @opaque: the opaque pointer to be passed to the callback
 *
 * Creeate a new timer and associate it with the default
 * timer list for the clock type @type.
 *
 * Returns: a pointer to the timer
 */
static inline QEMUTimer *timer_new(QEMUClockType type, int scale,
                                   QEMUTimerCB *cb, void *opaque)
{
    return timer_new_tl(main_loop_tlg.tl[type], scale, cb, opaque);
}

/**
 * qemu_free_timer:
 * @ts: the timer to operate on
 *
 * free the timer @ts. @ts must not be active.
 *
 * This is a legacy function. Use timer_free instead.
 */
void qemu_free_timer(QEMUTimer *ts);

/**
 * timer_free:
 * @ts: the timer to operate on
 *
 * free the timer @ts. @ts must not be active.
 */
static inline void timer_free(QEMUTimer *ts)
{
    qemu_free_timer(ts);
}

/**
 * qemu_del_timer:
 * @ts: the timer to operate on
 *
 * Delete a timer. This makes it inactive. It does not free
 * memory.
 *
 * This is a legacy function. Use timer_del instead.
 */
void qemu_del_timer(QEMUTimer *ts);

/**
 * timer_del:
 * @ts: the timer to operate on
 *
 * Delete a timer. This makes it inactive. It does not free
 * memory.
 */
static inline void timer_del(QEMUTimer *ts)
{
    qemu_del_timer(ts);
}

/**
 * qemu_mod_timer_ns:
 * @ts: the timer to operate on
 * @expire_time: the expiry time in nanoseconds
 *
 * Modify a timer such that the expiry time is @expire_time
 * as measured in nanoseconds
 *
 * This is a legacy function. Use timer_mod_ns.
 */
void qemu_mod_timer_ns(QEMUTimer *ts, int64_t expire_time);

/**
 * timer_mod_ns:
 * @ts: the timer to operate on
 * @expire_time: the expiry time in nanoseconds
 *
 * Modify a timer such that the expiry time is @expire_time
 * as measured in nanoseconds
 */
static inline void timer_mod_ns(QEMUTimer *ts, int64_t expire_time)
{
    qemu_mod_timer_ns(ts, expire_time);
}

/**
 * qemu_mod_timer:
 * @ts: the timer to operate on
 * @expire_time: the expiry time
 *
 * Modify a timer such that the expiry time is @expire_time
 * as measured in the timer's scale
 *
 * This is a legacy function. Use timer_mod.
 */
void qemu_mod_timer(QEMUTimer *ts, int64_t expire_time);

/**
 * timer_mod:
 * @ts: the timer to operate on
 * @expire_time: the expiry time in nanoseconds
 *
 * Modify a timer such that the expiry time is @expire_time
 * as measured in the timer's scale
 */
static inline void timer_mod(QEMUTimer *ts, int64_t expire_time)
{
    qemu_mod_timer(ts, expire_time);
}

/**
 * timer_pending:
 * @ts: the timer to operate on
 *
 * Determines whether a timer is pending (i.e. is on the
 * active list of timers, whether or not it has not yet expired).
 *
 * Returns: true if the timer is pending
 */
bool timer_pending(QEMUTimer *ts);

/**
 * timer_expired:
 * @ts: the timer to operate on
 *
 * Determines whether a timer has expired.
 *
 * Returns: true if the timer has expired
 */
bool timer_expired(QEMUTimer *timer_head, int64_t current_time);

/**
 * timer_expire_time_ns:
 * @ts: the timer to operate on
 *
 * Determines the time until a timer expires
 *
 * Returns: the time (in nanoseonds) until a timer expires
 */
uint64_t timer_expire_time_ns(QEMUTimer *ts);

/**
 * qemu_run_timers:
 * @clock: clock on which to operate
 *
 * Run all the timers associated with the default timer list
 * of a clock.
 *
 * Returns: true if any timer ran.
 */
bool qemu_run_timers(QEMUClock *clock);

/**
 * qemu_run_all_timers:
 *
 * Run all the timers associated with the default timer list
 * of every clock.
 *
 * Returns: true if any timer ran.
 */
bool qemu_run_all_timers(void);

/**
 * initclocks:
 *
 * Initialise the clock & timer infrastructure
 */
void init_clocks(void);

int64_t cpu_get_ticks(void);
void cpu_enable_ticks(void);
void cpu_disable_ticks(void);

/**
 * qemu_soonest_timeout:
 * @timeout1: first timeout in nanoseconds (or -1 for infinite)
 * @timeout2: second timeout in nanoseconds (or -1 for infinite)
 *
 * Calculates the soonest of two timeout values. -1 means infinite, which
 * is later than any other value.
 *
 * Returns: soonest timeout value in nanoseconds (or -1 for infinite)
 */
static inline int64_t qemu_soonest_timeout(int64_t timeout1, int64_t timeout2)
{
    /* we can abuse the fact that -1 (which means infinite) is a maximal
     * value when cast to unsigned. As this is disgusting, it's kept in
     * one inline function.
     */
    return ((uint64_t) timeout1 < (uint64_t) timeout2) ? timeout1 : timeout2;
}

/**
 * qemu_new_timer_ns:
 * @clock: the clock to associate with the timer
 * @callback: the callback to call when the timer expires
 * @opaque: the opaque pointer to pass to the callback
 *
 * Create a new timer with nanosecond scale on the default timer list
 * associated with the clock.
 *
 * Returns: a pointer to the newly created timer
 */
static inline QEMUTimer *qemu_new_timer_ns(QEMUClock *clock, QEMUTimerCB *cb,
                                           void *opaque)
{
    return qemu_new_timer(clock, SCALE_NS, cb, opaque);
}

/**
 * timer_new_ns:
 * @clock: the clock to associate with the timer
 * @callback: the callback to call when the timer expires
 * @opaque: the opaque pointer to pass to the callback
 *
 * Create a new timer with nanosecond scale on the default timer list
 * associated with the clock.
 *
 * Returns: a pointer to the newly created timer
 */
static inline QEMUTimer *timer_new_ns(QEMUClockType type, QEMUTimerCB *cb,
                                      void *opaque)
{
    return timer_new(type, SCALE_NS, cb, opaque);
}

/**
 * qemu_new_timer_us:
 * @clock: the clock to associate with the timer
 * @callback: the callback to call when the timer expires
 * @opaque: the opaque pointer to pass to the callback
 *
 * Create a new timer with microsecond scale on the default timer list
 * associated with the clock.
 *
 * Returns: a pointer to the newly created timer
 */
static inline QEMUTimer *qemu_new_timer_us(QEMUClock *clock,
                                           QEMUTimerCB *cb,
                                           void *opaque)
{
    return qemu_new_timer(clock, SCALE_US, cb, opaque);
}

/**
 * timer_new_us:
 * @clock: the clock to associate with the timer
 * @callback: the callback to call when the timer expires
 * @opaque: the opaque pointer to pass to the callback
 *
 * Create a new timer with microsecond scale on the default timer list
 * associated with the clock.
 *
 * Returns: a pointer to the newly created timer
 */
static inline QEMUTimer *timer_new_us(QEMUClockType type, QEMUTimerCB *cb,
                                      void *opaque)
{
    return timer_new(type, SCALE_US, cb, opaque);
}

/**
 * qemu_new_timer_ms:
 * @clock: the clock to associate with the timer
 * @callback: the callback to call when the timer expires
 * @opaque: the opaque pointer to pass to the callback
 *
 * Create a new timer with millisecond scale on the default timer list
 * associated with the clock.
 *
 * Returns: a pointer to the newly created timer
 */
static inline QEMUTimer *qemu_new_timer_ms(QEMUClock *clock,
                                           QEMUTimerCB *cb,
                                           void *opaque)
{
    return qemu_new_timer(clock, SCALE_MS, cb, opaque);
}

/**
 * timer_new_ms:
 * @clock: the clock to associate with the timer
 * @callback: the callback to call when the timer expires
 * @opaque: the opaque pointer to pass to the callback
 *
 * Create a new timer with millisecond scale on the default timer list
 * associated with the clock.
 *
 * Returns: a pointer to the newly created timer
 */
static inline QEMUTimer *timer_new_ms(QEMUClockType type, QEMUTimerCB *cb,
                                      void *opaque)
{
    return timer_new(type, SCALE_MS, cb, opaque);
}

static inline int64_t qemu_get_clock_ms(QEMUClock *clock)
{
    return qemu_get_clock_ns(clock) / SCALE_MS;
}

static inline int64_t get_ticks_per_sec(void)
{
    return 1000000000LL;
}

/* real time host monotonic timer */
static inline int64_t get_clock_realtime(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000000LL + (tv.tv_usec * 1000);
}

/* Warning: don't insert tracepoints into these functions, they are
   also used by simpletrace backend and tracepoints would cause
   an infinite recursion! */
#ifdef _WIN32
extern int64_t clock_freq;

static inline int64_t get_clock(void)
{
    LARGE_INTEGER ti;
    QueryPerformanceCounter(&ti);
    return muldiv64(ti.QuadPart, get_ticks_per_sec(), clock_freq);
}

#else

extern int use_rt_clock;

static inline int64_t get_clock(void)
{
#ifdef CLOCK_MONOTONIC
    if (use_rt_clock) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return ts.tv_sec * 1000000000LL + ts.tv_nsec;
    } else
#endif
    {
        /* XXX: using gettimeofday leads to problems if the date
           changes, so it should be avoided. */
        return get_clock_realtime();
    }
}
#endif

void qemu_get_timer(QEMUFile *f, QEMUTimer *ts);
void qemu_put_timer(QEMUFile *f, QEMUTimer *ts);

/* icount */
int64_t cpu_get_icount(void);
int64_t cpu_get_clock(void);

/*******************************************/
/* host CPU ticks (if available) */

#if defined(_ARCH_PPC)

static inline int64_t cpu_get_real_ticks(void)
{
    int64_t retval;
#ifdef _ARCH_PPC64
    /* This reads timebase in one 64bit go and includes Cell workaround from:
       http://ozlabs.org/pipermail/linuxppc-dev/2006-October/027052.html
    */
    __asm__ __volatile__ ("mftb    %0\n\t"
                          "cmpwi   %0,0\n\t"
                          "beq-    $-8"
                          : "=r" (retval));
#else
    /* http://ozlabs.org/pipermail/linuxppc-dev/1999-October/003889.html */
    unsigned long junk;
    __asm__ __volatile__ ("mfspr   %1,269\n\t"  /* mftbu */
                          "mfspr   %L0,268\n\t" /* mftb */
                          "mfspr   %0,269\n\t"  /* mftbu */
                          "cmpw    %0,%1\n\t"
                          "bne     $-16"
                          : "=r" (retval), "=r" (junk));
#endif
    return retval;
}

#elif defined(__i386__)

static inline int64_t cpu_get_real_ticks(void)
{
    int64_t val;
    asm volatile ("rdtsc" : "=A" (val));
    return val;
}

#elif defined(__x86_64__)

static inline int64_t cpu_get_real_ticks(void)
{
    uint32_t low,high;
    int64_t val;
    asm volatile("rdtsc" : "=a" (low), "=d" (high));
    val = high;
    val <<= 32;
    val |= low;
    return val;
}

#elif defined(__hppa__)

static inline int64_t cpu_get_real_ticks(void)
{
    int val;
    asm volatile ("mfctl %%cr16, %0" : "=r"(val));
    return val;
}

#elif defined(__ia64)

static inline int64_t cpu_get_real_ticks(void)
{
    int64_t val;
    asm volatile ("mov %0 = ar.itc" : "=r"(val) :: "memory");
    return val;
}

#elif defined(__s390__)

static inline int64_t cpu_get_real_ticks(void)
{
    int64_t val;
    asm volatile("stck 0(%1)" : "=m" (val) : "a" (&val) : "cc");
    return val;
}

#elif defined(__sparc__)

static inline int64_t cpu_get_real_ticks (void)
{
#if defined(_LP64)
    uint64_t        rval;
    asm volatile("rd %%tick,%0" : "=r"(rval));
    return rval;
#else
    /* We need an %o or %g register for this.  For recent enough gcc
       there is an "h" constraint for that.  Don't bother with that.  */
    union {
        uint64_t i64;
        struct {
            uint32_t high;
            uint32_t low;
        }       i32;
    } rval;
    asm volatile("rd %%tick,%%g1; srlx %%g1,32,%0; mov %%g1,%1"
                 : "=r"(rval.i32.high), "=r"(rval.i32.low) : : "g1");
    return rval.i64;
#endif
}

#elif defined(__mips__) && \
    ((defined(__mips_isa_rev) && __mips_isa_rev >= 2) || defined(__linux__))
/*
 * binutils wants to use rdhwr only on mips32r2
 * but as linux kernel emulate it, it's fine
 * to use it.
 *
 */
#define MIPS_RDHWR(rd, value) {                         \
        __asm__ __volatile__ (".set   push\n\t"         \
                              ".set mips32r2\n\t"       \
                              "rdhwr  %0, "rd"\n\t"     \
                              ".set   pop"              \
                              : "=r" (value));          \
    }

static inline int64_t cpu_get_real_ticks(void)
{
    /* On kernels >= 2.6.25 rdhwr <reg>, $2 and $3 are emulated */
    uint32_t count;
    static uint32_t cyc_per_count = 0;

    if (!cyc_per_count) {
        MIPS_RDHWR("$3", cyc_per_count);
    }

    MIPS_RDHWR("$2", count);
    return (int64_t)(count * cyc_per_count);
}

#elif defined(__alpha__)

static inline int64_t cpu_get_real_ticks(void)
{
    uint64_t cc;
    uint32_t cur, ofs;

    asm volatile("rpcc %0" : "=r"(cc));
    cur = cc;
    ofs = cc >> 32;
    return cur - ofs;
}

#else
/* The host CPU doesn't have an easily accessible cycle counter.
   Just return a monotonically increasing value.  This will be
   totally wrong, but hopefully better than nothing.  */
static inline int64_t cpu_get_real_ticks (void)
{
    static int64_t ticks = 0;
    return ticks++;
}
#endif

#ifdef CONFIG_PROFILER
static inline int64_t profile_getclock(void)
{
    return cpu_get_real_ticks();
}

extern int64_t qemu_time, qemu_time_start;
extern int64_t tlb_flush_time;
extern int64_t dev_time;
#endif

#endif
