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

#include "chardev/char-io.h"
#include "monitor-internal.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-control.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/qlist.h"
#include "trace.h"

struct QMPRequest {
    /* Owner of the request */
    MonitorQMP *mon;
    /*
     * Request object to be handled or Error to be reported
     * (exactly one of them is non-null)
     */
    QObject *req;
    Error *err;
};
typedef struct QMPRequest QMPRequest;

QmpCommandList qmp_commands, qmp_cap_negotiation_commands;

static bool qmp_oob_enabled(MonitorQMP *mon)
{
    return mon->capab[QMP_CAPABILITY_OOB];
}

static void monitor_qmp_caps_reset(MonitorQMP *mon)
{
    memset(mon->capab_offered, 0, sizeof(mon->capab_offered));
    memset(mon->capab, 0, sizeof(mon->capab));
    mon->capab_offered[QMP_CAPABILITY_OOB] = mon->common.use_io_thread;
}

static void qmp_request_free(QMPRequest *req)
{
    qobject_unref(req->req);
    error_free(req->err);
    g_free(req);
}

/* Caller must hold mon->qmp.qmp_queue_lock */
static void monitor_qmp_cleanup_req_queue_locked(MonitorQMP *mon)
{
    while (!g_queue_is_empty(mon->qmp_requests)) {
        qmp_request_free(g_queue_pop_head(mon->qmp_requests));
    }
}

static void monitor_qmp_cleanup_queue_and_resume(MonitorQMP *mon)
{
    qemu_mutex_lock(&mon->qmp_queue_lock);

    /*
     * Same condition as in monitor_qmp_dispatcher_co(), but before
     * removing an element from the queue (hence no `- 1`).
     * Also, the queue should not be empty either, otherwise the
     * monitor hasn't been suspended yet (or was already resumed).
     */
    bool need_resume = (!qmp_oob_enabled(mon) ||
        mon->qmp_requests->length == QMP_REQ_QUEUE_LEN_MAX)
        && !g_queue_is_empty(mon->qmp_requests);

    monitor_qmp_cleanup_req_queue_locked(mon);

    if (need_resume) {
        /*
         * handle_qmp_command() suspended the monitor because the
         * request queue filled up, to be resumed when the queue has
         * space again.  We just emptied it; resume the monitor.
         *
         * Without this, the monitor would remain suspended forever
         * when we get here while the monitor is suspended.  An
         * unfortunately timed CHR_EVENT_CLOSED can do the trick.
         */
        monitor_resume(&mon->common);
    }

    qemu_mutex_unlock(&mon->qmp_queue_lock);
}

void qmp_send_response(MonitorQMP *mon, const QDict *rsp)
{
    const QObject *data = QOBJECT(rsp);
    GString *json;

    json = qobject_to_json_pretty(data, mon->pretty);
    assert(json != NULL);
    trace_monitor_qmp_respond(mon, json->str);

    g_string_append_c(json, '\n');
    monitor_puts(&mon->common, json->str);

    g_string_free(json, true);
}

/*
 * Emit QMP response @rsp to @mon.
 * Null @rsp can only happen for commands with QCO_NO_SUCCESS_RESP.
 * Nothing is emitted then.
 */
static void monitor_qmp_respond(MonitorQMP *mon, QDict *rsp)
{
    if (rsp) {
        qmp_send_response(mon, rsp);
    }
}

/*
 * Runs outside of coroutine context for OOB commands, but in
 * coroutine context for everything else.
 */
static void monitor_qmp_dispatch(MonitorQMP *mon, QObject *req)
{
    QDict *rsp;
    QDict *error;

    rsp = qmp_dispatch(mon->commands, req, qmp_oob_enabled(mon),
                       &mon->common);

    if (mon->commands == &qmp_cap_negotiation_commands) {
        error = qdict_get_qdict(rsp, "error");
        if (error
            && !g_strcmp0(qdict_get_try_str(error, "class"),
                    QapiErrorClass_str(ERROR_CLASS_COMMAND_NOT_FOUND))) {
            /* Provide a more useful error message */
            qdict_del(error, "desc");
            qdict_put_str(error, "desc", "Expecting capabilities negotiation"
                          " with 'qmp_capabilities'");
        }
    }

    monitor_qmp_respond(mon, rsp);
    qobject_unref(rsp);
}

/*
 * Pop a QMP request from a monitor request queue.
 * Return the request, or NULL all request queues are empty.
 * We are using round-robin fashion to pop the request, to avoid
 * processing commands only on a very busy monitor.  To achieve that,
 * when we process one request on a specific monitor, we put that
 * monitor to the end of mon_list queue.
 *
 * Note: if the function returned with non-NULL, then the caller will
 * be with qmp_mon->qmp_queue_lock held, and the caller is responsible
 * to release it.
 */
static QMPRequest *monitor_qmp_requests_pop_any_with_lock(void)
{
    QMPRequest *req_obj = NULL;
    Monitor *mon;
    MonitorQMP *qmp_mon;

    qemu_mutex_lock(&monitor_lock);

    QTAILQ_FOREACH(mon, &mon_list, entry) {
        if (!monitor_is_qmp(mon)) {
            continue;
        }

        qmp_mon = container_of(mon, MonitorQMP, common);
        qemu_mutex_lock(&qmp_mon->qmp_queue_lock);
        req_obj = g_queue_pop_head(qmp_mon->qmp_requests);
        if (req_obj) {
            /* With the lock of corresponding queue held */
            break;
        }
        qemu_mutex_unlock(&qmp_mon->qmp_queue_lock);
    }

    if (req_obj) {
        /*
         * We found one request on the monitor. Degrade this monitor's
         * priority to lowest by re-inserting it to end of queue.
         */
        QTAILQ_REMOVE(&mon_list, mon, entry);
        QTAILQ_INSERT_TAIL(&mon_list, mon, entry);
    }

    qemu_mutex_unlock(&monitor_lock);

    return req_obj;
}

void coroutine_fn monitor_qmp_dispatcher_co(void *data)
{
    QMPRequest *req_obj = NULL;
    QDict *rsp;
    bool oob_enabled;
    MonitorQMP *mon;

    while (true) {
        assert(qatomic_mb_read(&qmp_dispatcher_co_busy) == true);

        /*
         * Mark the dispatcher as not busy already here so that we
         * don't miss any new requests coming in the middle of our
         * processing.
         */
        qatomic_mb_set(&qmp_dispatcher_co_busy, false);

        /* On shutdown, don't take any more requests from the queue */
        if (qmp_dispatcher_co_shutdown) {
            return;
        }

        while (!(req_obj = monitor_qmp_requests_pop_any_with_lock())) {
            /*
             * No more requests to process.  Wait to be reentered from
             * handle_qmp_command() when it pushes more requests, or
             * from monitor_cleanup() when it requests shutdown.
             */
            if (!qmp_dispatcher_co_shutdown) {
                qemu_coroutine_yield();

                /*
                 * busy must be set to true again by whoever
                 * rescheduled us to avoid double scheduling
                 */
                assert(qatomic_xchg(&qmp_dispatcher_co_busy, false) == true);
            }

            /*
             * qmp_dispatcher_co_shutdown may have changed if we
             * yielded and were reentered from monitor_cleanup()
             */
            if (qmp_dispatcher_co_shutdown) {
                return;
            }
        }

        trace_monitor_qmp_in_band_dequeue(req_obj,
                                          req_obj->mon->qmp_requests->length);

        if (qatomic_xchg(&qmp_dispatcher_co_busy, true) == true) {
            /*
             * Someone rescheduled us (probably because a new requests
             * came in), but we didn't actually yield. Do that now,
             * only to be immediately reentered and removed from the
             * list of scheduled coroutines.
             */
            qemu_coroutine_yield();
        }

        /*
         * Move the coroutine from iohandler_ctx to qemu_aio_context for
         * executing the command handler so that it can make progress if it
         * involves an AIO_WAIT_WHILE().
         */
        aio_co_schedule(qemu_get_aio_context(), qmp_dispatcher_co);
        qemu_coroutine_yield();

        /*
         * @req_obj has a request, we hold req_obj->mon->qmp_queue_lock
         */

        mon = req_obj->mon;

        /*
         * We need to resume the monitor if handle_qmp_command()
         * suspended it.  Two cases:
         * 1. OOB enabled: mon->qmp_requests has no more space
         *    Resume right away, so that OOB commands can get executed while
         *    this request is being processed.
         * 2. OOB disabled: always
         *    Resume only after we're done processing the request,
         * We need to save qmp_oob_enabled() for later, because
         * qmp_qmp_capabilities() can change it.
         */
        oob_enabled = qmp_oob_enabled(mon);
        if (oob_enabled
            && mon->qmp_requests->length == QMP_REQ_QUEUE_LEN_MAX - 1) {
            monitor_resume(&mon->common);
        }

        qemu_mutex_unlock(&mon->qmp_queue_lock);

        /* Process request */
        if (req_obj->req) {
            if (trace_event_get_state(TRACE_MONITOR_QMP_CMD_IN_BAND)) {
                QDict *qdict = qobject_to(QDict, req_obj->req);
                QObject *id = qdict ? qdict_get(qdict, "id") : NULL;
                GString *id_json;

                id_json = id ? qobject_to_json(id) : g_string_new(NULL);
                trace_monitor_qmp_cmd_in_band(id_json->str);
                g_string_free(id_json, true);
            }
            monitor_qmp_dispatch(mon, req_obj->req);
        } else {
            assert(req_obj->err);
            trace_monitor_qmp_err_in_band(error_get_pretty(req_obj->err));
            rsp = qmp_error_response(req_obj->err);
            req_obj->err = NULL;
            monitor_qmp_respond(mon, rsp);
            qobject_unref(rsp);
        }

        if (!oob_enabled) {
            monitor_resume(&mon->common);
        }

        qmp_request_free(req_obj);

        /*
         * Yield and reschedule so the main loop stays responsive.
         *
         * Move back to iohandler_ctx so that nested event loops for
         * qemu_aio_context don't start new monitor commands.
         */
        aio_co_schedule(iohandler_get_aio_context(), qmp_dispatcher_co);
        qemu_coroutine_yield();
    }
}

static void handle_qmp_command(void *opaque, QObject *req, Error *err)
{
    MonitorQMP *mon = opaque;
    QDict *qdict = qobject_to(QDict, req);
    QMPRequest *req_obj;

    assert(!req != !err);

    if (req && trace_event_get_state_backends(TRACE_HANDLE_QMP_COMMAND)) {
        GString *req_json = qobject_to_json(req);
        trace_handle_qmp_command(mon, req_json->str);
        g_string_free(req_json, true);
    }

    if (qdict && qmp_is_oob(qdict)) {
        /* OOB commands are executed immediately */
        if (trace_event_get_state(TRACE_MONITOR_QMP_CMD_OUT_OF_BAND)) {
            QObject *id = qdict_get(qdict, "id");
            GString *id_json;

            id_json = id ? qobject_to_json(id) : g_string_new(NULL);
            trace_monitor_qmp_cmd_out_of_band(id_json->str);
            g_string_free(id_json, true);
        }
        monitor_qmp_dispatch(mon, req);
        qobject_unref(req);
        return;
    }

    req_obj = g_new0(QMPRequest, 1);
    req_obj->mon = mon;
    req_obj->req = req;
    req_obj->err = err;

    /* Protect qmp_requests and fetching its length. */
    qemu_mutex_lock(&mon->qmp_queue_lock);

    /*
     * Suspend the monitor when we can't queue more requests after
     * this one.  Dequeuing in monitor_qmp_dispatcher_co() or
     * monitor_qmp_cleanup_queue_and_resume() will resume it.
     * Note that when OOB is disabled, we queue at most one command,
     * for backward compatibility.
     */
    if (!qmp_oob_enabled(mon) ||
        mon->qmp_requests->length == QMP_REQ_QUEUE_LEN_MAX - 1) {
        monitor_suspend(&mon->common);
    }

    /*
     * Put the request to the end of queue so that requests will be
     * handled in time order.  Ownership for req_obj, req,
     * etc. will be delivered to the handler side.
     */
    trace_monitor_qmp_in_band_enqueue(req_obj, mon,
                                      mon->qmp_requests->length);
    assert(mon->qmp_requests->length < QMP_REQ_QUEUE_LEN_MAX);
    g_queue_push_tail(mon->qmp_requests, req_obj);
    qemu_mutex_unlock(&mon->qmp_queue_lock);

    /* Kick the dispatcher routine */
    if (!qatomic_xchg(&qmp_dispatcher_co_busy, true)) {
        aio_co_wake(qmp_dispatcher_co);
    }
}

static void monitor_qmp_read(void *opaque, const uint8_t *buf, int size)
{
    MonitorQMP *mon = opaque;

    json_message_parser_feed(&mon->parser, (const char *) buf, size);
}

static QDict *qmp_greeting(MonitorQMP *mon)
{
    QList *cap_list = qlist_new();
    QObject *ver = NULL;
    QDict *args;
    QMPCapability cap;

    args = qdict_new();
    qmp_marshal_query_version(args, &ver, NULL);
    qobject_unref(args);

    for (cap = 0; cap < QMP_CAPABILITY__MAX; cap++) {
        if (mon->capab_offered[cap]) {
            qlist_append_str(cap_list, QMPCapability_str(cap));
        }
    }

    return qdict_from_jsonf_nofail(
        "{'QMP': {'version': %p, 'capabilities': %p}}",
        ver, cap_list);
}

static void monitor_qmp_event(void *opaque, QEMUChrEvent event)
{
    QDict *data;
    MonitorQMP *mon = opaque;

    switch (event) {
    case CHR_EVENT_OPENED:
        mon->commands = &qmp_cap_negotiation_commands;
        monitor_qmp_caps_reset(mon);
        data = qmp_greeting(mon);
        qmp_send_response(mon, data);
        qobject_unref(data);
        mon_refcount++;
        break;
    case CHR_EVENT_CLOSED:
        /*
         * Note: this is only useful when the output of the chardev
         * backend is still open.  For example, when the backend is
         * stdio, it's possible that stdout is still open when stdin
         * is closed.
         */
        monitor_qmp_cleanup_queue_and_resume(mon);
        json_message_parser_destroy(&mon->parser);
        json_message_parser_init(&mon->parser, handle_qmp_command,
                                 mon, NULL);
        mon_refcount--;
        monitor_fdsets_cleanup();
        break;
    case CHR_EVENT_BREAK:
    case CHR_EVENT_MUX_IN:
    case CHR_EVENT_MUX_OUT:
        /* Ignore */
        break;
    }
}

void monitor_data_destroy_qmp(MonitorQMP *mon)
{
    json_message_parser_destroy(&mon->parser);
    qemu_mutex_destroy(&mon->qmp_queue_lock);
    monitor_qmp_cleanup_req_queue_locked(mon);
    g_queue_free(mon->qmp_requests);
}

static void monitor_qmp_setup_handlers_bh(void *opaque)
{
    MonitorQMP *mon = opaque;
    GMainContext *context;

    assert(mon->common.use_io_thread);
    context = iothread_get_g_main_context(mon_iothread);
    assert(context);
    qemu_chr_fe_set_handlers(&mon->common.chr, monitor_can_read,
                             monitor_qmp_read, monitor_qmp_event,
                             NULL, &mon->common, context, true);
    monitor_list_append(&mon->common);
}

void monitor_init_qmp(Chardev *chr, bool pretty, Error **errp)
{
    MonitorQMP *mon = g_new0(MonitorQMP, 1);

    if (!qemu_chr_fe_init(&mon->common.chr, chr, errp)) {
        g_free(mon);
        return;
    }
    qemu_chr_fe_set_echo(&mon->common.chr, true);

    /* Note: we run QMP monitor in I/O thread when @chr supports that */
    monitor_data_init(&mon->common, true, false,
                      qemu_chr_has_feature(chr, QEMU_CHAR_FEATURE_GCONTEXT));

    mon->pretty = pretty;

    qemu_mutex_init(&mon->qmp_queue_lock);
    mon->qmp_requests = g_queue_new();

    json_message_parser_init(&mon->parser, handle_qmp_command, mon, NULL);
    if (mon->common.use_io_thread) {
        /*
         * Make sure the old iowatch is gone.  It's possible when
         * e.g. the chardev is in client mode, with wait=on.
         */
        remove_fd_in_watch(chr);
        /*
         * We can't call qemu_chr_fe_set_handlers() directly here
         * since chardev might be running in the monitor I/O
         * thread.  Schedule a bottom half.
         */
        aio_bh_schedule_oneshot(iothread_get_aio_context(mon_iothread),
                                monitor_qmp_setup_handlers_bh, mon);
        /* The bottom half will add @mon to @mon_list */
    } else {
        qemu_chr_fe_set_handlers(&mon->common.chr, monitor_can_read,
                                 monitor_qmp_read, monitor_qmp_event,
                                 NULL, &mon->common, NULL, true);
        monitor_list_append(&mon->common);
    }
}
