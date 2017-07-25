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
#include "qemu/cutils.h"
#include "monitor/monitor.h"
#include "sysemu/sysemu.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "chardev/char.h"
#include "qmp-commands.h"
#include "qapi-visit.h"
#include "sysemu/replay.h"
#include "qemu/help_option.h"

#include "chardev/char-mux.h"

/***********************************************************/
/* character device */

static Object *get_chardevs_root(void)
{
    return container_get(object_get_root(), "/chardevs");
}

void qemu_chr_be_event(Chardev *s, int event)
{
    CharBackend *be = s->be;

    /* Keep track if the char device is open */
    switch (event) {
        case CHR_EVENT_OPENED:
            s->be_open = 1;
            break;
        case CHR_EVENT_CLOSED:
            s->be_open = 0;
            break;
    }

    if (!be || !be->chr_event) {
        return;
    }

    be->chr_event(be->opaque, event);
}

/* Not reporting errors from writing to logfile, as logs are
 * defined to be "best effort" only */
static void qemu_chr_write_log(Chardev *s, const uint8_t *buf, size_t len)
{
    size_t done = 0;
    ssize_t ret;

    if (s->logfd < 0) {
        return;
    }

    while (done < len) {
    retry:
        ret = write(s->logfd, buf + done, len - done);
        if (ret == -1 && errno == EAGAIN) {
            g_usleep(100);
            goto retry;
        }

        if (ret <= 0) {
            return;
        }
        done += ret;
    }
}

static int qemu_chr_write_buffer(Chardev *s,
                                 const uint8_t *buf, int len,
                                 int *offset, bool write_all)
{
    ChardevClass *cc = CHARDEV_GET_CLASS(s);
    int res = 0;
    *offset = 0;

    qemu_mutex_lock(&s->chr_write_lock);
    while (*offset < len) {
    retry:
        res = cc->chr_write(s, buf + *offset, len - *offset);
        if (res < 0 && errno == EAGAIN && write_all) {
            g_usleep(100);
            goto retry;
        }

        if (res <= 0) {
            break;
        }

        *offset += res;
        if (!write_all) {
            break;
        }
    }
    if (*offset > 0) {
        qemu_chr_write_log(s, buf, *offset);
    }
    qemu_mutex_unlock(&s->chr_write_lock);

    return res;
}

int qemu_chr_write(Chardev *s, const uint8_t *buf, int len, bool write_all)
{
    int offset = 0;
    int res;

    if (qemu_chr_replay(s) && replay_mode == REPLAY_MODE_PLAY) {
        replay_char_write_event_load(&res, &offset);
        assert(offset <= len);
        qemu_chr_write_buffer(s, buf, offset, &offset, true);
        return res;
    }

    res = qemu_chr_write_buffer(s, buf, len, &offset, write_all);

    if (qemu_chr_replay(s) && replay_mode == REPLAY_MODE_RECORD) {
        replay_char_write_event_save(res, offset);
    }

    if (res < 0) {
        return res;
    }
    return offset;
}

int qemu_chr_be_can_write(Chardev *s)
{
    CharBackend *be = s->be;

    if (!be || !be->chr_can_read) {
        return 0;
    }

    return be->chr_can_read(be->opaque);
}

void qemu_chr_be_write_impl(Chardev *s, uint8_t *buf, int len)
{
    CharBackend *be = s->be;

    if (be && be->chr_read) {
        be->chr_read(be->opaque, buf, len);
    }
}

void qemu_chr_be_write(Chardev *s, uint8_t *buf, int len)
{
    if (qemu_chr_replay(s)) {
        if (replay_mode == REPLAY_MODE_PLAY) {
            return;
        }
        replay_chr_be_write(s, buf, len);
    } else {
        qemu_chr_be_write_impl(s, buf, len);
    }
}

int qemu_chr_add_client(Chardev *s, int fd)
{
    return CHARDEV_GET_CLASS(s)->chr_add_client ?
        CHARDEV_GET_CLASS(s)->chr_add_client(s, fd) : -1;
}

static void qemu_char_open(Chardev *chr, ChardevBackend *backend,
                           bool *be_opened, Error **errp)
{
    ChardevClass *cc = CHARDEV_GET_CLASS(chr);
    /* Any ChardevCommon member would work */
    ChardevCommon *common = backend ? backend->u.null.data : NULL;

    if (common && common->has_logfile) {
        int flags = O_WRONLY | O_CREAT;
        if (common->has_logappend &&
            common->logappend) {
            flags |= O_APPEND;
        } else {
            flags |= O_TRUNC;
        }
        chr->logfd = qemu_open(common->logfile, flags, 0666);
        if (chr->logfd < 0) {
            error_setg_errno(errp, errno,
                             "Unable to open logfile %s",
                             common->logfile);
            return;
        }
    }

    if (cc->open) {
        cc->open(chr, backend, be_opened, errp);
    }
}

static void char_init(Object *obj)
{
    Chardev *chr = CHARDEV(obj);

    chr->logfd = -1;
    qemu_mutex_init(&chr->chr_write_lock);
}

static int null_chr_write(Chardev *chr, const uint8_t *buf, int len)
{
    return len;
}

static void char_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->chr_write = null_chr_write;
}

static void char_finalize(Object *obj)
{
    Chardev *chr = CHARDEV(obj);

    if (chr->be) {
        chr->be->chr = NULL;
    }
    g_free(chr->filename);
    g_free(chr->label);
    if (chr->logfd != -1) {
        close(chr->logfd);
    }
    qemu_mutex_destroy(&chr->chr_write_lock);
}

static const TypeInfo char_type_info = {
    .name = TYPE_CHARDEV,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(Chardev),
    .instance_init = char_init,
    .instance_finalize = char_finalize,
    .abstract = true,
    .class_size = sizeof(ChardevClass),
    .class_init = char_class_init,
};

/**
 * Called after processing of default and command-line-specified
 * chardevs to deliver CHR_EVENT_OPENED events to any FEs attached
 * to a mux chardev. This is done here to ensure that
 * output/prompts/banners are only displayed for the FE that has
 * focus when initial command-line processing/machine init is
 * completed.
 *
 * After this point, any new FE attached to any new or existing
 * mux will receive CHR_EVENT_OPENED notifications for the BE
 * immediately.
 */
static int open_muxes(Object *child, void *opaque)
{
    if (CHARDEV_IS_MUX(child)) {
        /* send OPENED to all already-attached FEs */
        mux_chr_send_all_event(CHARDEV(child), CHR_EVENT_OPENED);
        /* mark mux as OPENED so any new FEs will immediately receive
         * OPENED event
         */
        qemu_chr_be_event(CHARDEV(child), CHR_EVENT_OPENED);
    }

    return 0;
}

static void muxes_realize_done(Notifier *notifier, void *unused)
{
    muxes_realized = true;
    object_child_foreach(get_chardevs_root(), open_muxes, NULL);
}

static Notifier muxes_realize_notify = {
    .notify = muxes_realize_done,
};

static bool qemu_chr_is_busy(Chardev *s)
{
    if (CHARDEV_IS_MUX(s)) {
        MuxChardev *d = MUX_CHARDEV(s);
        return d->mux_cnt >= 0;
    } else {
        return s->be != NULL;
    }
}

int qemu_chr_wait_connected(Chardev *chr, Error **errp)
{
    ChardevClass *cc = CHARDEV_GET_CLASS(chr);

    if (cc->chr_wait_connected) {
        return cc->chr_wait_connected(chr, errp);
    }

    return 0;
}

QemuOpts *qemu_chr_parse_compat(const char *label, const char *filename)
{
    char host[65], port[33], width[8], height[8];
    int pos;
    const char *p;
    QemuOpts *opts;
    Error *local_err = NULL;

    opts = qemu_opts_create(qemu_find_opts("chardev"), label, 1, &local_err);
    if (local_err) {
        error_report_err(local_err);
        return NULL;
    }

    if (strstart(filename, "mon:", &p)) {
        filename = p;
        qemu_opt_set(opts, "mux", "on", &error_abort);
        if (strcmp(filename, "stdio") == 0) {
            /* Monitor is muxed to stdio: do not exit on Ctrl+C by default
             * but pass it to the guest.  Handle this only for compat syntax,
             * for -chardev syntax we have special option for this.
             * This is what -nographic did, redirecting+muxing serial+monitor
             * to stdio causing Ctrl+C to be passed to guest. */
            qemu_opt_set(opts, "signal", "off", &error_abort);
        }
    }

    if (strcmp(filename, "null")    == 0 ||
        strcmp(filename, "pty")     == 0 ||
        strcmp(filename, "msmouse") == 0 ||
        strcmp(filename, "wctablet") == 0 ||
        strcmp(filename, "braille") == 0 ||
        strcmp(filename, "testdev") == 0 ||
        strcmp(filename, "stdio")   == 0) {
        qemu_opt_set(opts, "backend", filename, &error_abort);
        return opts;
    }
    if (strstart(filename, "vc", &p)) {
        qemu_opt_set(opts, "backend", "vc", &error_abort);
        if (*p == ':') {
            if (sscanf(p+1, "%7[0-9]x%7[0-9]", width, height) == 2) {
                /* pixels */
                qemu_opt_set(opts, "width", width, &error_abort);
                qemu_opt_set(opts, "height", height, &error_abort);
            } else if (sscanf(p+1, "%7[0-9]Cx%7[0-9]C", width, height) == 2) {
                /* chars */
                qemu_opt_set(opts, "cols", width, &error_abort);
                qemu_opt_set(opts, "rows", height, &error_abort);
            } else {
                goto fail;
            }
        }
        return opts;
    }
    if (strcmp(filename, "con:") == 0) {
        qemu_opt_set(opts, "backend", "console", &error_abort);
        return opts;
    }
    if (strstart(filename, "COM", NULL)) {
        qemu_opt_set(opts, "backend", "serial", &error_abort);
        qemu_opt_set(opts, "path", filename, &error_abort);
        return opts;
    }
    if (strstart(filename, "file:", &p)) {
        qemu_opt_set(opts, "backend", "file", &error_abort);
        qemu_opt_set(opts, "path", p, &error_abort);
        return opts;
    }
    if (strstart(filename, "pipe:", &p)) {
        qemu_opt_set(opts, "backend", "pipe", &error_abort);
        qemu_opt_set(opts, "path", p, &error_abort);
        return opts;
    }
    if (strstart(filename, "tcp:", &p) ||
        strstart(filename, "telnet:", &p) ||
        strstart(filename, "tn3270:", &p)) {
        if (sscanf(p, "%64[^:]:%32[^,]%n", host, port, &pos) < 2) {
            host[0] = 0;
            if (sscanf(p, ":%32[^,]%n", port, &pos) < 1)
                goto fail;
        }
        qemu_opt_set(opts, "backend", "socket", &error_abort);
        qemu_opt_set(opts, "host", host, &error_abort);
        qemu_opt_set(opts, "port", port, &error_abort);
        if (p[pos] == ',') {
            qemu_opts_do_parse(opts, p+pos+1, NULL, &local_err);
            if (local_err) {
                error_report_err(local_err);
                goto fail;
            }
        }
        if (strstart(filename, "telnet:", &p)) {
            qemu_opt_set(opts, "telnet", "on", &error_abort);
        } else if (strstart(filename, "tn3270:", &p)) {
            qemu_opt_set(opts, "tn3270", "on", &error_abort);
        }
        return opts;
    }
    if (strstart(filename, "udp:", &p)) {
        qemu_opt_set(opts, "backend", "udp", &error_abort);
        if (sscanf(p, "%64[^:]:%32[^@,]%n", host, port, &pos) < 2) {
            host[0] = 0;
            if (sscanf(p, ":%32[^@,]%n", port, &pos) < 1) {
                goto fail;
            }
        }
        qemu_opt_set(opts, "host", host, &error_abort);
        qemu_opt_set(opts, "port", port, &error_abort);
        if (p[pos] == '@') {
            p += pos + 1;
            if (sscanf(p, "%64[^:]:%32[^,]%n", host, port, &pos) < 2) {
                host[0] = 0;
                if (sscanf(p, ":%32[^,]%n", port, &pos) < 1) {
                    goto fail;
                }
            }
            qemu_opt_set(opts, "localaddr", host, &error_abort);
            qemu_opt_set(opts, "localport", port, &error_abort);
        }
        return opts;
    }
    if (strstart(filename, "unix:", &p)) {
        qemu_opt_set(opts, "backend", "socket", &error_abort);
        qemu_opts_do_parse(opts, p, "path", &local_err);
        if (local_err) {
            error_report_err(local_err);
            goto fail;
        }
        return opts;
    }
    if (strstart(filename, "/dev/parport", NULL) ||
        strstart(filename, "/dev/ppi", NULL)) {
        qemu_opt_set(opts, "backend", "parallel", &error_abort);
        qemu_opt_set(opts, "path", filename, &error_abort);
        return opts;
    }
    if (strstart(filename, "/dev/", NULL)) {
        qemu_opt_set(opts, "backend", "serial", &error_abort);
        qemu_opt_set(opts, "path", filename, &error_abort);
        return opts;
    }

fail:
    qemu_opts_del(opts);
    return NULL;
}

void qemu_chr_parse_common(QemuOpts *opts, ChardevCommon *backend)
{
    const char *logfile = qemu_opt_get(opts, "logfile");

    backend->has_logfile = logfile != NULL;
    backend->logfile = g_strdup(logfile);

    backend->has_logappend = true;
    backend->logappend = qemu_opt_get_bool(opts, "logappend", false);
}

static const ChardevClass *char_get_class(const char *driver, Error **errp)
{
    ObjectClass *oc;
    const ChardevClass *cc;
    char *typename = g_strdup_printf("chardev-%s", driver);

    oc = object_class_by_name(typename);
    g_free(typename);

    if (!object_class_dynamic_cast(oc, TYPE_CHARDEV)) {
        error_setg(errp, "'%s' is not a valid char driver name", driver);
        return NULL;
    }

    if (object_class_is_abstract(oc)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "driver",
                   "abstract device type");
        return NULL;
    }

    cc = CHARDEV_CLASS(oc);
    if (cc->internal) {
        error_setg(errp, "'%s' is not a valid char driver name", driver);
        return NULL;
    }

    return cc;
}

static const struct ChardevAlias {
    const char *typename;
    const char *alias;
} chardev_alias_table[] = {
#ifdef HAVE_CHARDEV_PARPORT
    { "parallel", "parport" },
#endif
#ifdef HAVE_CHARDEV_SERIAL
    { "serial", "tty" },
#endif
};

typedef struct ChadevClassFE {
    void (*fn)(const char *name, void *opaque);
    void *opaque;
} ChadevClassFE;

static void
chardev_class_foreach(ObjectClass *klass, void *opaque)
{
    ChadevClassFE *fe = opaque;

    assert(g_str_has_prefix(object_class_get_name(klass), "chardev-"));
    if (CHARDEV_CLASS(klass)->internal) {
        return;
    }

    fe->fn(object_class_get_name(klass) + 8, fe->opaque);
}

static void
chardev_name_foreach(void (*fn)(const char *name, void *opaque), void *opaque)
{
    ChadevClassFE fe = { .fn = fn, .opaque = opaque };
    int i;

    object_class_foreach(chardev_class_foreach, TYPE_CHARDEV, false, &fe);

    for (i = 0; i < (int)ARRAY_SIZE(chardev_alias_table); i++) {
        fn(chardev_alias_table[i].alias, opaque);
    }
}

static void
help_string_append(const char *name, void *opaque)
{
    GString *str = opaque;

    g_string_append_printf(str, "\n%s", name);
}

static const char *chardev_alias_translate(const char *name)
{
    int i;
    for (i = 0; i < (int)ARRAY_SIZE(chardev_alias_table); i++) {
        if (g_strcmp0(chardev_alias_table[i].alias, name) == 0) {
            return chardev_alias_table[i].typename;
        }
    }
    return name;
}

ChardevBackend *qemu_chr_parse_opts(QemuOpts *opts, Error **errp)
{
    Error *local_err = NULL;
    const ChardevClass *cc;
    ChardevBackend *backend = NULL;
    const char *name = chardev_alias_translate(qemu_opt_get(opts, "backend"));

    if (name == NULL) {
        error_setg(errp, "chardev: \"%s\" missing backend",
                   qemu_opts_id(opts));
        return NULL;
    }

    cc = char_get_class(name, errp);
    if (cc == NULL) {
        return NULL;
    }

    backend = g_new0(ChardevBackend, 1);
    backend->type = CHARDEV_BACKEND_KIND_NULL;

    if (cc->parse) {
        cc->parse(opts, backend, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            qapi_free_ChardevBackend(backend);
            return NULL;
        }
    } else {
        ChardevCommon *ccom = g_new0(ChardevCommon, 1);
        qemu_chr_parse_common(opts, ccom);
        backend->u.null.data = ccom; /* Any ChardevCommon member would work */
    }

    return backend;
}

Chardev *qemu_chr_new_from_opts(QemuOpts *opts, Error **errp)
{
    const ChardevClass *cc;
    Chardev *chr = NULL;
    ChardevBackend *backend = NULL;
    const char *name = chardev_alias_translate(qemu_opt_get(opts, "backend"));
    const char *id = qemu_opts_id(opts);
    char *bid = NULL;

    if (name && is_help_option(name)) {
        GString *str = g_string_new("");

        chardev_name_foreach(help_string_append, str);

        error_report("Available chardev backend types: %s", str->str);
        g_string_free(str, true);
        return NULL;
    }

    if (id == NULL) {
        error_setg(errp, "chardev: no id specified");
        return NULL;
    }

    backend = qemu_chr_parse_opts(opts, errp);
    if (backend == NULL) {
        return NULL;
    }

    cc = char_get_class(name, errp);
    if (cc == NULL) {
        goto out;
    }

    if (qemu_opt_get_bool(opts, "mux", 0)) {
        bid = g_strdup_printf("%s-base", id);
    }

    chr = qemu_chardev_new(bid ? bid : id,
                           object_class_get_name(OBJECT_CLASS(cc)),
                           backend, errp);

    if (chr == NULL) {
        goto out;
    }

    if (bid) {
        Chardev *mux;
        qapi_free_ChardevBackend(backend);
        backend = g_new0(ChardevBackend, 1);
        backend->type = CHARDEV_BACKEND_KIND_MUX;
        backend->u.mux.data = g_new0(ChardevMux, 1);
        backend->u.mux.data->chardev = g_strdup(bid);
        mux = qemu_chardev_new(id, TYPE_CHARDEV_MUX, backend, errp);
        if (mux == NULL) {
            object_unparent(OBJECT(chr));
            chr = NULL;
            goto out;
        }
        chr = mux;
    }

out:
    qapi_free_ChardevBackend(backend);
    g_free(bid);
    return chr;
}

Chardev *qemu_chr_new_noreplay(const char *label, const char *filename)
{
    const char *p;
    Chardev *chr;
    QemuOpts *opts;
    Error *err = NULL;

    if (strstart(filename, "chardev:", &p)) {
        return qemu_chr_find(p);
    }

    opts = qemu_chr_parse_compat(label, filename);
    if (!opts)
        return NULL;

    chr = qemu_chr_new_from_opts(opts, &err);
    if (err) {
        error_report_err(err);
    }
    if (chr && qemu_opt_get_bool(opts, "mux", 0)) {
        monitor_init(chr, MONITOR_USE_READLINE);
    }
    qemu_opts_del(opts);
    return chr;
}

Chardev *qemu_chr_new(const char *label, const char *filename)
{
    Chardev *chr;
    chr = qemu_chr_new_noreplay(label, filename);
    if (chr) {
        if (replay_mode != REPLAY_MODE_NONE) {
            qemu_chr_set_feature(chr, QEMU_CHAR_FEATURE_REPLAY);
        }
        if (qemu_chr_replay(chr) && CHARDEV_GET_CLASS(chr)->chr_ioctl) {
            error_report("Replay: ioctl is not supported "
                         "for serial devices yet");
        }
        replay_register_char_driver(chr);
    }
    return chr;
}

static int qmp_query_chardev_foreach(Object *obj, void *data)
{
    Chardev *chr = CHARDEV(obj);
    ChardevInfoList **list = data;
    ChardevInfoList *info = g_malloc0(sizeof(*info));

    info->value = g_malloc0(sizeof(*info->value));
    info->value->label = g_strdup(chr->label);
    info->value->filename = g_strdup(chr->filename);
    info->value->frontend_open = chr->be && chr->be->fe_open;

    info->next = *list;
    *list = info;

    return 0;
}

ChardevInfoList *qmp_query_chardev(Error **errp)
{
    ChardevInfoList *chr_list = NULL;

    object_child_foreach(get_chardevs_root(),
                         qmp_query_chardev_foreach, &chr_list);

    return chr_list;
}

static void
qmp_prepend_backend(const char *name, void *opaque)
{
    ChardevBackendInfoList **list = opaque;
    ChardevBackendInfoList *info = g_malloc0(sizeof(*info));

    info->value = g_malloc0(sizeof(*info->value));
    info->value->name = g_strdup(name);
    info->next = *list;
    *list = info;
}

ChardevBackendInfoList *qmp_query_chardev_backends(Error **errp)
{
    ChardevBackendInfoList *backend_list = NULL;

    chardev_name_foreach(qmp_prepend_backend, &backend_list);

    return backend_list;
}

Chardev *qemu_chr_find(const char *name)
{
    Object *obj = object_resolve_path_component(get_chardevs_root(), name);

    return obj ? CHARDEV(obj) : NULL;
}

QemuOptsList qemu_chardev_opts = {
    .name = "chardev",
    .implied_opt_name = "backend",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_chardev_opts.head),
    .desc = {
        {
            .name = "backend",
            .type = QEMU_OPT_STRING,
        },{
            .name = "path",
            .type = QEMU_OPT_STRING,
        },{
            .name = "host",
            .type = QEMU_OPT_STRING,
        },{
            .name = "port",
            .type = QEMU_OPT_STRING,
        },{
            .name = "localaddr",
            .type = QEMU_OPT_STRING,
        },{
            .name = "localport",
            .type = QEMU_OPT_STRING,
        },{
            .name = "to",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "ipv4",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "ipv6",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "wait",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "server",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "delay",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "reconnect",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "telnet",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "tn3270",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "tls-creds",
            .type = QEMU_OPT_STRING,
        },{
            .name = "width",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "height",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "cols",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "rows",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "mux",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "signal",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "name",
            .type = QEMU_OPT_STRING,
        },{
            .name = "debug",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "size",
            .type = QEMU_OPT_SIZE,
        },{
            .name = "chardev",
            .type = QEMU_OPT_STRING,
        },{
            .name = "append",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "logfile",
            .type = QEMU_OPT_STRING,
        },{
            .name = "logappend",
            .type = QEMU_OPT_BOOL,
        },
        { /* end of list */ }
    },
};

bool qemu_chr_has_feature(Chardev *chr,
                          ChardevFeature feature)
{
    return test_bit(feature, chr->features);
}

void qemu_chr_set_feature(Chardev *chr,
                           ChardevFeature feature)
{
    return set_bit(feature, chr->features);
}

Chardev *qemu_chardev_new(const char *id, const char *typename,
                          ChardevBackend *backend,
                          Error **errp)
{
    Object *obj;
    Chardev *chr = NULL;
    Error *local_err = NULL;
    bool be_opened = true;

    assert(g_str_has_prefix(typename, "chardev-"));

    obj = object_new(typename);
    chr = CHARDEV(obj);
    chr->label = g_strdup(id);

    qemu_char_open(chr, backend, &be_opened, &local_err);
    if (local_err) {
        goto end;
    }

    if (!chr->filename) {
        chr->filename = g_strdup(typename + 8);
    }
    if (be_opened) {
        qemu_chr_be_event(chr, CHR_EVENT_OPENED);
    }

    if (id) {
        object_property_add_child(get_chardevs_root(), id, obj, &local_err);
        if (local_err) {
            goto end;
        }
        object_unref(obj);
    }

end:
    if (local_err) {
        error_propagate(errp, local_err);
        object_unref(obj);
        return NULL;
    }

    return chr;
}

ChardevReturn *qmp_chardev_add(const char *id, ChardevBackend *backend,
                               Error **errp)
{
    const ChardevClass *cc;
    ChardevReturn *ret;
    Chardev *chr;

    cc = char_get_class(ChardevBackendKind_lookup[backend->type], errp);
    if (!cc) {
        return NULL;
    }

    chr = qemu_chardev_new(id, object_class_get_name(OBJECT_CLASS(cc)),
                           backend, errp);
    if (!chr) {
        return NULL;
    }

    ret = g_new0(ChardevReturn, 1);
    if (CHARDEV_IS_PTY(chr)) {
        ret->pty = g_strdup(chr->filename + 4);
        ret->has_pty = true;
    }

    return ret;
}

ChardevReturn *qmp_chardev_change(const char *id, ChardevBackend *backend,
                                  Error **errp)
{
    CharBackend *be;
    const ChardevClass *cc;
    Chardev *chr, *chr_new;
    bool closed_sent = false;
    ChardevReturn *ret;

    chr = qemu_chr_find(id);
    if (!chr) {
        error_setg(errp, "Chardev '%s' does not exist", id);
        return NULL;
    }

    if (CHARDEV_IS_MUX(chr)) {
        error_setg(errp, "Mux device hotswap not supported yet");
        return NULL;
    }

    if (qemu_chr_replay(chr)) {
        error_setg(errp,
            "Chardev '%s' cannot be changed in record/replay mode", id);
        return NULL;
    }

    be = chr->be;
    if (!be) {
        /* easy case */
        object_unparent(OBJECT(chr));
        return qmp_chardev_add(id, backend, errp);
    }

    if (!be->chr_be_change) {
        error_setg(errp, "Chardev user does not support chardev hotswap");
        return NULL;
    }

    cc = char_get_class(ChardevBackendKind_lookup[backend->type], errp);
    if (!cc) {
        return NULL;
    }

    chr_new = qemu_chardev_new(NULL, object_class_get_name(OBJECT_CLASS(cc)),
                               backend, errp);
    if (!chr_new) {
        return NULL;
    }
    chr_new->label = g_strdup(id);

    if (chr->be_open && !chr_new->be_open) {
        qemu_chr_be_event(chr, CHR_EVENT_CLOSED);
        closed_sent = true;
    }

    chr->be = NULL;
    qemu_chr_fe_init(be, chr_new, &error_abort);

    if (be->chr_be_change(be->opaque) < 0) {
        error_setg(errp, "Chardev '%s' change failed", chr_new->label);
        chr_new->be = NULL;
        qemu_chr_fe_init(be, chr, &error_abort);
        if (closed_sent) {
            qemu_chr_be_event(chr, CHR_EVENT_OPENED);
        }
        object_unref(OBJECT(chr_new));
        return NULL;
    }

    object_unparent(OBJECT(chr));
    object_property_add_child(get_chardevs_root(), chr_new->label,
                              OBJECT(chr_new), &error_abort);
    object_unref(OBJECT(chr_new));

    ret = g_new0(ChardevReturn, 1);
    if (CHARDEV_IS_PTY(chr_new)) {
        ret->pty = g_strdup(chr_new->filename + 4);
        ret->has_pty = true;
    }

    return ret;
}

void qmp_chardev_remove(const char *id, Error **errp)
{
    Chardev *chr;

    chr = qemu_chr_find(id);
    if (chr == NULL) {
        error_setg(errp, "Chardev '%s' not found", id);
        return;
    }
    if (qemu_chr_is_busy(chr)) {
        error_setg(errp, "Chardev '%s' is busy", id);
        return;
    }
    if (qemu_chr_replay(chr)) {
        error_setg(errp,
            "Chardev '%s' cannot be unplugged in record/replay mode", id);
        return;
    }
    object_unparent(OBJECT(chr));
}

void qmp_chardev_send_break(const char *id, Error **errp)
{
    Chardev *chr;

    chr = qemu_chr_find(id);
    if (chr == NULL) {
        error_setg(errp, "Chardev '%s' not found", id);
        return;
    }
    qemu_chr_be_event(chr, CHR_EVENT_BREAK);
}

void qemu_chr_cleanup(void)
{
    object_unparent(get_chardevs_root());
}

static void register_types(void)
{
    type_register_static(&char_type_info);

    /* this must be done after machine init, since we register FEs with muxes
     * as part of realize functions like serial_isa_realizefn when -nographic
     * is specified
     */
    qemu_add_machine_init_done_notifier(&muxes_realize_notify);
}

type_init(register_types);
