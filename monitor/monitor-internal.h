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
#include "qapi/qapi-types-control.h"
#include "qapi/qmp/dispatch.h"
#include "qapi/qmp/json-parser.h"
#include "qemu/readline.h"
#include "sysemu/iothread.h"

/*
 * Supported types:
 *
 * 'F'          filename
 * 'B'          block device name
 * 's'          string (accept optional quote)
 * 'S'          it just appends the rest of the string (accept optional quote)
 * 'O'          option string of the form NAME=VALUE,...
 *              parsed according to QemuOptsList given by its name
 *              Example: 'device:O' uses qemu_device_opts.
 *              Restriction: only lists with empty desc are supported
 *              TODO lift the restriction
 * 'i'          32 bit integer
 * 'l'          target long (32 or 64 bit)
 * 'M'          Non-negative target long (32 or 64 bit), in user mode the
 *              value is multiplied by 2^20 (think Mebibyte)
 * 'o'          octets (aka bytes)
 *              user mode accepts an optional E, e, P, p, T, t, G, g, M, m,
 *              K, k suffix, which multiplies the value by 2^60 for suffixes E
 *              and e, 2^50 for suffixes P and p, 2^40 for suffixes T and t,
 *              2^30 for suffixes G and g, 2^20 for M and m, 2^10 for K and k
 * 'T'          double
 *              user mode accepts an optional ms, us, ns suffix,
 *              which divides the value by 1e3, 1e6, 1e9, respectively
 * '/'          optional gdb-like print format (like "/10x")
 *
 * '?'          optional type (for all types, except '/')
 * '.'          other form of optional type (for 'i' and 'l')
 * 'b'          boolean
 *              user mode accepts "on" or "off"
 * '-'          optional parameter (eg. '-f')
 *
 */

typedef struct HMPCommand {
    const char *name;
    const char *args_type;
    const char *params;
    const char *help;
    const char *flags; /* p=preconfig */
    void (*cmd)(Monitor *mon, const QDict *qdict);
    /*
     * @sub_table is a list of 2nd level of commands. If it does not exist,
     * cmd should be used. If it exists, sub_table[?].cmd should be
     * used, and cmd of 1st level plays the role of help function.
     */
    struct HMPCommand *sub_table;
    void (*command_completion)(ReadLineState *rs, int nb_args, const char *str);
} HMPCommand;

struct Monitor {
    CharBackend chr;
    int reset_seen;
    int suspend_cnt;            /* Needs to be accessed atomically */
    bool is_qmp;
    bool skip_flush;
    bool use_io_thread;

    gchar *mon_cpu_path;
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
    QString *outbuf;
    guint out_watch;
    /* Read under either BQL or mon_lock, written with BQL+mon_lock.  */
    int mux_out;
};

struct MonitorHMP {
    Monitor common;
    bool use_readline;
    /*
     * State used only in the thread "owning" the monitor.
     * If @use_io_thread, this is @mon_iothread. (This does not actually happen
     * in the current state of the code.)
     * Else, it's the main thread.
     * These members can be safely accessed without locks.
     */
    ReadLineState *rs;
};

typedef struct {
    Monitor common;
    JSONMessageParser parser;
    bool pretty;
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
} MonitorQMP;

/**
 * Is @mon a QMP monitor?
 */
static inline bool monitor_is_qmp(const Monitor *mon)
{
    return mon->is_qmp;
}

typedef QTAILQ_HEAD(MonitorList, Monitor) MonitorList;
extern IOThread *mon_iothread;
extern QEMUBH *qmp_dispatcher_bh;
extern QmpCommandList qmp_commands, qmp_cap_negotiation_commands;
extern QemuMutex monitor_lock;
extern MonitorList mon_list;
extern int mon_refcount;

extern HMPCommand hmp_cmds[];

int monitor_puts(Monitor *mon, const char *str);
void monitor_data_init(Monitor *mon, bool is_qmp, bool skip_flush,
                       bool use_io_thread);
void monitor_data_destroy(Monitor *mon);
int monitor_can_read(void *opaque);
void monitor_list_append(Monitor *mon);
void monitor_fdsets_cleanup(void);

void qmp_send_response(MonitorQMP *mon, const QDict *rsp);
void monitor_data_destroy_qmp(MonitorQMP *mon);
void monitor_qmp_bh_dispatcher(void *data);

int get_monitor_def(int64_t *pval, const char *name);
void help_cmd(Monitor *mon, const char *name);
void handle_hmp_command(MonitorHMP *mon, const char *cmdline);
int hmp_compare_cmd(const char *name, const char *list);

void qmp_query_qmp_schema(QDict *qdict, QObject **ret_data,
                                 Error **errp);

#endif
