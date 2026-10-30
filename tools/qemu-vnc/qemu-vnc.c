/*
 * Standalone VNC server connecting to QEMU via D-Bus display interface.
 *
 * Copyright (C) 2026 Red Hat, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "qemu/cutils.h"
#include "qemu/datadir.h"
#include "qemu/error-report.h"
#include "qemu/config-file.h"
#include "qemu/option.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu-version.h"
#include "ui/vnc.h"
#include "crypto/secret.h"
#include "crypto/tlscredsx509.h"
#include "qom/object_interfaces.h"
#include "trace.h"
#include "qemu-vnc.h"

const char *qemu_name;
const char *keyboard_layout;

typedef struct {
    GDBusConnection *bus;
    const char *bus_name;
    const char * const *chardev_names;
    char *terminate_reason;
    bool no_vt;
    bool terminate;
    bool owner_seen;
    bool wait_for_owner;
} QemuVncState;

static GType
dbus_display_get_proxy_type(GDBusObjectManagerClient *manager,
                            const gchar *object_path,
                            const gchar *interface_name,
                            gpointer user_data)
{
    static const struct {
        const char *iface;
        GType (*get_type)(void);
    } types[] = {
        { "org.qemu.Display1.Clipboard",
          qemu_dbus_display1_clipboard_proxy_get_type },
        { "org.qemu.Display1.Audio",
          qemu_dbus_display1_audio_proxy_get_type },
        { "org.qemu.Display1.Chardev",
          qemu_dbus_display1_chardev_proxy_get_type },
        { "org.qemu.Display1.Chardev.VCEncoding",
          qemu_dbus_display1_chardev_vcencoding_proxy_get_type },
    };

    if (!interface_name) {
        return G_TYPE_DBUS_OBJECT_PROXY;
    }

    for (int i = 0; i < G_N_ELEMENTS(types); i++) {
        if (g_str_equal(interface_name, types[i].iface)) {
            return types[i].get_type();
        }
    }

    return G_TYPE_DBUS_PROXY;
}

static void
on_bus_closed(GDBusConnection *connection,
              gboolean remote_peer_vanished,
              GError *error,
              gpointer user_data)
{
    QemuVncState *state = user_data;

    state->terminate_reason = g_strdup("D-Bus connection closed");
    state->terminate = true;
    qemu_notify_event();
}

static void
on_manager_ready(GObject *source_object,
                 GAsyncResult *res,
                 gpointer user_data)
{
    QemuVncState *state = user_data;
    g_autoptr(GError) err = NULL;
    g_autoptr(GDBusObjectManager) manager = NULL;
    GList *objects, *l;
    g_autoptr(GPtrArray) console_paths = NULL;
    bool found = false;
    Error *local_err = NULL;
    VncDisplay *vd;

    manager = G_DBUS_OBJECT_MANAGER(
        g_dbus_object_manager_client_new_finish(res, &err));
    if (!manager) {
        error_report("Failed to create object manager: %s",
                     err->message);
        g_assert_not_reached();
        return;
    }

    /*
     * Discover all Console objects and sort them so that console
     * indices are assigned in a predictable order matching QEMU's.
     */
    console_paths = g_ptr_array_new_with_free_func(g_free);
    objects = g_dbus_object_manager_get_objects(manager);
    for (l = objects; l; l = l->next) {
        GDBusObject *obj = l->data;
        const char *path = g_dbus_object_get_object_path(obj);

        if (g_str_has_prefix(path, DBUS_DISPLAY1_ROOT "/Console_")) {
            g_ptr_array_add(console_paths, g_strdup(path));
        }
    }
    g_list_free_full(objects, g_object_unref);

    g_ptr_array_sort(console_paths, (GCompareFunc)qemu_pstrcmp0);

    for (guint i = 0; i < console_paths->len; i++) {
        const char *path = g_ptr_array_index(console_paths, i);

        if (!console_setup(state->bus, state->bus_name, path)) {
            error_report("Failed to setup console %s", path);
            continue;
        }
        found = true;
    }

    if (!found) {
        error_report("No consoles found");
        state->terminate_reason = g_strdup("No consoles found");
        state->terminate = true;
        qemu_notify_event();
        return;
    }

    /*
     * Create the VNC display now that consoles exist, so that the
     * display change listener is registered against a valid console.
     */
    vd = vnc_display_new("default", &local_err);
    if (!vd) {
        error_report("Failed to create VNC display: %s",
                     error_get_pretty(local_err));
        g_assert_not_reached();
        return;
    }

    /* guest clipboard peer presence is not exposed over D-Bus */
    vd->clipboard_availability = false;

    vnc_dbus_setup(state->bus);

    clipboard_setup(manager, state->bus);
    audio_setup(manager);
    if (!state->no_vt) {
        chardev_setup(state->chardev_names, manager);
    }
}

static void
start_display_setup(QemuVncState *state)
{
    g_autoptr(QemuDBusDisplay1VMProxy) vm_proxy =
        QEMU_DBUS_DISPLAY1_VM_PROXY(
            qemu_dbus_display1_vm_proxy_new_sync(
                state->bus, G_DBUS_PROXY_FLAGS_NONE,
                state->bus_name,
                DBUS_DISPLAY1_ROOT "/VM", NULL, NULL));
    if (vm_proxy) {
        qemu_name = g_strdup(qemu_dbus_display1_vm_get_name(
            QEMU_DBUS_DISPLAY1_VM(vm_proxy)));
    }

    g_dbus_object_manager_client_new(
        state->bus,
        G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
        state->bus_name, DBUS_DISPLAY1_ROOT,
        dbus_display_get_proxy_type,
        NULL, NULL, NULL,
        on_manager_ready, state);
}

static void
on_owner_appeared(GDBusConnection *connection,
                  const gchar *name,
                  const gchar *name_owner,
                  gpointer user_data)
{
    QemuVncState *state = user_data;

    if (state->owner_seen) {
        return;
    }

    info_report("D-Bus name %s appeared.", name);
    state->owner_seen = true;
    trace_qemu_vnc_owner_appeared(name);
    start_display_setup(state);
}

static void
on_owner_vanished(GDBusConnection *connection,
                  const gchar *name,
                  gpointer user_data)
{
    QemuVncState *state = user_data;

    trace_qemu_vnc_owner_vanished(name);

    if (!state->owner_seen) {
        if (state->wait_for_owner) {
            return;
        }
        error_report("D-Bus name %s not found. "
                     "Is QEMU running? "
                     "Use --wait to wait for it to appear.", name);
        state->terminate_reason =
            g_strdup_printf("D-Bus name %s not found", name);
    } else {
        error_report("D-Bus peer %s vanished, terminating", name);
        state->terminate_reason =
            g_strdup_printf("D-Bus peer %s vanished", name);
    }

    state->terminate = true;
    qemu_notify_event();
}

static GDBusConnection *
setup_dbus_connection(int dbus_p2p_fd, const char *dbus_address,
                      char **bus_name)
{
    g_autoptr(GError) err = NULL;
    GDBusConnection *bus;

    if (dbus_p2p_fd >= 0) {
        g_autoptr(GSocket) socket = NULL;
        g_autoptr(GSocketConnection) socketc = NULL;

        if (*bus_name) {
            error_report("--bus-name is not supported with --dbus-p2p-fd");
            return NULL;
        }

        socket = g_socket_new_from_fd(dbus_p2p_fd, &err);
        if (!socket) {
            error_report("Failed to create socket from fd %d: %s",
                         dbus_p2p_fd, err->message);
            return NULL;
        }

        socketc = g_socket_connection_factory_create_connection(socket);
        if (!socketc) {
            error_report("Failed to create socket connection");
            return NULL;
        }

        bus = g_dbus_connection_new_sync(
            G_IO_STREAM(socketc), NULL,
            G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT,
            NULL, NULL, &err);
    } else if (dbus_address) {
        GDBusConnectionFlags flags =
            G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT;
        if (*bus_name) {
            flags |= G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION;
        }
        bus = g_dbus_connection_new_for_address_sync(
            dbus_address, flags, NULL, NULL, &err);
    } else {
        bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
        if (!*bus_name) {
            *bus_name = g_strdup("org.qemu");
        }
    }

    if (!bus) {
        error_report("Failed to connect to D-Bus: %s", err->message);
    }

    return bus;
}

static bool
setup_credentials(const char *tls_creds_dir, const char *tls_authz,
                  bool *has_vnc_password)
{
    Error *local_err = NULL;
    const char *creds_dir;

    /*
     * Set up TLS credentials if requested.  The object must exist
     * before vnc_display_open() which looks it up by ID.
     */
    if (tls_creds_dir) {
        if (!object_new_with_props(TYPE_QCRYPTO_TLS_CREDS_X509,
                                   object_get_objects_root(),
                                   "tlscreds0",
                                   &local_err,
                                   "endpoint", "server",
                                   "dir", tls_creds_dir,
                                   "verify-peer", tls_authz ? "yes" : "no",
                                   NULL)) {
            error_report_err(local_err);
            return false;
        }
    }

    /*
     * Check for systemd credentials: if a vnc-password credential
     * file exists, create a QCryptoSecret and enable VNC password auth.
     */
    creds_dir = g_getenv("CREDENTIALS_DIRECTORY");
    if (creds_dir) {
        g_autofree char *password_path =
            g_build_filename(creds_dir, "vnc-password", NULL);
        if (g_file_test(password_path, G_FILE_TEST_EXISTS)) {
            if (!object_new_with_props(TYPE_QCRYPTO_SECRET,
                                       object_get_objects_root(),
                                       "vncsecret0",
                                       &local_err,
                                       "file", password_path,
                                       NULL)) {
                error_report_err(local_err);
                return false;
            }
            *has_vnc_password = true;
        }
    }

    return true;
}

static bool
setup_vnc_opts(const char *vnc_addr, const char *tls_creds_dir,
               const char *tls_authz, bool sasl, const char *sasl_authz,
               bool has_vnc_password, const char *ws_addr,
               const char *share, bool password, bool lossy,
               bool non_adaptive)
{
    g_autoptr(GString) opts_str = g_string_new(vnc_addr);
    QemuOptsList *olist = qemu_find_opts("vnc");
    QemuOpts *opts;

    if (tls_creds_dir) {
        g_string_append(opts_str, ",tls-creds=tlscreds0");
    }
    if (tls_authz) {
        g_string_append_printf(opts_str, ",tls-authz=%s", tls_authz);
    }
    if (sasl) {
        g_string_append(opts_str, ",sasl=on");
    }
    if (sasl_authz) {
        g_string_append_printf(opts_str, ",sasl-authz=%s", sasl_authz);
    }
    if (has_vnc_password) {
        g_string_append(opts_str, ",password-secret=vncsecret0");
    }
    if (ws_addr) {
        g_string_append_printf(opts_str, ",websocket=%s", ws_addr);
    }
    if (share) {
        g_string_append_printf(opts_str, ",share=%s", share);
    }
    if (password && !has_vnc_password) {
        g_string_append(opts_str, ",password=on");
    }
    if (lossy) {
        g_string_append(opts_str, ",lossy=on");
    }
    if (non_adaptive) {
        g_string_append(opts_str, ",non-adaptive=on");
    }

    opts = qemu_opts_parse_noisily(olist, opts_str->str, true);
    if (!opts) {
        return false;
    }
    qemu_opts_set_id(opts, g_strdup("default"));
    return true;
}

int
main(int argc, char *argv[])
{
    g_autoptr(GError) err = NULL;
    g_autoptr(GDBusConnection) bus = NULL;
    g_autofree char *dbus_address = NULL;
    g_autofree char *bus_name = NULL;
    int dbus_p2p_fd = -1;
    g_autofree char *vnc_addr = NULL;
    g_autofree char *ws_addr = NULL;
    g_autofree char *share = NULL;
    g_autofree char *tls_creds_dir = NULL;
    g_autofree char *tls_authz = NULL;
    g_autofree char *sasl_authz = NULL;
    g_autofree char *trace_opt = NULL;
    g_auto(GStrv) chardev_names = NULL;
    g_auto(GStrv) object_strs = NULL;
    QemuVncState state = { 0 };
    bool has_vnc_password = false;
    bool show_version = false;
    bool no_vt = false;
    bool wait_for_owner = false;
    bool password = false;
    bool sasl = false;
    bool lossy = false;
    bool non_adaptive = false;
    g_autoptr(GOptionContext) context = NULL;
    GOptionEntry entries[] = {
        { "dbus-address", 'a', 0, G_OPTION_ARG_STRING, &dbus_address,
          "D-Bus address to connect to (default: session bus)", "ADDRESS" },
        { "dbus-p2p-fd", 'p', 0, G_OPTION_ARG_INT, &dbus_p2p_fd,
          "D-Bus peer-to-peer socket file descriptor", "FD" },
        { "bus-name", 'n', 0, G_OPTION_ARG_STRING, &bus_name,
          "D-Bus bus name (default: org.qemu)", "NAME" },
        { "wait", 'W', 0, G_OPTION_ARG_NONE, &wait_for_owner,
          "Wait for the D-Bus name to appear", NULL },
        { "vnc-addr", 'l', 0, G_OPTION_ARG_STRING, &vnc_addr,
          "VNC display address (default localhost:0, \"none\" to disable)",
          "ADDR" },
        { "websocket", 'w', 0, G_OPTION_ARG_STRING, &ws_addr,
          "WebSocket address (e.g. port number or addr:port)", "ADDR" },
        { "share", 's', 0, G_OPTION_ARG_STRING, &share,
          "Display sharing policy "
          "(allow-exclusive|force-shared|ignore)", "POLICY" },
        { "tls-creds", 't', 0, G_OPTION_ARG_STRING, &tls_creds_dir,
          "TLS x509 credentials directory", "DIR" },
        { "tls-authz", 0, 0, G_OPTION_ARG_STRING, &tls_authz,
          "ID of a QAuthZ object for TLS client certificate "
          "authorization", "ID" },
        { "object", 'O', 0, G_OPTION_ARG_STRING_ARRAY, &object_strs,
          "QEMU user-creatable object "
          "(e.g. authz-list-file,id=auth0,filename=acl.json)", "OBJDEF" },
        { "vt-chardev", 'C', 0, G_OPTION_ARG_STRING_ARRAY, &chardev_names,
          "Chardev type names to expose as text console (repeatable, "
          "default: serial & hmp)", "NAME" },
        { "no-vt", 'N', 0, G_OPTION_ARG_NONE, &no_vt,
          "Do not expose any chardevs as text consoles", NULL },
        { "keyboard-layout", 'k', 0, G_OPTION_ARG_STRING, &keyboard_layout,
          "Keyboard layout", "LAYOUT" },
        { "trace", 'T', 0, G_OPTION_ARG_STRING, &trace_opt,
          "Trace options (same as QEMU -trace)", "PATTERN" },
        { "version", 'V', 0, G_OPTION_ARG_NONE, &show_version,
          "Print version information and exit", NULL },
        { "password", 0, 0, G_OPTION_ARG_NONE, &password,
          "Require password authentication (use D-Bus SetPassword to set)",
          NULL },
        { "lossy", 0, 0, G_OPTION_ARG_NONE, &lossy,
          "Enable lossy compression", NULL },
        { "non-adaptive", 0, 0, G_OPTION_ARG_NONE, &non_adaptive,
          "Disable adaptive encodings", NULL },
        { "sasl", 0, 0, G_OPTION_ARG_NONE, &sasl,
          "Enable SASL authentication", NULL },
        { "sasl-authz", 0, 0, G_OPTION_ARG_STRING, &sasl_authz,
          "ID of a QAuthZ object for SASL username "
          "authorization", "ID" },
        { NULL }
    };

    qemu_init_exec_dir(argv[0]);
    qemu_add_data_dir(g_strdup(CONFIG_QEMU_DATADIR));
    qemu_add_data_dir(get_relocated_path(CONFIG_QEMU_DATADIR));

    module_call_init(MODULE_INIT_TRACE);
    module_call_init(MODULE_INIT_QOM);
    module_call_init(MODULE_INIT_OPTS);
    qemu_add_opts(&qemu_trace_opts);

    context = g_option_context_new(NULL);
    g_option_context_set_summary(context,
        "Standalone VNC server connecting to a QEMU instance via the\n"
        "D-Bus display interface (org.qemu.Display1).");
    g_option_context_add_main_entries(context, entries, NULL);
    if (!g_option_context_parse(context, &argc, &argv, &err)) {
        error_report("Option parsing failed: %s", err->message);
        return 1;
    }

    if (show_version) {
        printf("qemu-vnc " QEMU_FULL_VERSION "\n");
        return 0;
    }

    if (trace_opt) {
        trace_opt_parse(trace_opt);
        qemu_set_log(LOG_TRACE, &error_fatal);
    }
    trace_init_file();

    qemu_init_main_loop(&error_fatal);

    if (!vnc_addr) {
        vnc_addr = g_strdup("localhost:0");
    }

    if (object_strs) {
        for (int i = 0; object_strs[i]; i++) {
            user_creatable_process_cmdline(object_strs[i]);
        }
    }

    if (tls_authz && !tls_creds_dir) {
        error_report("--tls-authz requires --tls-creds");
        return 1;
    }

    if (sasl_authz && !sasl) {
        error_report("--sasl-authz requires --sasl");
        return 1;
    }

    if (dbus_p2p_fd >= 0 && dbus_address) {
        error_report("--dbus-p2p-fd and --dbus-address are"
                     " mutually exclusive");
        return 1;
    }

    if (wait_for_owner && dbus_p2p_fd >= 0) {
        error_report("--wait is not supported with --dbus-p2p-fd");
        return 1;
    }

    bus = setup_dbus_connection(dbus_p2p_fd, dbus_address, &bus_name);
    if (!bus) {
        return 1;
    }

    if (wait_for_owner && !bus_name) {
        error_report("--wait requires a D-Bus bus name (--bus-name)");
        return 1;
    }

    if (!setup_credentials(tls_creds_dir, tls_authz, &has_vnc_password)) {
        return 1;
    }

    if (!setup_vnc_opts(vnc_addr, tls_creds_dir, tls_authz, sasl, sasl_authz,
                        has_vnc_password, ws_addr, share, password, lossy,
                        non_adaptive)) {
        return 1;
    }

    state.bus = bus;
    state.bus_name = bus_name;
    state.chardev_names = (const char * const *)chardev_names;
    state.no_vt = no_vt;
    state.wait_for_owner = wait_for_owner;

    g_signal_connect(bus, "closed", G_CALLBACK(on_bus_closed), &state);

    if (bus_name) {
        if (wait_for_owner) {
            info_report("Waiting for D-Bus name %s to appear...", bus_name);
        }
        g_bus_watch_name_on_connection(bus, bus_name,
                                       G_BUS_NAME_WATCHER_FLAGS_NONE,
                                       on_owner_appeared,
                                       on_owner_vanished,
                                       &state, NULL);
    } else {
        state.owner_seen = true;
        start_display_setup(&state);
    }

    while (!state.terminate) {
        main_loop_wait(false);
    }

    vnc_dbus_emit_leaving(state.terminate_reason ?: "Shutting down");
    vnc_dbus_cleanup();
    vnc_cleanup();
    g_free(state.terminate_reason);

    return 0;
}
