/*
 * QEMU monitor
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
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
#include "monitor-internal.h"
#include "qapi/error.h"
#include "qapi/opts-visitor.h"
#include "qapi/qapi-emit-events.h"
#include "qapi/qapi-visit-control.h"
#include "qapi/qmp/qdict.h"
#include "qemu/error-report.h"
#include "qemu/option.h"
#include "sysemu/qtest.h"
#include "trace.h"

/*
 * To prevent flooding clients, events can be throttled. The
 * throttling is calculated globally, rather than per-Monitor
 * instance.
 */
typedef struct MonitorQAPIEventState {
    QAPIEvent event;    /* Throttling state for this event type and... */
    QDict *data;        /* ... data, see qapi_event_throttle_equal() */
    QEMUTimer *timer;   /* Timer for handling delayed events */
    QDict *qdict;       /* Delayed event (if any) */
} MonitorQAPIEventState;

typedef struct {
    int64_t rate;       /* Minimum time (in ns) between two events */
} MonitorQAPIEventConf;

/* Shared monitor I/O thread */
IOThread *mon_iothread;

/* Coroutine to dispatch the requests received from I/O thread */
Coroutine *qmp_dispatcher_co;

/* Set to true when the dispatcher coroutine should terminate */
bool qmp_dispatcher_co_shutdown;

/*
 * qmp_dispatcher_co_busy is used for synchronisation between the
 * monitor thread and the main thread to ensure that the dispatcher
 * coroutine never gets scheduled a second time when it's already
 * scheduled (scheduling the same coroutine twice is forbidden).
 *
 * It is true if the coroutine is active and processing requests.
 * Additional requests may then be pushed onto mon->qmp_requests,
 * and @qmp_dispatcher_co_shutdown may be set without further ado.
 * @qmp_dispatcher_co_busy must not be woken up in this case.
 *
 * If false, you also have to set @qmp_dispatcher_co_busy to true and
 * wake up @qmp_dispatcher_co after pushing the new requests.
 *
 * The coroutine will automatically change this variable back to false
 * before it yields.  Nobody else may set the variable to false.
 *
 * Access must be atomic for thread safety.
 */
bool qmp_dispatcher_co_busy;

/*
 * Protects mon_list, monitor_qapi_event_state, coroutine_mon,
 * monitor_destroyed.
 */
QemuMutex monitor_lock;
static GHashTable *monitor_qapi_event_state;
static GHashTable *coroutine_mon; /* Maps Coroutine* to Monitor* */

MonitorList mon_list;
int mon_refcount;
static bool monitor_destroyed;

Monitor *monitor_cur(void)
{
    Monitor *mon;

    qemu_mutex_lock(&monitor_lock);
    mon = g_hash_table_lookup(coroutine_mon, qemu_coroutine_self());
    qemu_mutex_unlock(&monitor_lock);

    return mon;
}

/**
 * Sets a new current monitor and returns the old one.
 *
 * If a non-NULL monitor is set for a coroutine, another call
 * resetting it to NULL is required before the coroutine terminates,
 * otherwise a stale entry would remain in the hash table.
 */
Monitor *monitor_set_cur(Coroutine *co, Monitor *mon)
{
    Monitor *old_monitor = monitor_cur();

    qemu_mutex_lock(&monitor_lock);
    if (mon) {
        g_hash_table_replace(coroutine_mon, co, mon);
    } else {
        g_hash_table_remove(coroutine_mon, co);
    }
    qemu_mutex_unlock(&monitor_lock);

    return old_monitor;
}

/**
 * Is the current monitor, if any, a QMP monitor?
 */
bool monitor_cur_is_qmp(void)
{
    Monitor *cur_mon = monitor_cur();

    return cur_mon && monitor_is_qmp(cur_mon);
}

/**
 * Is @mon is using readline?
 * Note: not all HMP monitors use readline, e.g., gdbserver has a
 * non-interactive HMP monitor, so readline is not used there.
 */
static inline bool monitor_uses_readline(const MonitorHMP *mon)
{
    return mon->use_readline;
}

static inline bool monitor_is_hmp_non_interactive(const Monitor *mon)
{
    if (monitor_is_qmp(mon)) {
        return false;
    }

    return !monitor_uses_readline(container_of(mon, MonitorHMP, common));
}

static void monitor_flush_locked(Monitor *mon);

static gboolean monitor_unblocked(void *do_not_use, GIOCondition cond,
                                  void *opaque)
{
    Monitor *mon = opaque;

    qemu_mutex_lock(&mon->mon_lock);
    mon->out_watch = 0;
    monitor_flush_locked(mon);
    qemu_mutex_unlock(&mon->mon_lock);
    return FALSE;
}

/* Caller must hold mon->mon_lock */
static void monitor_flush_locked(Monitor *mon)
{
    int rc;
    size_t len;
    const char *buf;

    if (mon->skip_flush) {
        return;
    }

    buf = mon->outbuf->str;
    len = mon->outbuf->len;

    if (len && !mon->mux_out) {
        rc = qemu_chr_fe_write(&mon->chr, (const uint8_t *) buf, len);
        if ((rc < 0 && errno != EAGAIN) || (rc == len)) {
            /* all flushed or error */
            g_string_truncate(mon->outbuf, 0);
            return;
        }
        if (rc > 0) {
            /* partial write */
            g_string_erase(mon->outbuf, 0, rc);
        }
        if (mon->out_watch == 0) {
            mon->out_watch =
                qemu_chr_fe_add_watch(&mon->chr, G_IO_OUT | G_IO_HUP,
                                      monitor_unblocked, mon);
        }
    }
}

void monitor_flush(Monitor *mon)
{
    qemu_mutex_lock(&mon->mon_lock);
    monitor_flush_locked(mon);
    qemu_mutex_unlock(&mon->mon_lock);
}

/* flush at every end of line */
int monitor_puts(Monitor *mon, const char *str)
{
    int i;
    char c;

    qemu_mutex_lock(&mon->mon_lock);
    for (i = 0; str[i]; i++) {
        c = str[i];
        if (c == '\n') {
            g_string_append_c(mon->outbuf, '\r');
        }
        g_string_append_c(mon->outbuf, c);
        if (c == '\n') {
            monitor_flush_locked(mon);
        }
    }
    qemu_mutex_unlock(&mon->mon_lock);

    return i;
}

int monitor_vprintf(Monitor *mon, const char *fmt, va_list ap)
{
    char *buf;
    int n;

    if (!mon) {
        return -1;
    }

    if (monitor_is_qmp(mon)) {
        return -1;
    }

    buf = g_strdup_vprintf(fmt, ap);
    n = monitor_puts(mon, buf);
    g_free(buf);
    return n;
}

int monitor_printf(Monitor *mon, const char *fmt, ...)
{
    int ret;

    va_list ap;
    va_start(ap, fmt);
    ret = monitor_vprintf(mon, fmt, ap);
    va_end(ap);
    return ret;
}

/*
 * Print to current monitor if we have one, else to stderr.
 */
int error_vprintf(const char *fmt, va_list ap)
{
    Monitor *cur_mon = monitor_cur();

    if (cur_mon && !monitor_cur_is_qmp()) {
        return monitor_vprintf(cur_mon, fmt, ap);
    }
    return vfprintf(stderr, fmt, ap);
}

int error_vprintf_unless_qmp(const char *fmt, va_list ap)
{
    Monitor *cur_mon = monitor_cur();

    if (!cur_mon) {
        return vfprintf(stderr, fmt, ap);
    }
    if (!monitor_cur_is_qmp()) {
        return monitor_vprintf(cur_mon, fmt, ap);
    }
    return -1;
}

int error_printf_unless_qmp(const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = error_vprintf_unless_qmp(fmt, ap);
    va_end(ap);
    return ret;
}

static MonitorQAPIEventConf monitor_qapi_event_conf[QAPI_EVENT__MAX] = {
    /* Limit guest-triggerable events to 1 per second */
    [QAPI_EVENT_RTC_CHANGE]        = { 1000 * SCALE_MS },
    [QAPI_EVENT_WATCHDOG]          = { 1000 * SCALE_MS },
    [QAPI_EVENT_BALLOON_CHANGE]    = { 1000 * SCALE_MS },
    [QAPI_EVENT_QUORUM_REPORT_BAD] = { 1000 * SCALE_MS },
    [QAPI_EVENT_QUORUM_FAILURE]    = { 1000 * SCALE_MS },
    [QAPI_EVENT_VSERPORT_CHANGE]   = { 1000 * SCALE_MS },
    [QAPI_EVENT_MEMORY_DEVICE_SIZE_CHANGE] = { 1000 * SCALE_MS },
};

/*
 * Return the clock to use for recording an event's time.
 * It's QEMU_CLOCK_REALTIME, except for qtests it's
 * QEMU_CLOCK_VIRTUAL, to support testing rate limits.
 * Beware: result is invalid before configure_accelerator().
 */
static inline QEMUClockType monitor_get_event_clock(void)
{
    return qtest_enabled() ? QEMU_CLOCK_VIRTUAL : QEMU_CLOCK_REALTIME;
}

/*
 * Broadcast an event to all monitors.
 * @qdict is the event object.  Its member "event" must match @event.
 * Caller must hold monitor_lock.
 */
static void monitor_qapi_event_emit(QAPIEvent event, QDict *qdict)
{
    Monitor *mon;
    MonitorQMP *qmp_mon;

    trace_monitor_protocol_event_emit(event, qdict);
    QTAILQ_FOREACH(mon, &mon_list, entry) {
        if (!monitor_is_qmp(mon)) {
            continue;
        }

        qmp_mon = container_of(mon, MonitorQMP, common);
        if (qmp_mon->commands != &qmp_cap_negotiation_commands) {
            qmp_send_response(qmp_mon, qdict);
        }
    }
}

static void monitor_qapi_event_handler(void *opaque);

/*
 * Queue a new event for emission to Monitor instances,
 * applying any rate limiting if required.
 */
static void
monitor_qapi_event_queue_no_reenter(QAPIEvent event, QDict *qdict)
{
    MonitorQAPIEventConf *evconf;
    MonitorQAPIEventState *evstate;

    assert(event < QAPI_EVENT__MAX);
    evconf = &monitor_qapi_event_conf[event];
    trace_monitor_protocol_event_queue(event, qdict, evconf->rate);

    QEMU_LOCK_GUARD(&monitor_lock);

    if (!evconf->rate) {
        /* Unthrottled event */
        monitor_qapi_event_emit(event, qdict);
    } else {
        QDict *data = qobject_to(QDict, qdict_get(qdict, "data"));
        MonitorQAPIEventState key = { .event = event, .data = data };

        evstate = g_hash_table_lookup(monitor_qapi_event_state, &key);
        assert(!evstate || timer_pending(evstate->timer));

        if (evstate) {
            /*
             * Timer is pending for (at least) evconf->rate ns after
             * last send.  Store event for sending when timer fires,
             * replacing a prior stored event if any.
             */
            qobject_unref(evstate->qdict);
            evstate->qdict = qobject_ref(qdict);
        } else {
            /*
             * Last send was (at least) evconf->rate ns ago.
             * Send immediately, and arm the timer to call
             * monitor_qapi_event_handler() in evconf->rate ns.  Any
             * events arriving before then will be delayed until then.
             */
            int64_t now = qemu_clock_get_ns(monitor_get_event_clock());

            monitor_qapi_event_emit(event, qdict);

            evstate = g_new(MonitorQAPIEventState, 1);
            evstate->event = event;
            evstate->data = qobject_ref(data);
            evstate->qdict = NULL;
            evstate->timer = timer_new_ns(monitor_get_event_clock(),
                                          monitor_qapi_event_handler,
                                          evstate);
            g_hash_table_add(monitor_qapi_event_state, evstate);
            timer_mod_ns(evstate->timer, now + evconf->rate);
        }
    }
}

void qapi_event_emit(QAPIEvent event, QDict *qdict)
{
    /*
     * monitor_qapi_event_queue_no_reenter() is not reentrant: it
     * would deadlock on monitor_lock.  Work around by queueing
     * events in thread-local storage.
     * TODO: remove this, make it re-enter safe.
     */
    typedef struct MonitorQapiEvent {
        QAPIEvent event;
        QDict *qdict;
        QSIMPLEQ_ENTRY(MonitorQapiEvent) entry;
    } MonitorQapiEvent;
    static __thread QSIMPLEQ_HEAD(, MonitorQapiEvent) event_queue;
    static __thread bool reentered;
    MonitorQapiEvent *ev;

    if (!reentered) {
        QSIMPLEQ_INIT(&event_queue);
    }

    ev = g_new(MonitorQapiEvent, 1);
    ev->qdict = qobject_ref(qdict);
    ev->event = event;
    QSIMPLEQ_INSERT_TAIL(&event_queue, ev, entry);
    if (reentered) {
        return;
    }

    reentered = true;

    while ((ev = QSIMPLEQ_FIRST(&event_queue)) != NULL) {
        QSIMPLEQ_REMOVE_HEAD(&event_queue, entry);
        monitor_qapi_event_queue_no_reenter(ev->event, ev->qdict);
        qobject_unref(ev->qdict);
        g_free(ev);
    }

    reentered = false;
}

/*
 * This function runs evconf->rate ns after sending a throttled
 * event.
 * If another event has since been stored, send it.
 */
static void monitor_qapi_event_handler(void *opaque)
{
    MonitorQAPIEventState *evstate = opaque;
    MonitorQAPIEventConf *evconf = &monitor_qapi_event_conf[evstate->event];

    trace_monitor_protocol_event_handler(evstate->event, evstate->qdict);
    QEMU_LOCK_GUARD(&monitor_lock);

    if (evstate->qdict) {
        int64_t now = qemu_clock_get_ns(monitor_get_event_clock());

        monitor_qapi_event_emit(evstate->event, evstate->qdict);
        qobject_unref(evstate->qdict);
        evstate->qdict = NULL;
        timer_mod_ns(evstate->timer, now + evconf->rate);
    } else {
        g_hash_table_remove(monitor_qapi_event_state, evstate);
        qobject_unref(evstate->data);
        timer_free(evstate->timer);
        g_free(evstate);
    }
}

static unsigned int qapi_event_throttle_hash(const void *key)
{
    const MonitorQAPIEventState *evstate = key;
    unsigned int hash = evstate->event * 255;

    if (evstate->event == QAPI_EVENT_VSERPORT_CHANGE) {
        hash += g_str_hash(qdict_get_str(evstate->data, "id"));
    }

    if (evstate->event == QAPI_EVENT_QUORUM_REPORT_BAD) {
        hash += g_str_hash(qdict_get_str(evstate->data, "node-name"));
    }

    if (evstate->event == QAPI_EVENT_MEMORY_DEVICE_SIZE_CHANGE) {
        hash += g_str_hash(qdict_get_str(evstate->data, "qom-path"));
    }

    return hash;
}

static gboolean qapi_event_throttle_equal(const void *a, const void *b)
{
    const MonitorQAPIEventState *eva = a;
    const MonitorQAPIEventState *evb = b;

    if (eva->event != evb->event) {
        return FALSE;
    }

    if (eva->event == QAPI_EVENT_VSERPORT_CHANGE) {
        return !strcmp(qdict_get_str(eva->data, "id"),
                       qdict_get_str(evb->data, "id"));
    }

    if (eva->event == QAPI_EVENT_QUORUM_REPORT_BAD) {
        return !strcmp(qdict_get_str(eva->data, "node-name"),
                       qdict_get_str(evb->data, "node-name"));
    }

    if (eva->event == QAPI_EVENT_MEMORY_DEVICE_SIZE_CHANGE) {
        return !strcmp(qdict_get_str(eva->data, "qom-path"),
                       qdict_get_str(evb->data, "qom-path"));
    }

    return TRUE;
}

int monitor_suspend(Monitor *mon)
{
    if (monitor_is_hmp_non_interactive(mon)) {
        return -ENOTTY;
    }

    qatomic_inc(&mon->suspend_cnt);

    if (mon->use_io_thread) {
        /*
         * Kick I/O thread to make sure this takes effect.  It'll be
         * evaluated again in prepare() of the watch object.
         */
        aio_notify(iothread_get_aio_context(mon_iothread));
    }

    trace_monitor_suspend(mon, 1);
    return 0;
}

static void monitor_accept_input(void *opaque)
{
    Monitor *mon = opaque;

    qemu_chr_fe_accept_input(&mon->chr);
}

void monitor_resume(Monitor *mon)
{
    if (monitor_is_hmp_non_interactive(mon)) {
        return;
    }

    if (qatomic_dec_fetch(&mon->suspend_cnt) == 0) {
        AioContext *ctx;

        if (mon->use_io_thread) {
            ctx = iothread_get_aio_context(mon_iothread);
        } else {
            ctx = qemu_get_aio_context();
        }

        if (!monitor_is_qmp(mon)) {
            MonitorHMP *hmp_mon = container_of(mon, MonitorHMP, common);
            assert(hmp_mon->rs);
            readline_show_prompt(hmp_mon->rs);
        }

        aio_bh_schedule_oneshot(ctx, monitor_accept_input, mon);
    }

    trace_monitor_suspend(mon, -1);
}

int monitor_can_read(void *opaque)
{
    Monitor *mon = opaque;

    return !qatomic_mb_read(&mon->suspend_cnt);
}

void monitor_list_append(Monitor *mon)
{
    qemu_mutex_lock(&monitor_lock);
    /*
     * This prevents inserting new monitors during monitor_cleanup().
     * A cleaner solution would involve the main thread telling other
     * threads to terminate, waiting for their termination.
     */
    if (!monitor_destroyed) {
        QTAILQ_INSERT_HEAD(&mon_list, mon, entry);
        mon = NULL;
    }
    qemu_mutex_unlock(&monitor_lock);

    if (mon) {
        monitor_data_destroy(mon);
        g_free(mon);
    }
}

static void monitor_iothread_init(void)
{
    mon_iothread = iothread_create("mon_iothread", &error_abort);
}

void monitor_data_init(Monitor *mon, bool is_qmp, bool skip_flush,
                       bool use_io_thread)
{
    if (use_io_thread && !mon_iothread) {
        monitor_iothread_init();
    }
    qemu_mutex_init(&mon->mon_lock);
    mon->is_qmp = is_qmp;
    mon->outbuf = g_string_new(NULL);
    mon->skip_flush = skip_flush;
    mon->use_io_thread = use_io_thread;
}

void monitor_data_destroy(Monitor *mon)
{
    g_free(mon->mon_cpu_path);
    qemu_chr_fe_deinit(&mon->chr, false);
    if (monitor_is_qmp(mon)) {
        monitor_data_destroy_qmp(container_of(mon, MonitorQMP, common));
    } else {
        readline_free(container_of(mon, MonitorHMP, common)->rs);
    }
    g_string_free(mon->outbuf, true);
    qemu_mutex_destroy(&mon->mon_lock);
}

void monitor_cleanup(void)
{
    /*
     * The dispatcher needs to stop before destroying the monitor and
     * the I/O thread.
     *
     * We need to poll both qemu_aio_context and iohandler_ctx to make
     * sure that the dispatcher coroutine keeps making progress and
     * eventually terminates.  qemu_aio_context is automatically
     * polled by calling AIO_WAIT_WHILE on it, but we must poll
     * iohandler_ctx manually.
     *
     * Letting the iothread continue while shutting down the dispatcher
     * means that new requests may still be coming in. This is okay,
     * we'll just leave them in the queue without sending a response
     * and monitor_data_destroy() will free them.
     */
    qmp_dispatcher_co_shutdown = true;
    if (!qatomic_xchg(&qmp_dispatcher_co_busy, true)) {
        aio_co_wake(qmp_dispatcher_co);
    }

    AIO_WAIT_WHILE(qemu_get_aio_context(),
                   (aio_poll(iohandler_get_aio_context(), false),
                    qatomic_mb_read(&qmp_dispatcher_co_busy)));

    /*
     * We need to explicitly stop the I/O thread (but not destroy it),
     * clean up the monitor resources, then destroy the I/O thread since
     * we need to unregister from chardev below in
     * monitor_data_destroy(), and chardev is not thread-safe yet
     */
    if (mon_iothread) {
        iothread_stop(mon_iothread);
    }

    /* Flush output buffers and destroy monitors */
    qemu_mutex_lock(&monitor_lock);
    monitor_destroyed = true;
    while (!QTAILQ_EMPTY(&mon_list)) {
        Monitor *mon = QTAILQ_FIRST(&mon_list);
        QTAILQ_REMOVE(&mon_list, mon, entry);
        /* Permit QAPI event emission from character frontend release */
        qemu_mutex_unlock(&monitor_lock);
        monitor_flush(mon);
        monitor_data_destroy(mon);
        qemu_mutex_lock(&monitor_lock);
        g_free(mon);
    }
    qemu_mutex_unlock(&monitor_lock);

    if (mon_iothread) {
        iothread_destroy(mon_iothread);
        mon_iothread = NULL;
    }
}

static void monitor_qapi_event_init(void)
{
    monitor_qapi_event_state = g_hash_table_new(qapi_event_throttle_hash,
                                                qapi_event_throttle_equal);
}

void monitor_init_globals_core(void)
{
    monitor_qapi_event_init();
    qemu_mutex_init(&monitor_lock);
    coroutine_mon = g_hash_table_new(NULL, NULL);

    /*
     * The dispatcher BH must run in the main loop thread, since we
     * have commands assuming that context.  It would be nice to get
     * rid of those assumptions.
     */
    qmp_dispatcher_co = qemu_coroutine_create(monitor_qmp_dispatcher_co, NULL);
    qatomic_mb_set(&qmp_dispatcher_co_busy, true);
    aio_co_schedule(iohandler_get_aio_context(), qmp_dispatcher_co);
}

int monitor_init(MonitorOptions *opts, bool allow_hmp, Error **errp)
{
    ERRP_GUARD();
    Chardev *chr;

    chr = qemu_chr_find(opts->chardev);
    if (chr == NULL) {
        error_setg(errp, "chardev \"%s\" not found", opts->chardev);
        return -1;
    }

    if (!opts->has_mode) {
        opts->mode = allow_hmp ? MONITOR_MODE_READLINE : MONITOR_MODE_CONTROL;
    }

    switch (opts->mode) {
    case MONITOR_MODE_CONTROL:
        monitor_init_qmp(chr, opts->pretty, errp);
        break;
    case MONITOR_MODE_READLINE:
        if (!allow_hmp) {
            error_setg(errp, "Only QMP is supported");
            return -1;
        }
        if (opts->pretty) {
            error_setg(errp, "'pretty' is not compatible with HMP monitors");
            return -1;
        }
        monitor_init_hmp(chr, true, errp);
        break;
    default:
        g_assert_not_reached();
    }

    return *errp ? -1 : 0;
}

int monitor_init_opts(QemuOpts *opts, Error **errp)
{
    Visitor *v;
    MonitorOptions *options;
    int ret;

    v = opts_visitor_new(opts);
    visit_type_MonitorOptions(v, NULL, &options, errp);
    visit_free(v);
    if (!options) {
        return -1;
    }

    ret = monitor_init(options, true, errp);
    qapi_free_MonitorOptions(options);
    return ret;
}

QemuOptsList qemu_mon_opts = {
    .name = "mon",
    .implied_opt_name = "chardev",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_mon_opts.head),
    .desc = {
        {
            .name = "mode",
            .type = QEMU_OPT_STRING,
        },{
            .name = "chardev",
            .type = QEMU_OPT_STRING,
        },{
            .name = "pretty",
            .type = QEMU_OPT_BOOL,
        },
        { /* end of list */ }
    },
};
