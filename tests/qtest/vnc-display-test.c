/*
 * VNC display tests
 *
 * Copyright (c) 2022 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/sockets.h"
#include "libqtest.h"
#include <gio/gio.h>
#include <gvnc.h>

typedef struct Test {
    QTestState *qts;
    VncConnection *conn;
    GMainLoop *loop;
} Test;

#if !defined(WIN32) && !defined(CONFIG_DARWIN)

static void on_vnc_error(VncConnection* self,
                         const char* msg)
{
    g_error("vnc-error: %s", msg);
}

static void on_vnc_auth_failure(VncConnection *self,
                                const char *msg)
{
    g_error("vnc-auth-failure: %s", msg);
}

#endif

static bool
test_setup(Test *test)
{
#ifdef WIN32
    g_test_skip("Not supported on Windows yet");
    return false;
#elif defined(CONFIG_DARWIN)
    g_test_skip("Broken on Darwin");
    return false;
#else
    int pair[2];

    test->qts = qtest_init("-M none -vnc none -name vnc-test");

    g_assert_cmpint(qemu_socketpair(AF_UNIX, SOCK_STREAM, 0, pair), ==, 0);

    qtest_qmp_add_client(test->qts, "vnc", pair[1]);

    test->conn = vnc_connection_new();
    g_signal_connect(test->conn, "vnc-error",
                     G_CALLBACK(on_vnc_error), NULL);
    g_signal_connect(test->conn, "vnc-auth-failure",
                     G_CALLBACK(on_vnc_auth_failure), NULL);
    vnc_connection_set_auth_type(test->conn, VNC_CONNECTION_AUTH_NONE);
    vnc_connection_open_fd(test->conn, pair[0]);

    test->loop = g_main_loop_new(NULL, FALSE);
    return true;
#endif
}

static void
test_vnc_basic_on_vnc_initialized(VncConnection *self,
                                 Test *test)
{
    const char *name = vnc_connection_get_name(test->conn);

    g_assert_cmpstr(name, ==, "QEMU (vnc-test)");
    g_main_loop_quit(test->loop);
}

static void
test_vnc_basic(void)
{
    Test test;

    if (!test_setup(&test)) {
        return;
    }

    g_signal_connect(test.conn, "vnc-initialized",
                     G_CALLBACK(test_vnc_basic_on_vnc_initialized), &test);

    g_main_loop_run(test.loop);

    qtest_quit(test.qts);
    g_object_unref(test.conn);
    g_main_loop_unref(test.loop);
}

int
main(int argc, char **argv)
{
    if (getenv("GTK_VNC_DEBUG")) {
        vnc_util_set_debug(true);
    }

    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/vnc-display/basic", test_vnc_basic);

    return g_test_run();
}
