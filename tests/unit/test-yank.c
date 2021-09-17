/*
 * Tests for QEMU yank feature
 *
 * Copyright (c) Lukas Straub <lukasstraub2@web.de>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <glib/gstdio.h>

#include "qemu/config-file.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "chardev/char-fe.h"
#include "sysemu/sysemu.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-char.h"
#include "qapi/qapi-types-char.h"
#include "qapi/qapi-commands-yank.h"
#include "qapi/qapi-types-yank.h"
#include "io/channel-socket.h"
#include "socket-helpers.h"

typedef struct {
    SocketAddress *addr;
    bool old_yank;
    bool new_yank;
    bool fail;
} CharChangeTestConfig;

static int chardev_change(void *opaque)
{
    return 0;
}

static bool is_yank_instance_registered(void)
{
    YankInstanceList *list;
    bool ret;

    list = qmp_query_yank(&error_abort);

    ret = !!list;

    qapi_free_YankInstanceList(list);

    return ret;
}

static gpointer accept_thread(gpointer data)
{
    QIOChannelSocket *ioc = data;
    QIOChannelSocket *cioc;

    cioc = qio_channel_socket_accept(ioc, &error_abort);
    object_unref(OBJECT(cioc));

    return NULL;
}

static void char_change_test(gconstpointer opaque)
{
    CharChangeTestConfig *conf = (gpointer) opaque;
    SocketAddress *addr;
    Chardev *chr;
    CharBackend be;
    ChardevReturn *ret;
    QIOChannelSocket *ioc;
    QemuThread thread;

    /*
     * Setup a listener socket and determine its address
     * so we know the TCP port for the client later
     */
    ioc = qio_channel_socket_new();
    g_assert_nonnull(ioc);
    qio_channel_socket_listen_sync(ioc, conf->addr, 1, &error_abort);
    addr = qio_channel_socket_get_local_address(ioc, &error_abort);
    g_assert_nonnull(addr);

    ChardevBackend backend[2] = {
        /* doesn't support yank */
        { .type = CHARDEV_BACKEND_KIND_NULL },
        /* supports yank */
        {
            .type = CHARDEV_BACKEND_KIND_SOCKET,
            .u.socket.data = &(ChardevSocket) {
                .addr = &(SocketAddressLegacy) {
                    .type = SOCKET_ADDRESS_TYPE_INET,
                    .u.inet.data = &addr->u.inet
                },
                .has_server = true,
                .server = false
            }
        } };

    ChardevBackend fail_backend[2] = {
        /* doesn't support yank */
        {
            .type = CHARDEV_BACKEND_KIND_UDP,
            .u.udp.data = &(ChardevUdp) {
                .remote = &(SocketAddressLegacy) {
                    .type = SOCKET_ADDRESS_TYPE_UNIX,
                    .u.q_unix.data = &(UnixSocketAddress) {
                        .path = (char *)""
                    }
                }
            }
        },
        /* supports yank */
        {
            .type = CHARDEV_BACKEND_KIND_SOCKET,
            .u.socket.data = &(ChardevSocket) {
                .addr = &(SocketAddressLegacy) {
                    .type = SOCKET_ADDRESS_TYPE_INET,
                    .u.inet.data = &(InetSocketAddress) {
                        .host = (char *)"127.0.0.1",
                        .port = (char *)"0"
                    }
                },
                .has_server = true,
                .server = false
            }
        } };

    g_assert(!is_yank_instance_registered());

    if (conf->old_yank) {
        qemu_thread_create(&thread, "accept", accept_thread,
                           ioc, QEMU_THREAD_JOINABLE);
    }

    ret = qmp_chardev_add("chardev", &backend[conf->old_yank], &error_abort);
    qapi_free_ChardevReturn(ret);
    chr = qemu_chr_find("chardev");
    g_assert_nonnull(chr);

    g_assert(is_yank_instance_registered() == conf->old_yank);

    qemu_chr_wait_connected(chr, &error_abort);
    if (conf->old_yank) {
        qemu_thread_join(&thread);
    }

    qemu_chr_fe_init(&be, chr, &error_abort);
    /* allow chardev-change */
    qemu_chr_fe_set_handlers(&be, NULL, NULL,
                             NULL, chardev_change, NULL, NULL, true);

    if (conf->fail) {
        g_setenv("QTEST_SILENT_ERRORS", "1", 1);
        ret = qmp_chardev_change("chardev", &fail_backend[conf->new_yank],
                                 NULL);
        g_assert_null(ret);
        g_assert(be.chr == chr);
        g_assert(is_yank_instance_registered() == conf->old_yank);
        g_unsetenv("QTEST_SILENT_ERRORS");
    } else {
        if (conf->new_yank) {
                qemu_thread_create(&thread, "accept", accept_thread,
                                   ioc, QEMU_THREAD_JOINABLE);
        }
        ret = qmp_chardev_change("chardev", &backend[conf->new_yank],
                                 &error_abort);
        if (conf->new_yank) {
            qemu_thread_join(&thread);
        }
        g_assert_nonnull(ret);
        g_assert(be.chr != chr);
        g_assert(is_yank_instance_registered() == conf->new_yank);
    }

    object_unparent(OBJECT(be.chr));
    object_unref(OBJECT(ioc));
    qapi_free_ChardevReturn(ret);
    qapi_free_SocketAddress(addr);
}

static SocketAddress tcpaddr = {
    .type = SOCKET_ADDRESS_TYPE_INET,
    .u.inet.host = (char *)"127.0.0.1",
    .u.inet.port = (char *)"0",
};

int main(int argc, char **argv)
{
    bool has_ipv4, has_ipv6;

    qemu_init_main_loop(&error_abort);
    socket_init();

    g_test_init(&argc, &argv, NULL);

    if (socket_check_protocol_support(&has_ipv4, &has_ipv6) < 0) {
        g_printerr("socket_check_protocol_support() failed\n");
        goto end;
    }

    if (!has_ipv4) {
        goto end;
    }

    module_call_init(MODULE_INIT_QOM);
    qemu_add_opts(&qemu_chardev_opts);

    g_test_add_data_func("/yank/char_change/success/to_yank",
                         &(CharChangeTestConfig) { .addr = &tcpaddr,
                                                   .old_yank = false,
                                                   .new_yank = true,
                                                   .fail = false },
                         char_change_test);
    g_test_add_data_func("/yank/char_change/fail/to_yank",
                         &(CharChangeTestConfig) { .addr = &tcpaddr,
                                                   .old_yank = false,
                                                   .new_yank = true,
                                                   .fail = true },
                         char_change_test);

    g_test_add_data_func("/yank/char_change/success/yank_to_yank",
                         &(CharChangeTestConfig) { .addr = &tcpaddr,
                                                   .old_yank = true,
                                                   .new_yank = true,
                                                   .fail = false },
                         char_change_test);
    g_test_add_data_func("/yank/char_change/fail/yank_to_yank",
                         &(CharChangeTestConfig) { .addr = &tcpaddr,
                                                   .old_yank = true,
                                                   .new_yank = true,
                                                   .fail = true },
                         char_change_test);

    g_test_add_data_func("/yank/char_change/success/from_yank",
                         &(CharChangeTestConfig) { .addr = &tcpaddr,
                                                   .old_yank = true,
                                                   .new_yank = false,
                                                   .fail = false },
                         char_change_test);
    g_test_add_data_func("/yank/char_change/fail/from_yank",
                         &(CharChangeTestConfig) { .addr = &tcpaddr,
                                                   .old_yank = true,
                                                   .new_yank = false,
                                                   .fail = true },
                         char_change_test);

end:
    return g_test_run();
}
