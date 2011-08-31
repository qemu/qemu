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
#include "hw/qdev.h"
#include "hw/usb.h"
#include "hw/pcmcia.h"
#include "hw/pc.h"
#include "hw/pci.h"
#include "hw/watchdog.h"
#include "hw/loader.h"
#include "gdbstub.h"
#include "net.h"
#include "net/slirp.h"
#include "qemu-char.h"
#include "ui/qemu-spice.h"
#include "sysemu.h"
#include "monitor.h"
#include "readline.h"
#include "console.h"
#include "blockdev.h"
#include "audio/audio.h"
#include "disas.h"
#include "balloon.h"
#include "qemu-timer.h"
#include "migration.h"
#include "kvm.h"
#include "acl.h"
#include "qint.h"
#include "qfloat.h"
#include "qlist.h"
#include "qbool.h"
#include "qstring.h"
#include "qjson.h"
#include "json-streamer.h"
#include "json-parser.h"
#include "osdep.h"
#include "cpu.h"
#ifdef CONFIG_TRACE_SIMPLE
#include "trace.h"
#endif
#include "trace/control.h"
#include "ui/qemu-spice.h"

//#define DEBUG
//#define DEBUG_COMPLETION

/*
 * Supported types:
 *
 * 'F'          filename
 * 'B'          block device name
 * 's'          string (accept optional quote)
 * 'O'          option string of the form NAME=VALUE,...
 *              parsed according to QemuOptsList given by its name
 *              Example: 'device:O' uses qemu_device_opts.
 *              Restriction: only lists with empty desc are supported
 *              TODO lift the restriction
 * 'i'          32 bit integer
 * 'l'          target long (32 or 64 bit)
 * 'M'          just like 'l', except in user mode the value is
 *              multiplied by 2^20 (think Mebibyte)
 * 'o'          octets (aka bytes)
 *              user mode accepts an optional T, t, G, g, M, m, K, k
 *              suffix, which multiplies the value by 2^40 for
 *              suffixes T and t, 2^30 for suffixes G and g, 2^20 for
 *              M and m, 2^10 for K and k
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
        void (*info)(Monitor *mon);
        void (*info_new)(Monitor *mon, QObject **ret_data);
        int  (*info_async)(Monitor *mon, MonitorCompletion *cb, void *opaque);
        void (*cmd)(Monitor *mon, const QDict *qdict);
        int  (*cmd_new)(Monitor *mon, const QDict *params, QObject **ret_data);
        int  (*cmd_async)(Monitor *mon, const QDict *params,
                          MonitorCompletion *cb, void *opaque);
    } mhandler;
    int flags;
} mon_cmd_t;

/* file descriptors passed via SCM_RIGHTS */
typedef struct mon_fd_t mon_fd_t;
struct mon_fd_t {
    char *name;
    int fd;
    QLIST_ENTRY(mon_fd_t) next;
};

typedef struct MonitorControl {
    QObject *id;
    JSONMessageParser parser;
    int command_mode;
} MonitorControl;

struct Monitor {
    CharDriverState *chr;
    int mux_out;
    int reset_seen;
    int flags;
    int suspend_cnt;
    uint8_t outbuf[1024];
    int outbuf_index;
    ReadLineState *rs;
    MonitorControl *mc;
    CPUState *mon_cpu;
    BlockDriverCompletionFunc *password_completion_cb;
    void *password_opaque;
#ifdef CONFIG_DEBUG_MONITOR
    int print_calls_nr;
#endif
    QError *error;
    QLIST_HEAD(,mon_fd_t) fds;
    QLIST_ENTRY(Monitor) entry;
};

#ifdef CONFIG_DEBUG_MONITOR
#define MON_DEBUG(fmt, ...) do {    \
    fprintf(stderr, "Monitor: ");       \
    fprintf(stderr, fmt, ## __VA_ARGS__); } while (0)

static inline void mon_print_count_inc(Monitor *mon)
{
    mon->print_calls_nr++;
}

static inline void mon_print_count_init(Monitor *mon)
{
    mon->print_calls_nr = 0;
}

static inline int mon_print_count_get(const Monitor *mon)
{
    return mon->print_calls_nr;
}

#else /* !CONFIG_DEBUG_MONITOR */
#define MON_DEBUG(fmt, ...) do { } while (0)
static inline void mon_print_count_inc(Monitor *mon) { }
static inline void mon_print_count_init(Monitor *mon) { }
static inline int mon_print_count_get(const Monitor *mon) { return 0; }
#endif /* CONFIG_DEBUG_MONITOR */

/* QMP checker flags */
#define QMP_ACCEPT_UNKNOWNS 1

static QLIST_HEAD(mon_list, Monitor) mon_list;

static const mon_cmd_t mon_cmds[];
static const mon_cmd_t info_cmds[];

static const mon_cmd_t qmp_cmds[];
static const mon_cmd_t qmp_query_cmds[];

Monitor *cur_mon;
Monitor *default_mon;

static void monitor_command_cb(Monitor *mon, const char *cmdline,
                               void *opaque);

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

static void monitor_read_command(Monitor *mon, int show_prompt)
{
    if (!mon->rs)
        return;

    readline_start(mon->rs, "(qemu) ", 0, monitor_command_cb, NULL);
    if (show_prompt)
        readline_show_prompt(mon->rs);
}

static int monitor_read_password(Monitor *mon, ReadLineFunc *readline_func,
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

void monitor_flush(Monitor *mon)
{
    if (mon && mon->outbuf_index != 0 && !mon->mux_out) {
        qemu_chr_fe_write(mon->chr, mon->outbuf, mon->outbuf_index);
        mon->outbuf_index = 0;
    }
}

/* flush at every end of line or if the buffer is full */
static void monitor_puts(Monitor *mon, const char *str)
{
    char c;

    for(;;) {
        c = *str++;
        if (c == '\0')
            break;
        if (c == '\n')
            mon->outbuf[mon->outbuf_index++] = '\r';
        mon->outbuf[mon->outbuf_index++] = c;
        if (mon->outbuf_index >= (sizeof(mon->outbuf) - 1)
            || c == '\n')
            monitor_flush(mon);
    }
}

void monitor_vprintf(Monitor *mon, const char *fmt, va_list ap)
{
    char buf[4096];

    if (!mon)
        return;

    mon_print_count_inc(mon);

    if (monitor_ctrl_mode(mon)) {
        return;
    }

    vsnprintf(buf, sizeof(buf), fmt, ap);
    monitor_puts(mon, buf);
}

void monitor_printf(Monitor *mon, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    monitor_vprintf(mon, fmt, ap);
    va_end(ap);
}

void monitor_print_filename(Monitor *mon, const char *filename)
{
    int i;

    for (i = 0; filename[i]; i++) {
        switch (filename[i]) {
        case ' ':
        case '"':
        case '\\':
            monitor_printf(mon, "\\%c", filename[i]);
            break;
        case '\t':
            monitor_printf(mon, "\\t");
            break;
        case '\r':
            monitor_printf(mon, "\\r");
            break;
        case '\n':
            monitor_printf(mon, "\\n");
            break;
        default:
            monitor_printf(mon, "%c", filename[i]);
            break;
        }
    }
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

static void monitor_protocol_emitter(Monitor *mon, QObject *data)
{
    QDict *qmp;

    qmp = qdict_new();

    if (!monitor_has_error(mon)) {
        /* success response */
        if (data) {
            qobject_incref(data);
            qdict_put_obj(qmp, "return", data);
        } else {
            /* return an empty QDict by default */
            qdict_put(qmp, "return", qdict_new());
        }
    } else {
        /* error response */
        qdict_put(mon->error->error, "desc", qerror_human(mon->error));
        qdict_put(qmp, "error", mon->error->error);
        QINCREF(mon->error->error);
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

static void timestamp_put(QDict *qdict)
{
    int err;
    QObject *obj;
    qemu_timeval tv;

    err = qemu_gettimeofday(&tv);
    if (err < 0)
        return;

    obj = qobject_from_jsonf("{ 'seconds': %" PRId64 ", "
                                "'microseconds': %" PRId64 " }",
                                (int64_t) tv.tv_sec, (int64_t) tv.tv_usec);
    qdict_put_obj(qdict, "timestamp", obj);
}

/**
 * monitor_protocol_event(): Generate a Monitor event
 *
 * Event-specific data can be emitted through the (optional) 'data' parameter.
 */
void monitor_protocol_event(MonitorEvent event, QObject *data)
{
    QDict *qmp;
    const char *event_name;
    Monitor *mon;

    assert(event < QEVENT_MAX);

    switch (event) {
        case QEVENT_SHUTDOWN:
            event_name = "SHUTDOWN";
            break;
        case QEVENT_RESET:
            event_name = "RESET";
            break;
        case QEVENT_POWERDOWN:
            event_name = "POWERDOWN";
            break;
        case QEVENT_STOP:
            event_name = "STOP";
            break;
        case QEVENT_RESUME:
            event_name = "RESUME";
            break;
        case QEVENT_VNC_CONNECTED:
            event_name = "VNC_CONNECTED";
            break;
        case QEVENT_VNC_INITIALIZED:
            event_name = "VNC_INITIALIZED";
            break;
        case QEVENT_VNC_DISCONNECTED:
            event_name = "VNC_DISCONNECTED";
            break;
        case QEVENT_BLOCK_IO_ERROR:
            event_name = "BLOCK_IO_ERROR";
            break;
        case QEVENT_RTC_CHANGE:
            event_name = "RTC_CHANGE";
            break;
        case QEVENT_WATCHDOG:
            event_name = "WATCHDOG";
            break;
        case QEVENT_SPICE_CONNECTED:
            event_name = "SPICE_CONNECTED";
            break;
        case QEVENT_SPICE_INITIALIZED:
            event_name = "SPICE_INITIALIZED";
            break;
        case QEVENT_SPICE_DISCONNECTED:
            event_name = "SPICE_DISCONNECTED";
            break;
        default:
            abort();
            break;
    }

    qmp = qdict_new();
    timestamp_put(qmp);
    qdict_put(qmp, "event", qstring_from_str(event_name));
    if (data) {
        qobject_incref(data);
        qdict_put_obj(qmp, "data", data);
    }

    QLIST_FOREACH(mon, &mon_list, entry) {
        if (monitor_ctrl_mode(mon) && qmp_cmd_mode(mon)) {
            monitor_json_emitter(mon, QOBJECT(qmp));
        }
    }
    QDECREF(qmp);
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

static int mon_set_cpu(int cpu_index);
static void handle_user_command(Monitor *mon, const char *cmdline);

static int do_hmp_passthrough(Monitor *mon, const QDict *params,
                              QObject **ret_data)
{
    int ret = 0;
    Monitor *old_mon, hmp;
    CharDriverState mchar;

    memset(&hmp, 0, sizeof(hmp));
    qemu_chr_init_mem(&mchar);
    hmp.chr = &mchar;

    old_mon = cur_mon;
    cur_mon = &hmp;

    if (qdict_haskey(params, "cpu-index")) {
        ret = mon_set_cpu(qdict_get_int(params, "cpu-index"));
        if (ret < 0) {
            cur_mon = old_mon;
            qerror_report(QERR_INVALID_PARAMETER_VALUE, "cpu-index", "a CPU number");
            goto out;
        }
    }

    handle_user_command(&hmp, qdict_get_str(params, "command-line"));
    cur_mon = old_mon;

    if (qemu_chr_mem_osize(hmp.chr) > 0) {
        *ret_data = QOBJECT(qemu_chr_mem_to_qs(hmp.chr));
    }

out:
    qemu_chr_close_mem(hmp.chr);
    return ret;
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

static void help_cmd_dump(Monitor *mon, const mon_cmd_t *cmds,
                          const char *prefix, const char *name)
{
    const mon_cmd_t *cmd;

    for(cmd = cmds; cmd->name != NULL; cmd++) {
        if (!name || !strcmp(name, cmd->name))
            monitor_printf(mon, "%s%s %s -- %s\n", prefix, cmd->name,
                           cmd->params, cmd->help);
    }
}

static void help_cmd(Monitor *mon, const char *name)
{
    if (name && !strcmp(name, "info")) {
        help_cmd_dump(mon, info_cmds, "info ", NULL);
    } else {
        help_cmd_dump(mon, mon_cmds, "", name);
        if (name && !strcmp(name, "log")) {
            const CPULogItem *item;
            monitor_printf(mon, "Log items (comma separated):\n");
            monitor_printf(mon, "%-10s %s\n", "none", "remove all logs");
            for(item = cpu_log_items; item->mask != 0; item++) {
                monitor_printf(mon, "%-10s %s\n", item->name, item->help);
            }
        }
    }
}

static void do_help_cmd(Monitor *mon, const QDict *qdict)
{
    help_cmd(mon, qdict_get_try_str(qdict, "name"));
}

#ifdef CONFIG_TRACE_SIMPLE
static void do_trace_event_set_state(Monitor *mon, const QDict *qdict)
{
    const char *tp_name = qdict_get_str(qdict, "name");
    bool new_state = qdict_get_bool(qdict, "option");
    int ret = trace_event_set_state(tp_name, new_state);

    if (!ret) {
        monitor_printf(mon, "unknown event name \"%s\"\n", tp_name);
    }
}

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

static void qmp_async_info_handler(Monitor *mon, const mon_cmd_t *cmd)
{
    cmd->mhandler.info_async(mon, qmp_monitor_complete, mon);
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

static void user_async_info_handler(Monitor *mon, const mon_cmd_t *cmd)
{
    int ret;

    MonitorCompletionData *cb_data = g_malloc(sizeof(*cb_data));
    cb_data->mon = mon;
    cb_data->user_print = cmd->user_print;
    monitor_suspend(mon);
    ret = cmd->mhandler.info_async(mon, user_monitor_complete, cb_data);
    if (ret < 0) {
        monitor_resume(mon);
        g_free(cb_data);
    }
}

static void do_info(Monitor *mon, const QDict *qdict)
{
    const mon_cmd_t *cmd;
    const char *item = qdict_get_try_str(qdict, "item");

    if (!item) {
        goto help;
    }

    for (cmd = info_cmds; cmd->name != NULL; cmd++) {
        if (compare_cmd(item, cmd->name))
            break;
    }

    if (cmd->name == NULL) {
        goto help;
    }

    if (handler_is_async(cmd)) {
        user_async_info_handler(mon, cmd);
    } else if (handler_is_qobject(cmd)) {
        QObject *info_data = NULL;

        cmd->mhandler.info_new(mon, &info_data);
        if (info_data) {
            cmd->user_print(mon, info_data);
            qobject_decref(info_data);
        }
    } else {
        cmd->mhandler.info(mon);
    }

    return;

help:
    help_cmd(mon, "info");
}

static void do_info_version_print(Monitor *mon, const QObject *data)
{
    QDict *qdict;
    QDict *qemu;

    qdict = qobject_to_qdict(data);
    qemu = qdict_get_qdict(qdict, "qemu");

    monitor_printf(mon, "%" PRId64 ".%" PRId64 ".%" PRId64 "%s\n",
                  qdict_get_int(qemu, "major"),
                  qdict_get_int(qemu, "minor"),
                  qdict_get_int(qemu, "micro"),
                  qdict_get_str(qdict, "package"));
}

static void do_info_version(Monitor *mon, QObject **ret_data)
{
    const char *version = QEMU_VERSION;
    int major = 0, minor = 0, micro = 0;
    char *tmp;

    major = strtol(version, &tmp, 10);
    tmp++;
    minor = strtol(tmp, &tmp, 10);
    tmp++;
    micro = strtol(tmp, &tmp, 10);

    *ret_data = qobject_from_jsonf("{ 'qemu': { 'major': %d, 'minor': %d, \
        'micro': %d }, 'package': %s }", major, minor, micro, QEMU_PKGVERSION);
}

static void do_info_name_print(Monitor *mon, const QObject *data)
{
    QDict *qdict;

    qdict = qobject_to_qdict(data);
    if (qdict_size(qdict) == 0) {
        return;
    }

    monitor_printf(mon, "%s\n", qdict_get_str(qdict, "name"));
}

static void do_info_name(Monitor *mon, QObject **ret_data)
{
    *ret_data = qemu_name ? qobject_from_jsonf("{'name': %s }", qemu_name) :
                            qobject_from_jsonf("{}");
}

static QObject *get_cmd_dict(const char *name)
{
    const char *p;

    /* Remove '|' from some commands */
    p = strchr(name, '|');
    if (p) {
        p++;
    } else {
        p = name;
    }

    return qobject_from_jsonf("{ 'name': %s }", p);
}

static void do_info_commands(Monitor *mon, QObject **ret_data)
{
    QList *cmd_list;
    const mon_cmd_t *cmd;

    cmd_list = qlist_new();

    for (cmd = qmp_cmds; cmd->name != NULL; cmd++) {
        qlist_append_obj(cmd_list, get_cmd_dict(cmd->name));
    }

    for (cmd = qmp_query_cmds; cmd->name != NULL; cmd++) {
        char buf[128];
        snprintf(buf, sizeof(buf), "query-%s", cmd->name);
        qlist_append_obj(cmd_list, get_cmd_dict(buf));
    }

    *ret_data = QOBJECT(cmd_list);
}

static void do_info_uuid_print(Monitor *mon, const QObject *data)
{
    monitor_printf(mon, "%s\n", qdict_get_str(qobject_to_qdict(data), "UUID"));
}

static void do_info_uuid(Monitor *mon, QObject **ret_data)
{
    char uuid[64];

    snprintf(uuid, sizeof(uuid), UUID_FMT, qemu_uuid[0], qemu_uuid[1],
                   qemu_uuid[2], qemu_uuid[3], qemu_uuid[4], qemu_uuid[5],
                   qemu_uuid[6], qemu_uuid[7], qemu_uuid[8], qemu_uuid[9],
                   qemu_uuid[10], qemu_uuid[11], qemu_uuid[12], qemu_uuid[13],
                   qemu_uuid[14], qemu_uuid[15]);
    *ret_data = qobject_from_jsonf("{ 'UUID': %s }", uuid);
}

/* get the current CPU defined by the user */
static int mon_set_cpu(int cpu_index)
{
    CPUState *env;

    for(env = first_cpu; env != NULL; env = env->next_cpu) {
        if (env->cpu_index == cpu_index) {
            cur_mon->mon_cpu = env;
            return 0;
        }
    }
    return -1;
}

static CPUState *mon_get_cpu(void)
{
    if (!cur_mon->mon_cpu) {
        mon_set_cpu(0);
    }
    cpu_synchronize_state(cur_mon->mon_cpu);
    return cur_mon->mon_cpu;
}

static void do_info_registers(Monitor *mon)
{
    CPUState *env;
    env = mon_get_cpu();
#ifdef TARGET_I386
    cpu_dump_state(env, (FILE *)mon, monitor_fprintf,
                   X86_DUMP_FPU);
#else
    cpu_dump_state(env, (FILE *)mon, monitor_fprintf,
                   0);
#endif
}

static void print_cpu_iter(QObject *obj, void *opaque)
{
    QDict *cpu;
    int active = ' ';
    Monitor *mon = opaque;

    assert(qobject_type(obj) == QTYPE_QDICT);
    cpu = qobject_to_qdict(obj);

    if (qdict_get_bool(cpu, "current")) {
        active = '*';
    }

    monitor_printf(mon, "%c CPU #%d: ", active, (int)qdict_get_int(cpu, "CPU"));

#if defined(TARGET_I386)
    monitor_printf(mon, "pc=0x" TARGET_FMT_lx,
                   (target_ulong) qdict_get_int(cpu, "pc"));
#elif defined(TARGET_PPC)
    monitor_printf(mon, "nip=0x" TARGET_FMT_lx,
                   (target_long) qdict_get_int(cpu, "nip"));
#elif defined(TARGET_SPARC)
    monitor_printf(mon, "pc=0x " TARGET_FMT_lx,
                   (target_long) qdict_get_int(cpu, "pc"));
    monitor_printf(mon, "npc=0x" TARGET_FMT_lx,
                   (target_long) qdict_get_int(cpu, "npc"));
#elif defined(TARGET_MIPS)
    monitor_printf(mon, "PC=0x" TARGET_FMT_lx,
                   (target_long) qdict_get_int(cpu, "PC"));
#endif

    if (qdict_get_bool(cpu, "halted")) {
        monitor_printf(mon, " (halted)");
    }

    monitor_printf(mon, " thread_id=%" PRId64 " ",
                   qdict_get_int(cpu, "thread_id"));

    monitor_printf(mon, "\n");
}

static void monitor_print_cpus(Monitor *mon, const QObject *data)
{
    QList *cpu_list;

    assert(qobject_type(data) == QTYPE_QLIST);
    cpu_list = qobject_to_qlist(data);
    qlist_iter(cpu_list, print_cpu_iter, mon);
}

static void do_info_cpus(Monitor *mon, QObject **ret_data)
{
    CPUState *env;
    QList *cpu_list;

    cpu_list = qlist_new();

    /* just to set the default cpu if not already done */
    mon_get_cpu();

    for(env = first_cpu; env != NULL; env = env->next_cpu) {
        QDict *cpu;
        QObject *obj;

        cpu_synchronize_state(env);

        obj = qobject_from_jsonf("{ 'CPU': %d, 'current': %i, 'halted': %i }",
                                 env->cpu_index, env == mon->mon_cpu,
                                 env->halted);

        cpu = qobject_to_qdict(obj);

#if defined(TARGET_I386)
        qdict_put(cpu, "pc", qint_from_int(env->eip + env->segs[R_CS].base));
#elif defined(TARGET_PPC)
        qdict_put(cpu, "nip", qint_from_int(env->nip));
#elif defined(TARGET_SPARC)
        qdict_put(cpu, "pc", qint_from_int(env->pc));
        qdict_put(cpu, "npc", qint_from_int(env->npc));
#elif defined(TARGET_MIPS)
        qdict_put(cpu, "PC", qint_from_int(env->active_tc.PC));
#endif
        qdict_put(cpu, "thread_id", qint_from_int(env->thread_id));

        qlist_append(cpu_list, cpu);
    }

    *ret_data = QOBJECT(cpu_list);
}

static int do_cpu_set(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    int index = qdict_get_int(qdict, "index");
    if (mon_set_cpu(index) < 0) {
        qerror_report(QERR_INVALID_PARAMETER_VALUE, "index",
                      "a CPU number");
        return -1;
    }
    return 0;
}

static void do_info_jit(Monitor *mon)
{
    dump_exec_info((FILE *)mon, monitor_fprintf);
}

static void do_info_history(Monitor *mon)
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

#if defined(TARGET_PPC)
/* XXX: not implemented in other targets */
static void do_info_cpu_stats(Monitor *mon)
{
    CPUState *env;

    env = mon_get_cpu();
    cpu_dump_statistics(env, (FILE *)mon, &monitor_fprintf, 0);
}
#endif

#if defined(CONFIG_TRACE_SIMPLE)
static void do_info_trace(Monitor *mon)
{
    st_print_trace((FILE *)mon, &monitor_fprintf);
}

static void do_trace_print_events(Monitor *mon)
{
    trace_print_events((FILE *)mon, &monitor_fprintf);
}
#endif

/**
 * do_quit(): Quit QEMU execution
 */
static int do_quit(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    monitor_suspend(mon);
    no_shutdown = 0;
    qemu_system_shutdown_request();

    return 0;
}

#ifdef CONFIG_VNC
static int change_vnc_password(const char *password)
{
    if (!password || !password[0]) {
        if (vnc_display_disable_login(NULL)) {
            qerror_report(QERR_SET_PASSWD_FAILED);
            return -1;
        }
        return 0;
    }

    if (vnc_display_password(NULL, password) < 0) {
        qerror_report(QERR_SET_PASSWD_FAILED);
        return -1;
    }

    return 0;
}

static void change_vnc_password_cb(Monitor *mon, const char *password,
                                   void *opaque)
{
    change_vnc_password(password);
    monitor_read_command(mon, 1);
}

static int do_change_vnc(Monitor *mon, const char *target, const char *arg)
{
    if (strcmp(target, "passwd") == 0 ||
        strcmp(target, "password") == 0) {
        if (arg) {
            char password[9];
            strncpy(password, arg, sizeof(password));
            password[sizeof(password) - 1] = '\0';
            return change_vnc_password(password);
        } else {
            return monitor_read_password(mon, change_vnc_password_cb, NULL);
        }
    } else {
        if (vnc_display_open(NULL, target) < 0) {
            qerror_report(QERR_VNC_SERVER_FAILED, target);
            return -1;
        }
    }

    return 0;
}
#else
static int do_change_vnc(Monitor *mon, const char *target, const char *arg)
{
    qerror_report(QERR_FEATURE_DISABLED, "vnc");
    return -ENODEV;
}
#endif

/**
 * do_change(): Change a removable medium, or VNC configuration
 */
static int do_change(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    const char *device = qdict_get_str(qdict, "device");
    const char *target = qdict_get_str(qdict, "target");
    const char *arg = qdict_get_try_str(qdict, "arg");
    int ret;

    if (strcmp(device, "vnc") == 0) {
        ret = do_change_vnc(mon, target, arg);
    } else {
        ret = do_change_block(mon, device, target, arg);
    }

    return ret;
}

static int set_password(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    const char *protocol  = qdict_get_str(qdict, "protocol");
    const char *password  = qdict_get_str(qdict, "password");
    const char *connected = qdict_get_try_str(qdict, "connected");
    int disconnect_if_connected = 0;
    int fail_if_connected = 0;
    int rc;

    if (connected) {
        if (strcmp(connected, "fail") == 0) {
            fail_if_connected = 1;
        } else if (strcmp(connected, "disconnect") == 0) {
            disconnect_if_connected = 1;
        } else if (strcmp(connected, "keep") == 0) {
            /* nothing */
        } else {
            qerror_report(QERR_INVALID_PARAMETER, "connected");
            return -1;
        }
    }

    if (strcmp(protocol, "spice") == 0) {
        if (!using_spice) {
            /* correct one? spice isn't a device ,,, */
            qerror_report(QERR_DEVICE_NOT_ACTIVE, "spice");
            return -1;
        }
        rc = qemu_spice_set_passwd(password, fail_if_connected,
                                   disconnect_if_connected);
        if (rc != 0) {
            qerror_report(QERR_SET_PASSWD_FAILED);
            return -1;
        }
        return 0;
    }

    if (strcmp(protocol, "vnc") == 0) {
        if (fail_if_connected || disconnect_if_connected) {
            /* vnc supports "connected=keep" only */
            qerror_report(QERR_INVALID_PARAMETER, "connected");
            return -1;
        }
        /* Note that setting an empty password will not disable login through
         * this interface. */
        return vnc_display_password(NULL, password);
    }

    qerror_report(QERR_INVALID_PARAMETER, "protocol");
    return -1;
}

static int expire_password(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    const char *protocol  = qdict_get_str(qdict, "protocol");
    const char *whenstr = qdict_get_str(qdict, "time");
    time_t when;
    int rc;

    if (strcmp(whenstr, "now") == 0) {
        when = 0;
    } else if (strcmp(whenstr, "never") == 0) {
        when = TIME_MAX;
    } else if (whenstr[0] == '+') {
        when = time(NULL) + strtoull(whenstr+1, NULL, 10);
    } else {
        when = strtoull(whenstr, NULL, 10);
    }

    if (strcmp(protocol, "spice") == 0) {
        if (!using_spice) {
            /* correct one? spice isn't a device ,,, */
            qerror_report(QERR_DEVICE_NOT_ACTIVE, "spice");
            return -1;
        }
        rc = qemu_spice_set_pw_expire(when);
        if (rc != 0) {
            qerror_report(QERR_SET_PASSWD_FAILED);
            return -1;
        }
        return 0;
    }

    if (strcmp(protocol, "vnc") == 0) {
        return vnc_display_pw_expire(NULL, when);
    }

    qerror_report(QERR_INVALID_PARAMETER, "protocol");
    return -1;
}

static int add_graphics_client(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    const char *protocol  = qdict_get_str(qdict, "protocol");
    const char *fdname = qdict_get_str(qdict, "fdname");
    CharDriverState *s;

    if (strcmp(protocol, "spice") == 0) {
        if (!using_spice) {
            /* correct one? spice isn't a device ,,, */
            qerror_report(QERR_DEVICE_NOT_ACTIVE, "spice");
            return -1;
        }
	qerror_report(QERR_ADD_CLIENT_FAILED);
	return -1;
#ifdef CONFIG_VNC
    } else if (strcmp(protocol, "vnc") == 0) {
	int fd = monitor_get_fd(mon, fdname);
        int skipauth = qdict_get_try_bool(qdict, "skipauth", 0);
	vnc_display_add_client(NULL, fd, skipauth);
	return 0;
#endif
    } else if ((s = qemu_chr_find(protocol)) != NULL) {
	int fd = monitor_get_fd(mon, fdname);
	if (qemu_chr_add_client(s, fd) < 0) {
	    qerror_report(QERR_ADD_CLIENT_FAILED);
	    return -1;
	}
	return 0;
    }

    qerror_report(QERR_INVALID_PARAMETER, "protocol");
    return -1;
}

static int client_migrate_info(Monitor *mon, const QDict *qdict, QObject **ret_data)
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

        ret = qemu_spice_migrate_info(hostname, port, tls_port, subject);
        if (ret != 0) {
            qerror_report(QERR_UNDEFINED_ERROR);
            return -1;
        }
        return 0;
    }

    qerror_report(QERR_INVALID_PARAMETER, "protocol");
    return -1;
}

static int do_screen_dump(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    vga_hw_screen_dump(qdict_get_str(qdict, "filename"));
    return 0;
}

static void do_logfile(Monitor *mon, const QDict *qdict)
{
    cpu_set_log_filename(qdict_get_str(qdict, "filename"));
}

static void do_log(Monitor *mon, const QDict *qdict)
{
    int mask;
    const char *items = qdict_get_str(qdict, "items");

    if (!strcmp(items, "none")) {
        mask = 0;
    } else {
        mask = cpu_str_to_log_mask(items);
        if (!mask) {
            help_cmd(mon, "log");
            return;
        }
    }
    cpu_set_log(mask);
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

/**
 * do_stop(): Stop VM execution
 */
static int do_stop(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    vm_stop(VMSTOP_USER);
    return 0;
}

static void encrypted_bdrv_it(void *opaque, BlockDriverState *bs);

struct bdrv_iterate_context {
    Monitor *mon;
    int err;
};

/**
 * do_cont(): Resume emulation.
 */
static int do_cont(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    struct bdrv_iterate_context context = { mon, 0 };

    if (incoming_expected) {
        qerror_report(QERR_MIGRATION_EXPECTED);
        return -1;
    }
    bdrv_iterate(encrypted_bdrv_it, &context);
    /* only resume the vm if all keys are set and valid */
    if (!context.err) {
        vm_start();
        return 0;
    } else {
        return -1;
    }
}

static void bdrv_key_cb(void *opaque, int err)
{
    Monitor *mon = opaque;

    /* another key was set successfully, retry to continue */
    if (!err)
        do_cont(mon, NULL, NULL);
}

static void encrypted_bdrv_it(void *opaque, BlockDriverState *bs)
{
    struct bdrv_iterate_context *context = opaque;

    if (!context->err && bdrv_key_required(bs)) {
        context->err = -EBUSY;
        monitor_read_bdrv_key_start(context->mon, bs, bdrv_key_cb,
                                    context->mon);
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
                        target_phys_addr_t addr, int is_physical)
{
    CPUState *env;
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
            if (cpu_memory_rw_debug(env, addr, buf, l, 0) < 0) {
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
    target_phys_addr_t addr = qdict_get_int(qdict, "addr");

    memory_dump(mon, count, format, size, addr, 1);
}

static void do_print(Monitor *mon, const QDict *qdict)
{
    int format = qdict_get_int(qdict, "format");
    target_phys_addr_t val = qdict_get_int(qdict, "val");

#if TARGET_PHYS_ADDR_BITS == 32
    switch(format) {
    case 'o':
        monitor_printf(mon, "%#o", val);
        break;
    case 'x':
        monitor_printf(mon, "%#x", val);
        break;
    case 'u':
        monitor_printf(mon, "%u", val);
        break;
    default:
    case 'd':
        monitor_printf(mon, "%d", val);
        break;
    case 'c':
        monitor_printc(mon, val);
        break;
    }
#else
    switch(format) {
    case 'o':
        monitor_printf(mon, "%#" PRIo64, val);
        break;
    case 'x':
        monitor_printf(mon, "%#" PRIx64, val);
        break;
    case 'u':
        monitor_printf(mon, "%" PRIu64, val);
        break;
    default:
    case 'd':
        monitor_printf(mon, "%" PRId64, val);
        break;
    case 'c':
        monitor_printc(mon, val);
        break;
    }
#endif
    monitor_printf(mon, "\n");
}

static int do_memory_save(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    FILE *f;
    uint32_t size = qdict_get_int(qdict, "size");
    const char *filename = qdict_get_str(qdict, "filename");
    target_long addr = qdict_get_int(qdict, "val");
    uint32_t l;
    CPUState *env;
    uint8_t buf[1024];
    int ret = -1;

    env = mon_get_cpu();

    f = fopen(filename, "wb");
    if (!f) {
        qerror_report(QERR_OPEN_FILE_FAILED, filename);
        return -1;
    }
    while (size != 0) {
        l = sizeof(buf);
        if (l > size)
            l = size;
        cpu_memory_rw_debug(env, addr, buf, l, 0);
        if (fwrite(buf, 1, l, f) != l) {
            monitor_printf(mon, "fwrite() error in do_memory_save\n");
            goto exit;
        }
        addr += l;
        size -= l;
    }

    ret = 0;

exit:
    fclose(f);
    return ret;
}

static int do_physical_memory_save(Monitor *mon, const QDict *qdict,
                                    QObject **ret_data)
{
    FILE *f;
    uint32_t l;
    uint8_t buf[1024];
    uint32_t size = qdict_get_int(qdict, "size");
    const char *filename = qdict_get_str(qdict, "filename");
    target_phys_addr_t addr = qdict_get_int(qdict, "val");
    int ret = -1;

    f = fopen(filename, "wb");
    if (!f) {
        qerror_report(QERR_OPEN_FILE_FAILED, filename);
        return -1;
    }
    while (size != 0) {
        l = sizeof(buf);
        if (l > size)
            l = size;
        cpu_physical_memory_read(addr, buf, l);
        if (fwrite(buf, 1, l, f) != l) {
            monitor_printf(mon, "fwrite() error in do_physical_memory_save\n");
            goto exit;
        }
        fflush(f);
        addr += l;
        size -= l;
    }

    ret = 0;

exit:
    fclose(f);
    return ret;
}

static void do_sum(Monitor *mon, const QDict *qdict)
{
    uint32_t addr;
    uint16_t sum;
    uint32_t start = qdict_get_int(qdict, "start");
    uint32_t size = qdict_get_int(qdict, "size");

    sum = 0;
    for(addr = start; addr < (start + size); addr++) {
        uint8_t val = ldub_phys(addr);
        /* BSD sum algorithm ('sum' Unix command) */
        sum = (sum >> 1) | (sum << 15);
        sum += val;
    }
    monitor_printf(mon, "%05d\n", sum);
}

typedef struct {
    int keycode;
    const char *name;
} KeyDef;

static const KeyDef key_defs[] = {
    { 0x2a, "shift" },
    { 0x36, "shift_r" },

    { 0x38, "alt" },
    { 0xb8, "alt_r" },
    { 0x64, "altgr" },
    { 0xe4, "altgr_r" },
    { 0x1d, "ctrl" },
    { 0x9d, "ctrl_r" },

    { 0xdd, "menu" },

    { 0x01, "esc" },

    { 0x02, "1" },
    { 0x03, "2" },
    { 0x04, "3" },
    { 0x05, "4" },
    { 0x06, "5" },
    { 0x07, "6" },
    { 0x08, "7" },
    { 0x09, "8" },
    { 0x0a, "9" },
    { 0x0b, "0" },
    { 0x0c, "minus" },
    { 0x0d, "equal" },
    { 0x0e, "backspace" },

    { 0x0f, "tab" },
    { 0x10, "q" },
    { 0x11, "w" },
    { 0x12, "e" },
    { 0x13, "r" },
    { 0x14, "t" },
    { 0x15, "y" },
    { 0x16, "u" },
    { 0x17, "i" },
    { 0x18, "o" },
    { 0x19, "p" },
    { 0x1a, "bracket_left" },
    { 0x1b, "bracket_right" },
    { 0x1c, "ret" },

    { 0x1e, "a" },
    { 0x1f, "s" },
    { 0x20, "d" },
    { 0x21, "f" },
    { 0x22, "g" },
    { 0x23, "h" },
    { 0x24, "j" },
    { 0x25, "k" },
    { 0x26, "l" },
    { 0x27, "semicolon" },
    { 0x28, "apostrophe" },
    { 0x29, "grave_accent" },

    { 0x2b, "backslash" },
    { 0x2c, "z" },
    { 0x2d, "x" },
    { 0x2e, "c" },
    { 0x2f, "v" },
    { 0x30, "b" },
    { 0x31, "n" },
    { 0x32, "m" },
    { 0x33, "comma" },
    { 0x34, "dot" },
    { 0x35, "slash" },

    { 0x37, "asterisk" },

    { 0x39, "spc" },
    { 0x3a, "caps_lock" },
    { 0x3b, "f1" },
    { 0x3c, "f2" },
    { 0x3d, "f3" },
    { 0x3e, "f4" },
    { 0x3f, "f5" },
    { 0x40, "f6" },
    { 0x41, "f7" },
    { 0x42, "f8" },
    { 0x43, "f9" },
    { 0x44, "f10" },
    { 0x45, "num_lock" },
    { 0x46, "scroll_lock" },

    { 0xb5, "kp_divide" },
    { 0x37, "kp_multiply" },
    { 0x4a, "kp_subtract" },
    { 0x4e, "kp_add" },
    { 0x9c, "kp_enter" },
    { 0x53, "kp_decimal" },
    { 0x54, "sysrq" },

    { 0x52, "kp_0" },
    { 0x4f, "kp_1" },
    { 0x50, "kp_2" },
    { 0x51, "kp_3" },
    { 0x4b, "kp_4" },
    { 0x4c, "kp_5" },
    { 0x4d, "kp_6" },
    { 0x47, "kp_7" },
    { 0x48, "kp_8" },
    { 0x49, "kp_9" },

    { 0x56, "<" },

    { 0x57, "f11" },
    { 0x58, "f12" },

    { 0xb7, "print" },

    { 0xc7, "home" },
    { 0xc9, "pgup" },
    { 0xd1, "pgdn" },
    { 0xcf, "end" },

    { 0xcb, "left" },
    { 0xc8, "up" },
    { 0xd0, "down" },
    { 0xcd, "right" },

    { 0xd2, "insert" },
    { 0xd3, "delete" },
#if defined(TARGET_SPARC) && !defined(TARGET_SPARC64)
    { 0xf0, "stop" },
    { 0xf1, "again" },
    { 0xf2, "props" },
    { 0xf3, "undo" },
    { 0xf4, "front" },
    { 0xf5, "copy" },
    { 0xf6, "open" },
    { 0xf7, "paste" },
    { 0xf8, "find" },
    { 0xf9, "cut" },
    { 0xfa, "lf" },
    { 0xfb, "help" },
    { 0xfc, "meta_l" },
    { 0xfd, "meta_r" },
    { 0xfe, "compose" },
#endif
    { 0, NULL },
};

static int get_keycode(const char *key)
{
    const KeyDef *p;
    char *endp;
    int ret;

    for(p = key_defs; p->name != NULL; p++) {
        if (!strcmp(key, p->name))
            return p->keycode;
    }
    if (strstart(key, "0x", NULL)) {
        ret = strtoul(key, &endp, 0);
        if (*endp == '\0' && ret >= 0x01 && ret <= 0xff)
            return ret;
    }
    return -1;
}

#define MAX_KEYCODES 16
static uint8_t keycodes[MAX_KEYCODES];
static int nb_pending_keycodes;
static QEMUTimer *key_timer;

static void release_keys(void *opaque)
{
    int keycode;

    while (nb_pending_keycodes > 0) {
        nb_pending_keycodes--;
        keycode = keycodes[nb_pending_keycodes];
        if (keycode & 0x80)
            kbd_put_keycode(0xe0);
        kbd_put_keycode(keycode | 0x80);
    }
}

static void do_sendkey(Monitor *mon, const QDict *qdict)
{
    char keyname_buf[16];
    char *separator;
    int keyname_len, keycode, i;
    const char *string = qdict_get_str(qdict, "string");
    int has_hold_time = qdict_haskey(qdict, "hold_time");
    int hold_time = qdict_get_try_int(qdict, "hold_time", -1);

    if (nb_pending_keycodes > 0) {
        qemu_del_timer(key_timer);
        release_keys(NULL);
    }
    if (!has_hold_time)
        hold_time = 100;
    i = 0;
    while (1) {
        separator = strchr(string, '-');
        keyname_len = separator ? separator - string : strlen(string);
        if (keyname_len > 0) {
            pstrcpy(keyname_buf, sizeof(keyname_buf), string);
            if (keyname_len > sizeof(keyname_buf) - 1) {
                monitor_printf(mon, "invalid key: '%s...'\n", keyname_buf);
                return;
            }
            if (i == MAX_KEYCODES) {
                monitor_printf(mon, "too many keys\n");
                return;
            }
            keyname_buf[keyname_len] = 0;
            keycode = get_keycode(keyname_buf);
            if (keycode < 0) {
                monitor_printf(mon, "unknown key: '%s'\n", keyname_buf);
                return;
            }
            keycodes[i++] = keycode;
        }
        if (!separator)
            break;
        string = separator + 1;
    }
    nb_pending_keycodes = i;
    /* key down events */
    for (i = 0; i < nb_pending_keycodes; i++) {
        keycode = keycodes[i];
        if (keycode & 0x80)
            kbd_put_keycode(0xe0);
        kbd_put_keycode(keycode & 0x7f);
    }
    /* delayed key up events */
    qemu_mod_timer(key_timer, qemu_get_clock_ns(vm_clock) +
                   muldiv64(get_ticks_per_sec(), hold_time, 1000));
}

static int mouse_button_state;

static void do_mouse_move(Monitor *mon, const QDict *qdict)
{
    int dx, dy, dz;
    const char *dx_str = qdict_get_str(qdict, "dx_str");
    const char *dy_str = qdict_get_str(qdict, "dy_str");
    const char *dz_str = qdict_get_try_str(qdict, "dz_str");
    dx = strtol(dx_str, NULL, 0);
    dy = strtol(dy_str, NULL, 0);
    dz = 0;
    if (dz_str)
        dz = strtol(dz_str, NULL, 0);
    kbd_mouse_event(dx, dy, dz, mouse_button_state);
}

static void do_mouse_button(Monitor *mon, const QDict *qdict)
{
    int button_state = qdict_get_int(qdict, "button_state");
    mouse_button_state = button_state;
    kbd_mouse_event(0, 0, 0, mouse_button_state);
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

/**
 * do_system_reset(): Issue a machine reset
 */
static int do_system_reset(Monitor *mon, const QDict *qdict,
                           QObject **ret_data)
{
    qemu_system_reset_request();
    return 0;
}

/**
 * do_system_powerdown(): Issue a machine powerdown
 */
static int do_system_powerdown(Monitor *mon, const QDict *qdict,
                               QObject **ret_data)
{
    qemu_system_powerdown_request();
    return 0;
}

#if defined(TARGET_I386)
static void print_pte(Monitor *mon, target_phys_addr_t addr,
                      target_phys_addr_t pte,
                      target_phys_addr_t mask)
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

static void tlb_info_32(Monitor *mon, CPUState *env)
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

static void tlb_info_pae32(Monitor *mon, CPUState *env)
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
                                  ~((target_phys_addr_t)(1 << 20) - 1));
                    } else {
                        pt_addr = pde & 0x3fffffffff000ULL;
                        for (l3 = 0; l3 < 512; l3++) {
                            cpu_physical_memory_read(pt_addr + l3 * 8, &pte, 8);
                            pte = le64_to_cpu(pte);
                            if (pte & PG_PRESENT_MASK) {
                                print_pte(mon, (l1 << 30 ) + (l2 << 21)
                                          + (l3 << 12),
                                          pte & ~PG_PSE_MASK,
                                          ~(target_phys_addr_t)0xfff);
                            }
                        }
                    }
                }
            }
        }
    }
}

#ifdef TARGET_X86_64
static void tlb_info_64(Monitor *mon, CPUState *env)
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

static void tlb_info(Monitor *mon)
{
    CPUState *env;

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

static void mem_print(Monitor *mon, target_phys_addr_t *pstart,
                      int *plast_prot,
                      target_phys_addr_t end, int prot)
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

static void mem_info_32(Monitor *mon, CPUState *env)
{
    unsigned int l1, l2;
    int prot, last_prot;
    uint32_t pgd, pde, pte;
    target_phys_addr_t start, end;

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
    mem_print(mon, &start, &last_prot, (target_phys_addr_t)1 << 32, 0);
}

static void mem_info_pae32(Monitor *mon, CPUState *env)
{
    unsigned int l1, l2, l3;
    int prot, last_prot;
    uint64_t pdpe, pde, pte;
    uint64_t pdp_addr, pd_addr, pt_addr;
    target_phys_addr_t start, end;

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
    mem_print(mon, &start, &last_prot, (target_phys_addr_t)1 << 32, 0);
}


#ifdef TARGET_X86_64
static void mem_info_64(Monitor *mon, CPUState *env)
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
    mem_print(mon, &start, &last_prot, (target_phys_addr_t)1 << 48, 0);
}
#endif

static void mem_info(Monitor *mon)
{
    CPUState *env;

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

static void tlb_info(Monitor *mon)
{
    CPUState *env = mon_get_cpu();
    int i;

    monitor_printf (mon, "ITLB:\n");
    for (i = 0 ; i < ITLB_SIZE ; i++)
        print_tlb (mon, i, &env->itlb[i]);
    monitor_printf (mon, "UTLB:\n");
    for (i = 0 ; i < UTLB_SIZE ; i++)
        print_tlb (mon, i, &env->utlb[i]);
}

#endif

#if defined(TARGET_SPARC)
static void tlb_info(Monitor *mon)
{
    CPUState *env1 = mon_get_cpu();

    dump_mmu((FILE*)mon, (fprintf_function)monitor_printf, env1);
}
#endif

static void do_info_kvm_print(Monitor *mon, const QObject *data)
{
    QDict *qdict;

    qdict = qobject_to_qdict(data);

    monitor_printf(mon, "kvm support: ");
    if (qdict_get_bool(qdict, "present")) {
        monitor_printf(mon, "%s\n", qdict_get_bool(qdict, "enabled") ?
                                    "enabled" : "disabled");
    } else {
        monitor_printf(mon, "not compiled\n");
    }
}

static void do_info_kvm(Monitor *mon, QObject **ret_data)
{
#ifdef CONFIG_KVM
    *ret_data = qobject_from_jsonf("{ 'enabled': %i, 'present': true }",
                                   kvm_enabled());
#else
    *ret_data = qobject_from_jsonf("{ 'enabled': false, 'present': false }");
#endif
}

static void do_info_numa(Monitor *mon)
{
    int i;
    CPUState *env;

    monitor_printf(mon, "%d nodes\n", nb_numa_nodes);
    for (i = 0; i < nb_numa_nodes; i++) {
        monitor_printf(mon, "node %d cpus:", i);
        for (env = first_cpu; env != NULL; env = env->next_cpu) {
            if (env->numa_node == i) {
                monitor_printf(mon, " %d", env->cpu_index);
            }
        }
        monitor_printf(mon, "\n");
        monitor_printf(mon, "node %d size: %" PRId64 " MB\n", i,
            node_mem[i] >> 20);
    }
}

#ifdef CONFIG_PROFILER

int64_t qemu_time;
int64_t dev_time;

static void do_info_profile(Monitor *mon)
{
    int64_t total;
    total = qemu_time;
    if (total == 0)
        total = 1;
    monitor_printf(mon, "async time  %" PRId64 " (%0.3f)\n",
                   dev_time, dev_time / (double)get_ticks_per_sec());
    monitor_printf(mon, "qemu time   %" PRId64 " (%0.3f)\n",
                   qemu_time, qemu_time / (double)get_ticks_per_sec());
    qemu_time = 0;
    dev_time = 0;
}
#else
static void do_info_profile(Monitor *mon)
{
    monitor_printf(mon, "Internal profiler not compiled\n");
}
#endif

/* Capture support */
static QLIST_HEAD (capture_list_head, CaptureState) capture_head;

static void do_info_capture(Monitor *mon)
{
    int i;
    CaptureState *s;

    for (s = capture_head.lh_first, i = 0; s; s = s->entries.le_next, ++i) {
        monitor_printf(mon, "[%d]: ", i);
        s->ops.info (s->opaque);
    }
}

#ifdef HAS_AUDIO
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
#endif

#if defined(TARGET_I386)
static int do_inject_nmi(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    CPUState *env;

    for (env = first_cpu; env != NULL; env = env->next_cpu) {
        cpu_interrupt(env, CPU_INTERRUPT_NMI);
    }

    return 0;
}
#else
static int do_inject_nmi(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    qerror_report(QERR_UNSUPPORTED);
    return -1;
}
#endif

static void do_info_status_print(Monitor *mon, const QObject *data)
{
    QDict *qdict;

    qdict = qobject_to_qdict(data);

    monitor_printf(mon, "VM status: ");
    if (qdict_get_bool(qdict, "running")) {
        monitor_printf(mon, "running");
        if (qdict_get_bool(qdict, "singlestep")) {
            monitor_printf(mon, " (single step mode)");
        }
    } else {
        monitor_printf(mon, "paused");
    }

    monitor_printf(mon, "\n");
}

static void do_info_status(Monitor *mon, QObject **ret_data)
{
    *ret_data = qobject_from_jsonf("{ 'running': %i, 'singlestep': %i }",
                                    vm_running, singlestep);
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
    CPUState *cenv;
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
    for (cenv = first_cpu; cenv != NULL; cenv = cenv->next_cpu) {
        if (cenv->cpu_index == cpu_index) {
            cpu_x86_inject_mce(mon, cenv, bank, status, mcg_status, addr, misc,
                               flags);
            break;
        }
    }
}
#endif

static int do_getfd(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    const char *fdname = qdict_get_str(qdict, "fdname");
    mon_fd_t *monfd;
    int fd;

    fd = qemu_chr_fe_get_msgfd(mon->chr);
    if (fd == -1) {
        qerror_report(QERR_FD_NOT_SUPPLIED);
        return -1;
    }

    if (qemu_isdigit(fdname[0])) {
        qerror_report(QERR_INVALID_PARAMETER_VALUE, "fdname",
                      "a name not starting with a digit");
        return -1;
    }

    QLIST_FOREACH(monfd, &mon->fds, next) {
        if (strcmp(monfd->name, fdname) != 0) {
            continue;
        }

        close(monfd->fd);
        monfd->fd = fd;
        return 0;
    }

    monfd = g_malloc0(sizeof(mon_fd_t));
    monfd->name = g_strdup(fdname);
    monfd->fd = fd;

    QLIST_INSERT_HEAD(&mon->fds, monfd, next);
    return 0;
}

static int do_closefd(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    const char *fdname = qdict_get_str(qdict, "fdname");
    mon_fd_t *monfd;

    QLIST_FOREACH(monfd, &mon->fds, next) {
        if (strcmp(monfd->name, fdname) != 0) {
            continue;
        }

        QLIST_REMOVE(monfd, next);
        close(monfd->fd);
        g_free(monfd->name);
        g_free(monfd);
        return 0;
    }

    qerror_report(QERR_FD_NOT_FOUND, fdname);
    return -1;
}

static void do_loadvm(Monitor *mon, const QDict *qdict)
{
    int saved_vm_running  = vm_running;
    const char *name = qdict_get_str(qdict, "name");

    vm_stop(VMSTOP_LOADVM);

    if (load_vmstate(name) == 0 && saved_vm_running) {
        vm_start();
    }
}

int monitor_get_fd(Monitor *mon, const char *fdname)
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

    return -1;
}

static const mon_cmd_t mon_cmds[] = {
#include "hmp-commands.h"
    { NULL, NULL, },
};

/* Please update hmp-commands.hx when adding or changing commands */
static const mon_cmd_t info_cmds[] = {
    {
        .name       = "version",
        .args_type  = "",
        .params     = "",
        .help       = "show the version of QEMU",
        .user_print = do_info_version_print,
        .mhandler.info_new = do_info_version,
    },
    {
        .name       = "network",
        .args_type  = "",
        .params     = "",
        .help       = "show the network state",
        .mhandler.info = do_info_network,
    },
    {
        .name       = "chardev",
        .args_type  = "",
        .params     = "",
        .help       = "show the character devices",
        .user_print = qemu_chr_info_print,
        .mhandler.info_new = qemu_chr_info,
    },
    {
        .name       = "block",
        .args_type  = "",
        .params     = "",
        .help       = "show the block devices",
        .user_print = bdrv_info_print,
        .mhandler.info_new = bdrv_info,
    },
    {
        .name       = "blockstats",
        .args_type  = "",
        .params     = "",
        .help       = "show block device statistics",
        .user_print = bdrv_stats_print,
        .mhandler.info_new = bdrv_info_stats,
    },
    {
        .name       = "registers",
        .args_type  = "",
        .params     = "",
        .help       = "show the cpu registers",
        .mhandler.info = do_info_registers,
    },
    {
        .name       = "cpus",
        .args_type  = "",
        .params     = "",
        .help       = "show infos for each CPU",
        .user_print = monitor_print_cpus,
        .mhandler.info_new = do_info_cpus,
    },
    {
        .name       = "history",
        .args_type  = "",
        .params     = "",
        .help       = "show the command line history",
        .mhandler.info = do_info_history,
    },
    {
        .name       = "irq",
        .args_type  = "",
        .params     = "",
        .help       = "show the interrupts statistics (if available)",
        .mhandler.info = irq_info,
    },
    {
        .name       = "pic",
        .args_type  = "",
        .params     = "",
        .help       = "show i8259 (PIC) state",
        .mhandler.info = pic_info,
    },
    {
        .name       = "pci",
        .args_type  = "",
        .params     = "",
        .help       = "show PCI info",
        .user_print = do_pci_info_print,
        .mhandler.info_new = do_pci_info,
    },
#if defined(TARGET_I386) || defined(TARGET_SH4) || defined(TARGET_SPARC)
    {
        .name       = "tlb",
        .args_type  = "",
        .params     = "",
        .help       = "show virtual to physical memory mappings",
        .mhandler.info = tlb_info,
    },
#endif
#if defined(TARGET_I386)
    {
        .name       = "mem",
        .args_type  = "",
        .params     = "",
        .help       = "show the active virtual memory mappings",
        .mhandler.info = mem_info,
    },
#endif
    {
        .name       = "jit",
        .args_type  = "",
        .params     = "",
        .help       = "show dynamic compiler info",
        .mhandler.info = do_info_jit,
    },
    {
        .name       = "kvm",
        .args_type  = "",
        .params     = "",
        .help       = "show KVM information",
        .user_print = do_info_kvm_print,
        .mhandler.info_new = do_info_kvm,
    },
    {
        .name       = "numa",
        .args_type  = "",
        .params     = "",
        .help       = "show NUMA information",
        .mhandler.info = do_info_numa,
    },
    {
        .name       = "usb",
        .args_type  = "",
        .params     = "",
        .help       = "show guest USB devices",
        .mhandler.info = usb_info,
    },
    {
        .name       = "usbhost",
        .args_type  = "",
        .params     = "",
        .help       = "show host USB devices",
        .mhandler.info = usb_host_info,
    },
    {
        .name       = "profile",
        .args_type  = "",
        .params     = "",
        .help       = "show profiling information",
        .mhandler.info = do_info_profile,
    },
    {
        .name       = "capture",
        .args_type  = "",
        .params     = "",
        .help       = "show capture information",
        .mhandler.info = do_info_capture,
    },
    {
        .name       = "snapshots",
        .args_type  = "",
        .params     = "",
        .help       = "show the currently saved VM snapshots",
        .mhandler.info = do_info_snapshots,
    },
    {
        .name       = "status",
        .args_type  = "",
        .params     = "",
        .help       = "show the current VM status (running|paused)",
        .user_print = do_info_status_print,
        .mhandler.info_new = do_info_status,
    },
    {
        .name       = "pcmcia",
        .args_type  = "",
        .params     = "",
        .help       = "show guest PCMCIA status",
        .mhandler.info = pcmcia_info,
    },
    {
        .name       = "mice",
        .args_type  = "",
        .params     = "",
        .help       = "show which guest mouse is receiving events",
        .user_print = do_info_mice_print,
        .mhandler.info_new = do_info_mice,
    },
    {
        .name       = "vnc",
        .args_type  = "",
        .params     = "",
        .help       = "show the vnc server status",
        .user_print = do_info_vnc_print,
        .mhandler.info_new = do_info_vnc,
    },
#if defined(CONFIG_SPICE)
    {
        .name       = "spice",
        .args_type  = "",
        .params     = "",
        .help       = "show the spice server status",
        .user_print = do_info_spice_print,
        .mhandler.info_new = do_info_spice,
    },
#endif
    {
        .name       = "name",
        .args_type  = "",
        .params     = "",
        .help       = "show the current VM name",
        .user_print = do_info_name_print,
        .mhandler.info_new = do_info_name,
    },
    {
        .name       = "uuid",
        .args_type  = "",
        .params     = "",
        .help       = "show the current VM UUID",
        .user_print = do_info_uuid_print,
        .mhandler.info_new = do_info_uuid,
    },
#if defined(TARGET_PPC)
    {
        .name       = "cpustats",
        .args_type  = "",
        .params     = "",
        .help       = "show CPU statistics",
        .mhandler.info = do_info_cpu_stats,
    },
#endif
#if defined(CONFIG_SLIRP)
    {
        .name       = "usernet",
        .args_type  = "",
        .params     = "",
        .help       = "show user network stack connection states",
        .mhandler.info = do_info_usernet,
    },
#endif
    {
        .name       = "migrate",
        .args_type  = "",
        .params     = "",
        .help       = "show migration status",
        .user_print = do_info_migrate_print,
        .mhandler.info_new = do_info_migrate,
    },
    {
        .name       = "balloon",
        .args_type  = "",
        .params     = "",
        .help       = "show balloon information",
        .user_print = monitor_print_balloon,
        .mhandler.info_async = do_info_balloon,
        .flags      = MONITOR_CMD_ASYNC,
    },
    {
        .name       = "qtree",
        .args_type  = "",
        .params     = "",
        .help       = "show device tree",
        .mhandler.info = do_info_qtree,
    },
    {
        .name       = "qdm",
        .args_type  = "",
        .params     = "",
        .help       = "show qdev device model list",
        .mhandler.info = do_info_qdm,
    },
    {
        .name       = "roms",
        .args_type  = "",
        .params     = "",
        .help       = "show roms",
        .mhandler.info = do_info_roms,
    },
#if defined(CONFIG_TRACE_SIMPLE)
    {
        .name       = "trace",
        .args_type  = "",
        .params     = "",
        .help       = "show current contents of trace buffer",
        .mhandler.info = do_info_trace,
    },
    {
        .name       = "trace-events",
        .args_type  = "",
        .params     = "",
        .help       = "show available trace-events & their state",
        .mhandler.info = do_trace_print_events,
    },
#endif
    {
        .name       = NULL,
    },
};

static const mon_cmd_t qmp_cmds[] = {
#include "qmp-commands.h"
    { /* NULL */ },
};

static const mon_cmd_t qmp_query_cmds[] = {
    {
        .name       = "version",
        .args_type  = "",
        .params     = "",
        .help       = "show the version of QEMU",
        .user_print = do_info_version_print,
        .mhandler.info_new = do_info_version,
    },
    {
        .name       = "commands",
        .args_type  = "",
        .params     = "",
        .help       = "list QMP available commands",
        .user_print = monitor_user_noop,
        .mhandler.info_new = do_info_commands,
    },
    {
        .name       = "chardev",
        .args_type  = "",
        .params     = "",
        .help       = "show the character devices",
        .user_print = qemu_chr_info_print,
        .mhandler.info_new = qemu_chr_info,
    },
    {
        .name       = "block",
        .args_type  = "",
        .params     = "",
        .help       = "show the block devices",
        .user_print = bdrv_info_print,
        .mhandler.info_new = bdrv_info,
    },
    {
        .name       = "blockstats",
        .args_type  = "",
        .params     = "",
        .help       = "show block device statistics",
        .user_print = bdrv_stats_print,
        .mhandler.info_new = bdrv_info_stats,
    },
    {
        .name       = "cpus",
        .args_type  = "",
        .params     = "",
        .help       = "show infos for each CPU",
        .user_print = monitor_print_cpus,
        .mhandler.info_new = do_info_cpus,
    },
    {
        .name       = "pci",
        .args_type  = "",
        .params     = "",
        .help       = "show PCI info",
        .user_print = do_pci_info_print,
        .mhandler.info_new = do_pci_info,
    },
    {
        .name       = "kvm",
        .args_type  = "",
        .params     = "",
        .help       = "show KVM information",
        .user_print = do_info_kvm_print,
        .mhandler.info_new = do_info_kvm,
    },
    {
        .name       = "status",
        .args_type  = "",
        .params     = "",
        .help       = "show the current VM status (running|paused)",
        .user_print = do_info_status_print,
        .mhandler.info_new = do_info_status,
    },
    {
        .name       = "mice",
        .args_type  = "",
        .params     = "",
        .help       = "show which guest mouse is receiving events",
        .user_print = do_info_mice_print,
        .mhandler.info_new = do_info_mice,
    },
    {
        .name       = "vnc",
        .args_type  = "",
        .params     = "",
        .help       = "show the vnc server status",
        .user_print = do_info_vnc_print,
        .mhandler.info_new = do_info_vnc,
    },
#if defined(CONFIG_SPICE)
    {
        .name       = "spice",
        .args_type  = "",
        .params     = "",
        .help       = "show the spice server status",
        .user_print = do_info_spice_print,
        .mhandler.info_new = do_info_spice,
    },
#endif
    {
        .name       = "name",
        .args_type  = "",
        .params     = "",
        .help       = "show the current VM name",
        .user_print = do_info_name_print,
        .mhandler.info_new = do_info_name,
    },
    {
        .name       = "uuid",
        .args_type  = "",
        .params     = "",
        .help       = "show the current VM UUID",
        .user_print = do_info_uuid_print,
        .mhandler.info_new = do_info_uuid,
    },
    {
        .name       = "migrate",
        .args_type  = "",
        .params     = "",
        .help       = "show migration status",
        .user_print = do_info_migrate_print,
        .mhandler.info_new = do_info_migrate,
    },
    {
        .name       = "balloon",
        .args_type  = "",
        .params     = "",
        .help       = "show balloon information",
        .user_print = monitor_print_balloon,
        .mhandler.info_async = do_info_balloon,
        .flags      = MONITOR_CMD_ASYNC,
    },
    { /* NULL */ },
};

/*******************************************************************/

static const char *pch;
static jmp_buf expr_env;

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
    CPUState *env = mon_get_cpu();
    return env->eip + env->segs[R_CS].base;
}
#endif

#if defined(TARGET_PPC)
static target_long monitor_get_ccr (const struct MonitorDef *md, int val)
{
    CPUState *env = mon_get_cpu();
    unsigned int u;
    int i;

    u = 0;
    for (i = 0; i < 8; i++)
        u |= env->crf[i] << (32 - (4 * i));

    return u;
}

static target_long monitor_get_msr (const struct MonitorDef *md, int val)
{
    CPUState *env = mon_get_cpu();
    return env->msr;
}

static target_long monitor_get_xer (const struct MonitorDef *md, int val)
{
    CPUState *env = mon_get_cpu();
    return env->xer;
}

static target_long monitor_get_decr (const struct MonitorDef *md, int val)
{
    CPUState *env = mon_get_cpu();
    return cpu_ppc_load_decr(env);
}

static target_long monitor_get_tbu (const struct MonitorDef *md, int val)
{
    CPUState *env = mon_get_cpu();
    return cpu_ppc_load_tbu(env);
}

static target_long monitor_get_tbl (const struct MonitorDef *md, int val)
{
    CPUState *env = mon_get_cpu();
    return cpu_ppc_load_tbl(env);
}
#endif

#if defined(TARGET_SPARC)
#ifndef TARGET_SPARC64
static target_long monitor_get_psr (const struct MonitorDef *md, int val)
{
    CPUState *env = mon_get_cpu();

    return cpu_get_psr(env);
}
#endif

static target_long monitor_get_reg(const struct MonitorDef *md, int val)
{
    CPUState *env = mon_get_cpu();
    return env->regwptr[val];
}
#endif

static const MonitorDef monitor_defs[] = {
#ifdef TARGET_I386

#define SEG(name, seg) \
    { name, offsetof(CPUState, segs[seg].selector), NULL, MD_I32 },\
    { name ".base", offsetof(CPUState, segs[seg].base) },\
    { name ".limit", offsetof(CPUState, segs[seg].limit), NULL, MD_I32 },

    { "eax", offsetof(CPUState, regs[0]) },
    { "ecx", offsetof(CPUState, regs[1]) },
    { "edx", offsetof(CPUState, regs[2]) },
    { "ebx", offsetof(CPUState, regs[3]) },
    { "esp|sp", offsetof(CPUState, regs[4]) },
    { "ebp|fp", offsetof(CPUState, regs[5]) },
    { "esi", offsetof(CPUState, regs[6]) },
    { "edi", offsetof(CPUState, regs[7]) },
#ifdef TARGET_X86_64
    { "r8", offsetof(CPUState, regs[8]) },
    { "r9", offsetof(CPUState, regs[9]) },
    { "r10", offsetof(CPUState, regs[10]) },
    { "r11", offsetof(CPUState, regs[11]) },
    { "r12", offsetof(CPUState, regs[12]) },
    { "r13", offsetof(CPUState, regs[13]) },
    { "r14", offsetof(CPUState, regs[14]) },
    { "r15", offsetof(CPUState, regs[15]) },
#endif
    { "eflags", offsetof(CPUState, eflags) },
    { "eip", offsetof(CPUState, eip) },
    SEG("cs", R_CS)
    SEG("ds", R_DS)
    SEG("es", R_ES)
    SEG("ss", R_SS)
    SEG("fs", R_FS)
    SEG("gs", R_GS)
    { "pc", 0, monitor_get_pc, },
#elif defined(TARGET_PPC)
    /* General purpose registers */
    { "r0", offsetof(CPUState, gpr[0]) },
    { "r1", offsetof(CPUState, gpr[1]) },
    { "r2", offsetof(CPUState, gpr[2]) },
    { "r3", offsetof(CPUState, gpr[3]) },
    { "r4", offsetof(CPUState, gpr[4]) },
    { "r5", offsetof(CPUState, gpr[5]) },
    { "r6", offsetof(CPUState, gpr[6]) },
    { "r7", offsetof(CPUState, gpr[7]) },
    { "r8", offsetof(CPUState, gpr[8]) },
    { "r9", offsetof(CPUState, gpr[9]) },
    { "r10", offsetof(CPUState, gpr[10]) },
    { "r11", offsetof(CPUState, gpr[11]) },
    { "r12", offsetof(CPUState, gpr[12]) },
    { "r13", offsetof(CPUState, gpr[13]) },
    { "r14", offsetof(CPUState, gpr[14]) },
    { "r15", offsetof(CPUState, gpr[15]) },
    { "r16", offsetof(CPUState, gpr[16]) },
    { "r17", offsetof(CPUState, gpr[17]) },
    { "r18", offsetof(CPUState, gpr[18]) },
    { "r19", offsetof(CPUState, gpr[19]) },
    { "r20", offsetof(CPUState, gpr[20]) },
    { "r21", offsetof(CPUState, gpr[21]) },
    { "r22", offsetof(CPUState, gpr[22]) },
    { "r23", offsetof(CPUState, gpr[23]) },
    { "r24", offsetof(CPUState, gpr[24]) },
    { "r25", offsetof(CPUState, gpr[25]) },
    { "r26", offsetof(CPUState, gpr[26]) },
    { "r27", offsetof(CPUState, gpr[27]) },
    { "r28", offsetof(CPUState, gpr[28]) },
    { "r29", offsetof(CPUState, gpr[29]) },
    { "r30", offsetof(CPUState, gpr[30]) },
    { "r31", offsetof(CPUState, gpr[31]) },
    /* Floating point registers */
    { "f0", offsetof(CPUState, fpr[0]) },
    { "f1", offsetof(CPUState, fpr[1]) },
    { "f2", offsetof(CPUState, fpr[2]) },
    { "f3", offsetof(CPUState, fpr[3]) },
    { "f4", offsetof(CPUState, fpr[4]) },
    { "f5", offsetof(CPUState, fpr[5]) },
    { "f6", offsetof(CPUState, fpr[6]) },
    { "f7", offsetof(CPUState, fpr[7]) },
    { "f8", offsetof(CPUState, fpr[8]) },
    { "f9", offsetof(CPUState, fpr[9]) },
    { "f10", offsetof(CPUState, fpr[10]) },
    { "f11", offsetof(CPUState, fpr[11]) },
    { "f12", offsetof(CPUState, fpr[12]) },
    { "f13", offsetof(CPUState, fpr[13]) },
    { "f14", offsetof(CPUState, fpr[14]) },
    { "f15", offsetof(CPUState, fpr[15]) },
    { "f16", offsetof(CPUState, fpr[16]) },
    { "f17", offsetof(CPUState, fpr[17]) },
    { "f18", offsetof(CPUState, fpr[18]) },
    { "f19", offsetof(CPUState, fpr[19]) },
    { "f20", offsetof(CPUState, fpr[20]) },
    { "f21", offsetof(CPUState, fpr[21]) },
    { "f22", offsetof(CPUState, fpr[22]) },
    { "f23", offsetof(CPUState, fpr[23]) },
    { "f24", offsetof(CPUState, fpr[24]) },
    { "f25", offsetof(CPUState, fpr[25]) },
    { "f26", offsetof(CPUState, fpr[26]) },
    { "f27", offsetof(CPUState, fpr[27]) },
    { "f28", offsetof(CPUState, fpr[28]) },
    { "f29", offsetof(CPUState, fpr[29]) },
    { "f30", offsetof(CPUState, fpr[30]) },
    { "f31", offsetof(CPUState, fpr[31]) },
    { "fpscr", offsetof(CPUState, fpscr) },
    /* Next instruction pointer */
    { "nip|pc", offsetof(CPUState, nip) },
    { "lr", offsetof(CPUState, lr) },
    { "ctr", offsetof(CPUState, ctr) },
    { "decr", 0, &monitor_get_decr, },
    { "ccr", 0, &monitor_get_ccr, },
    /* Machine state register */
    { "msr", 0, &monitor_get_msr, },
    { "xer", 0, &monitor_get_xer, },
    { "tbu", 0, &monitor_get_tbu, },
    { "tbl", 0, &monitor_get_tbl, },
#if defined(TARGET_PPC64)
    /* Address space register */
    { "asr", offsetof(CPUState, asr) },
#endif
    /* Segment registers */
    { "sdr1", offsetof(CPUState, spr[SPR_SDR1]) },
    { "sr0", offsetof(CPUState, sr[0]) },
    { "sr1", offsetof(CPUState, sr[1]) },
    { "sr2", offsetof(CPUState, sr[2]) },
    { "sr3", offsetof(CPUState, sr[3]) },
    { "sr4", offsetof(CPUState, sr[4]) },
    { "sr5", offsetof(CPUState, sr[5]) },
    { "sr6", offsetof(CPUState, sr[6]) },
    { "sr7", offsetof(CPUState, sr[7]) },
    { "sr8", offsetof(CPUState, sr[8]) },
    { "sr9", offsetof(CPUState, sr[9]) },
    { "sr10", offsetof(CPUState, sr[10]) },
    { "sr11", offsetof(CPUState, sr[11]) },
    { "sr12", offsetof(CPUState, sr[12]) },
    { "sr13", offsetof(CPUState, sr[13]) },
    { "sr14", offsetof(CPUState, sr[14]) },
    { "sr15", offsetof(CPUState, sr[15]) },
    /* Too lazy to put BATs... */
    { "pvr", offsetof(CPUState, spr[SPR_PVR]) },

    { "srr0", offsetof(CPUState, spr[SPR_SRR0]) },
    { "srr1", offsetof(CPUState, spr[SPR_SRR1]) },
    { "sprg0", offsetof(CPUState, spr[SPR_SPRG0]) },
    { "sprg1", offsetof(CPUState, spr[SPR_SPRG1]) },
    { "sprg2", offsetof(CPUState, spr[SPR_SPRG2]) },
    { "sprg3", offsetof(CPUState, spr[SPR_SPRG3]) },
    { "sprg4", offsetof(CPUState, spr[SPR_SPRG4]) },
    { "sprg5", offsetof(CPUState, spr[SPR_SPRG5]) },
    { "sprg6", offsetof(CPUState, spr[SPR_SPRG6]) },
    { "sprg7", offsetof(CPUState, spr[SPR_SPRG7]) },
    { "pid", offsetof(CPUState, spr[SPR_BOOKE_PID]) },
    { "csrr0", offsetof(CPUState, spr[SPR_BOOKE_CSRR0]) },
    { "csrr1", offsetof(CPUState, spr[SPR_BOOKE_CSRR1]) },
    { "esr", offsetof(CPUState, spr[SPR_BOOKE_ESR]) },
    { "dear", offsetof(CPUState, spr[SPR_BOOKE_DEAR]) },
    { "mcsr", offsetof(CPUState, spr[SPR_BOOKE_MCSR]) },
    { "tsr", offsetof(CPUState, spr[SPR_BOOKE_TSR]) },
    { "tcr", offsetof(CPUState, spr[SPR_BOOKE_TCR]) },
    { "vrsave", offsetof(CPUState, spr[SPR_VRSAVE]) },
    { "pir", offsetof(CPUState, spr[SPR_BOOKE_PIR]) },
    { "mcsrr0", offsetof(CPUState, spr[SPR_BOOKE_MCSRR0]) },
    { "mcsrr1", offsetof(CPUState, spr[SPR_BOOKE_MCSRR1]) },
    { "decar", offsetof(CPUState, spr[SPR_BOOKE_DECAR]) },
    { "ivpr", offsetof(CPUState, spr[SPR_BOOKE_IVPR]) },
    { "epcr", offsetof(CPUState, spr[SPR_BOOKE_EPCR]) },
    { "sprg8", offsetof(CPUState, spr[SPR_BOOKE_SPRG8]) },
    { "ivor0", offsetof(CPUState, spr[SPR_BOOKE_IVOR0]) },
    { "ivor1", offsetof(CPUState, spr[SPR_BOOKE_IVOR1]) },
    { "ivor2", offsetof(CPUState, spr[SPR_BOOKE_IVOR2]) },
    { "ivor3", offsetof(CPUState, spr[SPR_BOOKE_IVOR3]) },
    { "ivor4", offsetof(CPUState, spr[SPR_BOOKE_IVOR4]) },
    { "ivor5", offsetof(CPUState, spr[SPR_BOOKE_IVOR5]) },
    { "ivor6", offsetof(CPUState, spr[SPR_BOOKE_IVOR6]) },
    { "ivor7", offsetof(CPUState, spr[SPR_BOOKE_IVOR7]) },
    { "ivor8", offsetof(CPUState, spr[SPR_BOOKE_IVOR8]) },
    { "ivor9", offsetof(CPUState, spr[SPR_BOOKE_IVOR9]) },
    { "ivor10", offsetof(CPUState, spr[SPR_BOOKE_IVOR10]) },
    { "ivor11", offsetof(CPUState, spr[SPR_BOOKE_IVOR11]) },
    { "ivor12", offsetof(CPUState, spr[SPR_BOOKE_IVOR12]) },
    { "ivor13", offsetof(CPUState, spr[SPR_BOOKE_IVOR13]) },
    { "ivor14", offsetof(CPUState, spr[SPR_BOOKE_IVOR14]) },
    { "ivor15", offsetof(CPUState, spr[SPR_BOOKE_IVOR15]) },
    { "ivor32", offsetof(CPUState, spr[SPR_BOOKE_IVOR32]) },
    { "ivor33", offsetof(CPUState, spr[SPR_BOOKE_IVOR33]) },
    { "ivor34", offsetof(CPUState, spr[SPR_BOOKE_IVOR34]) },
    { "ivor35", offsetof(CPUState, spr[SPR_BOOKE_IVOR35]) },
    { "ivor36", offsetof(CPUState, spr[SPR_BOOKE_IVOR36]) },
    { "ivor37", offsetof(CPUState, spr[SPR_BOOKE_IVOR37]) },
    { "mas0", offsetof(CPUState, spr[SPR_BOOKE_MAS0]) },
    { "mas1", offsetof(CPUState, spr[SPR_BOOKE_MAS1]) },
    { "mas2", offsetof(CPUState, spr[SPR_BOOKE_MAS2]) },
    { "mas3", offsetof(CPUState, spr[SPR_BOOKE_MAS3]) },
    { "mas4", offsetof(CPUState, spr[SPR_BOOKE_MAS4]) },
    { "mas6", offsetof(CPUState, spr[SPR_BOOKE_MAS6]) },
    { "mas7", offsetof(CPUState, spr[SPR_BOOKE_MAS7]) },
    { "mmucfg", offsetof(CPUState, spr[SPR_MMUCFG]) },
    { "tlb0cfg", offsetof(CPUState, spr[SPR_BOOKE_TLB0CFG]) },
    { "tlb1cfg", offsetof(CPUState, spr[SPR_BOOKE_TLB1CFG]) },
    { "epr", offsetof(CPUState, spr[SPR_BOOKE_EPR]) },
    { "eplc", offsetof(CPUState, spr[SPR_BOOKE_EPLC]) },
    { "epsc", offsetof(CPUState, spr[SPR_BOOKE_EPSC]) },
    { "svr", offsetof(CPUState, spr[SPR_E500_SVR]) },
    { "mcar", offsetof(CPUState, spr[SPR_Exxx_MCAR]) },
    { "pid1", offsetof(CPUState, spr[SPR_BOOKE_PID1]) },
    { "pid2", offsetof(CPUState, spr[SPR_BOOKE_PID2]) },
    { "hid0", offsetof(CPUState, spr[SPR_HID0]) },

#elif defined(TARGET_SPARC)
    { "g0", offsetof(CPUState, gregs[0]) },
    { "g1", offsetof(CPUState, gregs[1]) },
    { "g2", offsetof(CPUState, gregs[2]) },
    { "g3", offsetof(CPUState, gregs[3]) },
    { "g4", offsetof(CPUState, gregs[4]) },
    { "g5", offsetof(CPUState, gregs[5]) },
    { "g6", offsetof(CPUState, gregs[6]) },
    { "g7", offsetof(CPUState, gregs[7]) },
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
    { "pc", offsetof(CPUState, pc) },
    { "npc", offsetof(CPUState, npc) },
    { "y", offsetof(CPUState, y) },
#ifndef TARGET_SPARC64
    { "psr", 0, &monitor_get_psr, },
    { "wim", offsetof(CPUState, wim) },
#endif
    { "tbr", offsetof(CPUState, tbr) },
    { "fsr", offsetof(CPUState, fsr) },
    { "f0", offsetof(CPUState, fpr[0]) },
    { "f1", offsetof(CPUState, fpr[1]) },
    { "f2", offsetof(CPUState, fpr[2]) },
    { "f3", offsetof(CPUState, fpr[3]) },
    { "f4", offsetof(CPUState, fpr[4]) },
    { "f5", offsetof(CPUState, fpr[5]) },
    { "f6", offsetof(CPUState, fpr[6]) },
    { "f7", offsetof(CPUState, fpr[7]) },
    { "f8", offsetof(CPUState, fpr[8]) },
    { "f9", offsetof(CPUState, fpr[9]) },
    { "f10", offsetof(CPUState, fpr[10]) },
    { "f11", offsetof(CPUState, fpr[11]) },
    { "f12", offsetof(CPUState, fpr[12]) },
    { "f13", offsetof(CPUState, fpr[13]) },
    { "f14", offsetof(CPUState, fpr[14]) },
    { "f15", offsetof(CPUState, fpr[15]) },
    { "f16", offsetof(CPUState, fpr[16]) },
    { "f17", offsetof(CPUState, fpr[17]) },
    { "f18", offsetof(CPUState, fpr[18]) },
    { "f19", offsetof(CPUState, fpr[19]) },
    { "f20", offsetof(CPUState, fpr[20]) },
    { "f21", offsetof(CPUState, fpr[21]) },
    { "f22", offsetof(CPUState, fpr[22]) },
    { "f23", offsetof(CPUState, fpr[23]) },
    { "f24", offsetof(CPUState, fpr[24]) },
    { "f25", offsetof(CPUState, fpr[25]) },
    { "f26", offsetof(CPUState, fpr[26]) },
    { "f27", offsetof(CPUState, fpr[27]) },
    { "f28", offsetof(CPUState, fpr[28]) },
    { "f29", offsetof(CPUState, fpr[29]) },
    { "f30", offsetof(CPUState, fpr[30]) },
    { "f31", offsetof(CPUState, fpr[31]) },
#ifdef TARGET_SPARC64
    { "f32", offsetof(CPUState, fpr[32]) },
    { "f34", offsetof(CPUState, fpr[34]) },
    { "f36", offsetof(CPUState, fpr[36]) },
    { "f38", offsetof(CPUState, fpr[38]) },
    { "f40", offsetof(CPUState, fpr[40]) },
    { "f42", offsetof(CPUState, fpr[42]) },
    { "f44", offsetof(CPUState, fpr[44]) },
    { "f46", offsetof(CPUState, fpr[46]) },
    { "f48", offsetof(CPUState, fpr[48]) },
    { "f50", offsetof(CPUState, fpr[50]) },
    { "f52", offsetof(CPUState, fpr[52]) },
    { "f54", offsetof(CPUState, fpr[54]) },
    { "f56", offsetof(CPUState, fpr[56]) },
    { "f58", offsetof(CPUState, fpr[58]) },
    { "f60", offsetof(CPUState, fpr[60]) },
    { "f62", offsetof(CPUState, fpr[62]) },
    { "asi", offsetof(CPUState, asi) },
    { "pstate", offsetof(CPUState, pstate) },
    { "cansave", offsetof(CPUState, cansave) },
    { "canrestore", offsetof(CPUState, canrestore) },
    { "otherwin", offsetof(CPUState, otherwin) },
    { "wstate", offsetof(CPUState, wstate) },
    { "cleanwin", offsetof(CPUState, cleanwin) },
    { "fprs", offsetof(CPUState, fprs) },
#endif
#endif
    { NULL },
};

static void expr_error(Monitor *mon, const char *msg)
{
    monitor_printf(mon, "%s\n", msg);
    longjmp(expr_env, 1);
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
                CPUState *env = mon_get_cpu();
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
#if TARGET_PHYS_ADDR_BITS > 32
        n = strtoull(pch, &p, 0);
#else
        n = strtoul(pch, &p, 0);
#endif
        if (pch == p) {
            expr_error(mon, "invalid char in expression");
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
    if (setjmp(expr_env)) {
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

static int get_str(char *buf, int buf_size, const char **pp)
{
    const char *p;
    char *q;
    int c;

    q = buf;
    p = *pp;
    while (qemu_isspace(*p))
        p++;
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
                switch(c) {
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

#define MAX_ARGS 16

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

static const mon_cmd_t *monitor_find_command(const char *cmdname)
{
    return search_dispatch_table(mon_cmds, cmdname);
}

static const mon_cmd_t *qmp_find_query_cmd(const char *info_item)
{
    return search_dispatch_table(qmp_query_cmds, info_item);
}

static const mon_cmd_t *qmp_find_cmd(const char *cmdname)
{
    return search_dispatch_table(qmp_cmds, cmdname);
}

static const mon_cmd_t *monitor_parse_command(Monitor *mon,
                                              const char *cmdline,
                                              QDict *qdict)
{
    const char *p, *typestr;
    int c;
    const mon_cmd_t *cmd;
    char cmdname[256];
    char buf[1024];
    char *key;

#ifdef DEBUG
    monitor_printf(mon, "command='%s'\n", cmdline);
#endif

    /* extract the command name */
    p = get_command_name(cmdline, cmdname, sizeof(cmdname));
    if (!p)
        return NULL;

    cmd = monitor_find_command(cmdname);
    if (!cmd) {
        monitor_printf(mon, "unknown command: '%s'\n", cmdname);
        return NULL;
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
        MON_DEBUG("Additional error report at %s:%d\n",
                  qerror->file, qerror->linenr);
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
        MON_DEBUG("command '%s' returned failure but did not pass an error\n",
                  cmd->name);
    }

#ifdef CONFIG_DEBUG_MONITOR
    if (!ret && monitor_has_error(mon)) {
        /*
         * If it returns success, it must not have passed an error.
         *
         * Action: Report the passed error to the client.
         */
        MON_DEBUG("command '%s' returned success but passed an error\n",
                  cmd->name);
    }

    if (mon_print_count_get(mon) > 0 && strcmp(cmd->name, "info") != 0) {
        /*
         * Handlers should not call Monitor print functions.
         *
         * Action: Ignore them in QMP.
         *
         * (XXX: we don't check any 'info' or 'query' command here
         * because the user print function _is_ called by do_info(), hence
         * we will trigger this check. This problem will go away when we
         * make 'query' commands real and kill do_info())
         */
        MON_DEBUG("command '%s' called print functions %d time(s)\n",
                  cmd->name, mon_print_count_get(mon));
    }
#endif
}

static void handle_user_command(Monitor *mon, const char *cmdline)
{
    QDict *qdict;
    const mon_cmd_t *cmd;

    qdict = qdict_new();

    cmd = monitor_parse_command(mon, cmdline, qdict);
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

static void cmd_completion(const char *name, const char *list)
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
            readline_add_completion(cur_mon->rs, cmd);
        }
        if (*p == '\0')
            break;
        p++;
    }
}

static void file_completion(const char *input)
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
    monitor_printf(cur_mon, "input='%s' path='%s' prefix='%s'\n",
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
            stat(file, &sb);
            if(S_ISDIR(sb.st_mode))
                pstrcat(file, sizeof(file), "/");
            readline_add_completion(cur_mon->rs, file);
        }
    }
    closedir(ffs);
}

static void block_completion_it(void *opaque, BlockDriverState *bs)
{
    const char *name = bdrv_get_device_name(bs);
    const char *input = opaque;

    if (input[0] == '\0' ||
        !strncmp(name, (char *)input, strlen(input))) {
        readline_add_completion(cur_mon->rs, name);
    }
}

/* NOTE: this parser is an approximate form of the real command parser */
static void parse_cmdline(const char *cmdline,
                         int *pnb_args, char **args)
{
    const char *p;
    int nb_args, ret;
    char buf[1024];

    p = cmdline;
    nb_args = 0;
    for(;;) {
        while (qemu_isspace(*p))
            p++;
        if (*p == '\0')
            break;
        if (nb_args >= MAX_ARGS)
            break;
        ret = get_str(buf, sizeof(buf), &p);
        args[nb_args] = g_strdup(buf);
        nb_args++;
        if (ret < 0)
            break;
    }
    *pnb_args = nb_args;
}

static const char *next_arg_type(const char *typestr)
{
    const char *p = strchr(typestr, ':');
    return (p != NULL ? ++p : typestr);
}

static void monitor_find_completion(const char *cmdline)
{
    const char *cmdname;
    char *args[MAX_ARGS];
    int nb_args, i, len;
    const char *ptype, *str;
    const mon_cmd_t *cmd;
    const KeyDef *key;

    parse_cmdline(cmdline, &nb_args, args);
#ifdef DEBUG_COMPLETION
    for(i = 0; i < nb_args; i++) {
        monitor_printf(cur_mon, "arg%d = '%s'\n", i, (char *)args[i]);
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
    if (nb_args <= 1) {
        /* command completion */
        if (nb_args == 0)
            cmdname = "";
        else
            cmdname = args[0];
        readline_set_completion_index(cur_mon->rs, strlen(cmdname));
        for(cmd = mon_cmds; cmd->name != NULL; cmd++) {
            cmd_completion(cmdname, cmd->name);
        }
    } else {
        /* find the command */
        for (cmd = mon_cmds; cmd->name != NULL; cmd++) {
            if (compare_cmd(args[0], cmd->name)) {
                break;
            }
        }
        if (!cmd->name) {
            goto cleanup;
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
            readline_set_completion_index(cur_mon->rs, strlen(str));
            file_completion(str);
            break;
        case 'B':
            /* block device name completion */
            readline_set_completion_index(cur_mon->rs, strlen(str));
            bdrv_iterate(block_completion_it, (void *)str);
            break;
        case 's':
            /* XXX: more generic ? */
            if (!strcmp(cmd->name, "info")) {
                readline_set_completion_index(cur_mon->rs, strlen(str));
                for(cmd = info_cmds; cmd->name != NULL; cmd++) {
                    cmd_completion(str, cmd->name);
                }
            } else if (!strcmp(cmd->name, "sendkey")) {
                char *sep = strrchr(str, '-');
                if (sep)
                    str = sep + 1;
                readline_set_completion_index(cur_mon->rs, strlen(str));
                for(key = key_defs; key->name != NULL; key++) {
                    cmd_completion(str, key->name);
                }
            } else if (!strcmp(cmd->name, "help|?")) {
                readline_set_completion_index(cur_mon->rs, strlen(str));
                for (cmd = mon_cmds; cmd->name != NULL; cmd++) {
                    cmd_completion(str, cmd->name);
                }
            }
            break;
        default:
            break;
        }
    }

cleanup:
    for (i = 0; i < nb_args; i++) {
        g_free(args[i]);
    }
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

static void qmp_call_query_cmd(Monitor *mon, const mon_cmd_t *cmd)
{
    QObject *ret_data = NULL;

    if (handler_is_async(cmd)) {
        qmp_async_info_handler(mon, cmd);
        if (monitor_has_error(mon)) {
            monitor_protocol_emitter(mon, NULL);
        }
    } else {
        cmd->mhandler.info_new(mon, &ret_data);
        monitor_protocol_emitter(mon, ret_data);
        qobject_decref(ret_data);
    }
}

static void qmp_call_cmd(Monitor *mon, const mon_cmd_t *cmd,
                         const QDict *params)
{
    int ret;
    QObject *data = NULL;

    mon_print_count_init(mon);

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
    Monitor *mon = cur_mon;
    const char *cmd_name, *query_cmd;

    query_cmd = NULL;
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
    if (invalid_qmp_mode(mon, cmd_name)) {
        qerror_report(QERR_COMMAND_NOT_FOUND, cmd_name);
        goto err_out;
    }

    if (strstart(cmd_name, "query-", &query_cmd)) {
        cmd = qmp_find_query_cmd(query_cmd);
    } else {
        cmd = qmp_find_cmd(cmd_name);
    }

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

    if (query_cmd) {
        qmp_call_query_cmd(mon, cmd);
    } else if (handler_is_async(cmd)) {
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

static void monitor_command_cb(Monitor *mon, const char *cmdline, void *opaque)
{
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
    QObject *ver;

    do_info_version(NULL, &ver);
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
        json_message_parser_init(&mon->mc->parser, handle_qmp_command);
        data = get_qmp_greeting();
        monitor_json_emitter(mon, data);
        qobject_decref(data);
        break;
    case CHR_EVENT_CLOSED:
        json_message_parser_destroy(&mon->mc->parser);
        break;
    }
}

static void monitor_event(void *opaque, int event)
{
    Monitor *mon = opaque;

    switch (event) {
    case CHR_EVENT_MUX_IN:
        mon->mux_out = 0;
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
        mon->mux_out = 1;
        break;

    case CHR_EVENT_OPENED:
        monitor_printf(mon, "QEMU %s monitor - type 'help' for more "
                       "information\n", QEMU_VERSION);
        if (!mon->mux_out) {
            readline_show_prompt(mon->rs);
        }
        mon->reset_seen = 1;
        break;
    }
}


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 8
 * End:
 */

void monitor_init(CharDriverState *chr, int flags)
{
    static int is_first_init = 1;
    Monitor *mon;

    if (is_first_init) {
        key_timer = qemu_new_timer_ns(vm_clock, release_keys, NULL);
        is_first_init = 0;
    }

    mon = g_malloc0(sizeof(*mon));

    mon->chr = chr;
    mon->flags = flags;
    if (flags & MONITOR_USE_READLINE) {
        mon->rs = readline_init(mon, monitor_find_completion);
        monitor_read_command(mon, 0);
    }

    if (monitor_ctrl_mode(mon)) {
        mon->mc = g_malloc0(sizeof(MonitorControl));
        /* Control mode requires special handlers */
        qemu_chr_add_handlers(chr, monitor_can_read, monitor_control_read,
                              monitor_control_event, mon);
        qemu_chr_fe_set_echo(chr, true);
    } else {
        qemu_chr_add_handlers(chr, monitor_can_read, monitor_read,
                              monitor_event, mon);
    }

    QLIST_INSERT_HEAD(&mon_list, mon, entry);
    if (!default_mon || (flags & MONITOR_IS_DEFAULT))
        default_mon = mon;
}

static void bdrv_password_cb(Monitor *mon, const char *password, void *opaque)
{
    BlockDriverState *bs = opaque;
    int ret = 0;

    if (bdrv_set_key(bs, password) != 0) {
        monitor_printf(mon, "invalid password\n");
        ret = -EPERM;
    }
    if (mon->password_completion_cb)
        mon->password_completion_cb(mon->password_opaque, ret);

    monitor_read_command(mon, 1);
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
        qerror_report(QERR_DEVICE_ENCRYPTED, bdrv_get_device_name(bs));
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
