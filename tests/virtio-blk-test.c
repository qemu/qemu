/*
 * QTest testcase for VirtIO Block Device
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 * Copyright (c) 2014 Marc Marí
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <glib.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include "libqtest.h"
#include "libqos/virtio.h"
#include "libqos/virtio-pci.h"
#include "libqos/pci-pc.h"
#include "libqos/malloc.h"
#include "libqos/malloc-pc.h"
#include "qemu/bswap.h"

#define QVIRTIO_BLK_F_BARRIER       0x00000001
#define QVIRTIO_BLK_F_SIZE_MAX      0x00000002
#define QVIRTIO_BLK_F_SEG_MAX       0x00000004
#define QVIRTIO_BLK_F_GEOMETRY      0x00000010
#define QVIRTIO_BLK_F_RO            0x00000020
#define QVIRTIO_BLK_F_BLK_SIZE      0x00000040
#define QVIRTIO_BLK_F_SCSI          0x00000080
#define QVIRTIO_BLK_F_WCE           0x00000200
#define QVIRTIO_BLK_F_TOPOLOGY      0x00000400
#define QVIRTIO_BLK_F_CONFIG_WCE    0x00000800

#define QVIRTIO_BLK_T_IN            0
#define QVIRTIO_BLK_T_OUT           1
#define QVIRTIO_BLK_T_SCSI_CMD      2
#define QVIRTIO_BLK_T_SCSI_CMD_OUT  3
#define QVIRTIO_BLK_T_FLUSH         4
#define QVIRTIO_BLK_T_FLUSH_OUT     5
#define QVIRTIO_BLK_T_GET_ID        8

#define TEST_IMAGE_SIZE         (64 * 1024 * 1024)
#define QVIRTIO_BLK_TIMEOUT     100
#define PCI_SLOT                0x04
#define PCI_FN                  0x00

typedef struct QVirtioBlkReq {
    uint32_t type;
    uint32_t ioprio;
    uint64_t sector;
    char *data;
    uint8_t status;
} QVirtioBlkReq;

static QPCIBus *test_start(void)
{
    char cmdline[100];
    char tmp_path[] = "/tmp/qtest.XXXXXX";
    int fd, ret;

    /* Create a temporary raw image */
    fd = mkstemp(tmp_path);
    g_assert_cmpint(fd, >=, 0);
    ret = ftruncate(fd, TEST_IMAGE_SIZE);
    g_assert_cmpint(ret, ==, 0);
    close(fd);

    snprintf(cmdline, 100, "-drive if=none,id=drive0,file=%s "
                            "-device virtio-blk-pci,drive=drive0,addr=%x.%x",
                            tmp_path, PCI_SLOT, PCI_FN);
    qtest_start(cmdline);
    unlink(tmp_path);

    return qpci_init_pc();
}

static void test_end(void)
{
    qtest_end();
}

static QVirtioPCIDevice *virtio_blk_init(QPCIBus *bus)
{
    QVirtioPCIDevice *dev;

    dev = qvirtio_pci_device_find(bus, QVIRTIO_BLK_DEVICE_ID);
    g_assert(dev != NULL);
    g_assert_cmphex(dev->vdev.device_type, ==, QVIRTIO_BLK_DEVICE_ID);
    g_assert_cmphex(dev->pdev->devfn, ==, ((PCI_SLOT << 3) | PCI_FN));

    qvirtio_pci_device_enable(dev);
    qvirtio_reset(&qvirtio_pci, &dev->vdev);
    qvirtio_set_acknowledge(&qvirtio_pci, &dev->vdev);
    qvirtio_set_driver(&qvirtio_pci, &dev->vdev);

    return dev;
}

static inline void virtio_blk_fix_request(QVirtioBlkReq *req)
{
#ifdef HOST_WORDS_BIGENDIAN
    bool host_endian = true;
#else
    bool host_endian = false;
#endif

    if (qtest_big_endian() != host_endian) {
        req->type = bswap32(req->type);
        req->ioprio = bswap32(req->ioprio);
        req->sector = bswap64(req->sector);
    }
}

static uint64_t virtio_blk_request(QGuestAllocator *alloc, QVirtioBlkReq *req,
                                                            uint64_t data_size)
{
    uint64_t addr;
    uint8_t status = 0xFF;

    g_assert_cmpuint(data_size % 512, ==, 0);
    addr = guest_alloc(alloc, sizeof(*req) + data_size);

    virtio_blk_fix_request(req);

    memwrite(addr, req, 16);
    memwrite(addr + 16, req->data, data_size);
    memwrite(addr + 16 + data_size, &status, sizeof(status));

    return addr;
}

static void pci_basic(void)
{
    QVirtioPCIDevice *dev;
    QPCIBus *bus;
    QVirtQueue *vq;
    QGuestAllocator *alloc;
    QVirtioBlkReq req;
    void *addr;
    uint64_t req_addr;
    uint64_t capacity;
    uint32_t features;
    uint32_t free_head;
    uint8_t status;
    char *data;

    bus = test_start();

    dev = virtio_blk_init(bus);

    /* MSI-X is not enabled */
    addr = dev->addr + QVIRTIO_DEVICE_SPECIFIC_NO_MSIX;

    capacity = qvirtio_config_readq(&qvirtio_pci, &dev->vdev, addr);
    g_assert_cmpint(capacity, ==, TEST_IMAGE_SIZE / 512);

    features = qvirtio_get_features(&qvirtio_pci, &dev->vdev);
    features = features & ~(QVIRTIO_F_BAD_FEATURE |
                    QVIRTIO_F_RING_INDIRECT_DESC | QVIRTIO_F_RING_EVENT_IDX |
                            QVIRTIO_BLK_F_SCSI);
    qvirtio_set_features(&qvirtio_pci, &dev->vdev, features);

    alloc = pc_alloc_init();
    vq = qvirtqueue_setup(&qvirtio_pci, &dev->vdev, alloc, 0);

    qvirtio_set_driver_ok(&qvirtio_pci, &dev->vdev);

    /* Write and read with 2 descriptor layout */
    /* Write request */
    req.type = QVIRTIO_BLK_T_OUT;
    req.ioprio = 1;
    req.sector = 0;
    req.data = g_malloc0(512);
    strcpy(req.data, "TEST");

    req_addr = virtio_blk_request(alloc, &req, 512);

    g_free(req.data);

    free_head = qvirtqueue_add(vq, req_addr, 528, false, true);
    qvirtqueue_add(vq, req_addr + 528, 1, true, false);
    qvirtqueue_kick(&qvirtio_pci, &dev->vdev, vq, free_head);

    g_assert(qvirtio_wait_isr(&qvirtio_pci, &dev->vdev, 0x1,
                                                        QVIRTIO_BLK_TIMEOUT));
    status = readb(req_addr + 528);
    g_assert_cmpint(status, ==, 0);

    guest_free(alloc, req_addr);

    /* Read request */
    req.type = QVIRTIO_BLK_T_IN;
    req.ioprio = 1;
    req.sector = 0;
    req.data = g_malloc0(512);

    req_addr = virtio_blk_request(alloc, &req, 512);

    g_free(req.data);

    free_head = qvirtqueue_add(vq, req_addr, 16, false, true);
    qvirtqueue_add(vq, req_addr + 16, 513, true, false);

    qvirtqueue_kick(&qvirtio_pci, &dev->vdev, vq, free_head);

    g_assert(qvirtio_wait_isr(&qvirtio_pci, &dev->vdev, 0x1,
                                                        QVIRTIO_BLK_TIMEOUT));
    status = readb(req_addr + 528);
    g_assert_cmpint(status, ==, 0);

    data = g_malloc0(512);
    memread(req_addr + 16, data, 512);
    g_assert_cmpstr(data, ==, "TEST");
    g_free(data);

    guest_free(alloc, req_addr);

    /* Write and read with 3 descriptor layout */
    /* Write request */
    req.type = QVIRTIO_BLK_T_OUT;
    req.ioprio = 1;
    req.sector = 1;
    req.data = g_malloc0(512);
    strcpy(req.data, "TEST");

    req_addr = virtio_blk_request(alloc, &req, 512);

    g_free(req.data);

    free_head = qvirtqueue_add(vq, req_addr, 16, false, true);
    qvirtqueue_add(vq, req_addr + 16, 512, false, true);
    qvirtqueue_add(vq, req_addr + 528, 1, true, false);

    qvirtqueue_kick(&qvirtio_pci, &dev->vdev, vq, free_head);

    g_assert(qvirtio_wait_isr(&qvirtio_pci, &dev->vdev, 0x1,
                                                        QVIRTIO_BLK_TIMEOUT));
    status = readb(req_addr + 528);
    g_assert_cmpint(status, ==, 0);

    guest_free(alloc, req_addr);

    /* Read request */
    req.type = QVIRTIO_BLK_T_IN;
    req.ioprio = 1;
    req.sector = 1;
    req.data = g_malloc0(512);

    req_addr = virtio_blk_request(alloc, &req, 512);

    g_free(req.data);

    free_head = qvirtqueue_add(vq, req_addr, 16, false, true);
    qvirtqueue_add(vq, req_addr + 16, 512, true, true);
    qvirtqueue_add(vq, req_addr + 528, 1, true, false);

    qvirtqueue_kick(&qvirtio_pci, &dev->vdev, vq, free_head);

    g_assert(qvirtio_wait_isr(&qvirtio_pci, &dev->vdev, 0x1,
                                                        QVIRTIO_BLK_TIMEOUT));
    status = readb(req_addr + 528);
    g_assert_cmpint(status, ==, 0);

    guest_free(alloc, req_addr);

    data = g_malloc0(512);
    memread(req_addr + 16, data, 512);
    g_assert_cmpstr(data, ==, "TEST");
    g_free(data);

    guest_free(alloc, req_addr);

    /* End test */
    guest_free(alloc, vq->desc);
    qvirtio_pci_device_disable(dev);
    g_free(dev);
    test_end();
}

static void pci_indirect(void)
{
    QVirtioPCIDevice *dev;
    QPCIBus *bus;
    QVirtQueue *vq;
    QGuestAllocator *alloc;
    QVirtioBlkReq req;
    QVRingIndirectDesc *indirect;
    void *addr;
    uint64_t req_addr;
    uint64_t capacity;
    uint32_t features;
    uint32_t free_head;
    uint8_t status;
    char *data;

    bus = test_start();

    dev = virtio_blk_init(bus);

    /* MSI-X is not enabled */
    addr = dev->addr + QVIRTIO_DEVICE_SPECIFIC_NO_MSIX;

    capacity = qvirtio_config_readq(&qvirtio_pci, &dev->vdev, addr);
    g_assert_cmpint(capacity, ==, TEST_IMAGE_SIZE / 512);

    features = qvirtio_get_features(&qvirtio_pci, &dev->vdev);
    g_assert_cmphex(features & QVIRTIO_F_RING_INDIRECT_DESC, !=, 0);
    features = features & ~(QVIRTIO_F_BAD_FEATURE | QVIRTIO_F_RING_EVENT_IDX |
                                                            QVIRTIO_BLK_F_SCSI);
    qvirtio_set_features(&qvirtio_pci, &dev->vdev, features);

    alloc = pc_alloc_init();
    vq = qvirtqueue_setup(&qvirtio_pci, &dev->vdev, alloc, 0);

    qvirtio_set_driver_ok(&qvirtio_pci, &dev->vdev);

    /* Write request */
    req.type = QVIRTIO_BLK_T_OUT;
    req.ioprio = 1;
    req.sector = 0;
    req.data = g_malloc0(512);
    strcpy(req.data, "TEST");

    req_addr = virtio_blk_request(alloc, &req, 512);

    g_free(req.data);

    indirect = qvring_indirect_desc_setup(&dev->vdev, alloc, 2);
    qvring_indirect_desc_add(indirect, req_addr, 528, false);
    qvring_indirect_desc_add(indirect, req_addr + 528, 1, true);
    free_head = qvirtqueue_add_indirect(vq, indirect);
    qvirtqueue_kick(&qvirtio_pci, &dev->vdev, vq, free_head);

    g_assert(qvirtio_wait_isr(&qvirtio_pci, &dev->vdev, 0x1,
                                                        QVIRTIO_BLK_TIMEOUT));
    status = readb(req_addr + 528);
    g_assert_cmpint(status, ==, 0);

    g_free(indirect);
    guest_free(alloc, req_addr);

    /* Read request */
    req.type = QVIRTIO_BLK_T_IN;
    req.ioprio = 1;
    req.sector = 0;
    req.data = g_malloc0(512);
    strcpy(req.data, "TEST");

    req_addr = virtio_blk_request(alloc, &req, 512);

    g_free(req.data);

    indirect = qvring_indirect_desc_setup(&dev->vdev, alloc, 2);
    qvring_indirect_desc_add(indirect, req_addr, 16, false);
    qvring_indirect_desc_add(indirect, req_addr + 16, 513, true);
    free_head = qvirtqueue_add_indirect(vq, indirect);
    qvirtqueue_kick(&qvirtio_pci, &dev->vdev, vq, free_head);

    g_assert(qvirtio_wait_isr(&qvirtio_pci, &dev->vdev, 0x1,
                                                        QVIRTIO_BLK_TIMEOUT));
    status = readb(req_addr + 528);
    g_assert_cmpint(status, ==, 0);

    data = g_malloc0(512);
    memread(req_addr + 16, data, 512);
    g_assert_cmpstr(data, ==, "TEST");
    g_free(data);

    g_free(indirect);
    guest_free(alloc, req_addr);

    /* End test */
    guest_free(alloc, vq->desc);
    qvirtio_pci_device_disable(dev);
    g_free(dev);
    test_end();
}

static void pci_config(void)
{
    QVirtioPCIDevice *dev;
    QPCIBus *bus;
    int n_size = TEST_IMAGE_SIZE / 2;
    void *addr;
    uint64_t capacity;

    bus = test_start();

    dev = virtio_blk_init(bus);

    /* MSI-X is not enabled */
    addr = dev->addr + QVIRTIO_DEVICE_SPECIFIC_NO_MSIX;

    capacity = qvirtio_config_readq(&qvirtio_pci, &dev->vdev, addr);
    g_assert_cmpint(capacity, ==, TEST_IMAGE_SIZE / 512);

    qvirtio_set_driver_ok(&qvirtio_pci, &dev->vdev);

    qmp("{ 'execute': 'block_resize', 'arguments': { 'device': 'drive0', "
                                                    " 'size': %d } }", n_size);
    g_assert(qvirtio_wait_isr(&qvirtio_pci, &dev->vdev, 0x2,
                                                        QVIRTIO_BLK_TIMEOUT));

    capacity = qvirtio_config_readq(&qvirtio_pci, &dev->vdev, addr);
    g_assert_cmpint(capacity, ==, n_size / 512);

    qvirtio_pci_device_disable(dev);
    g_free(dev);
    test_end();
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/virtio/blk/pci/basic", pci_basic);
    g_test_add_func("/virtio/blk/pci/indirect", pci_indirect);
    g_test_add_func("/virtio/blk/pci/config", pci_config);

    ret = g_test_run();

    return ret;
}
