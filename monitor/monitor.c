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
#include "qobject/qdict.h"
#include "qom/object_interfaces.h"
#include "qemu/aio-wait.h"
#include "qemu/error-report.h"
#include "qemu/lockable.h"
#include "qemu/option.h"
#include "system/qtest.h"
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

/*
 * Set to true when the dispatcher coroutine should terminate.  Protected
 * by monitor_lock.
 */
bool qmp_dispatcher_co_shutdown;

/*
 * Protects mon_list, monitor_qapi_event_state, coroutine_mon,
 * monitor_destroyed.
 */
QemuMutex monitor_lock;
static GHashTable *monitor_qapi_event_state;
static GHashTable *coroutine_mon; /* Maps Coroutine* to Monitor* */

MonitorList mon_list;
static bool monitor_destroyed;

int monitor_device_index;

OBJECT_DEFINE_TYPE_EXTENDED(Monitor, monitor, MONITOR, OBJECT, true,
                            { TYPE_USER_CREATABLE }, {});

static void monitor_finalize(Object *obj)
{
    Monitor *mon = MONITOR(obj);

    if (mon->accept_input_bh) {
        qemu_bh_delete(mon->accept_input_bh);
    }
    g_free(mon->chardev_id);
    qemu_chr_fe_deinit(&mon->chr, false);
    g_string_free(mon->outbuf, true);
    qemu_mutex_destroy(&mon->mon_lock);
}

static char *monitor_get_chardev_id(Object *obj, Error **errp)
{
    Monitor *mon = MONITOR(obj);

    return g_strdup(mon->chardev_id);
}

static void monitor_set_chardev_id(Object *obj, const char *str, Error **errp)
{
    Monitor *mon = MONITOR(obj);

    g_free(mon->chardev_id);
    mon->chardev_id = g_strdup(str);
}

static void monitor_complete(UserCreatable *uc, Error **errp);

static void monitor_class_init(ObjectClass *cls, const void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(cls);

    object_class_property_add_str(cls, "chardev",
                                  monitor_get_chardev_id,
                                  monitor_set_chardev_id);

    ucc->complete = monitor_complete;
}

static void monitor_init(Object *obj)
{
    Monitor *mon = MONITOR(obj);

    qemu_mutex_init(&mon->mon_lock);
    mon->outbuf = g_string_new(NULL);
}

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

bool monitor_requires_iothread(const Monitor *mon)
{
    MonitorClass *cls = MONITOR_GET_CLASS(mon);
    return cls->requires_iothread && cls->requires_iothread(mon);
}

static gboolean monitor_unblocked(void *do_not_use, GIOCondition cond,
                                  void *opaque)
{
    Monitor *mon = opaque;

    QEMU_LOCK_GUARD(&mon->mon_lock);
    mon->out_watch = 0;
    monitor_flush_locked(mon);
    return G_SOURCE_REMOVE;
}

/* Cancel a pending out_watch GSource.  Caller must hold mon_lock. */
void monitor_cancel_out_watch(Monitor *mon)
{
    if (mon->out_watch) {
        GMainContext *ctx = NULL;
        GSource *src;

        if (monitor_requires_iothread(mon)) {
            ctx = iothread_get_g_main_context(mon_iothread);
        }
        src = g_main_context_find_source_by_id(ctx, mon->out_watch);
        if (!src && ctx) {
            /* Handler disconnect may have reset gcontext to NULL. */
            src = g_main_context_find_source_by_id(NULL, mon->out_watch);
        }
        if (src) {
            g_source_destroy(src);
        }
        mon->out_watch = 0;
    }
}

/* Caller must hold mon->mon_lock */
void monitor_flush_locked(Monitor *mon)
{
    int rc;
    size_t len;
    const char *buf;

    /*
     * When used by QMP human-monitor-command, no chardev
     * will be connected, as we want to just collect the
     * output in the buffer
     */
    if (!mon->chr.fe_is_open) {
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
    QEMU_LOCK_GUARD(&mon->mon_lock);
    monitor_flush_locked(mon);
}

/* flush at every end of line */
int monitor_puts_locked(Monitor *mon, const char *str)
{
    int i;
    char c;

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

    return i;
}

int monitor_puts(Monitor *mon, const char *str)
{
    QEMU_LOCK_GUARD(&mon->mon_lock);
    return monitor_puts_locked(mon, str);
}

static MonitorQAPIEventConf monitor_qapi_event_conf[QAPI_EVENT__MAX] = {
    /* Limit guest-triggerable events to 1 per second */
    [QAPI_EVENT_RTC_CHANGE]        = { 1000 * SCALE_MS },
    [QAPI_EVENT_BLOCK_IO_ERROR]    = { 1000 * SCALE_MS },
    [QAPI_EVENT_WATCHDOG]          = { 1000 * SCALE_MS },
    [QAPI_EVENT_BALLOON_CHANGE]    = { 1000 * SCALE_MS },
    [QAPI_EVENT_QUORUM_REPORT_BAD] = { 1000 * SCALE_MS },
    [QAPI_EVENT_QUORUM_FAILURE]    = { 1000 * SCALE_MS },
    [QAPI_EVENT_VSERPORT_CHANGE]   = { 1000 * SCALE_MS },
    [QAPI_EVENT_MEMORY_DEVICE_SIZE_CHANGE] = { 1000 * SCALE_MS },
    [QAPI_EVENT_HV_BALLOON_STATUS_REPORT] = { 1000 * SCALE_MS },
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

    trace_monitor_protocol_event_emit(event, qdict);
    QTAILQ_FOREACH(mon, &mon_list, entry) {
        MonitorClass *cls = MONITOR_GET_CLASS(mon);
        if (cls->emit_event) {
            cls->emit_event(mon, event, qdict);
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
    bool throttled;

    assert(event < QAPI_EVENT__MAX);
    evconf = &monitor_qapi_event_conf[event];
    trace_monitor_protocol_event_queue(event, qdict, evconf->rate);
    throttled = evconf->rate;

    /*
     * Rate limit BLOCK_IO_ERROR only for action != "stop".
     *
     * If the VM is stopped after an I/O error, this is important information
     * for the management tool to keep track of the state of QEMU and we can't
     * merge any events. At the same time, stopping the VM means that the guest
     * can't send additional requests and the number of events is already
     * limited, so we can do without rate limiting.
     */
    if (event == QAPI_EVENT_BLOCK_IO_ERROR) {
        QDict *data = qobject_to(QDict, qdict_get(qdict, "data"));
        const char *action = qdict_get_str(data, "action");
        if (!strcmp(action, "stop")) {
            throttled = false;
        }
    }

    QEMU_LOCK_GUARD(&monitor_lock);

    if (!throttled) {
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

    if (evstate->event == QAPI_EVENT_MEMORY_DEVICE_SIZE_CHANGE ||
        evstate->event == QAPI_EVENT_BLOCK_IO_ERROR) {
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

    if (eva->event == QAPI_EVENT_MEMORY_DEVICE_SIZE_CHANGE ||
        eva->event == QAPI_EVENT_BLOCK_IO_ERROR) {
        return !strcmp(qdict_get_str(eva->data, "qom-path"),
                       qdict_get_str(evb->data, "qom-path"));
    }

    return TRUE;
}

void monitor_suspend(Monitor *mon)
{
    qatomic_inc(&mon->suspend_cnt);

    if (monitor_requires_iothread(mon)) {
        /*
         * Kick I/O thread to make sure this takes effect.  It'll be
         * evaluated again in prepare() of the watch object.
         */
        aio_notify(iothread_get_aio_context(mon_iothread));
    }

    trace_monitor_suspend(mon, 1);
}

static void monitor_accept_input(void *opaque)
{
    Monitor *mon = opaque;
    MonitorClass *cls = MONITOR_GET_CLASS(mon);

    cls->accept_input(mon);
}

void monitor_resume(Monitor *mon)
{
    if (qatomic_dec_fetch(&mon->suspend_cnt) == 0) {
        qemu_bh_schedule(mon->accept_input_bh);
    }

    trace_monitor_suspend(mon, -1);
}

int monitor_can_read(void *opaque)
{
    Monitor *mon = opaque;

    return !qatomic_read(&mon->suspend_cnt);
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
        object_unparent(OBJECT(mon));
    }
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
     * polled by calling AIO_WAIT_WHILE_UNLOCKED on it, but we must poll
     * iohandler_ctx manually.
     *
     * Letting the iothread continue while shutting down the dispatcher
     * means that new requests may still be coming in. This is okay,
     * we'll just leave them in the queue without sending a response
     * and object finalization will free them.
     */
    WITH_QEMU_LOCK_GUARD(&monitor_lock) {
        qmp_dispatcher_co_shutdown = true;
    }
    qmp_dispatcher_co_wake();

    AIO_WAIT_WHILE_UNLOCKED(NULL,
                   (aio_poll(iohandler_get_aio_context(), false),
                    qatomic_read(&qmp_dispatcher_co)));

    /*
     * We need to explicitly stop the I/O thread (but not destroy it),
     * clean up the monitor resources, then destroy the I/O thread since
     * we need to unregister from chardev below in object
     * finalization, and chardev is not thread-safe yet
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
        qemu_mutex_lock(&monitor_lock);
        object_unparent(OBJECT(mon));
    }
    qemu_mutex_unlock(&monitor_lock);

    if (mon_iothread) {
        iothread_destroy(mon_iothread);
        mon_iothread = NULL;
    }
}

/*
 * Initialize static vars that have no deps on external
 * module initialization, and are required for external
 * functions to call things like monitor_cur()
 */
static void __attribute__((__constructor__(QEMU_CONSTRUCTOR_EARLY)))
monitor_init_static(void)
{
    qemu_mutex_init(&monitor_lock);
    coroutine_mon = g_hash_table_new(NULL, NULL);
    monitor_qapi_event_state = g_hash_table_new(qapi_event_throttle_hash,
                                                qapi_event_throttle_equal);
}

void monitor_init_globals(void)
{
    /*
     * The dispatcher BH must run in the main loop thread, since we
     * have commands assuming that context.  It would be nice to get
     * rid of those assumptions.
     */
    qmp_dispatcher_co = qemu_coroutine_create(monitor_qmp_dispatcher_co, NULL);
    aio_co_schedule(iohandler_get_aio_context(), qmp_dispatcher_co);
}

char *monitor_compat_id(void)
{
    static int monitor_device_index;

    return g_strdup_printf("compat_monitor%d", monitor_device_index++);
}

static void monitor_complete(UserCreatable *uc, Error **errp)
{
    Monitor *mon = MONITOR(uc);
    AioContext *ctx;

    if (mon->chardev_id) {
        Chardev *chr = qemu_chr_find(mon->chardev_id);
        if (chr == NULL) {
            error_setg(errp, "chardev \"%s\" not found", mon->chardev_id);
            return;
        }

        if (!qemu_chr_fe_init(&mon->chr, chr, errp)) {
            return;
        }
    }

    if (monitor_requires_iothread(mon)) {
        if (!mon_iothread) {
            mon_iothread = iothread_create("mon_iothread", &error_abort);
        }

        ctx = iothread_get_aio_context(mon_iothread);
    } else {
        ctx = qemu_get_aio_context();
    }
    mon->accept_input_bh = aio_bh_new(ctx, monitor_accept_input, mon);
}

int monitor_new(MonitorOptions *opts, bool allow_hmp, Error **errp)
{
    ERRP_GUARD();

    if (!opts->has_mode) {
#ifdef CONFIG_HMP
        opts->mode = allow_hmp ? MONITOR_MODE_READLINE : MONITOR_MODE_CONTROL;
#else
        if (allow_hmp) {
            error_setg(errp, "HMP support is not built in this QEMU");
            return -1;
        }
        opts->mode = MONITOR_MODE_CONTROL;
#endif
    }

    switch (opts->mode) {
    case MONITOR_MODE_CONTROL:
        monitor_new_qmp(opts->id, opts->chardev, opts->pretty, errp);
        break;
#ifdef CONFIG_HMP
    case MONITOR_MODE_READLINE:
        if (!allow_hmp) {
            error_setg(errp, "Only QMP is supported");
            return -1;
        }
        if (opts->pretty) {
            error_setg(errp, "'pretty' is not compatible with HMP monitors");
            return -1;
        }
        monitor_new_hmp(opts->id, opts->chardev, true, errp);
        break;
#endif /* CONFIG_HMP */
    default:
        g_assert_not_reached();
    }

    return *errp ? -1 : 0;
}

int monitor_new_opts(QemuOpts *opts, Error **errp)
{
    Visitor *v;
    MonitorOptions *options;
    int ret;

#ifndef CONFIG_HMP
    const char *mode = qemu_opt_get(opts, "mode");
    /* readline is HMP..  */
    if (mode && g_str_equal(mode, "readline")) {
        error_setg(errp, "HMP monitor is not available,"
                   " use '-qmp' instead of '-monitor'");
        return -1;
    }
#endif

    v = opts_visitor_new(opts);
    visit_type_MonitorOptions(v, NULL, &options, errp);
    visit_free(v);
    if (!options) {
        return -1;
    }

    ret = monitor_new(options, true, errp);
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
