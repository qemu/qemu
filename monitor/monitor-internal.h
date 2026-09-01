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

#ifndef MONITOR_INTERNAL_H
#define MONITOR_INTERNAL_H

#include "chardev/char-fe.h"
#include "monitor/monitor.h"
#include "qapi/qapi-emit-events.h"
#include "qapi/qapi-types-control.h"
#include "qapi/qapi-types-qom.h"
#include "qapi/qmp-registry.h"
#include "qobject/json-parser.h"
#include "qemu/readline.h"
#include "system/iothread.h"

struct MonitorClass {
    ObjectClass parent_class;

    /*
     * If non-NULL, the monitor is able to send event
     * notifications back to the client
     */
    void (*emit_event)(Monitor *mon, QAPIEvent event, QDict *qdict);
    /*
     * If non-NULL, perform any actions needed to prepare
     * the monitor to accept further client input
     */
    void (*accept_input)(Monitor *mon);

    /*
     * If non-NULL and returns true, then an I/O thread
     * is required for processing the monitor
     */
    bool (*requires_iothread)(const Monitor *mon);
};

struct Monitor {
    Object parent;
    char *chardev_id;
    CharFrontend chr;
    int suspend_cnt;            /* Needs to be accessed atomically */
    QEMUBH *accept_input_bh;    /* persistent BH for monitor_accept_input */
    QTAILQ_ENTRY(Monitor) entry;

    /*
     * The per-monitor lock. We can't access guest memory when holding
     * the lock.
     */
    QemuMutex mon_lock;

    /*
     * Members that are protected by the per-monitor lock
     */
    QLIST_HEAD(, mon_fd_t) fds;
    GString *outbuf;
    guint out_watch;
    int mux_out;
};

struct MonitorQMPClass {
    MonitorClass parent_class;
};

struct MonitorQMP {
    Monitor parent_obj;
    JSONMessageParser parser;
    bool pretty;
    MonitorQMPCloseAction close_action;
    bool setup_pending; /* iothread BH has not yet set up chardev handlers */
    bool delete_pending; /* close_action has started 'delete' process */
    /*
     * When a client connects, we're in capabilities negotiation mode.
     * @commands is &qmp_cap_negotiation_commands then.  When command
     * qmp_capabilities succeeds, we go into command mode, and
     * @command becomes &qmp_commands.
     */
    const QmpCommandList *commands;
    bool capab_offered[QMP_CAPABILITY__MAX]; /* capabilities offered */
    bool capab[QMP_CAPABILITY__MAX];         /* offered and accepted */
    /*
     * Protects qmp request/response queue.
     * Take monitor_lock first when you need both.
     */
    QemuMutex qmp_queue_lock;
    /* Input queue that holds all the parsed QMP requests */
    GQueue *qmp_requests;
};

typedef QTAILQ_HEAD(MonitorList, Monitor) MonitorList;
extern IOThread *mon_iothread;
extern Coroutine *qmp_dispatcher_co;
extern bool qmp_dispatcher_co_shutdown;
extern QmpCommandList qmp_commands, qmp_cap_negotiation_commands;
extern QemuMutex monitor_lock;
extern MonitorList mon_list;

bool monitor_requires_iothread(const Monitor *mon);
int monitor_can_read(void *opaque);
void monitor_cancel_out_watch(Monitor *mon);
void monitor_list_append(Monitor *mon);
void monitor_fdsets_cleanup(void);

void qmp_send_response(MonitorQMP *mon, const QDict *rsp);
void monitor_data_destroy_qmp(MonitorQMP *mon);
void coroutine_fn monitor_qmp_dispatcher_co(void *data);
void qmp_dispatcher_co_wake(void);

#endif
