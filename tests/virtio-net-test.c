/*
 * QTest testcase for VirtIO NIC
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu-common.h"
#include "qemu/sockets.h"
#include "qemu/iov.h"
#include "libqos/pci-pc.h"
#include "libqos/virtio.h"
#include "libqos/virtio-pci.h"
#include "libqos/malloc.h"
#include "libqos/malloc-pc.h"
#include "libqos/malloc-generic.h"
#include "qemu/bswap.h"
#include "hw/virtio/virtio-net.h"

#define PCI_SLOT_HP             0x06
#define PCI_SLOT                0x04
#define PCI_FN                  0x00

#define QVIRTIO_NET_TIMEOUT_US (30 * 1000 * 1000)
#define VNET_HDR_SIZE sizeof(struct virtio_net_hdr_mrg_rxbuf)

static void test_end(void)
{
    qtest_end();
}

#ifndef _WIN32

static QVirtioPCIDevice *virtio_net_pci_init(QPCIBus *bus, int slot)
{
    QVirtioPCIDevice *dev;

    dev = qvirtio_pci_device_find(bus, QVIRTIO_NET_DEVICE_ID);
    g_assert(dev != NULL);
    g_assert_cmphex(dev->vdev.device_type, ==, QVIRTIO_NET_DEVICE_ID);

    qvirtio_pci_device_enable(dev);
    qvirtio_reset(&qvirtio_pci, &dev->vdev);
    qvirtio_set_acknowledge(&qvirtio_pci, &dev->vdev);
    qvirtio_set_driver(&qvirtio_pci, &dev->vdev);

    return dev;
}

static QPCIBus *pci_test_start(int socket)
{
    char *cmdline;

    cmdline = g_strdup_printf("-netdev socket,fd=%d,id=hs0 -device "
                              "virtio-net-pci,netdev=hs0", socket);
    qtest_start(cmdline);
    g_free(cmdline);

    return qpci_init_pc();
}

static void driver_init(const QVirtioBus *bus, QVirtioDevice *dev)
{
    uint32_t features;

    features = qvirtio_get_features(bus, dev);
    features = features & ~(QVIRTIO_F_BAD_FEATURE |
                            QVIRTIO_F_RING_INDIRECT_DESC |
                            QVIRTIO_F_RING_EVENT_IDX);
    qvirtio_set_features(bus, dev, features);

    qvirtio_set_driver_ok(bus, dev);
}

static void rx_test(const QVirtioBus *bus, QVirtioDevice *dev,
                    QGuestAllocator *alloc, QVirtQueue *vq,
                    int socket)
{
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

    free_head = qvirtqueue_add(vq, req_addr, 64, true, false);
    qvirtqueue_kick(bus, dev, vq, free_head);

    ret = iov_send(socket, iov, 2, 0, sizeof(len) + sizeof(test));
    g_assert_cmpint(ret, ==, sizeof(test) + sizeof(len));

    qvirtio_wait_queue_isr(bus, dev, vq, QVIRTIO_NET_TIMEOUT_US);
    memread(req_addr + VNET_HDR_SIZE, buffer, sizeof(test));
    g_assert_cmpstr(buffer, ==, "TEST");

    guest_free(alloc, req_addr);
}

static void tx_test(const QVirtioBus *bus, QVirtioDevice *dev,
                    QGuestAllocator *alloc, QVirtQueue *vq,
                    int socket)
{
    uint64_t req_addr;
    uint32_t free_head;
    uint32_t len;
    char buffer[64];
    int ret;

    req_addr = guest_alloc(alloc, 64);
    memwrite(req_addr + VNET_HDR_SIZE, "TEST", 4);

    free_head = qvirtqueue_add(vq, req_addr, 64, false, false);
    qvirtqueue_kick(bus, dev, vq, free_head);

    qvirtio_wait_queue_isr(bus, dev, vq, QVIRTIO_NET_TIMEOUT_US);
    guest_free(alloc, req_addr);

    ret = qemu_recv(socket, &len, sizeof(len), 0);
    g_assert_cmpint(ret, ==, sizeof(len));
    len = ntohl(len);

    ret = qemu_recv(socket, buffer, len, 0);
    g_assert_cmpstr(buffer, ==, "TEST");
}

static void rx_stop_cont_test(const QVirtioBus *bus, QVirtioDevice *dev,
                              QGuestAllocator *alloc, QVirtQueue *vq,
                              int socket)
{
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

    free_head = qvirtqueue_add(vq, req_addr, 64, true, false);
    qvirtqueue_kick(bus, dev, vq, free_head);

    qmp("{ 'execute' : 'stop'}");

    ret = iov_send(socket, iov, 2, 0, sizeof(len) + sizeof(test));
    g_assert_cmpint(ret, ==, sizeof(test) + sizeof(len));

    /* We could check the status, but this command is more importantly to
     * ensure the packet data gets queued in QEMU, before we do 'cont'.
     */
    qmp("{ 'execute' : 'query-status'}");
    qmp("{ 'execute' : 'cont'}");

    qvirtio_wait_queue_isr(bus, dev, vq, QVIRTIO_NET_TIMEOUT_US);
    memread(req_addr + VNET_HDR_SIZE, buffer, sizeof(test));
    g_assert_cmpstr(buffer, ==, "TEST");

    guest_free(alloc, req_addr);
}

static void send_recv_test(const QVirtioBus *bus, QVirtioDevice *dev,
                           QGuestAllocator *alloc, QVirtQueue *rvq,
                           QVirtQueue *tvq, int socket)
{
    rx_test(bus, dev, alloc, rvq, socket);
    tx_test(bus, dev, alloc, tvq, socket);
}

static void stop_cont_test(const QVirtioBus *bus, QVirtioDevice *dev,
                           QGuestAllocator *alloc, QVirtQueue *rvq,
                           QVirtQueue *tvq, int socket)
{
    rx_stop_cont_test(bus, dev, alloc, rvq, socket);
}

static void pci_basic(gconstpointer data)
{
    QVirtioPCIDevice *dev;
    QPCIBus *bus;
    QVirtQueuePCI *tx, *rx;
    QGuestAllocator *alloc;
    void (*func) (const QVirtioBus *bus,
                  QVirtioDevice *dev,
                  QGuestAllocator *alloc,
                  QVirtQueue *rvq,
                  QVirtQueue *tvq,
                  int socket) = data;
    int sv[2], ret;

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sv);
    g_assert_cmpint(ret, !=, -1);

    bus = pci_test_start(sv[1]);
    dev = virtio_net_pci_init(bus, PCI_SLOT);

    alloc = pc_alloc_init();
    rx = (QVirtQueuePCI *)qvirtqueue_setup(&qvirtio_pci, &dev->vdev,
                                           alloc, 0);
    tx = (QVirtQueuePCI *)qvirtqueue_setup(&qvirtio_pci, &dev->vdev,
                                           alloc, 1);

    driver_init(&qvirtio_pci, &dev->vdev);
    func(&qvirtio_pci, &dev->vdev, alloc, &rx->vq, &tx->vq, sv[0]);

    /* End test */
    close(sv[0]);
    guest_free(alloc, tx->vq.desc);
    pc_alloc_uninit(alloc);
    qvirtio_pci_device_disable(dev);
    g_free(dev);
    qpci_free_pc(bus);
    test_end();
}
#endif

static void hotplug(void)
{
    qtest_start("-device virtio-net-pci");

    qpci_plug_device_test("virtio-net-pci", "net1", PCI_SLOT_HP, NULL);
    qpci_unplug_acpi_device_test("net1", PCI_SLOT_HP);

    test_end();
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
#ifndef _WIN32
    qtest_add_data_func("/virtio/net/pci/basic", send_recv_test, pci_basic);
    qtest_add_data_func("/virtio/net/pci/rx_stop_cont",
                        stop_cont_test, pci_basic);
#endif
    qtest_add_func("/virtio/net/pci/hotplug", hotplug);

    ret = g_test_run();

    return ret;
}
