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
#include <dirent.h>
#include "hw/hw.h"
#include "monitor/qdev.h"
#include "hw/usb.h"
#include "hw/pcmcia.h"
#include "hw/i386/pc.h"
#include "hw/pci/pci.h"
#include "sysemu/watchdog.h"
#include "hw/loader.h"
#include "exec/gdbstub.h"
#include "net/net.h"
#include "net/slirp.h"
#include "sysemu/char.h"
#include "ui/qemu-spice.h"
#include "sysemu/sysemu.h"
#include "monitor/monitor.h"
#include "qemu/readline.h"
#include "ui/console.h"
#include "ui/input.h"
#include "sysemu/blockdev.h"
#include "audio/audio.h"
#include "disas/disas.h"
#include "sysemu/balloon.h"
#include "qemu/timer.h"
#include "migration/migration.h"
#include "sysemu/kvm.h"
#include "qemu/acl.h"
#include "sysemu/tpm.h"
#include "qapi/qmp/qint.h"
#include "qapi/qmp/qfloat.h"
#include "qapi/qmp/qlist.h"
#include "qapi/qmp/qbool.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/json-streamer.h"
#include "qapi/qmp/json-parser.h"
#include <qom/object_interfaces.h>
#include "qemu/osdep.h"
#include "cpu.h"
#include "trace.h"
#include "trace/control.h"
#ifdef CONFIG_TRACE_SIMPLE
#include "trace/simple.h"
#endif
#include "exec/memory.h"
#include "exec/cpu_ldst.h"
#include "qmp-commands.h"
#include "hmp.h"
#include "qemu/thread.h"
#include "block/qapi.h"
#include "qapi/qmp-event.h"
#include "qapi-event.h"

/* for pic/irq_info */
#if defined(TARGET_SPARC)
#include "hw/sparc/sun4m.h"
#endif
#include "hw/lm32/lm32_pic.h"

//#define DEBUG
//#define DEBUG_COMPLETION

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

typedef struct MonitorCompletionData MonitorCompletionData;
struct MonitorCompletionData {
    Monitor *mon;
    void (*user_print)(Monitor *mon, const QObject *data);
};

typedef struct mon_cmd_t {
    const char *name;
    const char *args_type;
    const char *params;
    const char *help;
    void (*user_print)(Monitor *mon, const QObject *data);
    union {
        void (*cmd)(Monitor *mon, const QDict *qdict);
        int  (*cmd_new)(Monitor *mon, const QDict *params, QObject **ret_data);
        int  (*cmd_async)(Monitor *mon, const QDict *params,
                          MonitorCompletion *cb, void *opaque);
    } mhandler;
    int flags;
    /* @sub_table is a list of 2nd level of commands. If it do not exist,
     * mhandler should be used. If it exist, sub_table[?].mhandler should be
     * used, and mhandler of 1st level plays the role of help function.
     */
    struct mon_cmd_t *sub_table;
    void (*command_completion)(ReadLineState *rs, int nb_args, const char *str);
} mon_cmd_t;

/* file descriptors passed via SCM_RIGHTS */
typedef struct mon_fd_t mon_fd_t;
struct mon_fd_t {
    char *name;
    int fd;
    QLIST_ENTRY(mon_fd_t) next;
};

/* file descriptor associated with a file descriptor set */
typedef struct MonFdsetFd MonFdsetFd;
struct MonFdsetFd {
    int fd;
    bool removed;
    char *opaque;
    QLIST_ENTRY(MonFdsetFd) next;
};

/* file descriptor set containing fds passed via SCM_RIGHTS */
typedef struct MonFdset MonFdset;
struct MonFdset {
    int64_t id;
    QLIST_HEAD(, MonFdsetFd) fds;
    QLIST_HEAD(, MonFdsetFd) dup_fds;
    QLIST_ENTRY(MonFdset) next;
};

typedef struct MonitorControl {
    QObject *id;
    JSONMessageParser parser;
    int command_mode;
} MonitorControl;

/*
 * To prevent flooding clients, events can be throttled. The
 * throttling is calculated globally, rather than per-Monitor
 * instance.
 */
typedef struct MonitorQAPIEventState {
    QAPIEvent event;    /* Event being tracked */
    int64_t rate;       /* Minimum time (in ns) between two events */
    int64_t last;       /* QEMU_CLOCK_REALTIME value at last emission */
    QEMUTimer *timer;   /* Timer for handling delayed events */
    QObject *data;      /* Event pending delayed dispatch */
} MonitorQAPIEventState;

struct Monitor {
    CharDriverState *chr;
    int reset_seen;
    int flags;
    int suspend_cnt;
    bool skip_flush;

    QemuMutex out_lock;
    QString *outbuf;
    guint out_watch;

    /* Read under either BQL or out_lock, written with BQL+out_lock.  */
    int mux_out;

    ReadLineState *rs;
    MonitorControl *mc;
    CPUState *mon_cpu;
    BlockDriverCompletionFunc *password_completion_cb;
    void *password_opaque;
    mon_cmd_t *cmd_table;
    QError *error;
    QLIST_HEAD(,mon_fd_t) fds;
    QLIST_ENTRY(Monitor) entry;
};

/* QMP checker flags */
#define QMP_ACCEPT_UNKNOWNS 1

/* Protects mon_list, monitor_event_state.  */
static QemuMutex monitor_lock;

static QLIST_HEAD(mon_list, Monitor) mon_list;
static QLIST_HEAD(mon_fdsets, MonFdset) mon_fdsets;
static int mon_refcount;

static mon_cmd_t mon_cmds[];
static mon_cmd_t info_cmds[];

static const mon_cmd_t qmp_cmds[];

Monitor *cur_mon;
Monitor *default_mon;

static void monitor_command_cb(void *opaque, const char *cmdline,
                               void *readline_opaque);

static inline int qmp_cmd_mode(const Monitor *mon)
{
    return (mon->mc ? mon->mc->command_mode : 0);
}

/* Return true if in control mode, false otherwise */
static inline int monitor_ctrl_mode(const Monitor *mon)
{
    return (mon->flags & MONITOR_USE_CONTROL);
}

/* Return non-zero iff we have a current monitor, and it is in QMP mode.  */
int monitor_cur_is_qmp(void)
{
    return cur_mon && monitor_ctrl_mode(cur_mon);
}

void monitor_read_command(Monitor *mon, int show_prompt)
{
    if (!mon->rs)
        return;

    readline_start(mon->rs, "(qemu) ", 0, monitor_command_cb, NULL);
    if (show_prompt)
        readline_show_prompt(mon->rs);
}

int monitor_read_password(Monitor *mon, ReadLineFunc *readline_func,
                          void *opaque)
{
    if (monitor_ctrl_mode(mon)) {
        qerror_report(QERR_MISSING_PARAMETER, "password");
        return -EINVAL;
    } else if (mon->rs) {
        readline_start(mon->rs, "Password: ", 1, readline_func, opaque);
        /* prompt is printed on return from the command handler */
        return 0;
    } else {
        monitor_printf(mon, "terminal does not support password prompting\n");
        return -ENOTTY;
    }
}

static void monitor_flush_locked(Monitor *mon);

static gboolean monitor_unblocked(GIOChannel *chan, GIOCondition cond,
                                  void *opaque)
{
    Monitor *mon = opaque;

    qemu_mutex_lock(&mon->out_lock);
    mon->out_watch = 0;
    monitor_flush_locked(mon);
    qemu_mutex_unlock(&mon->out_lock);
    return FALSE;
}

/* Called with mon->out_lock held.  */
static void monitor_flush_locked(Monitor *mon)
{
    int rc;
    size_t len;
    const char *buf;

    if (mon->skip_flush) {
        return;
    }

    buf = qstring_get_str(mon->outbuf);
    len = qstring_get_length(mon->outbuf);

    if (len && !mon->mux_out) {
        rc = qemu_chr_fe_write(mon->chr, (const uint8_t *) buf, len);
        if ((rc < 0 && errno != EAGAIN) || (rc == len)) {
            /* all flushed or error */
            QDECREF(mon->outbuf);
            mon->outbuf = qstring_new();
            return;
        }
        if (rc > 0) {
            /* partinal write */
            QString *tmp = qstring_from_str(buf + rc);
            QDECREF(mon->outbuf);
            mon->outbuf = tmp;
        }
        if (mon->out_watch == 0) {
            mon->out_watch = qemu_chr_fe_add_watch(mon->chr, G_IO_OUT,
                                                   monitor_unblocked, mon);
        }
    }
}

void monitor_flush(Monitor *mon)
{
    qemu_mutex_lock(&mon->out_lock);
    monitor_flush_locked(mon);
    qemu_mutex_unlock(&mon->out_lock);
}

/* flush at every end of line */
static void monitor_puts(Monitor *mon, const char *str)
{
    char c;

    qemu_mutex_lock(&mon->out_lock);
    for(;;) {
        c = *str++;
        if (c == '\0')
            break;
        if (c == '\n') {
            qstring_append_chr(mon->outbuf, '\r');
        }
        qstring_append_chr(mon->outbuf, c);
        if (c == '\n') {
            monitor_flush_locked(mon);
        }
    }
    qemu_mutex_unlock(&mon->out_lock);
}

void monitor_vprintf(Monitor *mon, const char *fmt, va_list ap)
{
    char *buf;

    if (!mon)
        return;

    if (monitor_ctrl_mode(mon)) {
        return;
    }

    buf = g_strdup_vprintf(fmt, ap);
    monitor_puts(mon, buf);
    g_free(buf);
}

void monitor_printf(Monitor *mon, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    monitor_vprintf(mon, fmt, ap);
    va_end(ap);
}

static int GCC_FMT_ATTR(2, 3) monitor_fprintf(FILE *stream,
                                              const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    monitor_vprintf((Monitor *)stream, fmt, ap);
    va_end(ap);
    return 0;
}

static void monitor_user_noop(Monitor *mon, const QObject *data) { }

static inline int handler_is_qobject(const mon_cmd_t *cmd)
{
    return cmd->user_print != NULL;
}

static inline bool handler_is_async(const mon_cmd_t *cmd)
{
    return cmd->flags & MONITOR_CMD_ASYNC;
}

static inline int monitor_has_error(const Monitor *mon)
{
    return mon->error != NULL;
}

static void monitor_json_emitter(Monitor *mon, const QObject *data)
{
    QString *json;

    json = mon->flags & MONITOR_USE_PRETTY ? qobject_to_json_pretty(data) :
                                             qobject_to_json(data);
    assert(json != NULL);

    qstring_append_chr(json, '\n');
    monitor_puts(mon, qstring_get_str(json));

    QDECREF(json);
}

static QDict *build_qmp_error_dict(const QError *err)
{
    QObject *obj;

    obj = qobject_from_jsonf("{ 'error': { 'class': %s, 'desc': %p } }",
                             ErrorClass_lookup[err->err_class],
                             qerror_human(err));

    return qobject_to_qdict(obj);
}

static void monitor_protocol_emitter(Monitor *mon, QObject *data)
{
    QDict *qmp;

    trace_monitor_protocol_emitter(mon);

    if (!monitor_has_error(mon)) {
        /* success response */
        qmp = qdict_new();
        if (data) {
            qobject_incref(data);
            qdict_put_obj(qmp, "return", data);
        } else {
            /* return an empty QDict by default */
            qdict_put(qmp, "return", qdict_new());
        }
    } else {
        /* error response */
        qmp = build_qmp_error_dict(mon->error);
        QDECREF(mon->error);
        mon->error = NULL;
    }

    if (mon->mc->id) {
        qdict_put_obj(qmp, "id", mon->mc->id);
        mon->mc->id = NULL;
    }

    monitor_json_emitter(mon, QOBJECT(qmp));
    QDECREF(qmp);
}


static MonitorQAPIEventState monitor_qapi_event_state[QAPI_EVENT_MAX];

/*
 * Emits the event to every monitor instance, @event is only used for trace
 * Called with monitor_lock held.
 */
static void monitor_qapi_event_emit(QAPIEvent event, QObject *data)
{
    Monitor *mon;

    trace_monitor_protocol_event_emit(event, data);
    QLIST_FOREACH(mon, &mon_list, entry) {
        if (monitor_ctrl_mode(mon) && qmp_cmd_mode(mon)) {
            monitor_json_emitter(mon, data);
        }
    }
}

/*
 * Queue a new event for emission to Monitor instances,
 * applying any rate limiting if required.
 */
static void
monitor_qapi_event_queue(QAPIEvent event, QDict *data, Error **errp)
{
    MonitorQAPIEventState *evstate;
    assert(event < QAPI_EVENT_MAX);
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

    evstate = &(monitor_qapi_event_state[event]);
    trace_monitor_protocol_event_queue(event,
                                       data,
                                       evstate->rate,
                                       evstate->last,
                                       now);

    /* Rate limit of 0 indicates no throttling */
    qemu_mutex_lock(&monitor_lock);
    if (!evstate->rate) {
        monitor_qapi_event_emit(event, QOBJECT(data));
        evstate->last = now;
    } else {
        int64_t delta = now - evstate->last;
        if (evstate->data ||
            delta < evstate->rate) {
            /* If there's an existing event pending, replace
             * it with the new event, otherwise schedule a
             * timer for delayed emission
             */
            if (evstate->data) {
                qobject_decref(evstate->data);
            } else {
                int64_t then = evstate->last + evstate->rate;
                timer_mod_ns(evstate->timer, then);
            }
            evstate->data = QOBJECT(data);
            qobject_incref(evstate->data);
        } else {
            monitor_qapi_event_emit(event, QOBJECT(data));
            evstate->last = now;
        }
    }
    qemu_mutex_unlock(&monitor_lock);
}

/*
 * The callback invoked by QemuTimer when a delayed
 * event is ready to be emitted
 */
static void monitor_qapi_event_handler(void *opaque)
{
    MonitorQAPIEventState *evstate = opaque;
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

    trace_monitor_protocol_event_handler(evstate->event,
                                         evstate->data,
                                         evstate->last,
                                         now);
    qemu_mutex_lock(&monitor_lock);
    if (evstate->data) {
        monitor_qapi_event_emit(evstate->event, evstate->data);
        qobject_decref(evstate->data);
        evstate->data = NULL;
    }
    evstate->last = now;
    qemu_mutex_unlock(&monitor_lock);
}

/*
 * @event: the event ID to be limited
 * @rate: the rate limit in milliseconds
 *
 * Sets a rate limit on a particular event, so no
 * more than 1 event will be emitted within @rate
 * milliseconds
 */
static void
monitor_qapi_event_throttle(QAPIEvent event, int64_t rate)
{
    MonitorQAPIEventState *evstate;
    assert(event < QAPI_EVENT_MAX);

    evstate = &(monitor_qapi_event_state[event]);

    trace_monitor_protocol_event_throttle(event, rate);
    evstate->event = event;
    assert(rate * SCALE_MS <= INT64_MAX);
    evstate->rate = rate * SCALE_MS;
    evstate->last = 0;
    evstate->data = NULL;
    evstate->timer = timer_new(QEMU_CLOCK_REALTIME,
                               SCALE_MS,
                               monitor_qapi_event_handler,
                               evstate);
}

static void monitor_qapi_event_init(void)
{
    /* Limit guest-triggerable events to 1 per second */
    monitor_qapi_event_throttle(QAPI_EVENT_RTC_CHANGE, 1000);
    monitor_qapi_event_throttle(QAPI_EVENT_WATCHDOG, 1000);
    monitor_qapi_event_throttle(QAPI_EVENT_BALLOON_CHANGE, 1000);
    monitor_qapi_event_throttle(QAPI_EVENT_QUORUM_REPORT_BAD, 1000);
    monitor_qapi_event_throttle(QAPI_EVENT_QUORUM_FAILURE, 1000);
    monitor_qapi_event_throttle(QAPI_EVENT_VSERPORT_CHANGE, 1000);

    qmp_event_set_func_emit(monitor_qapi_event_queue);
}

static int do_qmp_capabilities(Monitor *mon, const QDict *params,
                               QObject **ret_data)
{
    /* Will setup QMP capabilities in the future */
    if (monitor_ctrl_mode(mon)) {
        mon->mc->command_mode = 1;
    }

    return 0;
}

static void handle_user_command(Monitor *mon, const char *cmdline);

static void monitor_data_init(Monitor *mon)
{
    memset(mon, 0, sizeof(Monitor));
    qemu_mutex_init(&mon->out_lock);
    mon->outbuf = qstring_new();
    /* Use *mon_cmds by default. */
    mon->cmd_table = mon_cmds;
}

static void monitor_data_destroy(Monitor *mon)
{
    QDECREF(mon->outbuf);
    qemu_mutex_destroy(&mon->out_lock);
}

char *qmp_human_monitor_command(const char *command_line, bool has_cpu_index,
                                int64_t cpu_index, Error **errp)
{
    char *output = NULL;
    Monitor *old_mon, hmp;

    monitor_data_init(&hmp);
    hmp.skip_flush = true;

    old_mon = cur_mon;
    cur_mon = &hmp;

    if (has_cpu_index) {
        int ret = monitor_set_cpu(cpu_index);
        if (ret < 0) {
            cur_mon = old_mon;
            error_set(errp, QERR_INVALID_PARAMETER_VALUE, "cpu-index",
                      "a CPU number");
            goto out;
        }
    }

    handle_user_command(&hmp, command_line);
    cur_mon = old_mon;

    qemu_mutex_lock(&hmp.out_lock);
    if (qstring_get_length(hmp.outbuf) > 0) {
        output = g_strdup(qstring_get_str(hmp.outbuf));
    } else {
        output = g_strdup("");
    }
    qemu_mutex_unlock(&hmp.out_lock);

out:
    monitor_data_destroy(&hmp);
    return output;
}

static int compare_cmd(const char *name, const char *list)
{
    const char *p, *pstart;
    int len;
    len = strlen(name);
    p = list;
    for(;;) {
        pstart = p;
        p = strchr(p, '|');
        if (!p)
            p = pstart + strlen(pstart);
        if ((p - pstart) == len && !memcmp(pstart, name, len))
            return 1;
        if (*p == '\0')
            break;
        p++;
    }
    return 0;
}

static int get_str(char *buf, int buf_size, const char **pp)
{
    const char *p;
    char *q;
    int c;

    q = buf;
    p = *pp;
    while (qemu_isspace(*p)) {
        p++;
    }
    if (*p == '\0') {
    fail:
        *q = '\0';
        *pp = p;
        return -1;
    }
    if (*p == '\"') {
        p++;
        while (*p != '\0' && *p != '\"') {
            if (*p == '\\') {
                p++;
                c = *p++;
                switch (c) {
                case 'n':
                    c = '\n';
                    break;
                case 'r':
                    c = '\r';
                    break;
                case '\\':
                case '\'':
                case '\"':
                    break;
                default:
                    qemu_printf("unsupported escape code: '\\%c'\n", c);
                    goto fail;
                }
                if ((q - buf) < buf_size - 1) {
                    *q++ = c;
                }
            } else {
                if ((q - buf) < buf_size - 1) {
                    *q++ = *p;
                }
                p++;
            }
        }
        if (*p != '\"') {
            qemu_printf("unterminated string\n");
            goto fail;
        }
        p++;
    } else {
        while (*p != '\0' && !qemu_isspace(*p)) {
            if ((q - buf) < buf_size - 1) {
                *q++ = *p;
            }
            p++;
        }
    }
    *q = '\0';
    *pp = p;
    return 0;
}

#define MAX_ARGS 16

static void free_cmdline_args(char **args, int nb_args)
{
    int i;

    assert(nb_args <= MAX_ARGS);

    for (i = 0; i < nb_args; i++) {
        g_free(args[i]);
    }

}

/*
 * Parse the command line to get valid args.
 * @cmdline: command line to be parsed.
 * @pnb_args: location to store the number of args, must NOT be NULL.
 * @args: location to store the args, which should be freed by caller, must
 *        NOT be NULL.
 *
 * Returns 0 on success, negative on failure.
 *
 * NOTE: this parser is an approximate form of the real command parser. Number
 *       of args have a limit of MAX_ARGS. If cmdline contains more, it will
 *       return with failure.
 */
static int parse_cmdline(const char *cmdline,
                         int *pnb_args, char **args)
{
    const char *p;
    int nb_args, ret;
    char buf[1024];

    p = cmdline;
    nb_args = 0;
    for (;;) {
        while (qemu_isspace(*p)) {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        if (nb_args >= MAX_ARGS) {
            goto fail;
        }
        ret = get_str(buf, sizeof(buf), &p);
        if (ret < 0) {
            goto fail;
        }
        args[nb_args] = g_strdup(buf);
        nb_args++;
    }
    *pnb_args = nb_args;
    return 0;

 fail:
    free_cmdline_args(args, nb_args);
    return -1;
}

static void help_cmd_dump_one(Monitor *mon,
                              const mon_cmd_t *cmd,
                              char **prefix_args,
                              int prefix_args_nb)
{
    int i;

    for (i = 0; i < prefix_args_nb; i++) {
        monitor_printf(mon, "%s ", prefix_args[i]);
    }
    monitor_printf(mon, "%s %s -- %s\n", cmd->name, cmd->params, cmd->help);
}

/* @args[@arg_index] is the valid command need to find in @cmds */
static void help_cmd_dump(Monitor *mon, const mon_cmd_t *cmds,
                          char **args, int nb_args, int arg_index)
{
    const mon_cmd_t *cmd;

    /* No valid arg need to compare with, dump all in *cmds */
    if (arg_index >= nb_args) {
        for (cmd = cmds; cmd->name != NULL; cmd++) {
            help_cmd_dump_one(mon, cmd, args, arg_index);
        }
        return;
    }

    /* Find one entry to dump */
    for (cmd = cmds; cmd->name != NULL; cmd++) {
        if (compare_cmd(args[arg_index], cmd->name)) {
            if (cmd->sub_table) {
                /* continue with next arg */
                help_cmd_dump(mon, cmd->sub_table,
                              args, nb_args, arg_index + 1);
            } else {
                help_cmd_dump_one(mon, cmd, args, arg_index);
            }
            break;
        }
    }
}

static void help_cmd(Monitor *mon, const char *name)
{
    char *args[MAX_ARGS];
    int nb_args = 0;

    /* 1. parse user input */
    if (name) {
        /* special case for log, directly dump and return */
        if (!strcmp(name, "log")) {
            const QEMULogItem *item;
            monitor_printf(mon, "Log items (comma separated):\n");
            monitor_printf(mon, "%-10s %s\n", "none", "remove all logs");
            for (item = qemu_log_items; item->mask != 0; item++) {
                monitor_printf(mon, "%-10s %s\n", item->name, item->help);
            }
            return;
        }

        if (parse_cmdline(name, &nb_args, args) < 0) {
            return;
        }
    }

    /* 2. dump the contents according to parsed args */
    help_cmd_dump(mon, mon->cmd_table, args, nb_args, 0);

    free_cmdline_args(args, nb_args);
}

static void do_help_cmd(Monitor *mon, const QDict *qdict)
{
    help_cmd(mon, qdict_get_try_str(qdict, "name"));
}

static void do_trace_event_set_state(Monitor *mon, const QDict *qdict)
{
    const char *tp_name = qdict_get_str(qdict, "name");
    bool new_state = qdict_get_bool(qdict, "option");

    bool found = false;
    TraceEvent *ev = NULL;
    while ((ev = trace_event_pattern(tp_name, ev)) != NULL) {
        found = true;
        if (!trace_event_get_state_static(ev)) {
            monitor_printf(mon, "event \"%s\" is not traceable\n", tp_name);
        } else {
            trace_event_set_state_dynamic(ev, new_state);
        }
    }
    if (!trace_event_is_pattern(tp_name) && !found) {
        monitor_printf(mon, "unknown event name \"%s\"\n", tp_name);
    }
}

#ifdef CONFIG_TRACE_SIMPLE
static void do_trace_file(Monitor *mon, const QDict *qdict)
{
    const char *op = qdict_get_try_str(qdict, "op");
    const char *arg = qdict_get_try_str(qdict, "arg");

    if (!op) {
        st_print_trace_file_status((FILE *)mon, &monitor_fprintf);
    } else if (!strcmp(op, "on")) {
        st_set_trace_file_enabled(true);
    } else if (!strcmp(op, "off")) {
        st_set_trace_file_enabled(false);
    } else if (!strcmp(op, "flush")) {
        st_flush_trace_buffer();
    } else if (!strcmp(op, "set")) {
        if (arg) {
            st_set_trace_file(arg);
        }
    } else {
        monitor_printf(mon, "unexpected argument \"%s\"\n", op);
        help_cmd(mon, "trace-file");
    }
}
#endif

static void user_monitor_complete(void *opaque, QObject *ret_data)
{
    MonitorCompletionData *data = (MonitorCompletionData *)opaque; 

    if (ret_data) {
        data->user_print(data->mon, ret_data);
    }
    monitor_resume(data->mon);
    g_free(data);
}

static void qmp_monitor_complete(void *opaque, QObject *ret_data)
{
    monitor_protocol_emitter(opaque, ret_data);
}

static int qmp_async_cmd_handler(Monitor *mon, const mon_cmd_t *cmd,
                                 const QDict *params)
{
    return cmd->mhandler.cmd_async(mon, params, qmp_monitor_complete, mon);
}

static void user_async_cmd_handler(Monitor *mon, const mon_cmd_t *cmd,
                                   const QDict *params)
{
    int ret;

    MonitorCompletionData *cb_data = g_malloc(sizeof(*cb_data));
    cb_data->mon = mon;
    cb_data->user_print = cmd->user_print;
    monitor_suspend(mon);
    ret = cmd->mhandler.cmd_async(mon, params,
                                  user_monitor_complete, cb_data);
    if (ret < 0) {
        monitor_resume(mon);
        g_free(cb_data);
    }
}

static void do_info_help(Monitor *mon, const QDict *qdict)
{
    help_cmd(mon, "info");
}

CommandInfoList *qmp_query_commands(Error **errp)
{
    CommandInfoList *info, *cmd_list = NULL;
    const mon_cmd_t *cmd;

    for (cmd = qmp_cmds; cmd->name != NULL; cmd++) {
        info = g_malloc0(sizeof(*info));
        info->value = g_malloc0(sizeof(*info->value));
        info->value->name = g_strdup(cmd->name);

        info->next = cmd_list;
        cmd_list = info;
    }

    return cmd_list;
}

EventInfoList *qmp_query_events(Error **errp)
{
    EventInfoList *info, *ev_list = NULL;
    QAPIEvent e;

    for (e = 0 ; e < QAPI_EVENT_MAX ; e++) {
        const char *event_name = QAPIEvent_lookup[e];
        assert(event_name != NULL);
        info = g_malloc0(sizeof(*info));
        info->value = g_malloc0(sizeof(*info->value));
        info->value->name = g_strdup(event_name);

        info->next = ev_list;
        ev_list = info;
    }

    return ev_list;
}

/* set the current CPU defined by the user */
int monitor_set_cpu(int cpu_index)
{
    CPUState *cpu;

    cpu = qemu_get_cpu(cpu_index);
    if (cpu == NULL) {
        return -1;
    }
    cur_mon->mon_cpu = cpu;
    return 0;
}

static CPUArchState *mon_get_cpu(void)
{
    if (!cur_mon->mon_cpu) {
        monitor_set_cpu(0);
    }
    cpu_synchronize_state(cur_mon->mon_cpu);
    return cur_mon->mon_cpu->env_ptr;
}

int monitor_get_cpu_index(void)
{
    CPUState *cpu = ENV_GET_CPU(mon_get_cpu());
    return cpu->cpu_index;
}

static void do_info_registers(Monitor *mon, const QDict *qdict)
{
    CPUState *cpu;
    CPUArchState *env;
    env = mon_get_cpu();
    cpu = ENV_GET_CPU(env);
    cpu_dump_state(cpu, (FILE *)mon, monitor_fprintf, CPU_DUMP_FPU);
}

static void do_info_jit(Monitor *mon, const QDict *qdict)
{
    dump_exec_info((FILE *)mon, monitor_fprintf);
}

static void do_info_history(Monitor *mon, const QDict *qdict)
{
    int i;
    const char *str;

    if (!mon->rs)
        return;
    i = 0;
    for(;;) {
        str = readline_get_history(mon->rs, i);
        if (!str)
            break;
        monitor_printf(mon, "%d: '%s'\n", i, str);
        i++;
    }
}

static void do_info_cpu_stats(Monitor *mon, const QDict *qdict)
{
    CPUState *cpu;
    CPUArchState *env;

    env = mon_get_cpu();
    cpu = ENV_GET_CPU(env);
    cpu_dump_statistics(cpu, (FILE *)mon, &monitor_fprintf, 0);
}

static void do_trace_print_events(Monitor *mon, const QDict *qdict)
{
    trace_print_events((FILE *)mon, &monitor_fprintf);
}

static int client_migrate_info(Monitor *mon, const QDict *qdict,
                               MonitorCompletion cb, void *opaque)
{
    const char *protocol = qdict_get_str(qdict, "protocol");
    const char *hostname = qdict_get_str(qdict, "hostname");
    const char *subject  = qdict_get_try_str(qdict, "cert-subject");
    int port             = qdict_get_try_int(qdict, "port", -1);
    int tls_port         = qdict_get_try_int(qdict, "tls-port", -1);
    int ret;

    if (strcmp(protocol, "spice") == 0) {
        if (!using_spice) {
            qerror_report(QERR_DEVICE_NOT_ACTIVE, "spice");
            return -1;
        }

        if (port == -1 && tls_port == -1) {
            qerror_report(QERR_MISSING_PARAMETER, "port/tls-port");
            return -1;
        }

        ret = qemu_spice_migrate_info(hostname, port, tls_port, subject,
                                      cb, opaque);
        if (ret != 0) {
            qerror_report(QERR_UNDEFINED_ERROR);
            return -1;
        }
        return 0;
    }

    qerror_report(QERR_INVALID_PARAMETER, "protocol");
    return -1;
}

static void do_logfile(Monitor *mon, const QDict *qdict)
{
    qemu_set_log_filename(qdict_get_str(qdict, "filename"));
}

static void do_log(Monitor *mon, const QDict *qdict)
{
    int mask;
    const char *items = qdict_get_str(qdict, "items");

    if (!strcmp(items, "none")) {
        mask = 0;
    } else {
        mask = qemu_str_to_log_mask(items);
        if (!mask) {
            help_cmd(mon, "log");
            return;
        }
    }
    qemu_set_log(mask);
}

static void do_singlestep(Monitor *mon, const QDict *qdict)
{
    const char *option = qdict_get_try_str(qdict, "option");
    if (!option || !strcmp(option, "on")) {
        singlestep = 1;
    } else if (!strcmp(option, "off")) {
        singlestep = 0;
    } else {
        monitor_printf(mon, "unexpected option %s\n", option);
    }
}

static void do_gdbserver(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_try_str(qdict, "device");
    if (!device)
        device = "tcp::" DEFAULT_GDBSTUB_PORT;
    if (gdbserver_start(device) < 0) {
        monitor_printf(mon, "Could not open gdbserver on device '%s'\n",
                       device);
    } else if (strcmp(device, "none") == 0) {
        monitor_printf(mon, "Disabled gdbserver\n");
    } else {
        monitor_printf(mon, "Waiting for gdb connection on device '%s'\n",
                       device);
    }
}

static void do_watchdog_action(Monitor *mon, const QDict *qdict)
{
    const char *action = qdict_get_str(qdict, "action");
    if (select_watchdog_action(action) == -1) {
        monitor_printf(mon, "Unknown watchdog action '%s'\n", action);
    }
}

static void monitor_printc(Monitor *mon, int c)
{
    monitor_printf(mon, "'");
    switch(c) {
    case '\'':
        monitor_printf(mon, "\\'");
        break;
    case '\\':
        monitor_printf(mon, "\\\\");
        break;
    case '\n':
        monitor_printf(mon, "\\n");
        break;
    case '\r':
        monitor_printf(mon, "\\r");
        break;
    default:
        if (c >= 32 && c <= 126) {
            monitor_printf(mon, "%c", c);
        } else {
            monitor_printf(mon, "\\x%02x", c);
        }
        break;
    }
    monitor_printf(mon, "'");
}

static void memory_dump(Monitor *mon, int count, int format, int wsize,
                        hwaddr addr, int is_physical)
{
    CPUArchState *env;
    int l, line_size, i, max_digits, len;
    uint8_t buf[16];
    uint64_t v;

    if (format == 'i') {
        int flags;
        flags = 0;
        env = mon_get_cpu();
#ifdef TARGET_I386
        if (wsize == 2) {
            flags = 1;
        } else if (wsize == 4) {
            flags = 0;
        } else {
            /* as default we use the current CS size */
            flags = 0;
            if (env) {
#ifdef TARGET_X86_64
                if ((env->efer & MSR_EFER_LMA) &&
                    (env->segs[R_CS].flags & DESC_L_MASK))
                    flags = 2;
                else
#endif
                if (!(env->segs[R_CS].flags & DESC_B_MASK))
                    flags = 1;
            }
        }
#endif
#ifdef TARGET_PPC
        flags = msr_le << 16;
        flags |= env->bfd_mach;
#endif
        monitor_disas(mon, env, addr, count, is_physical, flags);
        return;
    }

    len = wsize * count;
    if (wsize == 1)
        line_size = 8;
    else
        line_size = 16;
    max_digits = 0;

    switch(format) {
    case 'o':
        max_digits = (wsize * 8 + 2) / 3;
        break;
    default:
    case 'x':
        max_digits = (wsize * 8) / 4;
        break;
    case 'u':
    case 'd':
        max_digits = (wsize * 8 * 10 + 32) / 33;
        break;
    case 'c':
        wsize = 1;
        break;
    }

    while (len > 0) {
        if (is_physical)
            monitor_printf(mon, TARGET_FMT_plx ":", addr);
        else
            monitor_printf(mon, TARGET_FMT_lx ":", (target_ulong)addr);
        l = len;
        if (l > line_size)
            l = line_size;
        if (is_physical) {
            cpu_physical_memory_read(addr, buf, l);
        } else {
            env = mon_get_cpu();
            if (cpu_memory_rw_debug(ENV_GET_CPU(env), addr, buf, l, 0) < 0) {
                monitor_printf(mon, " Cannot access memory\n");
                break;
            }
        }
        i = 0;
        while (i < l) {
            switch(wsize) {
            default:
            case 1:
                v = ldub_raw(buf + i);
                break;
            case 2:
                v = lduw_raw(buf + i);
                break;
            case 4:
                v = (uint32_t)ldl_raw(buf + i);
                break;
            case 8:
                v = ldq_raw(buf + i);
                break;
            }
            monitor_printf(mon, " ");
            switch(format) {
            case 'o':
                monitor_printf(mon, "%#*" PRIo64, max_digits, v);
                break;
            case 'x':
                monitor_printf(mon, "0x%0*" PRIx64, max_digits, v);
                break;
            case 'u':
                monitor_printf(mon, "%*" PRIu64, max_digits, v);
                break;
            case 'd':
                monitor_printf(mon, "%*" PRId64, max_digits, v);
                break;
            case 'c':
                monitor_printc(mon, v);
                break;
            }
            i += wsize;
        }
        monitor_printf(mon, "\n");
        addr += l;
        len -= l;
    }
}

static void do_memory_dump(Monitor *mon, const QDict *qdict)
{
    int count = qdict_get_int(qdict, "count");
    int format = qdict_get_int(qdict, "format");
    int size = qdict_get_int(qdict, "size");
    target_long addr = qdict_get_int(qdict, "addr");

    memory_dump(mon, count, format, size, addr, 0);
}

static void do_physical_memory_dump(Monitor *mon, const QDict *qdict)
{
    int count = qdict_get_int(qdict, "count");
    int format = qdict_get_int(qdict, "format");
    int size = qdict_get_int(qdict, "size");
    hwaddr addr = qdict_get_int(qdict, "addr");

    memory_dump(mon, count, format, size, addr, 1);
}

static void do_print(Monitor *mon, const QDict *qdict)
{
    int format = qdict_get_int(qdict, "format");
    hwaddr val = qdict_get_int(qdict, "val");

    switch(format) {
    case 'o':
        monitor_printf(mon, "%#" HWADDR_PRIo, val);
        break;
    case 'x':
        monitor_printf(mon, "%#" HWADDR_PRIx, val);
        break;
    case 'u':
        monitor_printf(mon, "%" HWADDR_PRIu, val);
        break;
    default:
    case 'd':
        monitor_printf(mon, "%" HWADDR_PRId, val);
        break;
    case 'c':
        monitor_printc(mon, val);
        break;
    }
    monitor_printf(mon, "\n");
}

static void do_sum(Monitor *mon, const QDict *qdict)
{
    uint32_t addr;
    uint16_t sum;
    uint32_t start = qdict_get_int(qdict, "start");
    uint32_t size = qdict_get_int(qdict, "size");

    sum = 0;
    for(addr = start; addr < (start + size); addr++) {
        uint8_t val = ldub_phys(&address_space_memory, addr);
        /* BSD sum algorithm ('sum' Unix command) */
        sum = (sum >> 1) | (sum << 15);
        sum += val;
    }
    monitor_printf(mon, "%05d\n", sum);
}

static int mouse_button_state;

static void do_mouse_move(Monitor *mon, const QDict *qdict)
{
    int dx, dy, dz, button;
    const char *dx_str = qdict_get_str(qdict, "dx_str");
    const char *dy_str = qdict_get_str(qdict, "dy_str");
    const char *dz_str = qdict_get_try_str(qdict, "dz_str");

    dx = strtol(dx_str, NULL, 0);
    dy = strtol(dy_str, NULL, 0);
    qemu_input_queue_rel(NULL, INPUT_AXIS_X, dx);
    qemu_input_queue_rel(NULL, INPUT_AXIS_Y, dy);

    if (dz_str) {
        dz = strtol(dz_str, NULL, 0);
        if (dz != 0) {
            button = (dz > 0) ? INPUT_BUTTON_WHEEL_UP : INPUT_BUTTON_WHEEL_DOWN;
            qemu_input_queue_btn(NULL, button, true);
            qemu_input_event_sync();
            qemu_input_queue_btn(NULL, button, false);
        }
    }
    qemu_input_event_sync();
}

static void do_mouse_button(Monitor *mon, const QDict *qdict)
{
    static uint32_t bmap[INPUT_BUTTON_MAX] = {
        [INPUT_BUTTON_LEFT]       = MOUSE_EVENT_LBUTTON,
        [INPUT_BUTTON_MIDDLE]     = MOUSE_EVENT_MBUTTON,
        [INPUT_BUTTON_RIGHT]      = MOUSE_EVENT_RBUTTON,
    };
    int button_state = qdict_get_int(qdict, "button_state");

    if (mouse_button_state == button_state) {
        return;
    }
    qemu_input_update_buttons(NULL, bmap, mouse_button_state, button_state);
    qemu_input_event_sync();
    mouse_button_state = button_state;
}

static void do_ioport_read(Monitor *mon, const QDict *qdict)
{
    int size = qdict_get_int(qdict, "size");
    int addr = qdict_get_int(qdict, "addr");
    int has_index = qdict_haskey(qdict, "index");
    uint32_t val;
    int suffix;

    if (has_index) {
        int index = qdict_get_int(qdict, "index");
        cpu_outb(addr & IOPORTS_MASK, index & 0xff);
        addr++;
    }
    addr &= 0xffff;

    switch(size) {
    default:
    case 1:
        val = cpu_inb(addr);
        suffix = 'b';
        break;
    case 2:
        val = cpu_inw(addr);
        suffix = 'w';
        break;
    case 4:
        val = cpu_inl(addr);
        suffix = 'l';
        break;
    }
    monitor_printf(mon, "port%c[0x%04x] = %#0*x\n",
                   suffix, addr, size * 2, val);
}

static void do_ioport_write(Monitor *mon, const QDict *qdict)
{
    int size = qdict_get_int(qdict, "size");
    int addr = qdict_get_int(qdict, "addr");
    int val = qdict_get_int(qdict, "val");

    addr &= IOPORTS_MASK;

    switch (size) {
    default:
    case 1:
        cpu_outb(addr, val);
        break;
    case 2:
        cpu_outw(addr, val);
        break;
    case 4:
        cpu_outl(addr, val);
        break;
    }
}

static void do_boot_set(Monitor *mon, const QDict *qdict)
{
    int res;
    const char *bootdevice = qdict_get_str(qdict, "bootdevice");

    res = qemu_boot_set(bootdevice);
    if (res == 0) {
        monitor_printf(mon, "boot device list now set to %s\n", bootdevice);
    } else if (res > 0) {
        monitor_printf(mon, "setting boot device list failed\n");
    } else {
        monitor_printf(mon, "no function defined to set boot device list for "
                       "this architecture\n");
    }
}

#if defined(TARGET_I386)
static void print_pte(Monitor *mon, hwaddr addr,
                      hwaddr pte,
                      hwaddr mask)
{
#ifdef TARGET_X86_64
    if (addr & (1ULL << 47)) {
        addr |= -1LL << 48;
    }
#endif
    monitor_printf(mon, TARGET_FMT_plx ": " TARGET_FMT_plx
                   " %c%c%c%c%c%c%c%c%c\n",
                   addr,
                   pte & mask,
                   pte & PG_NX_MASK ? 'X' : '-',
                   pte & PG_GLOBAL_MASK ? 'G' : '-',
                   pte & PG_PSE_MASK ? 'P' : '-',
                   pte & PG_DIRTY_MASK ? 'D' : '-',
                   pte & PG_ACCESSED_MASK ? 'A' : '-',
                   pte & PG_PCD_MASK ? 'C' : '-',
                   pte & PG_PWT_MASK ? 'T' : '-',
                   pte & PG_USER_MASK ? 'U' : '-',
                   pte & PG_RW_MASK ? 'W' : '-');
}

static void tlb_info_32(Monitor *mon, CPUArchState *env)
{
    unsigned int l1, l2;
    uint32_t pgd, pde, pte;

    pgd = env->cr[3] & ~0xfff;
    for(l1 = 0; l1 < 1024; l1++) {
        cpu_physical_memory_read(pgd + l1 * 4, &pde, 4);
        pde = le32_to_cpu(pde);
        if (pde & PG_PRESENT_MASK) {
            if ((pde & PG_PSE_MASK) && (env->cr[4] & CR4_PSE_MASK)) {
                /* 4M pages */
                print_pte(mon, (l1 << 22), pde, ~((1 << 21) - 1));
            } else {
                for(l2 = 0; l2 < 1024; l2++) {
                    cpu_physical_memory_read((pde & ~0xfff) + l2 * 4, &pte, 4);
                    pte = le32_to_cpu(pte);
                    if (pte & PG_PRESENT_MASK) {
                        print_pte(mon, (l1 << 22) + (l2 << 12),
                                  pte & ~PG_PSE_MASK,
                                  ~0xfff);
                    }
                }
            }
        }
    }
}

static void tlb_info_pae32(Monitor *mon, CPUArchState *env)
{
    unsigned int l1, l2, l3;
    uint64_t pdpe, pde, pte;
    uint64_t pdp_addr, pd_addr, pt_addr;

    pdp_addr = env->cr[3] & ~0x1f;
    for (l1 = 0; l1 < 4; l1++) {
        cpu_physical_memory_read(pdp_addr + l1 * 8, &pdpe, 8);
        pdpe = le64_to_cpu(pdpe);
        if (pdpe & PG_PRESENT_MASK) {
            pd_addr = pdpe & 0x3fffffffff000ULL;
            for (l2 = 0; l2 < 512; l2++) {
                cpu_physical_memory_read(pd_addr + l2 * 8, &pde, 8);
                pde = le64_to_cpu(pde);
                if (pde & PG_PRESENT_MASK) {
                    if (pde & PG_PSE_MASK) {
                        /* 2M pages with PAE, CR4.PSE is ignored */
                        print_pte(mon, (l1 << 30 ) + (l2 << 21), pde,
                                  ~((hwaddr)(1 << 20) - 1));
                    } else {
                        pt_addr = pde & 0x3fffffffff000ULL;
                        for (l3 = 0; l3 < 512; l3++) {
                            cpu_physical_memory_read(pt_addr + l3 * 8, &pte, 8);
                            pte = le64_to_cpu(pte);
                            if (pte & PG_PRESENT_MASK) {
                                print_pte(mon, (l1 << 30 ) + (l2 << 21)
                                          + (l3 << 12),
                                          pte & ~PG_PSE_MASK,
                                          ~(hwaddr)0xfff);
                            }
                        }
                    }
                }
            }
        }
    }
}

#ifdef TARGET_X86_64
static void tlb_info_64(Monitor *mon, CPUArchState *env)
{
    uint64_t l1, l2, l3, l4;
    uint64_t pml4e, pdpe, pde, pte;
    uint64_t pml4_addr, pdp_addr, pd_addr, pt_addr;

    pml4_addr = env->cr[3] & 0x3fffffffff000ULL;
    for (l1 = 0; l1 < 512; l1++) {
        cpu_physical_memory_read(pml4_addr + l1 * 8, &pml4e, 8);
        pml4e = le64_to_cpu(pml4e);
        if (pml4e & PG_PRESENT_MASK) {
            pdp_addr = pml4e & 0x3fffffffff000ULL;
            for (l2 = 0; l2 < 512; l2++) {
                cpu_physical_memory_read(pdp_addr + l2 * 8, &pdpe, 8);
                pdpe = le64_to_cpu(pdpe);
                if (pdpe & PG_PRESENT_MASK) {
                    if (pdpe & PG_PSE_MASK) {
                        /* 1G pages, CR4.PSE is ignored */
                        print_pte(mon, (l1 << 39) + (l2 << 30), pdpe,
                                  0x3ffffc0000000ULL);
                    } else {
                        pd_addr = pdpe & 0x3fffffffff000ULL;
                        for (l3 = 0; l3 < 512; l3++) {
                            cpu_physical_memory_read(pd_addr + l3 * 8, &pde, 8);
                            pde = le64_to_cpu(pde);
                            if (pde & PG_PRESENT_MASK) {
                                if (pde & PG_PSE_MASK) {
                                    /* 2M pages, CR4.PSE is ignored */
                                    print_pte(mon, (l1 << 39) + (l2 << 30) +
                                              (l3 << 21), pde,
                                              0x3ffffffe00000ULL);
                                } else {
                                    pt_addr = pde & 0x3fffffffff000ULL;
                                    for (l4 = 0; l4 < 512; l4++) {
                                        cpu_physical_memory_read(pt_addr
                                                                 + l4 * 8,
                                                                 &pte, 8);
                                        pte = le64_to_cpu(pte);
                                        if (pte & PG_PRESENT_MASK) {
                                            print_pte(mon, (l1 << 39) +
                                                      (l2 << 30) +
                                                      (l3 << 21) + (l4 << 12),
                                                      pte & ~PG_PSE_MASK,
                                                      0x3fffffffff000ULL);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
#endif

static void tlb_info(Monitor *mon, const QDict *qdict)
{
    CPUArchState *env;

    env = mon_get_cpu();

    if (!(env->cr[0] & CR0_PG_MASK)) {
        monitor_printf(mon, "PG disabled\n");
        return;
    }
    if (env->cr[4] & CR4_PAE_MASK) {
#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            tlb_info_64(mon, env);
        } else
#endif
        {
            tlb_info_pae32(mon, env);
        }
    } else {
        tlb_info_32(mon, env);
    }
}

static void mem_print(Monitor *mon, hwaddr *pstart,
                      int *plast_prot,
                      hwaddr end, int prot)
{
    int prot1;
    prot1 = *plast_prot;
    if (prot != prot1) {
        if (*pstart != -1) {
            monitor_printf(mon, TARGET_FMT_plx "-" TARGET_FMT_plx " "
                           TARGET_FMT_plx " %c%c%c\n",
                           *pstart, end, end - *pstart,
                           prot1 & PG_USER_MASK ? 'u' : '-',
                           'r',
                           prot1 & PG_RW_MASK ? 'w' : '-');
        }
        if (prot != 0)
            *pstart = end;
        else
            *pstart = -1;
        *plast_prot = prot;
    }
}

static void mem_info_32(Monitor *mon, CPUArchState *env)
{
    unsigned int l1, l2;
    int prot, last_prot;
    uint32_t pgd, pde, pte;
    hwaddr start, end;

    pgd = env->cr[3] & ~0xfff;
    last_prot = 0;
    start = -1;
    for(l1 = 0; l1 < 1024; l1++) {
        cpu_physical_memory_read(pgd + l1 * 4, &pde, 4);
        pde = le32_to_cpu(pde);
        end = l1 << 22;
        if (pde & PG_PRESENT_MASK) {
            if ((pde & PG_PSE_MASK) && (env->cr[4] & CR4_PSE_MASK)) {
                prot = pde & (PG_USER_MASK | PG_RW_MASK | PG_PRESENT_MASK);
                mem_print(mon, &start, &last_prot, end, prot);
            } else {
                for(l2 = 0; l2 < 1024; l2++) {
                    cpu_physical_memory_read((pde & ~0xfff) + l2 * 4, &pte, 4);
                    pte = le32_to_cpu(pte);
                    end = (l1 << 22) + (l2 << 12);
                    if (pte & PG_PRESENT_MASK) {
                        prot = pte & pde &
                            (PG_USER_MASK | PG_RW_MASK | PG_PRESENT_MASK);
                    } else {
                        prot = 0;
                    }
                    mem_print(mon, &start, &last_prot, end, prot);
                }
            }
        } else {
            prot = 0;
            mem_print(mon, &start, &last_prot, end, prot);
        }
    }
    /* Flush last range */
    mem_print(mon, &start, &last_prot, (hwaddr)1 << 32, 0);
}

static void mem_info_pae32(Monitor *mon, CPUArchState *env)
{
    unsigned int l1, l2, l3;
    int prot, last_prot;
    uint64_t pdpe, pde, pte;
    uint64_t pdp_addr, pd_addr, pt_addr;
    hwaddr start, end;

    pdp_addr = env->cr[3] & ~0x1f;
    last_prot = 0;
    start = -1;
    for (l1 = 0; l1 < 4; l1++) {
        cpu_physical_memory_read(pdp_addr + l1 * 8, &pdpe, 8);
        pdpe = le64_to_cpu(pdpe);
        end = l1 << 30;
        if (pdpe & PG_PRESENT_MASK) {
            pd_addr = pdpe & 0x3fffffffff000ULL;
            for (l2 = 0; l2 < 512; l2++) {
                cpu_physical_memory_read(pd_addr + l2 * 8, &pde, 8);
                pde = le64_to_cpu(pde);
                end = (l1 << 30) + (l2 << 21);
                if (pde & PG_PRESENT_MASK) {
                    if (pde & PG_PSE_MASK) {
                        prot = pde & (PG_USER_MASK | PG_RW_MASK |
                                      PG_PRESENT_MASK);
                        mem_print(mon, &start, &last_prot, end, prot);
                    } else {
                        pt_addr = pde & 0x3fffffffff000ULL;
                        for (l3 = 0; l3 < 512; l3++) {
                            cpu_physical_memory_read(pt_addr + l3 * 8, &pte, 8);
                            pte = le64_to_cpu(pte);
                            end = (l1 << 30) + (l2 << 21) + (l3 << 12);
                            if (pte & PG_PRESENT_MASK) {
                                prot = pte & pde & (PG_USER_MASK | PG_RW_MASK |
                                                    PG_PRESENT_MASK);
                            } else {
                                prot = 0;
                            }
                            mem_print(mon, &start, &last_prot, end, prot);
                        }
                    }
                } else {
                    prot = 0;
                    mem_print(mon, &start, &last_prot, end, prot);
                }
            }
        } else {
            prot = 0;
            mem_print(mon, &start, &last_prot, end, prot);
        }
    }
    /* Flush last range */
    mem_print(mon, &start, &last_prot, (hwaddr)1 << 32, 0);
}


#ifdef TARGET_X86_64
static void mem_info_64(Monitor *mon, CPUArchState *env)
{
    int prot, last_prot;
    uint64_t l1, l2, l3, l4;
    uint64_t pml4e, pdpe, pde, pte;
    uint64_t pml4_addr, pdp_addr, pd_addr, pt_addr, start, end;

    pml4_addr = env->cr[3] & 0x3fffffffff000ULL;
    last_prot = 0;
    start = -1;
    for (l1 = 0; l1 < 512; l1++) {
        cpu_physical_memory_read(pml4_addr + l1 * 8, &pml4e, 8);
        pml4e = le64_to_cpu(pml4e);
        end = l1 << 39;
        if (pml4e & PG_PRESENT_MASK) {
            pdp_addr = pml4e & 0x3fffffffff000ULL;
            for (l2 = 0; l2 < 512; l2++) {
                cpu_physical_memory_read(pdp_addr + l2 * 8, &pdpe, 8);
                pdpe = le64_to_cpu(pdpe);
                end = (l1 << 39) + (l2 << 30);
                if (pdpe & PG_PRESENT_MASK) {
                    if (pdpe & PG_PSE_MASK) {
                        prot = pdpe & (PG_USER_MASK | PG_RW_MASK |
                                       PG_PRESENT_MASK);
                        prot &= pml4e;
                        mem_print(mon, &start, &last_prot, end, prot);
                    } else {
                        pd_addr = pdpe & 0x3fffffffff000ULL;
                        for (l3 = 0; l3 < 512; l3++) {
                            cpu_physical_memory_read(pd_addr + l3 * 8, &pde, 8);
                            pde = le64_to_cpu(pde);
                            end = (l1 << 39) + (l2 << 30) + (l3 << 21);
                            if (pde & PG_PRESENT_MASK) {
                                if (pde & PG_PSE_MASK) {
                                    prot = pde & (PG_USER_MASK | PG_RW_MASK |
                                                  PG_PRESENT_MASK);
                                    prot &= pml4e & pdpe;
                                    mem_print(mon, &start, &last_prot, end, prot);
                                } else {
                                    pt_addr = pde & 0x3fffffffff000ULL;
                                    for (l4 = 0; l4 < 512; l4++) {
                                        cpu_physical_memory_read(pt_addr
                                                                 + l4 * 8,
                                                                 &pte, 8);
                                        pte = le64_to_cpu(pte);
                                        end = (l1 << 39) + (l2 << 30) +
                                            (l3 << 21) + (l4 << 12);
                                        if (pte & PG_PRESENT_MASK) {
                                            prot = pte & (PG_USER_MASK | PG_RW_MASK |
                                                          PG_PRESENT_MASK);
                                            prot &= pml4e & pdpe & pde;
                                        } else {
                                            prot = 0;
                                        }
                                        mem_print(mon, &start, &last_prot, end, prot);
                                    }
                                }
                            } else {
                                prot = 0;
                                mem_print(mon, &start, &last_prot, end, prot);
                            }
                        }
                    }
                } else {
                    prot = 0;
                    mem_print(mon, &start, &last_prot, end, prot);
                }
            }
        } else {
            prot = 0;
            mem_print(mon, &start, &last_prot, end, prot);
        }
    }
    /* Flush last range */
    mem_print(mon, &start, &last_prot, (hwaddr)1 << 48, 0);
}
#endif

static void mem_info(Monitor *mon, const QDict *qdict)
{
    CPUArchState *env;

    env = mon_get_cpu();

    if (!(env->cr[0] & CR0_PG_MASK)) {
        monitor_printf(mon, "PG disabled\n");
        return;
    }
    if (env->cr[4] & CR4_PAE_MASK) {
#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            mem_info_64(mon, env);
        } else
#endif
        {
            mem_info_pae32(mon, env);
        }
    } else {
        mem_info_32(mon, env);
    }
}
#endif

#if defined(TARGET_SH4)

static void print_tlb(Monitor *mon, int idx, tlb_t *tlb)
{
    monitor_printf(mon, " tlb%i:\t"
                   "asid=%hhu vpn=%x\tppn=%x\tsz=%hhu size=%u\t"
                   "v=%hhu shared=%hhu cached=%hhu prot=%hhu "
                   "dirty=%hhu writethrough=%hhu\n",
                   idx,
                   tlb->asid, tlb->vpn, tlb->ppn, tlb->sz, tlb->size,
                   tlb->v, tlb->sh, tlb->c, tlb->pr,
                   tlb->d, tlb->wt);
}

static void tlb_info(Monitor *mon, const QDict *qdict)
{
    CPUArchState *env = mon_get_cpu();
    int i;

    monitor_printf (mon, "ITLB:\n");
    for (i = 0 ; i < ITLB_SIZE ; i++)
        print_tlb (mon, i, &env->itlb[i]);
    monitor_printf (mon, "UTLB:\n");
    for (i = 0 ; i < UTLB_SIZE ; i++)
        print_tlb (mon, i, &env->utlb[i]);
}

#endif

#if defined(TARGET_SPARC) || defined(TARGET_PPC) || defined(TARGET_XTENSA)
static void tlb_info(Monitor *mon, const QDict *qdict)
{
    CPUArchState *env1 = mon_get_cpu();

    dump_mmu((FILE*)mon, (fprintf_function)monitor_printf, env1);
}
#endif

static void do_info_mtree(Monitor *mon, const QDict *qdict)
{
    mtree_info((fprintf_function)monitor_printf, mon);
}

static void do_info_numa(Monitor *mon, const QDict *qdict)
{
    int i;
    CPUState *cpu;

    monitor_printf(mon, "%d nodes\n", nb_numa_nodes);
    for (i = 0; i < nb_numa_nodes; i++) {
        monitor_printf(mon, "node %d cpus:", i);
        CPU_FOREACH(cpu) {
            if (cpu->numa_node == i) {
                monitor_printf(mon, " %d", cpu->cpu_index);
            }
        }
        monitor_printf(mon, "\n");
        monitor_printf(mon, "node %d size: %" PRId64 " MB\n", i,
            numa_info[i].node_mem >> 20);
    }
}

#ifdef CONFIG_PROFILER

int64_t qemu_time;
int64_t dev_time;

static void do_info_profile(Monitor *mon, const QDict *qdict)
{
    monitor_printf(mon, "async time  %" PRId64 " (%0.3f)\n",
                   dev_time, dev_time / (double)get_ticks_per_sec());
    monitor_printf(mon, "qemu time   %" PRId64 " (%0.3f)\n",
                   qemu_time, qemu_time / (double)get_ticks_per_sec());
    qemu_time = 0;
    dev_time = 0;
}
#else
static void do_info_profile(Monitor *mon, const QDict *qdict)
{
    monitor_printf(mon, "Internal profiler not compiled\n");
}
#endif

/* Capture support */
static QLIST_HEAD (capture_list_head, CaptureState) capture_head;

static void do_info_capture(Monitor *mon, const QDict *qdict)
{
    int i;
    CaptureState *s;

    for (s = capture_head.lh_first, i = 0; s; s = s->entries.le_next, ++i) {
        monitor_printf(mon, "[%d]: ", i);
        s->ops.info (s->opaque);
    }
}

static void do_stop_capture(Monitor *mon, const QDict *qdict)
{
    int i;
    int n = qdict_get_int(qdict, "n");
    CaptureState *s;

    for (s = capture_head.lh_first, i = 0; s; s = s->entries.le_next, ++i) {
        if (i == n) {
            s->ops.destroy (s->opaque);
            QLIST_REMOVE (s, entries);
            g_free (s);
            return;
        }
    }
}

static void do_wav_capture(Monitor *mon, const QDict *qdict)
{
    const char *path = qdict_get_str(qdict, "path");
    int has_freq = qdict_haskey(qdict, "freq");
    int freq = qdict_get_try_int(qdict, "freq", -1);
    int has_bits = qdict_haskey(qdict, "bits");
    int bits = qdict_get_try_int(qdict, "bits", -1);
    int has_channels = qdict_haskey(qdict, "nchannels");
    int nchannels = qdict_get_try_int(qdict, "nchannels", -1);
    CaptureState *s;

    s = g_malloc0 (sizeof (*s));

    freq = has_freq ? freq : 44100;
    bits = has_bits ? bits : 16;
    nchannels = has_channels ? nchannels : 2;

    if (wav_start_capture (s, path, freq, bits, nchannels)) {
        monitor_printf(mon, "Failed to add wave capture\n");
        g_free (s);
        return;
    }
    QLIST_INSERT_HEAD (&capture_head, s, entries);
}

static qemu_acl *find_acl(Monitor *mon, const char *name)
{
    qemu_acl *acl = qemu_acl_find(name);

    if (!acl) {
        monitor_printf(mon, "acl: unknown list '%s'\n", name);
    }
    return acl;
}

static void do_acl_show(Monitor *mon, const QDict *qdict)
{
    const char *aclname = qdict_get_str(qdict, "aclname");
    qemu_acl *acl = find_acl(mon, aclname);
    qemu_acl_entry *entry;
    int i = 0;

    if (acl) {
        monitor_printf(mon, "policy: %s\n",
                       acl->defaultDeny ? "deny" : "allow");
        QTAILQ_FOREACH(entry, &acl->entries, next) {
            i++;
            monitor_printf(mon, "%d: %s %s\n", i,
                           entry->deny ? "deny" : "allow", entry->match);
        }
    }
}

static void do_acl_reset(Monitor *mon, const QDict *qdict)
{
    const char *aclname = qdict_get_str(qdict, "aclname");
    qemu_acl *acl = find_acl(mon, aclname);

    if (acl) {
        qemu_acl_reset(acl);
        monitor_printf(mon, "acl: removed all rules\n");
    }
}

static void do_acl_policy(Monitor *mon, const QDict *qdict)
{
    const char *aclname = qdict_get_str(qdict, "aclname");
    const char *policy = qdict_get_str(qdict, "policy");
    qemu_acl *acl = find_acl(mon, aclname);

    if (acl) {
        if (strcmp(policy, "allow") == 0) {
            acl->defaultDeny = 0;
            monitor_printf(mon, "acl: policy set to 'allow'\n");
        } else if (strcmp(policy, "deny") == 0) {
            acl->defaultDeny = 1;
            monitor_printf(mon, "acl: policy set to 'deny'\n");
        } else {
            monitor_printf(mon, "acl: unknown policy '%s', "
                           "expected 'deny' or 'allow'\n", policy);
        }
    }
}

static void do_acl_add(Monitor *mon, const QDict *qdict)
{
    const char *aclname = qdict_get_str(qdict, "aclname");
    const char *match = qdict_get_str(qdict, "match");
    const char *policy = qdict_get_str(qdict, "policy");
    int has_index = qdict_haskey(qdict, "index");
    int index = qdict_get_try_int(qdict, "index", -1);
    qemu_acl *acl = find_acl(mon, aclname);
    int deny, ret;

    if (acl) {
        if (strcmp(policy, "allow") == 0) {
            deny = 0;
        } else if (strcmp(policy, "deny") == 0) {
            deny = 1;
        } else {
            monitor_printf(mon, "acl: unknown policy '%s', "
                           "expected 'deny' or 'allow'\n", policy);
            return;
        }
        if (has_index)
            ret = qemu_acl_insert(acl, deny, match, index);
        else
            ret = qemu_acl_append(acl, deny, match);
        if (ret < 0)
            monitor_printf(mon, "acl: unable to add acl entry\n");
        else
            monitor_printf(mon, "acl: added rule at position %d\n", ret);
    }
}

static void do_acl_remove(Monitor *mon, const QDict *qdict)
{
    const char *aclname = qdict_get_str(qdict, "aclname");
    const char *match = qdict_get_str(qdict, "match");
    qemu_acl *acl = find_acl(mon, aclname);
    int ret;

    if (acl) {
        ret = qemu_acl_remove(acl, match);
        if (ret < 0)
            monitor_printf(mon, "acl: no matching acl entry\n");
        else
            monitor_printf(mon, "acl: removed rule at position %d\n", ret);
    }
}

#if defined(TARGET_I386)
static void do_inject_mce(Monitor *mon, const QDict *qdict)
{
    X86CPU *cpu;
    CPUState *cs;
    int cpu_index = qdict_get_int(qdict, "cpu_index");
    int bank = qdict_get_int(qdict, "bank");
    uint64_t status = qdict_get_int(qdict, "status");
    uint64_t mcg_status = qdict_get_int(qdict, "mcg_status");
    uint64_t addr = qdict_get_int(qdict, "addr");
    uint64_t misc = qdict_get_int(qdict, "misc");
    int flags = MCE_INJECT_UNCOND_AO;

    if (qdict_get_try_bool(qdict, "broadcast", 0)) {
        flags |= MCE_INJECT_BROADCAST;
    }
    cs = qemu_get_cpu(cpu_index);
    if (cs != NULL) {
        cpu = X86_CPU(cs);
        cpu_x86_inject_mce(mon, cpu, bank, status, mcg_status, addr, misc,
                           flags);
    }
}
#endif

void qmp_getfd(const char *fdname, Error **errp)
{
    mon_fd_t *monfd;
    int fd;

    fd = qemu_chr_fe_get_msgfd(cur_mon->chr);
    if (fd == -1) {
        error_set(errp, QERR_FD_NOT_SUPPLIED);
        return;
    }

    if (qemu_isdigit(fdname[0])) {
        close(fd);
        error_set(errp, QERR_INVALID_PARAMETER_VALUE, "fdname",
                  "a name not starting with a digit");
        return;
    }

    QLIST_FOREACH(monfd, &cur_mon->fds, next) {
        if (strcmp(monfd->name, fdname) != 0) {
            continue;
        }

        close(monfd->fd);
        monfd->fd = fd;
        return;
    }

    monfd = g_malloc0(sizeof(mon_fd_t));
    monfd->name = g_strdup(fdname);
    monfd->fd = fd;

    QLIST_INSERT_HEAD(&cur_mon->fds, monfd, next);
}

void qmp_closefd(const char *fdname, Error **errp)
{
    mon_fd_t *monfd;

    QLIST_FOREACH(monfd, &cur_mon->fds, next) {
        if (strcmp(monfd->name, fdname) != 0) {
            continue;
        }

        QLIST_REMOVE(monfd, next);
        close(monfd->fd);
        g_free(monfd->name);
        g_free(monfd);
        return;
    }

    error_set(errp, QERR_FD_NOT_FOUND, fdname);
}

static void do_loadvm(Monitor *mon, const QDict *qdict)
{
    int saved_vm_running  = runstate_is_running();
    const char *name = qdict_get_str(qdict, "name");

    vm_stop(RUN_STATE_RESTORE_VM);

    if (load_vmstate(name) == 0 && saved_vm_running) {
        vm_start();
    }
}

int monitor_get_fd(Monitor *mon, const char *fdname, Error **errp)
{
    mon_fd_t *monfd;

    QLIST_FOREACH(monfd, &mon->fds, next) {
        int fd;

        if (strcmp(monfd->name, fdname) != 0) {
            continue;
        }

        fd = monfd->fd;

        /* caller takes ownership of fd */
        QLIST_REMOVE(monfd, next);
        g_free(monfd->name);
        g_free(monfd);

        return fd;
    }

    error_setg(errp, "File descriptor named '%s' has not been found", fdname);
    return -1;
}

static void monitor_fdset_cleanup(MonFdset *mon_fdset)
{
    MonFdsetFd *mon_fdset_fd;
    MonFdsetFd *mon_fdset_fd_next;

    QLIST_FOREACH_SAFE(mon_fdset_fd, &mon_fdset->fds, next, mon_fdset_fd_next) {
        if ((mon_fdset_fd->removed ||
                (QLIST_EMPTY(&mon_fdset->dup_fds) && mon_refcount == 0)) &&
                runstate_is_running()) {
            close(mon_fdset_fd->fd);
            g_free(mon_fdset_fd->opaque);
            QLIST_REMOVE(mon_fdset_fd, next);
            g_free(mon_fdset_fd);
        }
    }

    if (QLIST_EMPTY(&mon_fdset->fds) && QLIST_EMPTY(&mon_fdset->dup_fds)) {
        QLIST_REMOVE(mon_fdset, next);
        g_free(mon_fdset);
    }
}

static void monitor_fdsets_cleanup(void)
{
    MonFdset *mon_fdset;
    MonFdset *mon_fdset_next;

    QLIST_FOREACH_SAFE(mon_fdset, &mon_fdsets, next, mon_fdset_next) {
        monitor_fdset_cleanup(mon_fdset);
    }
}

AddfdInfo *qmp_add_fd(bool has_fdset_id, int64_t fdset_id, bool has_opaque,
                      const char *opaque, Error **errp)
{
    int fd;
    Monitor *mon = cur_mon;
    AddfdInfo *fdinfo;

    fd = qemu_chr_fe_get_msgfd(mon->chr);
    if (fd == -1) {
        error_set(errp, QERR_FD_NOT_SUPPLIED);
        goto error;
    }

    fdinfo = monitor_fdset_add_fd(fd, has_fdset_id, fdset_id,
                                  has_opaque, opaque, errp);
    if (fdinfo) {
        return fdinfo;
    }

error:
    if (fd != -1) {
        close(fd);
    }
    return NULL;
}

void qmp_remove_fd(int64_t fdset_id, bool has_fd, int64_t fd, Error **errp)
{
    MonFdset *mon_fdset;
    MonFdsetFd *mon_fdset_fd;
    char fd_str[60];

    QLIST_FOREACH(mon_fdset, &mon_fdsets, next) {
        if (mon_fdset->id != fdset_id) {
            continue;
        }
        QLIST_FOREACH(mon_fdset_fd, &mon_fdset->fds, next) {
            if (has_fd) {
                if (mon_fdset_fd->fd != fd) {
                    continue;
                }
                mon_fdset_fd->removed = true;
                break;
            } else {
                mon_fdset_fd->removed = true;
            }
        }
        if (has_fd && !mon_fdset_fd) {
            goto error;
        }
        monitor_fdset_cleanup(mon_fdset);
        return;
    }

error:
    if (has_fd) {
        snprintf(fd_str, sizeof(fd_str), "fdset-id:%" PRId64 ", fd:%" PRId64,
                 fdset_id, fd);
    } else {
        snprintf(fd_str, sizeof(fd_str), "fdset-id:%" PRId64, fdset_id);
    }
    error_set(errp, QERR_FD_NOT_FOUND, fd_str);
}

FdsetInfoList *qmp_query_fdsets(Error **errp)
{
    MonFdset *mon_fdset;
    MonFdsetFd *mon_fdset_fd;
    FdsetInfoList *fdset_list = NULL;

    QLIST_FOREACH(mon_fdset, &mon_fdsets, next) {
        FdsetInfoList *fdset_info = g_malloc0(sizeof(*fdset_info));
        FdsetFdInfoList *fdsetfd_list = NULL;

        fdset_info->value = g_malloc0(sizeof(*fdset_info->value));
        fdset_info->value->fdset_id = mon_fdset->id;

        QLIST_FOREACH(mon_fdset_fd, &mon_fdset->fds, next) {
            FdsetFdInfoList *fdsetfd_info;

            fdsetfd_info = g_malloc0(sizeof(*fdsetfd_info));
            fdsetfd_info->value = g_malloc0(sizeof(*fdsetfd_info->value));
            fdsetfd_info->value->fd = mon_fdset_fd->fd;
            if (mon_fdset_fd->opaque) {
                fdsetfd_info->value->has_opaque = true;
                fdsetfd_info->value->opaque = g_strdup(mon_fdset_fd->opaque);
            } else {
                fdsetfd_info->value->has_opaque = false;
            }

            fdsetfd_info->next = fdsetfd_list;
            fdsetfd_list = fdsetfd_info;
        }

        fdset_info->value->fds = fdsetfd_list;

        fdset_info->next = fdset_list;
        fdset_list = fdset_info;
    }

    return fdset_list;
}

AddfdInfo *monitor_fdset_add_fd(int fd, bool has_fdset_id, int64_t fdset_id,
                                bool has_opaque, const char *opaque,
                                Error **errp)
{
    MonFdset *mon_fdset = NULL;
    MonFdsetFd *mon_fdset_fd;
    AddfdInfo *fdinfo;

    if (has_fdset_id) {
        QLIST_FOREACH(mon_fdset, &mon_fdsets, next) {
            /* Break if match found or match impossible due to ordering by ID */
            if (fdset_id <= mon_fdset->id) {
                if (fdset_id < mon_fdset->id) {
                    mon_fdset = NULL;
                }
                break;
            }
        }
    }

    if (mon_fdset == NULL) {
        int64_t fdset_id_prev = -1;
        MonFdset *mon_fdset_cur = QLIST_FIRST(&mon_fdsets);

        if (has_fdset_id) {
            if (fdset_id < 0) {
                error_set(errp, QERR_INVALID_PARAMETER_VALUE, "fdset-id",
                          "a non-negative value");
                return NULL;
            }
            /* Use specified fdset ID */
            QLIST_FOREACH(mon_fdset, &mon_fdsets, next) {
                mon_fdset_cur = mon_fdset;
                if (fdset_id < mon_fdset_cur->id) {
                    break;
                }
            }
        } else {
            /* Use first available fdset ID */
            QLIST_FOREACH(mon_fdset, &mon_fdsets, next) {
                mon_fdset_cur = mon_fdset;
                if (fdset_id_prev == mon_fdset_cur->id - 1) {
                    fdset_id_prev = mon_fdset_cur->id;
                    continue;
                }
                break;
            }
        }

        mon_fdset = g_malloc0(sizeof(*mon_fdset));
        if (has_fdset_id) {
            mon_fdset->id = fdset_id;
        } else {
            mon_fdset->id = fdset_id_prev + 1;
        }

        /* The fdset list is ordered by fdset ID */
        if (!mon_fdset_cur) {
            QLIST_INSERT_HEAD(&mon_fdsets, mon_fdset, next);
        } else if (mon_fdset->id < mon_fdset_cur->id) {
            QLIST_INSERT_BEFORE(mon_fdset_cur, mon_fdset, next);
        } else {
            QLIST_INSERT_AFTER(mon_fdset_cur, mon_fdset, next);
        }
    }

    mon_fdset_fd = g_malloc0(sizeof(*mon_fdset_fd));
    mon_fdset_fd->fd = fd;
    mon_fdset_fd->removed = false;
    if (has_opaque) {
        mon_fdset_fd->opaque = g_strdup(opaque);
    }
    QLIST_INSERT_HEAD(&mon_fdset->fds, mon_fdset_fd, next);

    fdinfo = g_malloc0(sizeof(*fdinfo));
    fdinfo->fdset_id = mon_fdset->id;
    fdinfo->fd = mon_fdset_fd->fd;

    return fdinfo;
}

int monitor_fdset_get_fd(int64_t fdset_id, int flags)
{
#ifndef _WIN32
    MonFdset *mon_fdset;
    MonFdsetFd *mon_fdset_fd;
    int mon_fd_flags;

    QLIST_FOREACH(mon_fdset, &mon_fdsets, next) {
        if (mon_fdset->id != fdset_id) {
            continue;
        }
        QLIST_FOREACH(mon_fdset_fd, &mon_fdset->fds, next) {
            mon_fd_flags = fcntl(mon_fdset_fd->fd, F_GETFL);
            if (mon_fd_flags == -1) {
                return -1;
            }

            if ((flags & O_ACCMODE) == (mon_fd_flags & O_ACCMODE)) {
                return mon_fdset_fd->fd;
            }
        }
        errno = EACCES;
        return -1;
    }
#endif

    errno = ENOENT;
    return -1;
}

int monitor_fdset_dup_fd_add(int64_t fdset_id, int dup_fd)
{
    MonFdset *mon_fdset;
    MonFdsetFd *mon_fdset_fd_dup;

    QLIST_FOREACH(mon_fdset, &mon_fdsets, next) {
        if (mon_fdset->id != fdset_id) {
            continue;
        }
        QLIST_FOREACH(mon_fdset_fd_dup, &mon_fdset->dup_fds, next) {
            if (mon_fdset_fd_dup->fd == dup_fd) {
                return -1;
            }
        }
        mon_fdset_fd_dup = g_malloc0(sizeof(*mon_fdset_fd_dup));
        mon_fdset_fd_dup->fd = dup_fd;
        QLIST_INSERT_HEAD(&mon_fdset->dup_fds, mon_fdset_fd_dup, next);
        return 0;
    }
    return -1;
}

static int monitor_fdset_dup_fd_find_remove(int dup_fd, bool remove)
{
    MonFdset *mon_fdset;
    MonFdsetFd *mon_fdset_fd_dup;

    QLIST_FOREACH(mon_fdset, &mon_fdsets, next) {
        QLIST_FOREACH(mon_fdset_fd_dup, &mon_fdset->dup_fds, next) {
            if (mon_fdset_fd_dup->fd == dup_fd) {
                if (remove) {
                    QLIST_REMOVE(mon_fdset_fd_dup, next);
                    if (QLIST_EMPTY(&mon_fdset->dup_fds)) {
                        monitor_fdset_cleanup(mon_fdset);
                    }
                }
                return mon_fdset->id;
            }
        }
    }
    return -1;
}

int monitor_fdset_dup_fd_find(int dup_fd)
{
    return monitor_fdset_dup_fd_find_remove(dup_fd, false);
}

int monitor_fdset_dup_fd_remove(int dup_fd)
{
    return monitor_fdset_dup_fd_find_remove(dup_fd, true);
}

int monitor_handle_fd_param(Monitor *mon, const char *fdname)
{
    int fd;
    Error *local_err = NULL;

    fd = monitor_handle_fd_param2(mon, fdname, &local_err);
    if (local_err) {
        qerror_report_err(local_err);
        error_free(local_err);
    }
    return fd;
}

int monitor_handle_fd_param2(Monitor *mon, const char *fdname, Error **errp)
{
    int fd;
    Error *local_err = NULL;

    if (!qemu_isdigit(fdname[0]) && mon) {
        fd = monitor_get_fd(mon, fdname, &local_err);
    } else {
        fd = qemu_parse_fd(fdname);
        if (fd == -1) {
            error_setg(&local_err, "Invalid file descriptor number '%s'",
                       fdname);
        }
    }
    if (local_err) {
        error_propagate(errp, local_err);
        assert(fd == -1);
    } else {
        assert(fd != -1);
    }

    return fd;
}

/* Please update hmp-commands.hx when adding or changing commands */
static mon_cmd_t info_cmds[] = {
    {
        .name       = "version",
        .args_type  = "",
        .params     = "",
        .help       = "show the version of QEMU",
        .mhandler.cmd = hmp_info_version,
    },
    {
        .name       = "network",
        .args_type  = "",
        .params     = "",
        .help       = "show the network state",
        .mhandler.cmd = do_info_network,
    },
    {
        .name       = "chardev",
        .args_type  = "",
        .params     = "",
        .help       = "show the character devices",
        .mhandler.cmd = hmp_info_chardev,
    },
    {
        .name       = "block",
        .args_type  = "verbose:-v,device:B?",
        .params     = "[-v] [device]",
        .help       = "show info of one block device or all block devices "
                      "(and details of images with -v option)",
        .mhandler.cmd = hmp_info_block,
    },
    {
        .name       = "blockstats",
        .args_type  = "",
        .params     = "",
        .help       = "show block device statistics",
        .mhandler.cmd = hmp_info_blockstats,
    },
    {
        .name       = "block-jobs",
        .args_type  = "",
        .params     = "",
        .help       = "show progress of ongoing block device operations",
        .mhandler.cmd = hmp_info_block_jobs,
    },
    {
        .name       = "registers",
        .args_type  = "",
        .params     = "",
        .help       = "show the cpu registers",
        .mhandler.cmd = do_info_registers,
    },
    {
        .name       = "cpus",
        .args_type  = "",
        .params     = "",
        .help       = "show infos for each CPU",
        .mhandler.cmd = hmp_info_cpus,
    },
    {
        .name       = "history",
        .args_type  = "",
        .params     = "",
        .help       = "show the command line history",
        .mhandler.cmd = do_info_history,
    },
#if defined(TARGET_I386) || defined(TARGET_PPC) || defined(TARGET_MIPS) || \
    defined(TARGET_LM32) || (defined(TARGET_SPARC) && !defined(TARGET_SPARC64))
    {
        .name       = "irq",
        .args_type  = "",
        .params     = "",
        .help       = "show the interrupts statistics (if available)",
#ifdef TARGET_SPARC
        .mhandler.cmd = sun4m_irq_info,
#elif defined(TARGET_LM32)
        .mhandler.cmd = lm32_irq_info,
#else
        .mhandler.cmd = irq_info,
#endif
    },
    {
        .name       = "pic",
        .args_type  = "",
        .params     = "",
        .help       = "show i8259 (PIC) state",
#ifdef TARGET_SPARC
        .mhandler.cmd = sun4m_pic_info,
#elif defined(TARGET_LM32)
        .mhandler.cmd = lm32_do_pic_info,
#else
        .mhandler.cmd = pic_info,
#endif
    },
#endif
    {
        .name       = "pci",
        .args_type  = "",
        .params     = "",
        .help       = "show PCI info",
        .mhandler.cmd = hmp_info_pci,
    },
#if defined(TARGET_I386) || defined(TARGET_SH4) || defined(TARGET_SPARC) || \
    defined(TARGET_PPC) || defined(TARGET_XTENSA)
    {
        .name       = "tlb",
        .args_type  = "",
        .params     = "",
        .help       = "show virtual to physical memory mappings",
        .mhandler.cmd = tlb_info,
    },
#endif
#if defined(TARGET_I386)
    {
        .name       = "mem",
        .args_type  = "",
        .params     = "",
        .help       = "show the active virtual memory mappings",
        .mhandler.cmd = mem_info,
    },
#endif
    {
        .name       = "mtree",
        .args_type  = "",
        .params     = "",
        .help       = "show memory tree",
        .mhandler.cmd = do_info_mtree,
    },
    {
        .name       = "jit",
        .args_type  = "",
        .params     = "",
        .help       = "show dynamic compiler info",
        .mhandler.cmd = do_info_jit,
    },
    {
        .name       = "kvm",
        .args_type  = "",
        .params     = "",
        .help       = "show KVM information",
        .mhandler.cmd = hmp_info_kvm,
    },
    {
        .name       = "numa",
        .args_type  = "",
        .params     = "",
        .help       = "show NUMA information",
        .mhandler.cmd = do_info_numa,
    },
    {
        .name       = "usb",
        .args_type  = "",
        .params     = "",
        .help       = "show guest USB devices",
        .mhandler.cmd = usb_info,
    },
    {
        .name       = "usbhost",
        .args_type  = "",
        .params     = "",
        .help       = "show host USB devices",
        .mhandler.cmd = usb_host_info,
    },
    {
        .name       = "profile",
        .args_type  = "",
        .params     = "",
        .help       = "show profiling information",
        .mhandler.cmd = do_info_profile,
    },
    {
        .name       = "capture",
        .args_type  = "",
        .params     = "",
        .help       = "show capture information",
        .mhandler.cmd = do_info_capture,
    },
    {
        .name       = "snapshots",
        .args_type  = "",
        .params     = "",
        .help       = "show the currently saved VM snapshots",
        .mhandler.cmd = do_info_snapshots,
    },
    {
        .name       = "status",
        .args_type  = "",
        .params     = "",
        .help       = "show the current VM status (running|paused)",
        .mhandler.cmd = hmp_info_status,
    },
    {
        .name       = "pcmcia",
        .args_type  = "",
        .params     = "",
        .help       = "show guest PCMCIA status",
        .mhandler.cmd = pcmcia_info,
    },
    {
        .name       = "mice",
        .args_type  = "",
        .params     = "",
        .help       = "show which guest mouse is receiving events",
        .mhandler.cmd = hmp_info_mice,
    },
    {
        .name       = "vnc",
        .args_type  = "",
        .params     = "",
        .help       = "show the vnc server status",
        .mhandler.cmd = hmp_info_vnc,
    },
#if defined(CONFIG_SPICE)
    {
        .name       = "spice",
        .args_type  = "",
        .params     = "",
        .help       = "show the spice server status",
        .mhandler.cmd = hmp_info_spice,
    },
#endif
    {
        .name       = "name",
        .args_type  = "",
        .params     = "",
        .help       = "show the current VM name",
        .mhandler.cmd = hmp_info_name,
    },
    {
        .name       = "uuid",
        .args_type  = "",
        .params     = "",
        .help       = "show the current VM UUID",
        .mhandler.cmd = hmp_info_uuid,
    },
    {
        .name       = "cpustats",
        .args_type  = "",
        .params     = "",
        .help       = "show CPU statistics",
        .mhandler.cmd = do_info_cpu_stats,
    },
#if defined(CONFIG_SLIRP)
    {
        .name       = "usernet",
        .args_type  = "",
        .params     = "",
        .help       = "show user network stack connection states",
        .mhandler.cmd = do_info_usernet,
    },
#endif
    {
        .name       = "migrate",
        .args_type  = "",
        .params     = "",
        .help       = "show migration status",
        .mhandler.cmd = hmp_info_migrate,
    },
    {
        .name       = "migrate_capabilities",
        .args_type  = "",
        .params     = "",
        .help       = "show current migration capabilities",
        .mhandler.cmd = hmp_info_migrate_capabilities,
    },
    {
        .name       = "migrate_cache_size",
        .args_type  = "",
        .params     = "",
        .help       = "show current migration xbzrle cache size",
        .mhandler.cmd = hmp_info_migrate_cache_size,
    },
    {
        .name       = "balloon",
        .args_type  = "",
        .params     = "",
        .help       = "show balloon information",
        .mhandler.cmd = hmp_info_balloon,
    },
    {
        .name       = "qtree",
        .args_type  = "",
        .params     = "",
        .help       = "show device tree",
        .mhandler.cmd = do_info_qtree,
    },
    {
        .name       = "qdm",
        .args_type  = "",
        .params     = "",
        .help       = "show qdev device model list",
        .mhandler.cmd = do_info_qdm,
    },
    {
        .name       = "roms",
        .args_type  = "",
        .params     = "",
        .help       = "show roms",
        .mhandler.cmd = do_info_roms,
    },
    {
        .name       = "trace-events",
        .args_type  = "",
        .params     = "",
        .help       = "show available trace-events & their state",
        .mhandler.cmd = do_trace_print_events,
    },
    {
        .name       = "tpm",
        .args_type  = "",
        .params     = "",
        .help       = "show the TPM device",
        .mhandler.cmd = hmp_info_tpm,
    },
    {
        .name       = "memdev",
        .args_type  = "",
        .params     = "",
        .help       = "show memory backends",
        .mhandler.cmd = hmp_info_memdev,
    },
    {
        .name       = NULL,
    },
};

/* mon_cmds and info_cmds would be sorted at runtime */
static mon_cmd_t mon_cmds[] = {
#include "hmp-commands.h"
    { NULL, NULL, },
};

static const mon_cmd_t qmp_cmds[] = {
#include "qmp-commands-old.h"
    { /* NULL */ },
};

/*******************************************************************/

static const char *pch;
static sigjmp_buf expr_env;

#define MD_TLONG 0
#define MD_I32   1

typedef struct MonitorDef {
    const char *name;
    int offset;
    target_long (*get_value)(const struct MonitorDef *md, int val);
    int type;
} MonitorDef;

#if defined(TARGET_I386)
static target_long monitor_get_pc (const struct MonitorDef *md, int val)
{
    CPUArchState *env = mon_get_cpu();
    return env->eip + env->segs[R_CS].base;
}
#endif

#if defined(TARGET_PPC)
static target_long monitor_get_ccr (const struct MonitorDef *md, int val)
{
    CPUArchState *env = mon_get_cpu();
    unsigned int u;
    int i;

    u = 0;
    for (i = 0; i < 8; i++)
        u |= env->crf[i] << (32 - (4 * i));

    return u;
}

static target_long monitor_get_msr (const struct MonitorDef *md, int val)
{
    CPUArchState *env = mon_get_cpu();
    return env->msr;
}

static target_long monitor_get_xer (const struct MonitorDef *md, int val)
{
    CPUArchState *env = mon_get_cpu();
    return env->xer;
}

static target_long monitor_get_decr (const struct MonitorDef *md, int val)
{
    CPUArchState *env = mon_get_cpu();
    return cpu_ppc_load_decr(env);
}

static target_long monitor_get_tbu (const struct MonitorDef *md, int val)
{
    CPUArchState *env = mon_get_cpu();
    return cpu_ppc_load_tbu(env);
}

static target_long monitor_get_tbl (const struct MonitorDef *md, int val)
{
    CPUArchState *env = mon_get_cpu();
    return cpu_ppc_load_tbl(env);
}
#endif

#if defined(TARGET_SPARC)
#ifndef TARGET_SPARC64
static target_long monitor_get_psr (const struct MonitorDef *md, int val)
{
    CPUArchState *env = mon_get_cpu();

    return cpu_get_psr(env);
}
#endif

static target_long monitor_get_reg(const struct MonitorDef *md, int val)
{
    CPUArchState *env = mon_get_cpu();
    return env->regwptr[val];
}
#endif

static const MonitorDef monitor_defs[] = {
#ifdef TARGET_I386

#define SEG(name, seg) \
    { name, offsetof(CPUX86State, segs[seg].selector), NULL, MD_I32 },\
    { name ".base", offsetof(CPUX86State, segs[seg].base) },\
    { name ".limit", offsetof(CPUX86State, segs[seg].limit), NULL, MD_I32 },

    { "eax", offsetof(CPUX86State, regs[0]) },
    { "ecx", offsetof(CPUX86State, regs[1]) },
    { "edx", offsetof(CPUX86State, regs[2]) },
    { "ebx", offsetof(CPUX86State, regs[3]) },
    { "esp|sp", offsetof(CPUX86State, regs[4]) },
    { "ebp|fp", offsetof(CPUX86State, regs[5]) },
    { "esi", offsetof(CPUX86State, regs[6]) },
    { "edi", offsetof(CPUX86State, regs[7]) },
#ifdef TARGET_X86_64
    { "r8", offsetof(CPUX86State, regs[8]) },
    { "r9", offsetof(CPUX86State, regs[9]) },
    { "r10", offsetof(CPUX86State, regs[10]) },
    { "r11", offsetof(CPUX86State, regs[11]) },
    { "r12", offsetof(CPUX86State, regs[12]) },
    { "r13", offsetof(CPUX86State, regs[13]) },
    { "r14", offsetof(CPUX86State, regs[14]) },
    { "r15", offsetof(CPUX86State, regs[15]) },
#endif
    { "eflags", offsetof(CPUX86State, eflags) },
    { "eip", offsetof(CPUX86State, eip) },
    SEG("cs", R_CS)
    SEG("ds", R_DS)
    SEG("es", R_ES)
    SEG("ss", R_SS)
    SEG("fs", R_FS)
    SEG("gs", R_GS)
    { "pc", 0, monitor_get_pc, },
#elif defined(TARGET_PPC)
    /* General purpose registers */
    { "r0", offsetof(CPUPPCState, gpr[0]) },
    { "r1", offsetof(CPUPPCState, gpr[1]) },
    { "r2", offsetof(CPUPPCState, gpr[2]) },
    { "r3", offsetof(CPUPPCState, gpr[3]) },
    { "r4", offsetof(CPUPPCState, gpr[4]) },
    { "r5", offsetof(CPUPPCState, gpr[5]) },
    { "r6", offsetof(CPUPPCState, gpr[6]) },
    { "r7", offsetof(CPUPPCState, gpr[7]) },
    { "r8", offsetof(CPUPPCState, gpr[8]) },
    { "r9", offsetof(CPUPPCState, gpr[9]) },
    { "r10", offsetof(CPUPPCState, gpr[10]) },
    { "r11", offsetof(CPUPPCState, gpr[11]) },
    { "r12", offsetof(CPUPPCState, gpr[12]) },
    { "r13", offsetof(CPUPPCState, gpr[13]) },
    { "r14", offsetof(CPUPPCState, gpr[14]) },
    { "r15", offsetof(CPUPPCState, gpr[15]) },
    { "r16", offsetof(CPUPPCState, gpr[16]) },
    { "r17", offsetof(CPUPPCState, gpr[17]) },
    { "r18", offsetof(CPUPPCState, gpr[18]) },
    { "r19", offsetof(CPUPPCState, gpr[19]) },
    { "r20", offsetof(CPUPPCState, gpr[20]) },
    { "r21", offsetof(CPUPPCState, gpr[21]) },
    { "r22", offsetof(CPUPPCState, gpr[22]) },
    { "r23", offsetof(CPUPPCState, gpr[23]) },
    { "r24", offsetof(CPUPPCState, gpr[24]) },
    { "r25", offsetof(CPUPPCState, gpr[25]) },
    { "r26", offsetof(CPUPPCState, gpr[26]) },
    { "r27", offsetof(CPUPPCState, gpr[27]) },
    { "r28", offsetof(CPUPPCState, gpr[28]) },
    { "r29", offsetof(CPUPPCState, gpr[29]) },
    { "r30", offsetof(CPUPPCState, gpr[30]) },
    { "r31", offsetof(CPUPPCState, gpr[31]) },
    /* Floating point registers */
    { "f0", offsetof(CPUPPCState, fpr[0]) },
    { "f1", offsetof(CPUPPCState, fpr[1]) },
    { "f2", offsetof(CPUPPCState, fpr[2]) },
    { "f3", offsetof(CPUPPCState, fpr[3]) },
    { "f4", offsetof(CPUPPCState, fpr[4]) },
    { "f5", offsetof(CPUPPCState, fpr[5]) },
    { "f6", offsetof(CPUPPCState, fpr[6]) },
    { "f7", offsetof(CPUPPCState, fpr[7]) },
    { "f8", offsetof(CPUPPCState, fpr[8]) },
    { "f9", offsetof(CPUPPCState, fpr[9]) },
    { "f10", offsetof(CPUPPCState, fpr[10]) },
    { "f11", offsetof(CPUPPCState, fpr[11]) },
    { "f12", offsetof(CPUPPCState, fpr[12]) },
    { "f13", offsetof(CPUPPCState, fpr[13]) },
    { "f14", offsetof(CPUPPCState, fpr[14]) },
    { "f15", offsetof(CPUPPCState, fpr[15]) },
    { "f16", offsetof(CPUPPCState, fpr[16]) },
    { "f17", offsetof(CPUPPCState, fpr[17]) },
    { "f18", offsetof(CPUPPCState, fpr[18]) },
    { "f19", offsetof(CPUPPCState, fpr[19]) },
    { "f20", offsetof(CPUPPCState, fpr[20]) },
    { "f21", offsetof(CPUPPCState, fpr[21]) },
    { "f22", offsetof(CPUPPCState, fpr[22]) },
    { "f23", offsetof(CPUPPCState, fpr[23]) },
    { "f24", offsetof(CPUPPCState, fpr[24]) },
    { "f25", offsetof(CPUPPCState, fpr[25]) },
    { "f26", offsetof(CPUPPCState, fpr[26]) },
    { "f27", offsetof(CPUPPCState, fpr[27]) },
    { "f28", offsetof(CPUPPCState, fpr[28]) },
    { "f29", offsetof(CPUPPCState, fpr[29]) },
    { "f30", offsetof(CPUPPCState, fpr[30]) },
    { "f31", offsetof(CPUPPCState, fpr[31]) },
    { "fpscr", offsetof(CPUPPCState, fpscr) },
    /* Next instruction pointer */
    { "nip|pc", offsetof(CPUPPCState, nip) },
    { "lr", offsetof(CPUPPCState, lr) },
    { "ctr", offsetof(CPUPPCState, ctr) },
    { "decr", 0, &monitor_get_decr, },
    { "ccr", 0, &monitor_get_ccr, },
    /* Machine state register */
    { "msr", 0, &monitor_get_msr, },
    { "xer", 0, &monitor_get_xer, },
    { "tbu", 0, &monitor_get_tbu, },
    { "tbl", 0, &monitor_get_tbl, },
    /* Segment registers */
    { "sdr1", offsetof(CPUPPCState, spr[SPR_SDR1]) },
    { "sr0", offsetof(CPUPPCState, sr[0]) },
    { "sr1", offsetof(CPUPPCState, sr[1]) },
    { "sr2", offsetof(CPUPPCState, sr[2]) },
    { "sr3", offsetof(CPUPPCState, sr[3]) },
    { "sr4", offsetof(CPUPPCState, sr[4]) },
    { "sr5", offsetof(CPUPPCState, sr[5]) },
    { "sr6", offsetof(CPUPPCState, sr[6]) },
    { "sr7", offsetof(CPUPPCState, sr[7]) },
    { "sr8", offsetof(CPUPPCState, sr[8]) },
    { "sr9", offsetof(CPUPPCState, sr[9]) },
    { "sr10", offsetof(CPUPPCState, sr[10]) },
    { "sr11", offsetof(CPUPPCState, sr[11]) },
    { "sr12", offsetof(CPUPPCState, sr[12]) },
    { "sr13", offsetof(CPUPPCState, sr[13]) },
    { "sr14", offsetof(CPUPPCState, sr[14]) },
    { "sr15", offsetof(CPUPPCState, sr[15]) },
    /* Too lazy to put BATs... */
    { "pvr", offsetof(CPUPPCState, spr[SPR_PVR]) },

    { "srr0", offsetof(CPUPPCState, spr[SPR_SRR0]) },
    { "srr1", offsetof(CPUPPCState, spr[SPR_SRR1]) },
    { "dar", offsetof(CPUPPCState, spr[SPR_DAR]) },
    { "dsisr", offsetof(CPUPPCState, spr[SPR_DSISR]) },
    { "cfar", offsetof(CPUPPCState, spr[SPR_CFAR]) },
    { "sprg0", offsetof(CPUPPCState, spr[SPR_SPRG0]) },
    { "sprg1", offsetof(CPUPPCState, spr[SPR_SPRG1]) },
    { "sprg2", offsetof(CPUPPCState, spr[SPR_SPRG2]) },
    { "sprg3", offsetof(CPUPPCState, spr[SPR_SPRG3]) },
    { "sprg4", offsetof(CPUPPCState, spr[SPR_SPRG4]) },
    { "sprg5", offsetof(CPUPPCState, spr[SPR_SPRG5]) },
    { "sprg6", offsetof(CPUPPCState, spr[SPR_SPRG6]) },
    { "sprg7", offsetof(CPUPPCState, spr[SPR_SPRG7]) },
    { "pid", offsetof(CPUPPCState, spr[SPR_BOOKE_PID]) },
    { "csrr0", offsetof(CPUPPCState, spr[SPR_BOOKE_CSRR0]) },
    { "csrr1", offsetof(CPUPPCState, spr[SPR_BOOKE_CSRR1]) },
    { "esr", offsetof(CPUPPCState, spr[SPR_BOOKE_ESR]) },
    { "dear", offsetof(CPUPPCState, spr[SPR_BOOKE_DEAR]) },
    { "mcsr", offsetof(CPUPPCState, spr[SPR_BOOKE_MCSR]) },
    { "tsr", offsetof(CPUPPCState, spr[SPR_BOOKE_TSR]) },
    { "tcr", offsetof(CPUPPCState, spr[SPR_BOOKE_TCR]) },
    { "vrsave", offsetof(CPUPPCState, spr[SPR_VRSAVE]) },
    { "pir", offsetof(CPUPPCState, spr[SPR_BOOKE_PIR]) },
    { "mcsrr0", offsetof(CPUPPCState, spr[SPR_BOOKE_MCSRR0]) },
    { "mcsrr1", offsetof(CPUPPCState, spr[SPR_BOOKE_MCSRR1]) },
    { "decar", offsetof(CPUPPCState, spr[SPR_BOOKE_DECAR]) },
    { "ivpr", offsetof(CPUPPCState, spr[SPR_BOOKE_IVPR]) },
    { "epcr", offsetof(CPUPPCState, spr[SPR_BOOKE_EPCR]) },
    { "sprg8", offsetof(CPUPPCState, spr[SPR_BOOKE_SPRG8]) },
    { "ivor0", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR0]) },
    { "ivor1", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR1]) },
    { "ivor2", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR2]) },
    { "ivor3", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR3]) },
    { "ivor4", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR4]) },
    { "ivor5", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR5]) },
    { "ivor6", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR6]) },
    { "ivor7", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR7]) },
    { "ivor8", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR8]) },
    { "ivor9", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR9]) },
    { "ivor10", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR10]) },
    { "ivor11", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR11]) },
    { "ivor12", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR12]) },
    { "ivor13", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR13]) },
    { "ivor14", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR14]) },
    { "ivor15", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR15]) },
    { "ivor32", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR32]) },
    { "ivor33", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR33]) },
    { "ivor34", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR34]) },
    { "ivor35", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR35]) },
    { "ivor36", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR36]) },
    { "ivor37", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR37]) },
    { "mas0", offsetof(CPUPPCState, spr[SPR_BOOKE_MAS0]) },
    { "mas1", offsetof(CPUPPCState, spr[SPR_BOOKE_MAS1]) },
    { "mas2", offsetof(CPUPPCState, spr[SPR_BOOKE_MAS2]) },
    { "mas3", offsetof(CPUPPCState, spr[SPR_BOOKE_MAS3]) },
    { "mas4", offsetof(CPUPPCState, spr[SPR_BOOKE_MAS4]) },
    { "mas6", offsetof(CPUPPCState, spr[SPR_BOOKE_MAS6]) },
    { "mas7", offsetof(CPUPPCState, spr[SPR_BOOKE_MAS7]) },
    { "mmucfg", offsetof(CPUPPCState, spr[SPR_MMUCFG]) },
    { "tlb0cfg", offsetof(CPUPPCState, spr[SPR_BOOKE_TLB0CFG]) },
    { "tlb1cfg", offsetof(CPUPPCState, spr[SPR_BOOKE_TLB1CFG]) },
    { "epr", offsetof(CPUPPCState, spr[SPR_BOOKE_EPR]) },
    { "eplc", offsetof(CPUPPCState, spr[SPR_BOOKE_EPLC]) },
    { "epsc", offsetof(CPUPPCState, spr[SPR_BOOKE_EPSC]) },
    { "svr", offsetof(CPUPPCState, spr[SPR_E500_SVR]) },
    { "mcar", offsetof(CPUPPCState, spr[SPR_Exxx_MCAR]) },
    { "pid1", offsetof(CPUPPCState, spr[SPR_BOOKE_PID1]) },
    { "pid2", offsetof(CPUPPCState, spr[SPR_BOOKE_PID2]) },
    { "hid0", offsetof(CPUPPCState, spr[SPR_HID0]) },

#elif defined(TARGET_SPARC)
    { "g0", offsetof(CPUSPARCState, gregs[0]) },
    { "g1", offsetof(CPUSPARCState, gregs[1]) },
    { "g2", offsetof(CPUSPARCState, gregs[2]) },
    { "g3", offsetof(CPUSPARCState, gregs[3]) },
    { "g4", offsetof(CPUSPARCState, gregs[4]) },
    { "g5", offsetof(CPUSPARCState, gregs[5]) },
    { "g6", offsetof(CPUSPARCState, gregs[6]) },
    { "g7", offsetof(CPUSPARCState, gregs[7]) },
    { "o0", 0, monitor_get_reg },
    { "o1", 1, monitor_get_reg },
    { "o2", 2, monitor_get_reg },
    { "o3", 3, monitor_get_reg },
    { "o4", 4, monitor_get_reg },
    { "o5", 5, monitor_get_reg },
    { "o6", 6, monitor_get_reg },
    { "o7", 7, monitor_get_reg },
    { "l0", 8, monitor_get_reg },
    { "l1", 9, monitor_get_reg },
    { "l2", 10, monitor_get_reg },
    { "l3", 11, monitor_get_reg },
    { "l4", 12, monitor_get_reg },
    { "l5", 13, monitor_get_reg },
    { "l6", 14, monitor_get_reg },
    { "l7", 15, monitor_get_reg },
    { "i0", 16, monitor_get_reg },
    { "i1", 17, monitor_get_reg },
    { "i2", 18, monitor_get_reg },
    { "i3", 19, monitor_get_reg },
    { "i4", 20, monitor_get_reg },
    { "i5", 21, monitor_get_reg },
    { "i6", 22, monitor_get_reg },
    { "i7", 23, monitor_get_reg },
    { "pc", offsetof(CPUSPARCState, pc) },
    { "npc", offsetof(CPUSPARCState, npc) },
    { "y", offsetof(CPUSPARCState, y) },
#ifndef TARGET_SPARC64
    { "psr", 0, &monitor_get_psr, },
    { "wim", offsetof(CPUSPARCState, wim) },
#endif
    { "tbr", offsetof(CPUSPARCState, tbr) },
    { "fsr", offsetof(CPUSPARCState, fsr) },
    { "f0", offsetof(CPUSPARCState, fpr[0].l.upper) },
    { "f1", offsetof(CPUSPARCState, fpr[0].l.lower) },
    { "f2", offsetof(CPUSPARCState, fpr[1].l.upper) },
    { "f3", offsetof(CPUSPARCState, fpr[1].l.lower) },
    { "f4", offsetof(CPUSPARCState, fpr[2].l.upper) },
    { "f5", offsetof(CPUSPARCState, fpr[2].l.lower) },
    { "f6", offsetof(CPUSPARCState, fpr[3].l.upper) },
    { "f7", offsetof(CPUSPARCState, fpr[3].l.lower) },
    { "f8", offsetof(CPUSPARCState, fpr[4].l.upper) },
    { "f9", offsetof(CPUSPARCState, fpr[4].l.lower) },
    { "f10", offsetof(CPUSPARCState, fpr[5].l.upper) },
    { "f11", offsetof(CPUSPARCState, fpr[5].l.lower) },
    { "f12", offsetof(CPUSPARCState, fpr[6].l.upper) },
    { "f13", offsetof(CPUSPARCState, fpr[6].l.lower) },
    { "f14", offsetof(CPUSPARCState, fpr[7].l.upper) },
    { "f15", offsetof(CPUSPARCState, fpr[7].l.lower) },
    { "f16", offsetof(CPUSPARCState, fpr[8].l.upper) },
    { "f17", offsetof(CPUSPARCState, fpr[8].l.lower) },
    { "f18", offsetof(CPUSPARCState, fpr[9].l.upper) },
    { "f19", offsetof(CPUSPARCState, fpr[9].l.lower) },
    { "f20", offsetof(CPUSPARCState, fpr[10].l.upper) },
    { "f21", offsetof(CPUSPARCState, fpr[10].l.lower) },
    { "f22", offsetof(CPUSPARCState, fpr[11].l.upper) },
    { "f23", offsetof(CPUSPARCState, fpr[11].l.lower) },
    { "f24", offsetof(CPUSPARCState, fpr[12].l.upper) },
    { "f25", offsetof(CPUSPARCState, fpr[12].l.lower) },
    { "f26", offsetof(CPUSPARCState, fpr[13].l.upper) },
    { "f27", offsetof(CPUSPARCState, fpr[13].l.lower) },
    { "f28", offsetof(CPUSPARCState, fpr[14].l.upper) },
    { "f29", offsetof(CPUSPARCState, fpr[14].l.lower) },
    { "f30", offsetof(CPUSPARCState, fpr[15].l.upper) },
    { "f31", offsetof(CPUSPARCState, fpr[15].l.lower) },
#ifdef TARGET_SPARC64
    { "f32", offsetof(CPUSPARCState, fpr[16]) },
    { "f34", offsetof(CPUSPARCState, fpr[17]) },
    { "f36", offsetof(CPUSPARCState, fpr[18]) },
    { "f38", offsetof(CPUSPARCState, fpr[19]) },
    { "f40", offsetof(CPUSPARCState, fpr[20]) },
    { "f42", offsetof(CPUSPARCState, fpr[21]) },
    { "f44", offsetof(CPUSPARCState, fpr[22]) },
    { "f46", offsetof(CPUSPARCState, fpr[23]) },
    { "f48", offsetof(CPUSPARCState, fpr[24]) },
    { "f50", offsetof(CPUSPARCState, fpr[25]) },
    { "f52", offsetof(CPUSPARCState, fpr[26]) },
    { "f54", offsetof(CPUSPARCState, fpr[27]) },
    { "f56", offsetof(CPUSPARCState, fpr[28]) },
    { "f58", offsetof(CPUSPARCState, fpr[29]) },
    { "f60", offsetof(CPUSPARCState, fpr[30]) },
    { "f62", offsetof(CPUSPARCState, fpr[31]) },
    { "asi", offsetof(CPUSPARCState, asi) },
    { "pstate", offsetof(CPUSPARCState, pstate) },
    { "cansave", offsetof(CPUSPARCState, cansave) },
    { "canrestore", offsetof(CPUSPARCState, canrestore) },
    { "otherwin", offsetof(CPUSPARCState, otherwin) },
    { "wstate", offsetof(CPUSPARCState, wstate) },
    { "cleanwin", offsetof(CPUSPARCState, cleanwin) },
    { "fprs", offsetof(CPUSPARCState, fprs) },
#endif
#endif
    { NULL },
};

static void GCC_FMT_ATTR(2, 3) QEMU_NORETURN
expr_error(Monitor *mon, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    monitor_vprintf(mon, fmt, ap);
    monitor_printf(mon, "\n");
    va_end(ap);
    siglongjmp(expr_env, 1);
}

/* return 0 if OK, -1 if not found */
static int get_monitor_def(target_long *pval, const char *name)
{
    const MonitorDef *md;
    void *ptr;

    for(md = monitor_defs; md->name != NULL; md++) {
        if (compare_cmd(name, md->name)) {
            if (md->get_value) {
                *pval = md->get_value(md, md->offset);
            } else {
                CPUArchState *env = mon_get_cpu();
                ptr = (uint8_t *)env + md->offset;
                switch(md->type) {
                case MD_I32:
                    *pval = *(int32_t *)ptr;
                    break;
                case MD_TLONG:
                    *pval = *(target_long *)ptr;
                    break;
                default:
                    *pval = 0;
                    break;
                }
            }
            return 0;
        }
    }
    return -1;
}

static void next(void)
{
    if (*pch != '\0') {
        pch++;
        while (qemu_isspace(*pch))
            pch++;
    }
}

static int64_t expr_sum(Monitor *mon);

static int64_t expr_unary(Monitor *mon)
{
    int64_t n;
    char *p;
    int ret;

    switch(*pch) {
    case '+':
        next();
        n = expr_unary(mon);
        break;
    case '-':
        next();
        n = -expr_unary(mon);
        break;
    case '~':
        next();
        n = ~expr_unary(mon);
        break;
    case '(':
        next();
        n = expr_sum(mon);
        if (*pch != ')') {
            expr_error(mon, "')' expected");
        }
        next();
        break;
    case '\'':
        pch++;
        if (*pch == '\0')
            expr_error(mon, "character constant expected");
        n = *pch;
        pch++;
        if (*pch != '\'')
            expr_error(mon, "missing terminating \' character");
        next();
        break;
    case '$':
        {
            char buf[128], *q;
            target_long reg=0;

            pch++;
            q = buf;
            while ((*pch >= 'a' && *pch <= 'z') ||
                   (*pch >= 'A' && *pch <= 'Z') ||
                   (*pch >= '0' && *pch <= '9') ||
                   *pch == '_' || *pch == '.') {
                if ((q - buf) < sizeof(buf) - 1)
                    *q++ = *pch;
                pch++;
            }
            while (qemu_isspace(*pch))
                pch++;
            *q = 0;
            ret = get_monitor_def(&reg, buf);
            if (ret < 0)
                expr_error(mon, "unknown register");
            n = reg;
        }
        break;
    case '\0':
        expr_error(mon, "unexpected end of expression");
        n = 0;
        break;
    default:
        errno = 0;
        n = strtoull(pch, &p, 0);
        if (errno == ERANGE) {
            expr_error(mon, "number too large");
        }
        if (pch == p) {
            expr_error(mon, "invalid char '%c' in expression", *p);
        }
        pch = p;
        while (qemu_isspace(*pch))
            pch++;
        break;
    }
    return n;
}


static int64_t expr_prod(Monitor *mon)
{
    int64_t val, val2;
    int op;

    val = expr_unary(mon);
    for(;;) {
        op = *pch;
        if (op != '*' && op != '/' && op != '%')
            break;
        next();
        val2 = expr_unary(mon);
        switch(op) {
        default:
        case '*':
            val *= val2;
            break;
        case '/':
        case '%':
            if (val2 == 0)
                expr_error(mon, "division by zero");
            if (op == '/')
                val /= val2;
            else
                val %= val2;
            break;
        }
    }
    return val;
}

static int64_t expr_logic(Monitor *mon)
{
    int64_t val, val2;
    int op;

    val = expr_prod(mon);
    for(;;) {
        op = *pch;
        if (op != '&' && op != '|' && op != '^')
            break;
        next();
        val2 = expr_prod(mon);
        switch(op) {
        default:
        case '&':
            val &= val2;
            break;
        case '|':
            val |= val2;
            break;
        case '^':
            val ^= val2;
            break;
        }
    }
    return val;
}

static int64_t expr_sum(Monitor *mon)
{
    int64_t val, val2;
    int op;

    val = expr_logic(mon);
    for(;;) {
        op = *pch;
        if (op != '+' && op != '-')
            break;
        next();
        val2 = expr_logic(mon);
        if (op == '+')
            val += val2;
        else
            val -= val2;
    }
    return val;
}

static int get_expr(Monitor *mon, int64_t *pval, const char **pp)
{
    pch = *pp;
    if (sigsetjmp(expr_env, 0)) {
        *pp = pch;
        return -1;
    }
    while (qemu_isspace(*pch))
        pch++;
    *pval = expr_sum(mon);
    *pp = pch;
    return 0;
}

static int get_double(Monitor *mon, double *pval, const char **pp)
{
    const char *p = *pp;
    char *tailp;
    double d;

    d = strtod(p, &tailp);
    if (tailp == p) {
        monitor_printf(mon, "Number expected\n");
        return -1;
    }
    if (d != d || d - d != 0) {
        /* NaN or infinity */
        monitor_printf(mon, "Bad number\n");
        return -1;
    }
    *pval = d;
    *pp = tailp;
    return 0;
}

/*
 * Store the command-name in cmdname, and return a pointer to
 * the remaining of the command string.
 */
static const char *get_command_name(const char *cmdline,
                                    char *cmdname, size_t nlen)
{
    size_t len;
    const char *p, *pstart;

    p = cmdline;
    while (qemu_isspace(*p))
        p++;
    if (*p == '\0')
        return NULL;
    pstart = p;
    while (*p != '\0' && *p != '/' && !qemu_isspace(*p))
        p++;
    len = p - pstart;
    if (len > nlen - 1)
        len = nlen - 1;
    memcpy(cmdname, pstart, len);
    cmdname[len] = '\0';
    return p;
}

/**
 * Read key of 'type' into 'key' and return the current
 * 'type' pointer.
 */
static char *key_get_info(const char *type, char **key)
{
    size_t len;
    char *p, *str;

    if (*type == ',')
        type++;

    p = strchr(type, ':');
    if (!p) {
        *key = NULL;
        return NULL;
    }
    len = p - type;

    str = g_malloc(len + 1);
    memcpy(str, type, len);
    str[len] = '\0';

    *key = str;
    return ++p;
}

static int default_fmt_format = 'x';
static int default_fmt_size = 4;

static int is_valid_option(const char *c, const char *typestr)
{
    char option[3];
  
    option[0] = '-';
    option[1] = *c;
    option[2] = '\0';
  
    typestr = strstr(typestr, option);
    return (typestr != NULL);
}

static const mon_cmd_t *search_dispatch_table(const mon_cmd_t *disp_table,
                                              const char *cmdname)
{
    const mon_cmd_t *cmd;

    for (cmd = disp_table; cmd->name != NULL; cmd++) {
        if (compare_cmd(cmdname, cmd->name)) {
            return cmd;
        }
    }

    return NULL;
}

static const mon_cmd_t *qmp_find_cmd(const char *cmdname)
{
    return search_dispatch_table(qmp_cmds, cmdname);
}

/*
 * Parse @cmdline according to command table @table.
 * If @cmdline is blank, return NULL.
 * If it can't be parsed, report to @mon, and return NULL.
 * Else, insert command arguments into @qdict, and return the command.
 * If a sub-command table exists, and if @cmdline contains an additional string
 * for a sub-command, this function will try to search the sub-command table.
 * If no additional string for a sub-command is present, this function will
 * return the command found in @table.
 * Do not assume the returned command points into @table!  It doesn't
 * when the command is a sub-command.
 */
static const mon_cmd_t *monitor_parse_command(Monitor *mon,
                                              const char *cmdline,
                                              int start,
                                              mon_cmd_t *table,
                                              QDict *qdict)
{
    const char *p, *typestr;
    int c;
    const mon_cmd_t *cmd;
    char cmdname[256];
    char buf[1024];
    char *key;

#ifdef DEBUG
    monitor_printf(mon, "command='%s', start='%d'\n", cmdline, start);
#endif

    /* extract the command name */
    p = get_command_name(cmdline + start, cmdname, sizeof(cmdname));
    if (!p)
        return NULL;

    cmd = search_dispatch_table(table, cmdname);
    if (!cmd) {
        monitor_printf(mon, "unknown command: '%.*s'\n",
                       (int)(p - cmdline), cmdline);
        return NULL;
    }

    /* filter out following useless space */
    while (qemu_isspace(*p)) {
        p++;
    }
    /* search sub command */
    if (cmd->sub_table != NULL) {
        /* check if user set additional command */
        if (*p == '\0') {
            return cmd;
        }
        return monitor_parse_command(mon, cmdline, p - cmdline,
                                     cmd->sub_table, qdict);
    }

    /* parse the parameters */
    typestr = cmd->args_type;
    for(;;) {
        typestr = key_get_info(typestr, &key);
        if (!typestr)
            break;
        c = *typestr;
        typestr++;
        switch(c) {
        case 'F':
        case 'B':
        case 's':
            {
                int ret;

                while (qemu_isspace(*p))
                    p++;
                if (*typestr == '?') {
                    typestr++;
                    if (*p == '\0') {
                        /* no optional string: NULL argument */
                        break;
                    }
                }
                ret = get_str(buf, sizeof(buf), &p);
                if (ret < 0) {
                    switch(c) {
                    case 'F':
                        monitor_printf(mon, "%s: filename expected\n",
                                       cmdname);
                        break;
                    case 'B':
                        monitor_printf(mon, "%s: block device name expected\n",
                                       cmdname);
                        break;
                    default:
                        monitor_printf(mon, "%s: string expected\n", cmdname);
                        break;
                    }
                    goto fail;
                }
                qdict_put(qdict, key, qstring_from_str(buf));
            }
            break;
        case 'O':
            {
                QemuOptsList *opts_list;
                QemuOpts *opts;

                opts_list = qemu_find_opts(key);
                if (!opts_list || opts_list->desc->name) {
                    goto bad_type;
                }
                while (qemu_isspace(*p)) {
                    p++;
                }
                if (!*p)
                    break;
                if (get_str(buf, sizeof(buf), &p) < 0) {
                    goto fail;
                }
                opts = qemu_opts_parse(opts_list, buf, 1);
                if (!opts) {
                    goto fail;
                }
                qemu_opts_to_qdict(opts, qdict);
                qemu_opts_del(opts);
            }
            break;
        case '/':
            {
                int count, format, size;

                while (qemu_isspace(*p))
                    p++;
                if (*p == '/') {
                    /* format found */
                    p++;
                    count = 1;
                    if (qemu_isdigit(*p)) {
                        count = 0;
                        while (qemu_isdigit(*p)) {
                            count = count * 10 + (*p - '0');
                            p++;
                        }
                    }
                    size = -1;
                    format = -1;
                    for(;;) {
                        switch(*p) {
                        case 'o':
                        case 'd':
                        case 'u':
                        case 'x':
                        case 'i':
                        case 'c':
                            format = *p++;
                            break;
                        case 'b':
                            size = 1;
                            p++;
                            break;
                        case 'h':
                            size = 2;
                            p++;
                            break;
                        case 'w':
                            size = 4;
                            p++;
                            break;
                        case 'g':
                        case 'L':
                            size = 8;
                            p++;
                            break;
                        default:
                            goto next;
                        }
                    }
                next:
                    if (*p != '\0' && !qemu_isspace(*p)) {
                        monitor_printf(mon, "invalid char in format: '%c'\n",
                                       *p);
                        goto fail;
                    }
                    if (format < 0)
                        format = default_fmt_format;
                    if (format != 'i') {
                        /* for 'i', not specifying a size gives -1 as size */
                        if (size < 0)
                            size = default_fmt_size;
                        default_fmt_size = size;
                    }
                    default_fmt_format = format;
                } else {
                    count = 1;
                    format = default_fmt_format;
                    if (format != 'i') {
                        size = default_fmt_size;
                    } else {
                        size = -1;
                    }
                }
                qdict_put(qdict, "count", qint_from_int(count));
                qdict_put(qdict, "format", qint_from_int(format));
                qdict_put(qdict, "size", qint_from_int(size));
            }
            break;
        case 'i':
        case 'l':
        case 'M':
            {
                int64_t val;

                while (qemu_isspace(*p))
                    p++;
                if (*typestr == '?' || *typestr == '.') {
                    if (*typestr == '?') {
                        if (*p == '\0') {
                            typestr++;
                            break;
                        }
                    } else {
                        if (*p == '.') {
                            p++;
                            while (qemu_isspace(*p))
                                p++;
                        } else {
                            typestr++;
                            break;
                        }
                    }
                    typestr++;
                }
                if (get_expr(mon, &val, &p))
                    goto fail;
                /* Check if 'i' is greater than 32-bit */
                if ((c == 'i') && ((val >> 32) & 0xffffffff)) {
                    monitor_printf(mon, "\'%s\' has failed: ", cmdname);
                    monitor_printf(mon, "integer is for 32-bit values\n");
                    goto fail;
                } else if (c == 'M') {
                    if (val < 0) {
                        monitor_printf(mon, "enter a positive value\n");
                        goto fail;
                    }
                    val <<= 20;
                }
                qdict_put(qdict, key, qint_from_int(val));
            }
            break;
        case 'o':
            {
                int64_t val;
                char *end;

                while (qemu_isspace(*p)) {
                    p++;
                }
                if (*typestr == '?') {
                    typestr++;
                    if (*p == '\0') {
                        break;
                    }
                }
                val = strtosz(p, &end);
                if (val < 0) {
                    monitor_printf(mon, "invalid size\n");
                    goto fail;
                }
                qdict_put(qdict, key, qint_from_int(val));
                p = end;
            }
            break;
        case 'T':
            {
                double val;

                while (qemu_isspace(*p))
                    p++;
                if (*typestr == '?') {
                    typestr++;
                    if (*p == '\0') {
                        break;
                    }
                }
                if (get_double(mon, &val, &p) < 0) {
                    goto fail;
                }
                if (p[0] && p[1] == 's') {
                    switch (*p) {
                    case 'm':
                        val /= 1e3; p += 2; break;
                    case 'u':
                        val /= 1e6; p += 2; break;
                    case 'n':
                        val /= 1e9; p += 2; break;
                    }
                }
                if (*p && !qemu_isspace(*p)) {
                    monitor_printf(mon, "Unknown unit suffix\n");
                    goto fail;
                }
                qdict_put(qdict, key, qfloat_from_double(val));
            }
            break;
        case 'b':
            {
                const char *beg;
                int val;

                while (qemu_isspace(*p)) {
                    p++;
                }
                beg = p;
                while (qemu_isgraph(*p)) {
                    p++;
                }
                if (p - beg == 2 && !memcmp(beg, "on", p - beg)) {
                    val = 1;
                } else if (p - beg == 3 && !memcmp(beg, "off", p - beg)) {
                    val = 0;
                } else {
                    monitor_printf(mon, "Expected 'on' or 'off'\n");
                    goto fail;
                }
                qdict_put(qdict, key, qbool_from_int(val));
            }
            break;
        case '-':
            {
                const char *tmp = p;
                int skip_key = 0;
                /* option */

                c = *typestr++;
                if (c == '\0')
                    goto bad_type;
                while (qemu_isspace(*p))
                    p++;
                if (*p == '-') {
                    p++;
                    if(c != *p) {
                        if(!is_valid_option(p, typestr)) {
                  
                            monitor_printf(mon, "%s: unsupported option -%c\n",
                                           cmdname, *p);
                            goto fail;
                        } else {
                            skip_key = 1;
                        }
                    }
                    if(skip_key) {
                        p = tmp;
                    } else {
                        /* has option */
                        p++;
                        qdict_put(qdict, key, qbool_from_int(1));
                    }
                }
            }
            break;
        case 'S':
            {
                /* package all remaining string */
                int len;

                while (qemu_isspace(*p)) {
                    p++;
                }
                if (*typestr == '?') {
                    typestr++;
                    if (*p == '\0') {
                        /* no remaining string: NULL argument */
                        break;
                    }
                }
                len = strlen(p);
                if (len <= 0) {
                    monitor_printf(mon, "%s: string expected\n",
                                   cmdname);
                    break;
                }
                qdict_put(qdict, key, qstring_from_str(p));
                p += len;
            }
            break;
        default:
        bad_type:
            monitor_printf(mon, "%s: unknown type '%c'\n", cmdname, c);
            goto fail;
        }
        g_free(key);
        key = NULL;
    }
    /* check that all arguments were parsed */
    while (qemu_isspace(*p))
        p++;
    if (*p != '\0') {
        monitor_printf(mon, "%s: extraneous characters at the end of line\n",
                       cmdname);
        goto fail;
    }

    return cmd;

fail:
    g_free(key);
    return NULL;
}

void monitor_set_error(Monitor *mon, QError *qerror)
{
    /* report only the first error */
    if (!mon->error) {
        mon->error = qerror;
    } else {
        QDECREF(qerror);
    }
}

static void handler_audit(Monitor *mon, const mon_cmd_t *cmd, int ret)
{
    if (ret && !monitor_has_error(mon)) {
        /*
         * If it returns failure, it must have passed on error.
         *
         * Action: Report an internal error to the client if in QMP.
         */
        qerror_report(QERR_UNDEFINED_ERROR);
    }
}

static void handle_user_command(Monitor *mon, const char *cmdline)
{
    QDict *qdict;
    const mon_cmd_t *cmd;

    qdict = qdict_new();

    cmd = monitor_parse_command(mon, cmdline, 0, mon->cmd_table, qdict);
    if (!cmd)
        goto out;

    if (handler_is_async(cmd)) {
        user_async_cmd_handler(mon, cmd, qdict);
    } else if (handler_is_qobject(cmd)) {
        QObject *data = NULL;

        /* XXX: ignores the error code */
        cmd->mhandler.cmd_new(mon, qdict, &data);
        assert(!monitor_has_error(mon));
        if (data) {
            cmd->user_print(mon, data);
            qobject_decref(data);
        }
    } else {
        cmd->mhandler.cmd(mon, qdict);
    }

out:
    QDECREF(qdict);
}

static void cmd_completion(Monitor *mon, const char *name, const char *list)
{
    const char *p, *pstart;
    char cmd[128];
    int len;

    p = list;
    for(;;) {
        pstart = p;
        p = strchr(p, '|');
        if (!p)
            p = pstart + strlen(pstart);
        len = p - pstart;
        if (len > sizeof(cmd) - 2)
            len = sizeof(cmd) - 2;
        memcpy(cmd, pstart, len);
        cmd[len] = '\0';
        if (name[0] == '\0' || !strncmp(name, cmd, strlen(name))) {
            readline_add_completion(mon->rs, cmd);
        }
        if (*p == '\0')
            break;
        p++;
    }
}

static void file_completion(Monitor *mon, const char *input)
{
    DIR *ffs;
    struct dirent *d;
    char path[1024];
    char file[1024], file_prefix[1024];
    int input_path_len;
    const char *p;

    p = strrchr(input, '/');
    if (!p) {
        input_path_len = 0;
        pstrcpy(file_prefix, sizeof(file_prefix), input);
        pstrcpy(path, sizeof(path), ".");
    } else {
        input_path_len = p - input + 1;
        memcpy(path, input, input_path_len);
        if (input_path_len > sizeof(path) - 1)
            input_path_len = sizeof(path) - 1;
        path[input_path_len] = '\0';
        pstrcpy(file_prefix, sizeof(file_prefix), p + 1);
    }
#ifdef DEBUG_COMPLETION
    monitor_printf(mon, "input='%s' path='%s' prefix='%s'\n",
                   input, path, file_prefix);
#endif
    ffs = opendir(path);
    if (!ffs)
        return;
    for(;;) {
        struct stat sb;
        d = readdir(ffs);
        if (!d)
            break;

        if (strcmp(d->d_name, ".") == 0 || strcmp(d->d_name, "..") == 0) {
            continue;
        }

        if (strstart(d->d_name, file_prefix, NULL)) {
            memcpy(file, input, input_path_len);
            if (input_path_len < sizeof(file))
                pstrcpy(file + input_path_len, sizeof(file) - input_path_len,
                        d->d_name);
            /* stat the file to find out if it's a directory.
             * In that case add a slash to speed up typing long paths
             */
            if (stat(file, &sb) == 0 && S_ISDIR(sb.st_mode)) {
                pstrcat(file, sizeof(file), "/");
            }
            readline_add_completion(mon->rs, file);
        }
    }
    closedir(ffs);
}

typedef struct MonitorBlockComplete {
    Monitor *mon;
    const char *input;
} MonitorBlockComplete;

static void block_completion_it(void *opaque, BlockDriverState *bs)
{
    const char *name = bdrv_get_device_name(bs);
    MonitorBlockComplete *mbc = opaque;
    Monitor *mon = mbc->mon;
    const char *input = mbc->input;

    if (input[0] == '\0' ||
        !strncmp(name, (char *)input, strlen(input))) {
        readline_add_completion(mon->rs, name);
    }
}

static const char *next_arg_type(const char *typestr)
{
    const char *p = strchr(typestr, ':');
    return (p != NULL ? ++p : typestr);
}

static void add_completion_option(ReadLineState *rs, const char *str,
                                  const char *option)
{
    if (!str || !option) {
        return;
    }
    if (!strncmp(option, str, strlen(str))) {
        readline_add_completion(rs, option);
    }
}

void chardev_add_completion(ReadLineState *rs, int nb_args, const char *str)
{
    size_t len;
    ChardevBackendInfoList *list, *start;

    if (nb_args != 2) {
        return;
    }
    len = strlen(str);
    readline_set_completion_index(rs, len);

    start = list = qmp_query_chardev_backends(NULL);
    while (list) {
        const char *chr_name = list->value->name;

        if (!strncmp(chr_name, str, len)) {
            readline_add_completion(rs, chr_name);
        }
        list = list->next;
    }
    qapi_free_ChardevBackendInfoList(start);
}

void netdev_add_completion(ReadLineState *rs, int nb_args, const char *str)
{
    size_t len;
    int i;

    if (nb_args != 2) {
        return;
    }
    len = strlen(str);
    readline_set_completion_index(rs, len);
    for (i = 0; NetClientOptionsKind_lookup[i]; i++) {
        add_completion_option(rs, str, NetClientOptionsKind_lookup[i]);
    }
}

void device_add_completion(ReadLineState *rs, int nb_args, const char *str)
{
    GSList *list, *elt;
    size_t len;

    if (nb_args != 2) {
        return;
    }

    len = strlen(str);
    readline_set_completion_index(rs, len);
    list = elt = object_class_get_list(TYPE_DEVICE, false);
    while (elt) {
        const char *name;
        DeviceClass *dc = OBJECT_CLASS_CHECK(DeviceClass, elt->data,
                                             TYPE_DEVICE);
        name = object_class_get_name(OBJECT_CLASS(dc));

        if (!dc->cannot_instantiate_with_device_add_yet
            && !strncmp(name, str, len)) {
            readline_add_completion(rs, name);
        }
        elt = elt->next;
    }
    g_slist_free(list);
}

void object_add_completion(ReadLineState *rs, int nb_args, const char *str)
{
    GSList *list, *elt;
    size_t len;

    if (nb_args != 2) {
        return;
    }

    len = strlen(str);
    readline_set_completion_index(rs, len);
    list = elt = object_class_get_list(TYPE_USER_CREATABLE, false);
    while (elt) {
        const char *name;

        name = object_class_get_name(OBJECT_CLASS(elt->data));
        if (!strncmp(name, str, len) && strcmp(name, TYPE_USER_CREATABLE)) {
            readline_add_completion(rs, name);
        }
        elt = elt->next;
    }
    g_slist_free(list);
}

static void device_del_bus_completion(ReadLineState *rs,  BusState *bus,
                                      const char *str, size_t len)
{
    BusChild *kid;

    QTAILQ_FOREACH(kid, &bus->children, sibling) {
        DeviceState *dev = kid->child;
        BusState *dev_child;

        if (dev->id && !strncmp(str, dev->id, len)) {
            readline_add_completion(rs, dev->id);
        }

        QLIST_FOREACH(dev_child, &dev->child_bus, sibling) {
            device_del_bus_completion(rs, dev_child, str, len);
        }
    }
}

void chardev_remove_completion(ReadLineState *rs, int nb_args, const char *str)
{
    size_t len;
    ChardevInfoList *list, *start;

    if (nb_args != 2) {
        return;
    }
    len = strlen(str);
    readline_set_completion_index(rs, len);

    start = list = qmp_query_chardev(NULL);
    while (list) {
        ChardevInfo *chr = list->value;

        if (!strncmp(chr->label, str, len)) {
            readline_add_completion(rs, chr->label);
        }
        list = list->next;
    }
    qapi_free_ChardevInfoList(start);
}

static void ringbuf_completion(ReadLineState *rs, const char *str)
{
    size_t len;
    ChardevInfoList *list, *start;

    len = strlen(str);
    readline_set_completion_index(rs, len);

    start = list = qmp_query_chardev(NULL);
    while (list) {
        ChardevInfo *chr_info = list->value;

        if (!strncmp(chr_info->label, str, len)) {
            CharDriverState *chr = qemu_chr_find(chr_info->label);
            if (chr && chr_is_ringbuf(chr)) {
                readline_add_completion(rs, chr_info->label);
            }
        }
        list = list->next;
    }
    qapi_free_ChardevInfoList(start);
}

void ringbuf_read_completion(ReadLineState *rs, int nb_args, const char *str)
{
    if (nb_args != 2) {
        return;
    }
    ringbuf_completion(rs, str);
}

void ringbuf_write_completion(ReadLineState *rs, int nb_args, const char *str)
{
    if (nb_args != 2) {
        return;
    }
    ringbuf_completion(rs, str);
}

void device_del_completion(ReadLineState *rs, int nb_args, const char *str)
{
    size_t len;

    if (nb_args != 2) {
        return;
    }

    len = strlen(str);
    readline_set_completion_index(rs, len);
    device_del_bus_completion(rs, sysbus_get_default(), str, len);
}

void object_del_completion(ReadLineState *rs, int nb_args, const char *str)
{
    ObjectPropertyInfoList *list, *start;
    size_t len;

    if (nb_args != 2) {
        return;
    }
    len = strlen(str);
    readline_set_completion_index(rs, len);

    start = list = qmp_qom_list("/objects", NULL);
    while (list) {
        ObjectPropertyInfo *info = list->value;

        if (!strncmp(info->type, "child<", 5)
            && !strncmp(info->name, str, len)) {
            readline_add_completion(rs, info->name);
        }
        list = list->next;
    }
    qapi_free_ObjectPropertyInfoList(start);
}

void sendkey_completion(ReadLineState *rs, int nb_args, const char *str)
{
    int i;
    char *sep;
    size_t len;

    if (nb_args != 2) {
        return;
    }
    sep = strrchr(str, '-');
    if (sep) {
        str = sep + 1;
    }
    len = strlen(str);
    readline_set_completion_index(rs, len);
    for (i = 0; i < Q_KEY_CODE_MAX; i++) {
        if (!strncmp(str, QKeyCode_lookup[i], len)) {
            readline_add_completion(rs, QKeyCode_lookup[i]);
        }
    }
}

void set_link_completion(ReadLineState *rs, int nb_args, const char *str)
{
    size_t len;

    len = strlen(str);
    readline_set_completion_index(rs, len);
    if (nb_args == 2) {
        NetClientState *ncs[255];
        int count, i;
        count = qemu_find_net_clients_except(NULL, ncs,
                                             NET_CLIENT_OPTIONS_KIND_NONE, 255);
        for (i = 0; i < count; i++) {
            const char *name = ncs[i]->name;
            if (!strncmp(str, name, len)) {
                readline_add_completion(rs, name);
            }
        }
    } else if (nb_args == 3) {
        add_completion_option(rs, str, "on");
        add_completion_option(rs, str, "off");
    }
}

void netdev_del_completion(ReadLineState *rs, int nb_args, const char *str)
{
    int len, count, i;
    NetClientState *ncs[255];

    if (nb_args != 2) {
        return;
    }

    len = strlen(str);
    readline_set_completion_index(rs, len);
    count = qemu_find_net_clients_except(NULL, ncs, NET_CLIENT_OPTIONS_KIND_NIC,
                                         255);
    for (i = 0; i < count; i++) {
        QemuOpts *opts;
        const char *name = ncs[i]->name;
        if (strncmp(str, name, len)) {
            continue;
        }
        opts = qemu_opts_find(qemu_find_opts_err("netdev", NULL), name);
        if (opts) {
            readline_add_completion(rs, name);
        }
    }
}

void watchdog_action_completion(ReadLineState *rs, int nb_args, const char *str)
{
    if (nb_args != 2) {
        return;
    }
    readline_set_completion_index(rs, strlen(str));
    add_completion_option(rs, str, "reset");
    add_completion_option(rs, str, "shutdown");
    add_completion_option(rs, str, "poweroff");
    add_completion_option(rs, str, "pause");
    add_completion_option(rs, str, "debug");
    add_completion_option(rs, str, "none");
}

void migrate_set_capability_completion(ReadLineState *rs, int nb_args,
                                       const char *str)
{
    size_t len;

    len = strlen(str);
    readline_set_completion_index(rs, len);
    if (nb_args == 2) {
        int i;
        for (i = 0; i < MIGRATION_CAPABILITY_MAX; i++) {
            const char *name = MigrationCapability_lookup[i];
            if (!strncmp(str, name, len)) {
                readline_add_completion(rs, name);
            }
        }
    } else if (nb_args == 3) {
        add_completion_option(rs, str, "on");
        add_completion_option(rs, str, "off");
    }
}

void host_net_add_completion(ReadLineState *rs, int nb_args, const char *str)
{
    int i;
    size_t len;
    if (nb_args != 2) {
        return;
    }
    len = strlen(str);
    readline_set_completion_index(rs, len);
    for (i = 0; host_net_devices[i]; i++) {
        if (!strncmp(host_net_devices[i], str, len)) {
            readline_add_completion(rs, host_net_devices[i]);
        }
    }
}

void host_net_remove_completion(ReadLineState *rs, int nb_args, const char *str)
{
    NetClientState *ncs[255];
    int count, i, len;

    len = strlen(str);
    readline_set_completion_index(rs, len);
    if (nb_args == 2) {
        count = qemu_find_net_clients_except(NULL, ncs,
                                             NET_CLIENT_OPTIONS_KIND_NONE, 255);
        for (i = 0; i < count; i++) {
            int id;
            char name[16];

            if (net_hub_id_for_client(ncs[i], &id)) {
                continue;
            }
            snprintf(name, sizeof(name), "%d", id);
            if (!strncmp(str, name, len)) {
                readline_add_completion(rs, name);
            }
        }
        return;
    } else if (nb_args == 3) {
        count = qemu_find_net_clients_except(NULL, ncs,
                                             NET_CLIENT_OPTIONS_KIND_NIC, 255);
        for (i = 0; i < count; i++) {
            const char *name;

            name = ncs[i]->name;
            if (!strncmp(str, name, len)) {
                readline_add_completion(rs, name);
            }
        }
        return;
    }
}

static void vm_completion(ReadLineState *rs, const char *str)
{
    size_t len;
    BlockDriverState *bs = NULL;

    len = strlen(str);
    readline_set_completion_index(rs, len);
    while ((bs = bdrv_next(bs))) {
        SnapshotInfoList *snapshots, *snapshot;

        if (!bdrv_can_snapshot(bs)) {
            continue;
        }
        if (bdrv_query_snapshot_info_list(bs, &snapshots, NULL)) {
            continue;
        }
        snapshot = snapshots;
        while (snapshot) {
            char *completion = snapshot->value->name;
            if (!strncmp(str, completion, len)) {
                readline_add_completion(rs, completion);
            }
            completion = snapshot->value->id;
            if (!strncmp(str, completion, len)) {
                readline_add_completion(rs, completion);
            }
            snapshot = snapshot->next;
        }
        qapi_free_SnapshotInfoList(snapshots);
    }

}

void delvm_completion(ReadLineState *rs, int nb_args, const char *str)
{
    if (nb_args == 2) {
        vm_completion(rs, str);
    }
}

void loadvm_completion(ReadLineState *rs, int nb_args, const char *str)
{
    if (nb_args == 2) {
        vm_completion(rs, str);
    }
}

static void monitor_find_completion_by_table(Monitor *mon,
                                             const mon_cmd_t *cmd_table,
                                             char **args,
                                             int nb_args)
{
    const char *cmdname;
    int i;
    const char *ptype, *str;
    const mon_cmd_t *cmd;
    MonitorBlockComplete mbs;

    if (nb_args <= 1) {
        /* command completion */
        if (nb_args == 0)
            cmdname = "";
        else
            cmdname = args[0];
        readline_set_completion_index(mon->rs, strlen(cmdname));
        for (cmd = cmd_table; cmd->name != NULL; cmd++) {
            cmd_completion(mon, cmdname, cmd->name);
        }
    } else {
        /* find the command */
        for (cmd = cmd_table; cmd->name != NULL; cmd++) {
            if (compare_cmd(args[0], cmd->name)) {
                break;
            }
        }
        if (!cmd->name) {
            return;
        }

        if (cmd->sub_table) {
            /* do the job again */
            return monitor_find_completion_by_table(mon, cmd->sub_table,
                                                    &args[1], nb_args - 1);
        }
        if (cmd->command_completion) {
            return cmd->command_completion(mon->rs, nb_args, args[nb_args - 1]);
        }

        ptype = next_arg_type(cmd->args_type);
        for(i = 0; i < nb_args - 2; i++) {
            if (*ptype != '\0') {
                ptype = next_arg_type(ptype);
                while (*ptype == '?')
                    ptype = next_arg_type(ptype);
            }
        }
        str = args[nb_args - 1];
        if (*ptype == '-' && ptype[1] != '\0') {
            ptype = next_arg_type(ptype);
        }
        switch(*ptype) {
        case 'F':
            /* file completion */
            readline_set_completion_index(mon->rs, strlen(str));
            file_completion(mon, str);
            break;
        case 'B':
            /* block device name completion */
            mbs.mon = mon;
            mbs.input = str;
            readline_set_completion_index(mon->rs, strlen(str));
            bdrv_iterate(block_completion_it, &mbs);
            break;
        case 's':
        case 'S':
            if (!strcmp(cmd->name, "help|?")) {
                monitor_find_completion_by_table(mon, cmd_table,
                                                 &args[1], nb_args - 1);
            }
            break;
        default:
            break;
        }
    }
}

static void monitor_find_completion(void *opaque,
                                    const char *cmdline)
{
    Monitor *mon = opaque;
    char *args[MAX_ARGS];
    int nb_args, len;

    /* 1. parse the cmdline */
    if (parse_cmdline(cmdline, &nb_args, args) < 0) {
        return;
    }
#ifdef DEBUG_COMPLETION
    for (i = 0; i < nb_args; i++) {
        monitor_printf(mon, "arg%d = '%s'\n", i, args[i]);
    }
#endif

    /* if the line ends with a space, it means we want to complete the
       next arg */
    len = strlen(cmdline);
    if (len > 0 && qemu_isspace(cmdline[len - 1])) {
        if (nb_args >= MAX_ARGS) {
            goto cleanup;
        }
        args[nb_args++] = g_strdup("");
    }

    /* 2. auto complete according to args */
    monitor_find_completion_by_table(mon, mon->cmd_table, args, nb_args);

cleanup:
    free_cmdline_args(args, nb_args);
}

static int monitor_can_read(void *opaque)
{
    Monitor *mon = opaque;

    return (mon->suspend_cnt == 0) ? 1 : 0;
}

static int invalid_qmp_mode(const Monitor *mon, const char *cmd_name)
{
    int is_cap = compare_cmd(cmd_name, "qmp_capabilities");
    return (qmp_cmd_mode(mon) ? is_cap : !is_cap);
}

/*
 * Argument validation rules:
 *
 * 1. The argument must exist in cmd_args qdict
 * 2. The argument type must be the expected one
 *
 * Special case: If the argument doesn't exist in cmd_args and
 *               the QMP_ACCEPT_UNKNOWNS flag is set, then the
 *               checking is skipped for it.
 */
static int check_client_args_type(const QDict *client_args,
                                  const QDict *cmd_args, int flags)
{
    const QDictEntry *ent;

    for (ent = qdict_first(client_args); ent;ent = qdict_next(client_args,ent)){
        QObject *obj;
        QString *arg_type;
        const QObject *client_arg = qdict_entry_value(ent);
        const char *client_arg_name = qdict_entry_key(ent);

        obj = qdict_get(cmd_args, client_arg_name);
        if (!obj) {
            if (flags & QMP_ACCEPT_UNKNOWNS) {
                /* handler accepts unknowns */
                continue;
            }
            /* client arg doesn't exist */
            qerror_report(QERR_INVALID_PARAMETER, client_arg_name);
            return -1;
        }

        arg_type = qobject_to_qstring(obj);
        assert(arg_type != NULL);

        /* check if argument's type is correct */
        switch (qstring_get_str(arg_type)[0]) {
        case 'F':
        case 'B':
        case 's':
            if (qobject_type(client_arg) != QTYPE_QSTRING) {
                qerror_report(QERR_INVALID_PARAMETER_TYPE, client_arg_name,
                              "string");
                return -1;
            }
        break;
        case 'i':
        case 'l':
        case 'M':
        case 'o':
            if (qobject_type(client_arg) != QTYPE_QINT) {
                qerror_report(QERR_INVALID_PARAMETER_TYPE, client_arg_name,
                              "int");
                return -1; 
            }
            break;
        case 'T':
            if (qobject_type(client_arg) != QTYPE_QINT &&
                qobject_type(client_arg) != QTYPE_QFLOAT) {
                qerror_report(QERR_INVALID_PARAMETER_TYPE, client_arg_name,
                              "number");
               return -1; 
            }
            break;
        case 'b':
        case '-':
            if (qobject_type(client_arg) != QTYPE_QBOOL) {
                qerror_report(QERR_INVALID_PARAMETER_TYPE, client_arg_name,
                              "bool");
               return -1; 
            }
            break;
        case 'O':
            assert(flags & QMP_ACCEPT_UNKNOWNS);
            break;
        case 'q':
            /* Any QObject can be passed.  */
            break;
        case '/':
        case '.':
            /*
             * These types are not supported by QMP and thus are not
             * handled here. Fall through.
             */
        default:
            abort();
        }
    }

    return 0;
}

/*
 * - Check if the client has passed all mandatory args
 * - Set special flags for argument validation
 */
static int check_mandatory_args(const QDict *cmd_args,
                                const QDict *client_args, int *flags)
{
    const QDictEntry *ent;

    for (ent = qdict_first(cmd_args); ent; ent = qdict_next(cmd_args, ent)) {
        const char *cmd_arg_name = qdict_entry_key(ent);
        QString *type = qobject_to_qstring(qdict_entry_value(ent));
        assert(type != NULL);

        if (qstring_get_str(type)[0] == 'O') {
            assert((*flags & QMP_ACCEPT_UNKNOWNS) == 0);
            *flags |= QMP_ACCEPT_UNKNOWNS;
        } else if (qstring_get_str(type)[0] != '-' &&
                   qstring_get_str(type)[1] != '?' &&
                   !qdict_haskey(client_args, cmd_arg_name)) {
            qerror_report(QERR_MISSING_PARAMETER, cmd_arg_name);
            return -1;
        }
    }

    return 0;
}

static QDict *qdict_from_args_type(const char *args_type)
{
    int i;
    QDict *qdict;
    QString *key, *type, *cur_qs;

    assert(args_type != NULL);

    qdict = qdict_new();

    if (args_type == NULL || args_type[0] == '\0') {
        /* no args, empty qdict */
        goto out;
    }

    key = qstring_new();
    type = qstring_new();

    cur_qs = key;

    for (i = 0;; i++) {
        switch (args_type[i]) {
            case ',':
            case '\0':
                qdict_put(qdict, qstring_get_str(key), type);
                QDECREF(key);
                if (args_type[i] == '\0') {
                    goto out;
                }
                type = qstring_new(); /* qdict has ref */
                cur_qs = key = qstring_new();
                break;
            case ':':
                cur_qs = type;
                break;
            default:
                qstring_append_chr(cur_qs, args_type[i]);
                break;
        }
    }

out:
    return qdict;
}

/*
 * Client argument checking rules:
 *
 * 1. Client must provide all mandatory arguments
 * 2. Each argument provided by the client must be expected
 * 3. Each argument provided by the client must have the type expected
 *    by the command
 */
static int qmp_check_client_args(const mon_cmd_t *cmd, QDict *client_args)
{
    int flags, err;
    QDict *cmd_args;

    cmd_args = qdict_from_args_type(cmd->args_type);

    flags = 0;
    err = check_mandatory_args(cmd_args, client_args, &flags);
    if (err) {
        goto out;
    }

    err = check_client_args_type(client_args, cmd_args, flags);

out:
    QDECREF(cmd_args);
    return err;
}

/*
 * Input object checking rules
 *
 * 1. Input object must be a dict
 * 2. The "execute" key must exist
 * 3. The "execute" key must be a string
 * 4. If the "arguments" key exists, it must be a dict
 * 5. If the "id" key exists, it can be anything (ie. json-value)
 * 6. Any argument not listed above is considered invalid
 */
static QDict *qmp_check_input_obj(QObject *input_obj)
{
    const QDictEntry *ent;
    int has_exec_key = 0;
    QDict *input_dict;

    if (qobject_type(input_obj) != QTYPE_QDICT) {
        qerror_report(QERR_QMP_BAD_INPUT_OBJECT, "object");
        return NULL;
    }

    input_dict = qobject_to_qdict(input_obj);

    for (ent = qdict_first(input_dict); ent; ent = qdict_next(input_dict, ent)){
        const char *arg_name = qdict_entry_key(ent);
        const QObject *arg_obj = qdict_entry_value(ent);

        if (!strcmp(arg_name, "execute")) {
            if (qobject_type(arg_obj) != QTYPE_QSTRING) {
                qerror_report(QERR_QMP_BAD_INPUT_OBJECT_MEMBER, "execute",
                              "string");
                return NULL;
            }
            has_exec_key = 1;
        } else if (!strcmp(arg_name, "arguments")) {
            if (qobject_type(arg_obj) != QTYPE_QDICT) {
                qerror_report(QERR_QMP_BAD_INPUT_OBJECT_MEMBER, "arguments",
                              "object");
                return NULL;
            }
        } else if (!strcmp(arg_name, "id")) {
            /* FIXME: check duplicated IDs for async commands */
        } else {
            qerror_report(QERR_QMP_EXTRA_MEMBER, arg_name);
            return NULL;
        }
    }

    if (!has_exec_key) {
        qerror_report(QERR_QMP_BAD_INPUT_OBJECT, "execute");
        return NULL;
    }

    return input_dict;
}

static void qmp_call_cmd(Monitor *mon, const mon_cmd_t *cmd,
                         const QDict *params)
{
    int ret;
    QObject *data = NULL;

    ret = cmd->mhandler.cmd_new(mon, params, &data);
    handler_audit(mon, cmd, ret);
    monitor_protocol_emitter(mon, data);
    qobject_decref(data);
}

static void handle_qmp_command(JSONMessageParser *parser, QList *tokens)
{
    int err;
    QObject *obj;
    QDict *input, *args;
    const mon_cmd_t *cmd;
    const char *cmd_name;
    Monitor *mon = cur_mon;

    args = input = NULL;

    obj = json_parser_parse(tokens, NULL);
    if (!obj) {
        // FIXME: should be triggered in json_parser_parse()
        qerror_report(QERR_JSON_PARSING);
        goto err_out;
    }

    input = qmp_check_input_obj(obj);
    if (!input) {
        qobject_decref(obj);
        goto err_out;
    }

    mon->mc->id = qdict_get(input, "id");
    qobject_incref(mon->mc->id);

    cmd_name = qdict_get_str(input, "execute");
    trace_handle_qmp_command(mon, cmd_name);
    if (invalid_qmp_mode(mon, cmd_name)) {
        qerror_report(QERR_COMMAND_NOT_FOUND, cmd_name);
        goto err_out;
    }

    cmd = qmp_find_cmd(cmd_name);
    if (!cmd) {
        qerror_report(QERR_COMMAND_NOT_FOUND, cmd_name);
        goto err_out;
    }

    obj = qdict_get(input, "arguments");
    if (!obj) {
        args = qdict_new();
    } else {
        args = qobject_to_qdict(obj);
        QINCREF(args);
    }

    err = qmp_check_client_args(cmd, args);
    if (err < 0) {
        goto err_out;
    }

    if (handler_is_async(cmd)) {
        err = qmp_async_cmd_handler(mon, cmd, args);
        if (err) {
            /* emit the error response */
            goto err_out;
        }
    } else {
        qmp_call_cmd(mon, cmd, args);
    }

    goto out;

err_out:
    monitor_protocol_emitter(mon, NULL);
out:
    QDECREF(input);
    QDECREF(args);
}

/**
 * monitor_control_read(): Read and handle QMP input
 */
static void monitor_control_read(void *opaque, const uint8_t *buf, int size)
{
    Monitor *old_mon = cur_mon;

    cur_mon = opaque;

    json_message_parser_feed(&cur_mon->mc->parser, (const char *) buf, size);

    cur_mon = old_mon;
}

static void monitor_read(void *opaque, const uint8_t *buf, int size)
{
    Monitor *old_mon = cur_mon;
    int i;

    cur_mon = opaque;

    if (cur_mon->rs) {
        for (i = 0; i < size; i++)
            readline_handle_byte(cur_mon->rs, buf[i]);
    } else {
        if (size == 0 || buf[size - 1] != 0)
            monitor_printf(cur_mon, "corrupted command\n");
        else
            handle_user_command(cur_mon, (char *)buf);
    }

    cur_mon = old_mon;
}

static void monitor_command_cb(void *opaque, const char *cmdline,
                               void *readline_opaque)
{
    Monitor *mon = opaque;

    monitor_suspend(mon);
    handle_user_command(mon, cmdline);
    monitor_resume(mon);
}

int monitor_suspend(Monitor *mon)
{
    if (!mon->rs)
        return -ENOTTY;
    mon->suspend_cnt++;
    return 0;
}

void monitor_resume(Monitor *mon)
{
    if (!mon->rs)
        return;
    if (--mon->suspend_cnt == 0)
        readline_show_prompt(mon->rs);
}

static QObject *get_qmp_greeting(void)
{
    QObject *ver = NULL;

    qmp_marshal_input_query_version(NULL, NULL, &ver);
    return qobject_from_jsonf("{'QMP':{'version': %p,'capabilities': []}}",ver);
}

/**
 * monitor_control_event(): Print QMP gretting
 */
static void monitor_control_event(void *opaque, int event)
{
    QObject *data;
    Monitor *mon = opaque;

    switch (event) {
    case CHR_EVENT_OPENED:
        mon->mc->command_mode = 0;
        data = get_qmp_greeting();
        monitor_json_emitter(mon, data);
        qobject_decref(data);
        mon_refcount++;
        break;
    case CHR_EVENT_CLOSED:
        json_message_parser_destroy(&mon->mc->parser);
        json_message_parser_init(&mon->mc->parser, handle_qmp_command);
        mon_refcount--;
        monitor_fdsets_cleanup();
        break;
    }
}

static void monitor_event(void *opaque, int event)
{
    Monitor *mon = opaque;

    switch (event) {
    case CHR_EVENT_MUX_IN:
        qemu_mutex_lock(&mon->out_lock);
        mon->mux_out = 0;
        qemu_mutex_unlock(&mon->out_lock);
        if (mon->reset_seen) {
            readline_restart(mon->rs);
            monitor_resume(mon);
            monitor_flush(mon);
        } else {
            mon->suspend_cnt = 0;
        }
        break;

    case CHR_EVENT_MUX_OUT:
        if (mon->reset_seen) {
            if (mon->suspend_cnt == 0) {
                monitor_printf(mon, "\n");
            }
            monitor_flush(mon);
            monitor_suspend(mon);
        } else {
            mon->suspend_cnt++;
        }
        qemu_mutex_lock(&mon->out_lock);
        mon->mux_out = 1;
        qemu_mutex_unlock(&mon->out_lock);
        break;

    case CHR_EVENT_OPENED:
        monitor_printf(mon, "QEMU %s monitor - type 'help' for more "
                       "information\n", QEMU_VERSION);
        if (!mon->mux_out) {
            readline_show_prompt(mon->rs);
        }
        mon->reset_seen = 1;
        mon_refcount++;
        break;

    case CHR_EVENT_CLOSED:
        mon_refcount--;
        monitor_fdsets_cleanup();
        break;
    }
}

static int
compare_mon_cmd(const void *a, const void *b)
{
    return strcmp(((const mon_cmd_t *)a)->name,
            ((const mon_cmd_t *)b)->name);
}

static void sortcmdlist(void)
{
    int array_num;
    int elem_size = sizeof(mon_cmd_t);

    array_num = sizeof(mon_cmds)/elem_size-1;
    qsort((void *)mon_cmds, array_num, elem_size, compare_mon_cmd);

    array_num = sizeof(info_cmds)/elem_size-1;
    qsort((void *)info_cmds, array_num, elem_size, compare_mon_cmd);
}


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 8
 * End:
 */

/* These functions just adapt the readline interface in a typesafe way.  We
 * could cast function pointers but that discards compiler checks.
 */
static void GCC_FMT_ATTR(2, 3) monitor_readline_printf(void *opaque,
                                                       const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    monitor_vprintf(opaque, fmt, ap);
    va_end(ap);
}

static void monitor_readline_flush(void *opaque)
{
    monitor_flush(opaque);
}

static void __attribute__((constructor)) monitor_lock_init(void)
{
    qemu_mutex_init(&monitor_lock);
}

void monitor_init(CharDriverState *chr, int flags)
{
    static int is_first_init = 1;
    Monitor *mon;

    if (is_first_init) {
        monitor_qapi_event_init();
        sortcmdlist();
        is_first_init = 0;
    }

    mon = g_malloc(sizeof(*mon));
    monitor_data_init(mon);

    mon->chr = chr;
    mon->flags = flags;
    if (flags & MONITOR_USE_READLINE) {
        mon->rs = readline_init(monitor_readline_printf,
                                monitor_readline_flush,
                                mon,
                                monitor_find_completion);
        monitor_read_command(mon, 0);
    }

    if (monitor_ctrl_mode(mon)) {
        mon->mc = g_malloc0(sizeof(MonitorControl));
        /* Control mode requires special handlers */
        qemu_chr_add_handlers(chr, monitor_can_read, monitor_control_read,
                              monitor_control_event, mon);
        qemu_chr_fe_set_echo(chr, true);

        json_message_parser_init(&mon->mc->parser, handle_qmp_command);
    } else {
        qemu_chr_add_handlers(chr, monitor_can_read, monitor_read,
                              monitor_event, mon);
    }

    qemu_mutex_lock(&monitor_lock);
    QLIST_INSERT_HEAD(&mon_list, mon, entry);
    qemu_mutex_unlock(&monitor_lock);

    if (!default_mon || (flags & MONITOR_IS_DEFAULT))
        default_mon = mon;
}

static void bdrv_password_cb(void *opaque, const char *password,
                             void *readline_opaque)
{
    Monitor *mon = opaque;
    BlockDriverState *bs = readline_opaque;
    int ret = 0;

    if (bdrv_set_key(bs, password) != 0) {
        monitor_printf(mon, "invalid password\n");
        ret = -EPERM;
    }
    if (mon->password_completion_cb)
        mon->password_completion_cb(mon->password_opaque, ret);

    monitor_read_command(mon, 1);
}

ReadLineState *monitor_get_rs(Monitor *mon)
{
    return mon->rs;
}

int monitor_read_bdrv_key_start(Monitor *mon, BlockDriverState *bs,
                                BlockDriverCompletionFunc *completion_cb,
                                void *opaque)
{
    int err;

    if (!bdrv_key_required(bs)) {
        if (completion_cb)
            completion_cb(opaque, 0);
        return 0;
    }

    if (monitor_ctrl_mode(mon)) {
        qerror_report(QERR_DEVICE_ENCRYPTED, bdrv_get_device_name(bs),
                      bdrv_get_encrypted_filename(bs));
        return -1;
    }

    monitor_printf(mon, "%s (%s) is encrypted.\n", bdrv_get_device_name(bs),
                   bdrv_get_encrypted_filename(bs));

    mon->password_completion_cb = completion_cb;
    mon->password_opaque = opaque;

    err = monitor_read_password(mon, bdrv_password_cb, bs);

    if (err && completion_cb)
        completion_cb(opaque, err);

    return err;
}

int monitor_read_block_device_key(Monitor *mon, const char *device,
                                  BlockDriverCompletionFunc *completion_cb,
                                  void *opaque)
{
    BlockDriverState *bs;

    bs = bdrv_find(device);
    if (!bs) {
        monitor_printf(mon, "Device not found %s\n", device);
        return -1;
    }

    return monitor_read_bdrv_key_start(mon, bs, completion_cb, opaque);
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
            .name = "default",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "pretty",
            .type = QEMU_OPT_BOOL,
        },
        { /* end of list */ }
    },
};

#ifndef TARGET_I386
void qmp_rtc_reset_reinjection(Error **errp)
{
    error_set(errp, QERR_FEATURE_DISABLED, "rtc-reset-reinjection");
}
#endif
