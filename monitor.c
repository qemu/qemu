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
#include <dirent.h>
#include "qemu-common.h"
#include "cpu.h"
#include "hw/hw.h"
#include "monitor/qdev.h"
#include "hw/usb.h"
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
#include "sysemu/numa.h"
#include "monitor/monitor.h"
#include "qemu/readline.h"
#include "ui/console.h"
#include "ui/input.h"
#include "sysemu/blockdev.h"
#include "sysemu/block-backend.h"
#include "audio/audio.h"
#include "disas/disas.h"
#include "sysemu/balloon.h"
#include "qemu/timer.h"
#include "migration/migration.h"
#include "sysemu/kvm.h"
#include "qemu/acl.h"
#include "sysemu/tpm.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qmp/qint.h"
#include "qapi/qmp/qfloat.h"
#include "qapi/qmp/qlist.h"
#include "qapi/qmp/qbool.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/json-streamer.h"
#include "qapi/qmp/json-parser.h"
#include <qom/object_interfaces.h>
#include "cpu.h"
#include "trace.h"
#include "trace/control.h"
#include "monitor/hmp-target.h"
#ifdef CONFIG_TRACE_SIMPLE
#include "trace/simple.h"
#endif
#include "exec/memory.h"
#include "exec/exec-all.h"
#include "qemu/log.h"
#include "qmp-commands.h"
#include "hmp.h"
#include "qemu/thread.h"
#include "block/qapi.h"
#include "qapi/qmp-event.h"
#include "qapi-event.h"
#include "qmp-introspect.h"
#include "sysemu/block-backend.h"
#include "sysemu/qtest.h"
#include "qemu/cutils.h"

/* for hmp_info_irq/pic */
#if defined(TARGET_SPARC)
#include "hw/sparc/sun4m.h"
#endif
#include "hw/lm32/lm32_pic.h"

#if defined(TARGET_S390X)
#include "hw/s390x/storage-keys.h"
#endif

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

typedef struct mon_cmd_t {
    const char *name;
    const char *args_type;
    const char *params;
    const char *help;
    union {
        void (*cmd)(Monitor *mon, const QDict *qdict);
        void (*cmd_new)(QDict *params, QObject **ret_data, Error **errp);
    } mhandler;
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

typedef struct {
    QObject *id;
    JSONMessageParser parser;
    /*
     * When a client connects, we're in capabilities negotiation mode.
     * When command qmp_capabilities succeeds, we go into command
     * mode.
     */
    bool in_command_mode;       /* are we in command mode? */
} MonitorQMP;

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
    MonitorQMP qmp;
    CPUState *mon_cpu;
    BlockCompletionFunc *password_completion_cb;
    void *password_opaque;
    mon_cmd_t *cmd_table;
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

static QEMUClockType event_clock_type = QEMU_CLOCK_REALTIME;

static void monitor_command_cb(void *opaque, const char *cmdline,
                               void *readline_opaque);

/**
 * Is @mon a QMP monitor?
 */
static inline bool monitor_is_qmp(const Monitor *mon)
{
    return (mon->flags & MONITOR_USE_CONTROL);
}

/**
 * Is the current monitor, if any, a QMP monitor?
 */
bool monitor_cur_is_qmp(void)
{
    return cur_mon && monitor_is_qmp(cur_mon);
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
    if (mon->rs) {
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
            mon->out_watch = qemu_chr_fe_add_watch(mon->chr, G_IO_OUT|G_IO_HUP,
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

    if (monitor_is_qmp(mon)) {
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

int monitor_fprintf(FILE *stream, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    monitor_vprintf((Monitor *)stream, fmt, ap);
    va_end(ap);
    return 0;
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

static QDict *build_qmp_error_dict(Error *err)
{
    QObject *obj;

    obj = qobject_from_jsonf("{ 'error': { 'class': %s, 'desc': %s } }",
                             QapiErrorClass_lookup[error_get_class(err)],
                             error_get_pretty(err));

    return qobject_to_qdict(obj);
}

static void monitor_protocol_emitter(Monitor *mon, QObject *data,
                                     Error *err)
{
    QDict *qmp;

    trace_monitor_protocol_emitter(mon);

    if (!err) {
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
        qmp = build_qmp_error_dict(err);
    }

    if (mon->qmp.id) {
        qdict_put_obj(qmp, "id", mon->qmp.id);
        mon->qmp.id = NULL;
    }

    monitor_json_emitter(mon, QOBJECT(qmp));
    QDECREF(qmp);
}


static MonitorQAPIEventConf monitor_qapi_event_conf[QAPI_EVENT__MAX] = {
    /* Limit guest-triggerable events to 1 per second */
    [QAPI_EVENT_RTC_CHANGE]        = { 1000 * SCALE_MS },
    [QAPI_EVENT_WATCHDOG]          = { 1000 * SCALE_MS },
    [QAPI_EVENT_BALLOON_CHANGE]    = { 1000 * SCALE_MS },
    [QAPI_EVENT_QUORUM_REPORT_BAD] = { 1000 * SCALE_MS },
    [QAPI_EVENT_QUORUM_FAILURE]    = { 1000 * SCALE_MS },
    [QAPI_EVENT_VSERPORT_CHANGE]   = { 1000 * SCALE_MS },
};

GHashTable *monitor_qapi_event_state;

/*
 * Emits the event to every monitor instance, @event is only used for trace
 * Called with monitor_lock held.
 */
static void monitor_qapi_event_emit(QAPIEvent event, QDict *qdict)
{
    Monitor *mon;

    trace_monitor_protocol_event_emit(event, qdict);
    QLIST_FOREACH(mon, &mon_list, entry) {
        if (monitor_is_qmp(mon) && mon->qmp.in_command_mode) {
            monitor_json_emitter(mon, QOBJECT(qdict));
        }
    }
}

static void monitor_qapi_event_handler(void *opaque);

/*
 * Queue a new event for emission to Monitor instances,
 * applying any rate limiting if required.
 */
static void
monitor_qapi_event_queue(QAPIEvent event, QDict *qdict, Error **errp)
{
    MonitorQAPIEventConf *evconf;
    MonitorQAPIEventState *evstate;

    assert(event < QAPI_EVENT__MAX);
    evconf = &monitor_qapi_event_conf[event];
    trace_monitor_protocol_event_queue(event, qdict, evconf->rate);

    qemu_mutex_lock(&monitor_lock);

    if (!evconf->rate) {
        /* Unthrottled event */
        monitor_qapi_event_emit(event, qdict);
    } else {
        QDict *data = qobject_to_qdict(qdict_get(qdict, "data"));
        MonitorQAPIEventState key = { .event = event, .data = data };

        evstate = g_hash_table_lookup(monitor_qapi_event_state, &key);
        assert(!evstate || timer_pending(evstate->timer));

        if (evstate) {
            /*
             * Timer is pending for (at least) evconf->rate ns after
             * last send.  Store event for sending when timer fires,
             * replacing a prior stored event if any.
             */
            QDECREF(evstate->qdict);
            evstate->qdict = qdict;
            QINCREF(evstate->qdict);
        } else {
            /*
             * Last send was (at least) evconf->rate ns ago.
             * Send immediately, and arm the timer to call
             * monitor_qapi_event_handler() in evconf->rate ns.  Any
             * events arriving before then will be delayed until then.
             */
            int64_t now = qemu_clock_get_ns(event_clock_type);

            monitor_qapi_event_emit(event, qdict);

            evstate = g_new(MonitorQAPIEventState, 1);
            evstate->event = event;
            evstate->data = data;
            QINCREF(evstate->data);
            evstate->qdict = NULL;
            evstate->timer = timer_new_ns(event_clock_type,
                                          monitor_qapi_event_handler,
                                          evstate);
            g_hash_table_add(monitor_qapi_event_state, evstate);
            timer_mod_ns(evstate->timer, now + evconf->rate);
        }
    }

    qemu_mutex_unlock(&monitor_lock);
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
    qemu_mutex_lock(&monitor_lock);

    if (evstate->qdict) {
        int64_t now = qemu_clock_get_ns(event_clock_type);

        monitor_qapi_event_emit(evstate->event, evstate->qdict);
        QDECREF(evstate->qdict);
        evstate->qdict = NULL;
        timer_mod_ns(evstate->timer, now + evconf->rate);
    } else {
        g_hash_table_remove(monitor_qapi_event_state, evstate);
        QDECREF(evstate->data);
        timer_free(evstate->timer);
        g_free(evstate);
    }

    qemu_mutex_unlock(&monitor_lock);
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

    return TRUE;
}

static void monitor_qapi_event_init(void)
{
    if (qtest_enabled()) {
        event_clock_type = QEMU_CLOCK_VIRTUAL;
    }

    monitor_qapi_event_state = g_hash_table_new(qapi_event_throttle_hash,
                                                qapi_event_throttle_equal);
    qmp_event_set_func_emit(monitor_qapi_event_queue);
}

static void qmp_capabilities(QDict *params, QObject **ret_data, Error **errp)
{
    cur_mon->qmp.in_command_mode = true;
}

static void handle_hmp_command(Monitor *mon, const char *cmdline);

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
            error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "cpu-index",
                       "a CPU number");
            goto out;
        }
    }

    handle_hmp_command(&hmp, command_line);
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
                    printf("unsupported escape code: '\\%c'\n", c);
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
            printf("unterminated string\n");
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

static void hmp_trace_event(Monitor *mon, const QDict *qdict)
{
    const char *tp_name = qdict_get_str(qdict, "name");
    bool new_state = qdict_get_bool(qdict, "option");
    Error *local_err = NULL;

    qmp_trace_event_set_state(tp_name, new_state, true, true, &local_err);
    if (local_err) {
        error_report_err(local_err);
    }
}

#ifdef CONFIG_TRACE_SIMPLE
static void hmp_trace_file(Monitor *mon, const QDict *qdict)
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

static void hmp_info_help(Monitor *mon, const QDict *qdict)
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

    for (e = 0 ; e < QAPI_EVENT__MAX ; e++) {
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

/*
 * Minor hack: generated marshalling suppressed for this command
 * ('gen': false in the schema) so we can parse the JSON string
 * directly into QObject instead of first parsing it with
 * visit_type_SchemaInfoList() into a SchemaInfoList, then marshal it
 * to QObject with generated output marshallers, every time.  Instead,
 * we do it in test-qmp-input-visitor.c, just to make sure
 * qapi-introspect.py's output actually conforms to the schema.
 */
static void qmp_query_qmp_schema(QDict *qdict, QObject **ret_data,
                                 Error **errp)
{
    *ret_data = qobject_from_json(qmp_schema_json);
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

CPUState *mon_get_cpu(void)
{
    if (!cur_mon->mon_cpu) {
        monitor_set_cpu(0);
    }
    cpu_synchronize_state(cur_mon->mon_cpu);
    return cur_mon->mon_cpu;
}

CPUArchState *mon_get_cpu_env(void)
{
    return mon_get_cpu()->env_ptr;
}

int monitor_get_cpu_index(void)
{
    return mon_get_cpu()->cpu_index;
}

static void hmp_info_registers(Monitor *mon, const QDict *qdict)
{
    cpu_dump_state(mon_get_cpu(), (FILE *)mon, monitor_fprintf, CPU_DUMP_FPU);
}

static void hmp_info_jit(Monitor *mon, const QDict *qdict)
{
    dump_exec_info((FILE *)mon, monitor_fprintf);
    dump_drift_info((FILE *)mon, monitor_fprintf);
}

static void hmp_info_opcount(Monitor *mon, const QDict *qdict)
{
    dump_opcount_info((FILE *)mon, monitor_fprintf);
}

static void hmp_info_history(Monitor *mon, const QDict *qdict)
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

static void hmp_info_cpustats(Monitor *mon, const QDict *qdict)
{
    cpu_dump_statistics(mon_get_cpu(), (FILE *)mon, &monitor_fprintf, 0);
}

static void hmp_info_trace_events(Monitor *mon, const QDict *qdict)
{
    TraceEventInfoList *events = qmp_trace_event_get_state("*", NULL);
    TraceEventInfoList *elem;

    for (elem = events; elem != NULL; elem = elem->next) {
        monitor_printf(mon, "%s : state %u\n",
                       elem->value->name,
                       elem->value->state == TRACE_EVENT_STATE_ENABLED ? 1 : 0);
    }
    qapi_free_TraceEventInfoList(events);
}

void qmp_client_migrate_info(const char *protocol, const char *hostname,
                             bool has_port, int64_t port,
                             bool has_tls_port, int64_t tls_port,
                             bool has_cert_subject, const char *cert_subject,
                             Error **errp)
{
    if (strcmp(protocol, "spice") == 0) {
        if (!qemu_using_spice(errp)) {
            return;
        }

        if (!has_port && !has_tls_port) {
            error_setg(errp, QERR_MISSING_PARAMETER, "port/tls-port");
            return;
        }

        if (qemu_spice_migrate_info(hostname,
                                    has_port ? port : -1,
                                    has_tls_port ? tls_port : -1,
                                    cert_subject)) {
            error_setg(errp, QERR_UNDEFINED_ERROR);
            return;
        }
        return;
    }

    error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "protocol", "spice");
}

static void hmp_logfile(Monitor *mon, const QDict *qdict)
{
    qemu_set_log_filename(qdict_get_str(qdict, "filename"));
}

static void hmp_log(Monitor *mon, const QDict *qdict)
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

static void hmp_singlestep(Monitor *mon, const QDict *qdict)
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

static void hmp_gdbserver(Monitor *mon, const QDict *qdict)
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

static void hmp_watchdog_action(Monitor *mon, const QDict *qdict)
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
    int l, line_size, i, max_digits, len;
    uint8_t buf[16];
    uint64_t v;

    if (format == 'i') {
        int flags = 0;
#ifdef TARGET_I386
        CPUArchState *env = mon_get_cpu_env();
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
        CPUArchState *env = mon_get_cpu_env();
        flags = msr_le << 16;
        flags |= env->bfd_mach;
#endif
        monitor_disas(mon, mon_get_cpu(), addr, count, is_physical, flags);
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
            if (cpu_memory_rw_debug(mon_get_cpu(), addr, buf, l, 0) < 0) {
                monitor_printf(mon, " Cannot access memory\n");
                break;
            }
        }
        i = 0;
        while (i < l) {
            switch(wsize) {
            default:
            case 1:
                v = ldub_p(buf + i);
                break;
            case 2:
                v = lduw_p(buf + i);
                break;
            case 4:
                v = (uint32_t)ldl_p(buf + i);
                break;
            case 8:
                v = ldq_p(buf + i);
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

static void hmp_memory_dump(Monitor *mon, const QDict *qdict)
{
    int count = qdict_get_int(qdict, "count");
    int format = qdict_get_int(qdict, "format");
    int size = qdict_get_int(qdict, "size");
    target_long addr = qdict_get_int(qdict, "addr");

    memory_dump(mon, count, format, size, addr, 0);
}

static void hmp_physical_memory_dump(Monitor *mon, const QDict *qdict)
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

static void hmp_sum(Monitor *mon, const QDict *qdict)
{
    uint32_t addr;
    uint16_t sum;
    uint32_t start = qdict_get_int(qdict, "start");
    uint32_t size = qdict_get_int(qdict, "size");

    sum = 0;
    for(addr = start; addr < (start + size); addr++) {
        uint8_t val = address_space_ldub(&address_space_memory, addr,
                                         MEMTXATTRS_UNSPECIFIED, NULL);
        /* BSD sum algorithm ('sum' Unix command) */
        sum = (sum >> 1) | (sum << 15);
        sum += val;
    }
    monitor_printf(mon, "%05d\n", sum);
}

static int mouse_button_state;

static void hmp_mouse_move(Monitor *mon, const QDict *qdict)
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

static void hmp_mouse_button(Monitor *mon, const QDict *qdict)
{
    static uint32_t bmap[INPUT_BUTTON__MAX] = {
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

static void hmp_ioport_read(Monitor *mon, const QDict *qdict)
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

static void hmp_ioport_write(Monitor *mon, const QDict *qdict)
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

static void hmp_boot_set(Monitor *mon, const QDict *qdict)
{
    Error *local_err = NULL;
    const char *bootdevice = qdict_get_str(qdict, "bootdevice");

    qemu_boot_set(bootdevice, &local_err);
    if (local_err) {
        error_report_err(local_err);
    } else {
        monitor_printf(mon, "boot device list now set to %s\n", bootdevice);
    }
}

static void hmp_info_mtree(Monitor *mon, const QDict *qdict)
{
    mtree_info((fprintf_function)monitor_printf, mon);
}

static void hmp_info_numa(Monitor *mon, const QDict *qdict)
{
    int i;
    CPUState *cpu;
    uint64_t *node_mem;

    node_mem = g_new0(uint64_t, nb_numa_nodes);
    query_numa_node_mem(node_mem);
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
                       node_mem[i] >> 20);
    }
    g_free(node_mem);
}

#ifdef CONFIG_PROFILER

int64_t tcg_time;
int64_t dev_time;

static void hmp_info_profile(Monitor *mon, const QDict *qdict)
{
    monitor_printf(mon, "async time  %" PRId64 " (%0.3f)\n",
                   dev_time, dev_time / (double)NANOSECONDS_PER_SECOND);
    monitor_printf(mon, "qemu time   %" PRId64 " (%0.3f)\n",
                   tcg_time, tcg_time / (double)NANOSECONDS_PER_SECOND);
    tcg_time = 0;
    dev_time = 0;
}
#else
static void hmp_info_profile(Monitor *mon, const QDict *qdict)
{
    monitor_printf(mon, "Internal profiler not compiled\n");
}
#endif

/* Capture support */
static QLIST_HEAD (capture_list_head, CaptureState) capture_head;

static void hmp_info_capture(Monitor *mon, const QDict *qdict)
{
    int i;
    CaptureState *s;

    for (s = capture_head.lh_first, i = 0; s; s = s->entries.le_next, ++i) {
        monitor_printf(mon, "[%d]: ", i);
        s->ops.info (s->opaque);
    }
}

static void hmp_stopcapture(Monitor *mon, const QDict *qdict)
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

static void hmp_wavcapture(Monitor *mon, const QDict *qdict)
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

static void hmp_acl_show(Monitor *mon, const QDict *qdict)
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

static void hmp_acl_reset(Monitor *mon, const QDict *qdict)
{
    const char *aclname = qdict_get_str(qdict, "aclname");
    qemu_acl *acl = find_acl(mon, aclname);

    if (acl) {
        qemu_acl_reset(acl);
        monitor_printf(mon, "acl: removed all rules\n");
    }
}

static void hmp_acl_policy(Monitor *mon, const QDict *qdict)
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

static void hmp_acl_add(Monitor *mon, const QDict *qdict)
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

static void hmp_acl_remove(Monitor *mon, const QDict *qdict)
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

void qmp_getfd(const char *fdname, Error **errp)
{
    mon_fd_t *monfd;
    int fd;

    fd = qemu_chr_fe_get_msgfd(cur_mon->chr);
    if (fd == -1) {
        error_setg(errp, QERR_FD_NOT_SUPPLIED);
        return;
    }

    if (qemu_isdigit(fdname[0])) {
        close(fd);
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "fdname",
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

    error_setg(errp, QERR_FD_NOT_FOUND, fdname);
}

static void hmp_loadvm(Monitor *mon, const QDict *qdict)
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
        error_setg(errp, QERR_FD_NOT_SUPPLIED);
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
    error_setg(errp, QERR_FD_NOT_FOUND, fd_str);
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
                error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "fdset-id",
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
                    return -1;
                } else {
                    return mon_fdset->id;
                }
            }
        }
    }
    return -1;
}

int monitor_fdset_dup_fd_find(int dup_fd)
{
    return monitor_fdset_dup_fd_find_remove(dup_fd, false);
}

void monitor_fdset_dup_fd_remove(int dup_fd)
{
    monitor_fdset_dup_fd_find_remove(dup_fd, true);
}

int monitor_fd_param(Monitor *mon, const char *fdname, Error **errp)
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
#include "hmp-commands-info.h"
    { NULL, NULL, },
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
    const MonitorDef *md = target_monitor_defs();
    void *ptr;
    uint64_t tmp = 0;
    int ret;

    if (md == NULL) {
        return -1;
    }

    for(; md->name != NULL; md++) {
        if (compare_cmd(name, md->name)) {
            if (md->get_value) {
                *pval = md->get_value(md, md->offset);
            } else {
                CPUArchState *env = mon_get_cpu_env();
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

    ret = target_get_monitor_def(mon_get_cpu(), name, &tmp);
    if (!ret) {
        *pval = (target_long) tmp;
    }

    return ret;
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
 * Parse command name from @cmdp according to command table @table.
 * If blank, return NULL.
 * Else, if no valid command can be found, report to @mon, and return
 * NULL.
 * Else, change @cmdp to point right behind the name, and return its
 * command table entry.
 * Do not assume the return value points into @table!  It doesn't when
 * the command is found in a sub-command table.
 */
static const mon_cmd_t *monitor_parse_command(Monitor *mon,
                                              const char **cmdp,
                                              mon_cmd_t *table)
{
    const char *p;
    const mon_cmd_t *cmd;
    char cmdname[256];

    /* extract the command name */
    p = get_command_name(*cmdp, cmdname, sizeof(cmdname));
    if (!p)
        return NULL;

    cmd = search_dispatch_table(table, cmdname);
    if (!cmd) {
        monitor_printf(mon, "unknown command: '%.*s'\n",
                       (int)(p - *cmdp), *cmdp);
        return NULL;
    }

    /* filter out following useless space */
    while (qemu_isspace(*p)) {
        p++;
    }

    *cmdp = p;
    /* search sub command */
    if (cmd->sub_table != NULL && *p != '\0') {
        return monitor_parse_command(mon, cmdp, cmd->sub_table);
    }

    return cmd;
}

/*
 * Parse arguments for @cmd.
 * If it can't be parsed, report to @mon, and return NULL.
 * Else, insert command arguments into a QDict, and return it.
 * Note: On success, caller has to free the QDict structure.
 */

static QDict *monitor_parse_arguments(Monitor *mon,
                                      const char **endp,
                                      const mon_cmd_t *cmd)
{
    const char *typestr;
    char *key;
    int c;
    const char *p = *endp;
    char buf[1024];
    QDict *qdict = qdict_new();

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
                                       cmd->name);
                        break;
                    case 'B':
                        monitor_printf(mon, "%s: block device name expected\n",
                                       cmd->name);
                        break;
                    default:
                        monitor_printf(mon, "%s: string expected\n", cmd->name);
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
                opts = qemu_opts_parse_noisily(opts_list, buf, true);
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
                    monitor_printf(mon, "\'%s\' has failed: ", cmd->name);
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
                val = qemu_strtosz(p, &end);
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
                bool val;

                while (qemu_isspace(*p)) {
                    p++;
                }
                beg = p;
                while (qemu_isgraph(*p)) {
                    p++;
                }
                if (p - beg == 2 && !memcmp(beg, "on", p - beg)) {
                    val = true;
                } else if (p - beg == 3 && !memcmp(beg, "off", p - beg)) {
                    val = false;
                } else {
                    monitor_printf(mon, "Expected 'on' or 'off'\n");
                    goto fail;
                }
                qdict_put(qdict, key, qbool_from_bool(val));
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
                                           cmd->name, *p);
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
                        qdict_put(qdict, key, qbool_from_bool(true));
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
                                   cmd->name);
                    goto fail;
                }
                qdict_put(qdict, key, qstring_from_str(p));
                p += len;
            }
            break;
        default:
        bad_type:
            monitor_printf(mon, "%s: unknown type '%c'\n", cmd->name, c);
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
                       cmd->name);
        goto fail;
    }

    return qdict;

fail:
    QDECREF(qdict);
    g_free(key);
    return NULL;
}

static void handle_hmp_command(Monitor *mon, const char *cmdline)
{
    QDict *qdict;
    const mon_cmd_t *cmd;

    cmd = monitor_parse_command(mon, &cmdline, mon->cmd_table);
    if (!cmd) {
        return;
    }

    qdict = monitor_parse_arguments(mon, &cmdline, cmd);
    if (!qdict) {
        monitor_printf(mon, "Try \"help %s\" for more information\n",
                       cmd->name);
        return;
    }

    cmd->mhandler.cmd(mon, qdict);
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

static void peripheral_device_del_completion(ReadLineState *rs,
                                             const char *str, size_t len)
{
    Object *peripheral = container_get(qdev_get_machine(), "/peripheral");
    GSList *list, *item;

    list = qdev_build_hotpluggable_device_list(peripheral);
    if (!list) {
        return;
    }

    for (item = list; item; item = g_slist_next(item)) {
        DeviceState *dev = item->data;

        if (dev->id && !strncmp(str, dev->id, len)) {
            readline_add_completion(rs, dev->id);
        }
    }

    g_slist_free(list);
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
    peripheral_device_del_completion(rs, str, len);
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
    for (i = 0; i < Q_KEY_CODE__MAX; i++) {
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
        NetClientState *ncs[MAX_QUEUE_NUM];
        int count, i;
        count = qemu_find_net_clients_except(NULL, ncs,
                                             NET_CLIENT_OPTIONS_KIND_NONE,
                                             MAX_QUEUE_NUM);
        for (i = 0; i < MIN(count, MAX_QUEUE_NUM); i++) {
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
    NetClientState *ncs[MAX_QUEUE_NUM];

    if (nb_args != 2) {
        return;
    }

    len = strlen(str);
    readline_set_completion_index(rs, len);
    count = qemu_find_net_clients_except(NULL, ncs, NET_CLIENT_OPTIONS_KIND_NIC,
                                         MAX_QUEUE_NUM);
    for (i = 0; i < MIN(count, MAX_QUEUE_NUM); i++) {
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

void trace_event_completion(ReadLineState *rs, int nb_args, const char *str)
{
    size_t len;

    len = strlen(str);
    readline_set_completion_index(rs, len);
    if (nb_args == 2) {
        TraceEventID id;
        for (id = 0; id < trace_event_count(); id++) {
            const char *event_name = trace_event_get_name(trace_event_id(id));
            if (!strncmp(str, event_name, len)) {
                readline_add_completion(rs, event_name);
            }
        }
    } else if (nb_args == 3) {
        add_completion_option(rs, str, "on");
        add_completion_option(rs, str, "off");
    }
}

void watchdog_action_completion(ReadLineState *rs, int nb_args, const char *str)
{
    int i;

    if (nb_args != 2) {
        return;
    }
    readline_set_completion_index(rs, strlen(str));
    for (i = 0; WatchdogExpirationAction_lookup[i]; i++) {
        add_completion_option(rs, str, WatchdogExpirationAction_lookup[i]);
    }
}

void migrate_set_capability_completion(ReadLineState *rs, int nb_args,
                                       const char *str)
{
    size_t len;

    len = strlen(str);
    readline_set_completion_index(rs, len);
    if (nb_args == 2) {
        int i;
        for (i = 0; i < MIGRATION_CAPABILITY__MAX; i++) {
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

void migrate_set_parameter_completion(ReadLineState *rs, int nb_args,
                                      const char *str)
{
    size_t len;

    len = strlen(str);
    readline_set_completion_index(rs, len);
    if (nb_args == 2) {
        int i;
        for (i = 0; i < MIGRATION_PARAMETER__MAX; i++) {
            const char *name = MigrationParameter_lookup[i];
            if (!strncmp(str, name, len)) {
                readline_add_completion(rs, name);
            }
        }
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
    NetClientState *ncs[MAX_QUEUE_NUM];
    int count, i, len;

    len = strlen(str);
    readline_set_completion_index(rs, len);
    if (nb_args == 2) {
        count = qemu_find_net_clients_except(NULL, ncs,
                                             NET_CLIENT_OPTIONS_KIND_NONE,
                                             MAX_QUEUE_NUM);
        for (i = 0; i < MIN(count, MAX_QUEUE_NUM); i++) {
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
                                             NET_CLIENT_OPTIONS_KIND_NIC,
                                             MAX_QUEUE_NUM);
        for (i = 0; i < MIN(count, MAX_QUEUE_NUM); i++) {
            int id;
            const char *name;

            if (ncs[i]->info->type == NET_CLIENT_OPTIONS_KIND_HUBPORT ||
                net_hub_id_for_client(ncs[i], &id)) {
                continue;
            }
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
    BlockDriverState *bs;
    BdrvNextIterator it;

    len = strlen(str);
    readline_set_completion_index(rs, len);

    for (bs = bdrv_first(&it); bs; bs = bdrv_next(&it)) {
        SnapshotInfoList *snapshots, *snapshot;
        AioContext *ctx = bdrv_get_aio_context(bs);
        bool ok = false;

        aio_context_acquire(ctx);
        if (bdrv_can_snapshot(bs)) {
            ok = bdrv_query_snapshot_info_list(bs, &snapshots, NULL) == 0;
        }
        aio_context_release(ctx);
        if (!ok) {
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
    const char *ptype, *str, *name;
    const mon_cmd_t *cmd;
    BlockBackend *blk = NULL;

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
            monitor_find_completion_by_table(mon, cmd->sub_table,
                                             &args[1], nb_args - 1);
            return;
        }
        if (cmd->command_completion) {
            cmd->command_completion(mon->rs, nb_args, args[nb_args - 1]);
            return;
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
        while (*ptype == '-' && ptype[1] != '\0') {
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
            readline_set_completion_index(mon->rs, strlen(str));
            while ((blk = blk_next(blk)) != NULL) {
                name = blk_name(blk);
                if (str[0] == '\0' ||
                    !strncmp(name, str, strlen(str))) {
                    readline_add_completion(mon->rs, name);
                }
            }
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

static bool invalid_qmp_mode(const Monitor *mon, const mon_cmd_t *cmd,
                             Error **errp)
{
    bool is_cap = cmd->mhandler.cmd_new == qmp_capabilities;

    if (is_cap && mon->qmp.in_command_mode) {
        error_set(errp, ERROR_CLASS_COMMAND_NOT_FOUND,
                  "Capabilities negotiation is already complete, command "
                  "'%s' ignored", cmd->name);
        return true;
    }
    if (!is_cap && !mon->qmp.in_command_mode) {
        error_set(errp, ERROR_CLASS_COMMAND_NOT_FOUND,
                  "Expecting capabilities negotiation with "
                  "'qmp_capabilities' before command '%s'", cmd->name);
        return true;
    }
    return false;
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
static void check_client_args_type(const QDict *client_args,
                                   const QDict *cmd_args, int flags,
                                   Error **errp)
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
            error_setg(errp, QERR_INVALID_PARAMETER, client_arg_name);
            return;
        }

        arg_type = qobject_to_qstring(obj);
        assert(arg_type != NULL);

        /* check if argument's type is correct */
        switch (qstring_get_str(arg_type)[0]) {
        case 'F':
        case 'B':
        case 's':
            if (qobject_type(client_arg) != QTYPE_QSTRING) {
                error_setg(errp, QERR_INVALID_PARAMETER_TYPE,
                           client_arg_name, "string");
                return;
            }
        break;
        case 'i':
        case 'l':
        case 'M':
        case 'o':
            if (qobject_type(client_arg) != QTYPE_QINT) {
                error_setg(errp, QERR_INVALID_PARAMETER_TYPE,
                           client_arg_name, "int");
                return;
            }
            break;
        case 'T':
            if (qobject_type(client_arg) != QTYPE_QINT &&
                qobject_type(client_arg) != QTYPE_QFLOAT) {
                error_setg(errp, QERR_INVALID_PARAMETER_TYPE,
                           client_arg_name, "number");
                return;
            }
            break;
        case 'b':
        case '-':
            if (qobject_type(client_arg) != QTYPE_QBOOL) {
                error_setg(errp, QERR_INVALID_PARAMETER_TYPE,
                           client_arg_name, "bool");
                return;
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
}

/*
 * - Check if the client has passed all mandatory args
 * - Set special flags for argument validation
 */
static void check_mandatory_args(const QDict *cmd_args,
                                 const QDict *client_args, int *flags,
                                 Error **errp)
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
            error_setg(errp, QERR_MISSING_PARAMETER, cmd_arg_name);
            return;
        }
    }
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
static void qmp_check_client_args(const mon_cmd_t *cmd, QDict *client_args,
                                  Error **errp)
{
    Error *err = NULL;
    int flags;
    QDict *cmd_args;

    cmd_args = qdict_from_args_type(cmd->args_type);

    flags = 0;
    check_mandatory_args(cmd_args, client_args, &flags, &err);
    if (err) {
        goto out;
    }

    check_client_args_type(client_args, cmd_args, flags, &err);

out:
    error_propagate(errp, err);
    QDECREF(cmd_args);
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
static QDict *qmp_check_input_obj(QObject *input_obj, Error **errp)
{
    const QDictEntry *ent;
    int has_exec_key = 0;
    QDict *input_dict;

    if (qobject_type(input_obj) != QTYPE_QDICT) {
        error_setg(errp, QERR_QMP_BAD_INPUT_OBJECT, "object");
        return NULL;
    }

    input_dict = qobject_to_qdict(input_obj);

    for (ent = qdict_first(input_dict); ent; ent = qdict_next(input_dict, ent)){
        const char *arg_name = qdict_entry_key(ent);
        const QObject *arg_obj = qdict_entry_value(ent);

        if (!strcmp(arg_name, "execute")) {
            if (qobject_type(arg_obj) != QTYPE_QSTRING) {
                error_setg(errp, QERR_QMP_BAD_INPUT_OBJECT_MEMBER,
                           "execute", "string");
                return NULL;
            }
            has_exec_key = 1;
        } else if (!strcmp(arg_name, "arguments")) {
            if (qobject_type(arg_obj) != QTYPE_QDICT) {
                error_setg(errp, QERR_QMP_BAD_INPUT_OBJECT_MEMBER,
                           "arguments", "object");
                return NULL;
            }
        } else if (!strcmp(arg_name, "id")) {
            /* Any string is acceptable as "id", so nothing to check */
        } else {
            error_setg(errp, QERR_QMP_EXTRA_MEMBER, arg_name);
            return NULL;
        }
    }

    if (!has_exec_key) {
        error_setg(errp, QERR_QMP_BAD_INPUT_OBJECT, "execute");
        return NULL;
    }

    return input_dict;
}

static void handle_qmp_command(JSONMessageParser *parser, GQueue *tokens)
{
    Error *local_err = NULL;
    QObject *obj, *data;
    QDict *input, *args;
    const mon_cmd_t *cmd;
    const char *cmd_name;
    Monitor *mon = cur_mon;

    args = input = NULL;
    data = NULL;

    obj = json_parser_parse(tokens, NULL);
    if (!obj) {
        // FIXME: should be triggered in json_parser_parse()
        error_setg(&local_err, QERR_JSON_PARSING);
        goto err_out;
    }

    input = qmp_check_input_obj(obj, &local_err);
    if (!input) {
        qobject_decref(obj);
        goto err_out;
    }

    mon->qmp.id = qdict_get(input, "id");
    qobject_incref(mon->qmp.id);

    cmd_name = qdict_get_str(input, "execute");
    trace_handle_qmp_command(mon, cmd_name);
    cmd = qmp_find_cmd(cmd_name);
    if (!cmd) {
        error_set(&local_err, ERROR_CLASS_COMMAND_NOT_FOUND,
                  "The command %s has not been found", cmd_name);
        goto err_out;
    }
    if (invalid_qmp_mode(mon, cmd, &local_err)) {
        goto err_out;
    }

    obj = qdict_get(input, "arguments");
    if (!obj) {
        args = qdict_new();
    } else {
        args = qobject_to_qdict(obj);
        QINCREF(args);
    }

    qmp_check_client_args(cmd, args, &local_err);
    if (local_err) {
        goto err_out;
    }

    cmd->mhandler.cmd_new(args, &data, &local_err);

err_out:
    monitor_protocol_emitter(mon, data, local_err);
    qobject_decref(data);
    error_free(local_err);
    QDECREF(input);
    QDECREF(args);
}

static void monitor_qmp_read(void *opaque, const uint8_t *buf, int size)
{
    Monitor *old_mon = cur_mon;

    cur_mon = opaque;

    json_message_parser_feed(&cur_mon->qmp.parser, (const char *) buf, size);

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
            handle_hmp_command(cur_mon, (char *)buf);
    }

    cur_mon = old_mon;
}

static void monitor_command_cb(void *opaque, const char *cmdline,
                               void *readline_opaque)
{
    Monitor *mon = opaque;

    monitor_suspend(mon);
    handle_hmp_command(mon, cmdline);
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

    qmp_marshal_query_version(NULL, &ver, NULL);
    return qobject_from_jsonf("{'QMP':{'version': %p,'capabilities': []}}",ver);
}

static void monitor_qmp_event(void *opaque, int event)
{
    QObject *data;
    Monitor *mon = opaque;

    switch (event) {
    case CHR_EVENT_OPENED:
        mon->qmp.in_command_mode = false;
        data = get_qmp_greeting();
        monitor_json_emitter(mon, data);
        qobject_decref(data);
        mon_refcount++;
        break;
    case CHR_EVENT_CLOSED:
        json_message_parser_destroy(&mon->qmp.parser);
        json_message_parser_init(&mon->qmp.parser, handle_qmp_command);
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
            readline_restart(mon->rs);
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

    if (monitor_is_qmp(mon)) {
        qemu_chr_add_handlers(chr, monitor_can_read, monitor_qmp_read,
                              monitor_qmp_event, mon);
        qemu_chr_fe_set_echo(chr, true);
        json_message_parser_init(&mon->qmp.parser, handle_qmp_command);
    } else {
        qemu_chr_add_handlers(chr, monitor_can_read, monitor_read,
                              monitor_event, mon);
    }

    qemu_mutex_lock(&monitor_lock);
    QLIST_INSERT_HEAD(&mon_list, mon, entry);
    qemu_mutex_unlock(&monitor_lock);
}

static void bdrv_password_cb(void *opaque, const char *password,
                             void *readline_opaque)
{
    Monitor *mon = opaque;
    BlockDriverState *bs = readline_opaque;
    int ret = 0;
    Error *local_err = NULL;

    bdrv_add_key(bs, password, &local_err);
    if (local_err) {
        error_report_err(local_err);
        ret = -EPERM;
    }
    if (mon->password_completion_cb)
        mon->password_completion_cb(mon->password_opaque, ret);

    monitor_read_command(mon, 1);
}

int monitor_read_bdrv_key_start(Monitor *mon, BlockDriverState *bs,
                                BlockCompletionFunc *completion_cb,
                                void *opaque)
{
    int err;

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
                                  BlockCompletionFunc *completion_cb,
                                  void *opaque)
{
    Error *err = NULL;
    BlockBackend *blk;

    blk = blk_by_name(device);
    if (!blk) {
        monitor_printf(mon, "Device not found %s\n", device);
        return -1;
    }
    if (!blk_bs(blk)) {
        monitor_printf(mon, "Device '%s' has no medium\n", device);
        return -1;
    }

    bdrv_add_key(blk_bs(blk), NULL, &err);
    if (err) {
        error_free(err);
        return monitor_read_bdrv_key_start(mon, blk_bs(blk), completion_cb, opaque);
    }

    if (completion_cb) {
        completion_cb(opaque, 0);
    }
    return 0;
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
    error_setg(errp, QERR_FEATURE_DISABLED, "rtc-reset-reinjection");
}
#endif

#ifndef TARGET_S390X
void qmp_dump_skeys(const char *filename, Error **errp)
{
    error_setg(errp, QERR_FEATURE_DISABLED, "dump-skeys");
}
#endif

#ifndef TARGET_ARM
GICCapabilityList *qmp_query_gic_capabilities(Error **errp)
{
    error_setg(errp, QERR_FEATURE_DISABLED, "query-gic-capabilities");
    return NULL;
}
#endif
