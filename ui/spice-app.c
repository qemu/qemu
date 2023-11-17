/*
 * QEMU external Spice client display driver
 *
 * Copyright (c) 2018 Marc-André Lureau <marcandre.lureau@redhat.com>
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

#include <gio/gio.h>

#include "ui/console.h"
#include "ui/spice-display.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "qemu/option.h"
#include "qemu/cutils.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "io/channel-command.h"
#include "chardev/spice.h"
#include "sysemu/sysemu.h"
#include "qom/object.h"

static const char *tmp_dir;
static char *app_dir;
static char *sock_path;

struct VCChardev {
    SpiceChardev parent;
};

struct VCChardevClass {
    ChardevClass parent;
    void (*parent_open)(Chardev *chr, ChardevBackend *backend,
                        bool *be_opened, Error **errp);
};

#define TYPE_CHARDEV_VC "chardev-vc"
OBJECT_DECLARE_TYPE(VCChardev, VCChardevClass, CHARDEV_VC)

static ChardevBackend *
chr_spice_backend_new(void)
{
    ChardevBackend *be = g_new0(ChardevBackend, 1);

    be->type = CHARDEV_BACKEND_KIND_SPICEPORT;
    be->u.spiceport.data = g_new0(ChardevSpicePort, 1);

    return be;
}

static void vc_chr_open(Chardev *chr,
                        ChardevBackend *backend,
                        bool *be_opened,
                        Error **errp)
{
    VCChardevClass *vc = CHARDEV_VC_GET_CLASS(chr);
    ChardevBackend *be;
    const char *fqdn = NULL;

    if (strstart(chr->label, "serial", NULL)) {
        fqdn = "org.qemu.console.serial.0";
    } else if (strstart(chr->label, "parallel", NULL)) {
        fqdn = "org.qemu.console.parallel.0";
    } else if (strstart(chr->label, "compat_monitor", NULL)) {
        fqdn = "org.qemu.monitor.hmp.0";
    }

    be = chr_spice_backend_new();
    be->u.spiceport.data->fqdn = fqdn ?
        g_strdup(fqdn) : g_strdup_printf("org.qemu.console.%s", chr->label);
    vc->parent_open(chr, be, be_opened, errp);
    qapi_free_ChardevBackend(be);
}

static void vc_chr_set_echo(Chardev *chr, bool echo)
{
    /* TODO: set echo for frontends QMP and qtest */
}

static void vc_chr_parse(QemuOpts *opts, ChardevBackend *backend, Error **errp)
{
    /* fqdn is dealt with in vc_chr_open() */
}

static void char_vc_class_init(ObjectClass *oc, void *data)
{
    VCChardevClass *vc = CHARDEV_VC_CLASS(oc);
    ChardevClass *cc = CHARDEV_CLASS(oc);

    vc->parent_open = cc->open;

    cc->parse = vc_chr_parse;
    cc->open = vc_chr_open;
    cc->chr_set_echo = vc_chr_set_echo;
}

static const TypeInfo char_vc_type_info = {
    .name = TYPE_CHARDEV_VC,
    .parent = TYPE_CHARDEV_SPICEPORT,
    .instance_size = sizeof(VCChardev),
    .class_init = char_vc_class_init,
    .class_size = sizeof(VCChardevClass),
};

static void spice_app_atexit(void)
{
    if (sock_path) {
        unlink(sock_path);
    }
    if (tmp_dir) {
        rmdir(tmp_dir);
    }
    g_free(sock_path);
    g_free(app_dir);
}

static void spice_app_display_early_init(DisplayOptions *opts)
{
    QemuOpts *qopts;
    QemuOptsList *list;
    GError *err = NULL;

    if (opts->has_full_screen) {
        error_report("spice-app full-screen isn't supported yet.");
        exit(1);
    }
    if (opts->has_window_close) {
        error_report("spice-app window-close isn't supported yet.");
        exit(1);
    }

    atexit(spice_app_atexit);

    if (qemu_name) {
        app_dir = g_build_filename(g_get_user_runtime_dir(),
                                   "qemu", qemu_name, NULL);
        if (g_mkdir_with_parents(app_dir, S_IRWXU) < -1) {
            error_report("Failed to create directory %s: %s",
                         app_dir, strerror(errno));
            exit(1);
        }
    } else {
        app_dir = g_dir_make_tmp(NULL, &err);
        tmp_dir = app_dir;
        if (err) {
            error_report("Failed to create temporary directory: %s",
                         err->message);
            exit(1);
        }
    }
    list = qemu_find_opts("spice");
    if (list == NULL) {
        error_report("spice-app missing spice support");
        exit(1);
    }

    type_register(&char_vc_type_info);

    sock_path = g_strjoin("", app_dir, "/", "spice.sock", NULL);
    qopts = qemu_opts_create(list, NULL, 0, &error_abort);
    qemu_opt_set(qopts, "disable-ticketing", "on", &error_abort);
    qemu_opt_set(qopts, "unix", "on", &error_abort);
    qemu_opt_set(qopts, "addr", sock_path, &error_abort);
    qemu_opt_set(qopts, "image-compression", "off", &error_abort);
    qemu_opt_set(qopts, "streaming-video", "off", &error_abort);
#ifdef HAVE_SPICE_GL
    qemu_opt_set(qopts, "gl", opts->has_gl ? "on" : "off", &error_abort);
    display_opengl = opts->has_gl;
#endif
}

static void spice_app_display_init(DisplayState *ds, DisplayOptions *opts)
{
    ChardevBackend *be = chr_spice_backend_new();
    QemuOpts *qopts;
    GError *err = NULL;
    gchar *uri;

    be->u.spiceport.data->fqdn = g_strdup("org.qemu.monitor.qmp.0");
    qemu_chardev_new("org.qemu.monitor.qmp", TYPE_CHARDEV_SPICEPORT,
                     be, NULL, &error_abort);
    qopts = qemu_opts_create(qemu_find_opts("mon"),
                             NULL, 0, &error_fatal);
    qemu_opt_set(qopts, "chardev", "org.qemu.monitor.qmp", &error_abort);
    qemu_opt_set(qopts, "mode", "control", &error_abort);

    qapi_free_ChardevBackend(be);
    uri = g_strjoin("", "spice+unix://", app_dir, "/", "spice.sock", NULL);
    info_report("Launching display with URI: %s", uri);
    g_app_info_launch_default_for_uri(uri, NULL, &err);
    if (err) {
        error_report("Failed to launch %s URI: %s", uri, err->message);
        error_report("You need a capable Spice client, "
                     "such as virt-viewer 8.0");
        exit(1);
    }
    g_free(uri);
}

static QemuDisplay qemu_display_spice_app = {
    .type       = DISPLAY_TYPE_SPICE_APP,
    .early_init = spice_app_display_early_init,
    .init       = spice_app_display_init,
    .vc         = "vc",
};

static void register_spice_app(void)
{
    qemu_display_register(&qemu_display_spice_app);
}

type_init(register_spice_app);

module_dep("ui-spice-core");
module_dep("chardev-spice");
