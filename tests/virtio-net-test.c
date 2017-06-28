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
#include "libqos/libqos-pc.h"
#include "libqos/libqos-spapr.h"
#include "libqos/virtio.h"
#include "libqos/virtio-pci.h"
#include "qemu/bswap.h"
#include "hw/virtio/virtio-net.h"
#include "standard-headers/linux/virtio_ids.h"
#include "standard-headers/linux/virtio_ring.h"

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

    dev = qvirtio_pci_device_find(bus, VIRTIO_ID_NET);
    g_assert(dev != NULL);
    g_assert_cmphex(dev->vdev.device_type, ==, VIRTIO_ID_NET);

    qvirtio_pci_device_enable(dev);
    qvirtio_reset(&dev->vdev);
    qvirtio_set_acknowledge(&dev->vdev);
    qvirtio_set_driver(&dev->vdev);

    return dev;
}

static QOSState *pci_test_start(int socket)
{
    const char *arch = qtest_get_arch();
    const char *cmd = "-netdev socket,fd=%d,id=hs0 -device "
                      "virtio-net-pci,netdev=hs0";

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        return qtest_pc_boot(cmd, socket);
    }
    if (strcmp(arch, "ppc64") == 0) {
        return qtest_spapr_boot(cmd, socket);
    }
    g_printerr("virtio-net tests are only available on x86 or ppc64\n");
    exit(EXIT_FAILURE);
}

static void driver_init(QVirtioDevice *dev)
{
    uint32_t features;

    features = qvirtio_get_features(dev);
    features = features & ~(QVIRTIO_F_BAD_FEATURE |
                            (1u << VIRTIO_RING_F_INDIRECT_DESC) |
                            (1u << VIRTIO_RING_F_EVENT_IDX));
    qvirtio_set_features(dev, features);

    qvirtio_set_driver_ok(dev);
}

static void rx_test(QVirtioDevice *dev,
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
    qvirtqueue_kick(dev, vq, free_head);

    ret = iov_send(socket, iov, 2, 0, sizeof(len) + sizeof(test));
    g_assert_cmpint(ret, ==, sizeof(test) + sizeof(len));

    qvirtio_wait_used_elem(dev, vq, free_head, QVIRTIO_NET_TIMEOUT_US);
    memread(req_addr + VNET_HDR_SIZE, buffer, sizeof(test));
    g_assert_cmpstr(buffer, ==, "TEST");

    guest_free(alloc, req_addr);
}

static void tx_test(QVirtioDevice *dev,
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
    qvirtqueue_kick(dev, vq, free_head);

    qvirtio_wait_used_elem(dev, vq, free_head, QVIRTIO_NET_TIMEOUT_US);
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

    free_head = qvirtqueue_add(vq, req_addr, 64, true, false);
    qvirtqueue_kick(dev, vq, free_head);

    rsp = qmp("{ 'execute' : 'stop'}");
    QDECREF(rsp);

    ret = iov_send(socket, iov, 2, 0, sizeof(len) + sizeof(test));
    g_assert_cmpint(ret, ==, sizeof(test) + sizeof(len));

    /* We could check the status, but this command is more importantly to
     * ensure the packet data gets queued in QEMU, before we do 'cont'.
     */
    rsp = qmp("{ 'execute' : 'query-status'}");
    QDECREF(rsp);
    rsp = qmp("{ 'execute' : 'cont'}");
    QDECREF(rsp);

    qvirtio_wait_used_elem(dev, vq, free_head, QVIRTIO_NET_TIMEOUT_US);
    memread(req_addr + VNET_HDR_SIZE, buffer, sizeof(test));
    g_assert_cmpstr(buffer, ==, "TEST");

    guest_free(alloc, req_addr);
}

static void send_recv_test(QVirtioDevice *dev,
                           QGuestAllocator *alloc, QVirtQueue *rvq,
                           QVirtQueue *tvq, int socket)
{
    rx_test(dev, alloc, rvq, socket);
    tx_test(dev, alloc, tvq, socket);
}

static void stop_cont_test(QVirtioDevice *dev,
                           QGuestAllocator *alloc, QVirtQueue *rvq,
                           QVirtQueue *tvq, int socket)
{
    rx_stop_cont_test(dev, alloc, rvq, socket);
}

static void pci_basic(gconstpointer data)
{
    QVirtioPCIDevice *dev;
    QOSState *qs;
    QVirtQueuePCI *tx, *rx;
    void (*func) (QVirtioDevice *dev,
                  QGuestAllocator *alloc,
                  QVirtQueue *rvq,
                  QVirtQueue *tvq,
                  int socket) = data;
    int sv[2], ret;

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sv);
    g_assert_cmpint(ret, !=, -1);

    qs = pci_test_start(sv[1]);
    dev = virtio_net_pci_init(qs->pcibus, PCI_SLOT);

    rx = (QVirtQueuePCI *)qvirtqueue_setup(&dev->vdev, qs->alloc, 0);
    tx = (QVirtQueuePCI *)qvirtqueue_setup(&dev->vdev, qs->alloc, 1);

    driver_init(&dev->vdev);
    func(&dev->vdev, qs->alloc, &rx->vq, &tx->vq, sv[0]);

    /* End test */
    close(sv[0]);
    qvirtqueue_cleanup(dev->vdev.bus, &tx->vq, qs->alloc);
    qvirtqueue_cleanup(dev->vdev.bus, &rx->vq, qs->alloc);
    qvirtio_pci_device_disable(dev);
    g_free(dev->pdev);
    g_free(dev);
    qtest_shutdown(qs);
}
#endif

static void hotplug(void)
{
    const char *arch = qtest_get_arch();

    qtest_start("-device virtio-net-pci");

    qpci_plug_device_test("virtio-net-pci", "net1", PCI_SLOT_HP, NULL);

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        qpci_unplug_acpi_device_test("net1", PCI_SLOT_HP);
    }

    test_end();
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
#ifndef _WIN32
    qtest_add_data_func("/virtio/net/pci/basic", send_recv_test, pci_basic);
    qtest_add_data_func("/virtio/net/pci/rx_stop_cont",
                        stop_cont_test, pci_basic);
#endif
    qtest_add_func("/virtio/net/pci/hotplug", hotplug);

    return g_test_run();
}
