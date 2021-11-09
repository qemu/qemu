/*
 * QTest testcase for VirtIO NIC
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "libqtest-single.h"
#include "qemu/iov.h"
#include "qemu/module.h"
#include "qapi/qmp/qdict.h"
#include "hw/virtio/virtio-net.h"
#include "libqos/qgraph.h"
#include "libqos/virtio-net.h"

#ifndef ETH_P_RARP
#define ETH_P_RARP 0x8035
#endif

#define PCI_SLOT_HP             0x06
#define PCI_SLOT                0x04

#define QVIRTIO_NET_TIMEOUT_US (30 * 1000 * 1000)
#define VNET_HDR_SIZE sizeof(struct virtio_net_hdr_mrg_rxbuf)

#ifndef _WIN32

static void rx_test(QVirtioDevice *dev,
                    QGuestAllocator *alloc, QVirtQueue *vq,
                    int socket)
{
    QTestState *qts = global_qtest;
    uint64_t req_addr;
    uint32_t free_head;
    char test[] = "TEST";
    char buffer[64];
    int len = htonl(sizeof(test));
    struct iovec iov[] = {
        {
            .iov_base = &len,
            .iov_len = sizeof(len),
        }, {
            .iov_base = test,
            .iov_len = sizeof(test),
        },
    };
    int ret;

    req_addr = guest_alloc(alloc, 64);

    free_head = qvirtqueue_add(qts, vq, req_addr, 64, true, false);
    qvirtqueue_kick(qts, dev, vq, free_head);

    ret = iov_send(socket, iov, 2, 0, sizeof(len) + sizeof(test));
    g_assert_cmpint(ret, ==, sizeof(test) + sizeof(len));

    qvirtio_wait_used_elem(qts, dev, vq, free_head, NULL,
                           QVIRTIO_NET_TIMEOUT_US);
    memread(req_addr + VNET_HDR_SIZE, buffer, sizeof(test));
    g_assert_cmpstr(buffer, ==, "TEST");

    guest_free(alloc, req_addr);
}

static void tx_test(QVirtioDevice *dev,
                    QGuestAllocator *alloc, QVirtQueue *vq,
                    int socket)
{
    QTestState *qts = global_qtest;
    uint64_t req_addr;
    uint32_t free_head;
    uint32_t len;
    char buffer[64];
    int ret;

    req_addr = guest_alloc(alloc, 64);
    memwrite(req_addr + VNET_HDR_SIZE, "TEST", 4);

    free_head = qvirtqueue_add(qts, vq, req_addr, 64, false, false);
    qvirtqueue_kick(qts, dev, vq, free_head);

    qvirtio_wait_used_elem(qts, dev, vq, free_head, NULL,
                           QVIRTIO_NET_TIMEOUT_US);
    guest_free(alloc, req_addr);

    ret = qemu_recv(socket, &len, sizeof(len), 0);
    g_assert_cmpint(ret, ==, sizeof(len));
    len = ntohl(len);

    ret = qemu_recv(socket, buffer, len, 0);
    g_assert_cmpstr(buffer, ==, "TEST");
}

static void rx_stop_cont_test(QVirtioDevice *dev,
                              QGuestAllocator *alloc, QVirtQueue *vq,
                              int socket)
{
    QTestState *qts = global_qtest;
    uint64_t req_addr;
    uint32_t free_head;
    char test[] = "TEST";
    char buffer[64];
    int len = htonl(sizeof(test));
    QDict *rsp;
    struct iovec iov[] = {
        {
            .iov_base = &len,
            .iov_len = sizeof(len),
        }, {
            .iov_base = test,
            .iov_len = sizeof(test),
        },
    };
    int ret;

    req_addr = guest_alloc(alloc, 64);

    free_head = qvirtqueue_add(qts, vq, req_addr, 64, true, false);
    qvirtqueue_kick(qts, dev, vq, free_head);

    rsp = qmp("{ 'execute' : 'stop'}");
    qobject_unref(rsp);

    ret = iov_send(socket, iov, 2, 0, sizeof(len) + sizeof(test));
    g_assert_cmpint(ret, ==, sizeof(test) + sizeof(len));

    /* We could check the status, but this command is more importantly to
     * ensure the packet data gets queued in QEMU, before we do 'cont'.
     */
    rsp = qmp("{ 'execute' : 'query-status'}");
    qobject_unref(rsp);
    rsp = qmp("{ 'execute' : 'cont'}");
    qobject_unref(rsp);

    qvirtio_wait_used_elem(qts, dev, vq, free_head, NULL,
                           QVIRTIO_NET_TIMEOUT_US);
    memread(req_addr + VNET_HDR_SIZE, buffer, sizeof(test));
    g_assert_cmpstr(buffer, ==, "TEST");

    guest_free(alloc, req_addr);
}

static void send_recv_test(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtioNet *net_if = obj;
    QVirtioDevice *dev = net_if->vdev;
    QVirtQueue *rx = net_if->queues[0];
    QVirtQueue *tx = net_if->queues[1];
    int *sv = data;

    rx_test(dev, t_alloc, rx, sv[0]);
    tx_test(dev, t_alloc, tx, sv[0]);
}

static void stop_cont_test(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtioNet *net_if = obj;
    QVirtioDevice *dev = net_if->vdev;
    QVirtQueue *rx = net_if->queues[0];
    int *sv = data;

    rx_stop_cont_test(dev, t_alloc, rx, sv[0]);
}

#endif

static void hotplug(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtioPCIDevice *dev = obj;
    QTestState *qts = dev->pdev->bus->qts;
    const char *arch = qtest_get_arch();

    qtest_qmp_device_add(qts, "virtio-net-pci", "net1",
                         "{'addr': %s}", stringify(PCI_SLOT_HP));

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        qpci_unplug_acpi_device_test(qts, "net1", PCI_SLOT_HP);
    }
}

static void announce_self(void *obj, void *data, QGuestAllocator *t_alloc)
{
    int *sv = data;
    char buffer[60];
    int len;
    QDict *rsp;
    int ret;
    uint16_t *proto = (uint16_t *)&buffer[12];
    size_t total_received = 0;
    uint64_t start, now, last_rxt, deadline;

    /* Send a set of packets over a few second period */
    rsp = qmp("{ 'execute' : 'announce-self', "
                  " 'arguments': {"
                      " 'initial': 20, 'max': 100,"
                      " 'rounds': 300, 'step': 10, 'id': 'bob' } }");
    assert(!qdict_haskey(rsp, "error"));
    qobject_unref(rsp);

    /* Catch the first packet and make sure it's a RARP */
    ret = qemu_recv(sv[0], &len, sizeof(len), 0);
    g_assert_cmpint(ret, ==,  sizeof(len));
    len = ntohl(len);

    ret = qemu_recv(sv[0], buffer, len, 0);
    g_assert_cmpint(*proto, ==, htons(ETH_P_RARP));

    /*
     * Stop the announcment by settings rounds to 0 on the
     * existing timer.
     */
    rsp = qmp("{ 'execute' : 'announce-self', "
                  " 'arguments': {"
                      " 'initial': 20, 'max': 100,"
                      " 'rounds': 0, 'step': 10, 'id': 'bob' } }");
    assert(!qdict_haskey(rsp, "error"));
    qobject_unref(rsp);

    /* Now make sure the packets stop */

    /* Times are in us */
    start = g_get_monotonic_time();
    /* 30 packets, max gap 100ms, * 4 for wiggle */
    deadline = start + 1000 * (100 * 30 * 4);
    last_rxt = start;

    while (true) {
        int saved_err;
        ret = qemu_recv(sv[0], buffer, 60, MSG_DONTWAIT);
        saved_err = errno;
        now = g_get_monotonic_time();
        g_assert_cmpint(now, <, deadline);

        if (ret >= 0) {
            if (ret) {
                last_rxt = now;
            }
            total_received += ret;

            /* Check it's not spewing loads */
            g_assert_cmpint(total_received, <, 60 * 30 * 2);
        } else {
            g_assert_cmpint(saved_err, ==, EAGAIN);

            /* 400ms, i.e. 4 worst case gaps */
            if ((now - last_rxt) > (1000 * 100 * 4)) {
                /* Nothings arrived for a while - must have stopped */
                break;
            };

            /* 100ms */
            g_usleep(1000 * 100);
        }
    };
}

static void virtio_net_test_cleanup(void *sockets)
{
    int *sv = sockets;

    close(sv[0]);
    qos_invalidate_command_line();
    close(sv[1]);
    g_free(sv);
}

static void *virtio_net_test_setup(GString *cmd_line, void *arg)
{
    int ret;
    int *sv = g_new(int, 2);

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sv);
    g_assert_cmpint(ret, !=, -1);

    g_string_append_printf(cmd_line, " -netdev socket,fd=%d,id=hs0 ", sv[1]);

    g_test_queue_destroy(virtio_net_test_cleanup, sv);
    return sv;
}

static void large_tx(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtioNet *dev = obj;
    QVirtQueue *vq = dev->queues[1];
    uint64_t req_addr;
    uint32_t free_head;
    size_t alloc_size = (size_t)data / 64;
    QTestState *qts = global_qtest;
    int i;

    /* Bypass the limitation by pointing several descriptors to a single
     * smaller area */
    req_addr = guest_alloc(t_alloc, alloc_size);
    free_head = qvirtqueue_add(qts, vq, req_addr, alloc_size, false, true);

    for (i = 0; i < 64; i++) {
        qvirtqueue_add(qts, vq, req_addr, alloc_size, false, i != 63);
    }
    qvirtqueue_kick(qts, dev->vdev, vq, free_head);

    qvirtio_wait_used_elem(qts, dev->vdev, vq, free_head, NULL,
                           QVIRTIO_NET_TIMEOUT_US);
    guest_free(t_alloc, req_addr);
}

static void *virtio_net_test_setup_nosocket(GString *cmd_line, void *arg)
{
    g_string_append(cmd_line, " -netdev hubport,hubid=0,id=hs0 ");
    return arg;
}

static void register_virtio_net_test(void)
{
    QOSGraphTestOptions opts = {
        .before = virtio_net_test_setup,
    };

    qos_add_test("hotplug", "virtio-net-pci", hotplug, &opts);
#ifndef _WIN32
    qos_add_test("basic", "virtio-net", send_recv_test, &opts);
    qos_add_test("rx_stop_cont", "virtio-net", stop_cont_test, &opts);
#endif
    qos_add_test("announce-self", "virtio-net", announce_self, &opts);

    /* These tests do not need a loopback backend.  */
    opts.before = virtio_net_test_setup_nosocket;
    opts.arg = (gpointer)UINT_MAX;
    qos_add_test("large_tx/uint_max", "virtio-net", large_tx, &opts);
    opts.arg = (gpointer)NET_BUFSIZE;
    qos_add_test("large_tx/net_bufsize", "virtio-net", large_tx, &opts);
}

libqos_init(register_virtio_net_test);
