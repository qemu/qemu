/*
 * QTest testcase for qemu_announce_self
 *
 * Copyright (c) 2017 Red hat, Inc.
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qapi/qmp/qdict.h"
#include "qemu-common.h"
#include "qemu/sockets.h"
#include "qemu/iov.h"
#include "libqos/libqos-pc.h"
#include "libqos/libqos-spapr.h"

#ifndef ETH_P_RARP
#define ETH_P_RARP 0x8035
#endif

static QTestState *test_init(int socket)
{
    char *args;

    args = g_strdup_printf("-netdev socket,fd=%d,id=hs0 -device "
                           "virtio-net-pci,netdev=hs0", socket);

    return qtest_start(args);
}


static void test_announce(int socket)
{
    char buffer[60];
    int len;
    QDict *rsp;
    int ret;
    uint16_t *proto = (uint16_t *)&buffer[12];

    rsp = qmp("{ 'execute' : 'announce-self', "
                  " 'arguments': {"
                      " 'initial': 50, 'max': 550,"
                      " 'rounds': 10, 'step': 50 } }");
    assert(!qdict_haskey(rsp, "error"));
    qobject_unref(rsp);

    /* Catch the packet and make sure it's a RARP */
    ret = qemu_recv(socket, &len, sizeof(len), 0);
    g_assert_cmpint(ret, ==,  sizeof(len));
    len = ntohl(len);

    ret = qemu_recv(socket, buffer, len, 0);
    g_assert_cmpint(*proto, ==, htons(ETH_P_RARP));
}

static void setup(gconstpointer data)
{
    QTestState *qs;
    void (*func) (int socket) = data;
    int sv[2], ret;

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sv);
    g_assert_cmpint(ret, !=, -1);

    qs = test_init(sv[1]);
    func(sv[0]);

    /* End test */
    close(sv[0]);
    qtest_quit(qs);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_data_func("/virtio/net/test_announce_self", test_announce, setup);

    return g_test_run();
}
