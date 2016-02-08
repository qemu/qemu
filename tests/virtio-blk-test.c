/*
 * QTest testcase for VirtIO Block Device
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 * Copyright (c) 2014 Marc Marí
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <glib.h>
#include "libqtest.h"
#include "libqos/virtio.h"
#include "libqos/virtio-pci.h"
#include "libqos/virtio-mmio.h"
#include "libqos/pci-pc.h"
#include "libqos/malloc.h"
#include "libqos/malloc-pc.h"
#include "libqos/malloc-generic.h"
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
#define QVIRTIO_BLK_TIMEOUT_US  (30 * 1000 * 1000)
#define PCI_SLOT_HP             0x06
#define PCI_SLOT                0x04
#define PCI_FN                  0x00

#define MMIO_PAGE_SIZE          4096
#define MMIO_DEV_BASE_ADDR      0x0A003E00
#define MMIO_RAM_ADDR           0x40000000
#define MMIO_RAM_SIZE           0x20000000

typedef struct QVirtioBlkReq {
    uint32_t type;
    uint32_t ioprio;
    uint64_t sector;
    char *data;
    uint8_t status;
} QVirtioBlkReq;

static char *drive_create(void)
{
    int fd, ret;
    char *tmp_path = g_strdup("/tmp/qtest.XXXXXX");

    /* Create a temporary raw image */
    fd = mkstemp(tmp_path);
    g_assert_cmpint(fd, >=, 0);
    ret = ftruncate(fd, TEST_IMAGE_SIZE);
    g_assert_cmpint(ret, ==, 0);
    close(fd);

    return tmp_path;
}

static QPCIBus *pci_test_start(void)
{
    char *cmdline;
    char *tmp_path;

    tmp_path = drive_create();

    cmdline = g_strdup_printf("-drive if=none,id=drive0,file=%s,format=raw "
                        "-drive if=none,id=drive1,file=/dev/null,format=raw "
                        "-device virtio-blk-pci,id=drv0,drive=drive0,"
                        "addr=%x.%x",
                        tmp_path, PCI_SLOT, PCI_FN);
    qtest_start(cmdline);
    unlink(tmp_path);
    g_free(tmp_path);
    g_free(cmdline);

    return qpci_init_pc();
}

static void arm_test_start(void)
{
    char *cmdline;
    char *tmp_path;

    tmp_path = drive_create();

    cmdline = g_strdup_printf("-machine virt "
                                "-drive if=none,id=drive0,file=%s,format=raw "
                                "-device virtio-blk-device,drive=drive0",
                                tmp_path);
    qtest_start(cmdline);
    unlink(tmp_path);
    g_free(tmp_path);
    g_free(cmdline);
}

static void test_end(void)
{
    qtest_end();
}

static QVirtioPCIDevice *virtio_blk_pci_init(QPCIBus *bus, int slot)
{
    QVirtioPCIDevice *dev;

    dev = qvirtio_pci_device_find(bus, QVIRTIO_BLK_DEVICE_ID);
    g_assert(dev != NULL);
    g_assert_cmphex(dev->vdev.device_type, ==, QVIRTIO_BLK_DEVICE_ID);
    g_assert_cmphex(dev->pdev->devfn, ==, ((slot << 3) | PCI_FN));

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

static void test_basic(const QVirtioBus *bus, QVirtioDevice *dev,
            QGuestAllocator *alloc, QVirtQueue *vq, uint64_t device_specific)
{
    QVirtioBlkReq req;
    uint64_t req_addr;
    uint64_t capacity;
    uint32_t features;
    uint32_t free_head;
    uint8_t status;
    char *data;

    capacity = qvirtio_config_readq(bus, dev, device_specific);

    g_assert_cmpint(capacity, ==, TEST_IMAGE_SIZE / 512);

    features = qvirtio_get_features(bus, dev);
    features = features & ~(QVIRTIO_F_BAD_FEATURE |
                    QVIRTIO_F_RING_INDIRECT_DESC | QVIRTIO_F_RING_EVENT_IDX |
                            QVIRTIO_BLK_F_SCSI);
    qvirtio_set_features(bus, dev, features);

    qvirtio_set_driver_ok(bus, dev);

    /* Write and read with 3 descriptor layout */
    /* Write request */
    req.type = QVIRTIO_BLK_T_OUT;
    req.ioprio = 1;
    req.sector = 0;
    req.data = g_malloc0(512);
    strcpy(req.data, "TEST");

    req_addr = virtio_blk_request(alloc, &req, 512);

    g_free(req.data);

    free_head = qvirtqueue_add(vq, req_addr, 16, false, true);
    qvirtqueue_add(vq, req_addr + 16, 512, false, true);
    qvirtqueue_add(vq, req_addr + 528, 1, true, false);

    qvirtqueue_kick(bus, dev, vq, free_head);

    qvirtio_wait_queue_isr(bus, dev, vq, QVIRTIO_BLK_TIMEOUT_US);
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
    qvirtqueue_add(vq, req_addr + 16, 512, true, true);
    qvirtqueue_add(vq, req_addr + 528, 1, true, false);

    qvirtqueue_kick(bus, dev, vq, free_head);

    qvirtio_wait_queue_isr(bus, dev, vq, QVIRTIO_BLK_TIMEOUT_US);
    status = readb(req_addr + 528);
    g_assert_cmpint(status, ==, 0);

    data = g_malloc0(512);
    memread(req_addr + 16, data, 512);
    g_assert_cmpstr(data, ==, "TEST");
    g_free(data);

    guest_free(alloc, req_addr);

    if (features & QVIRTIO_F_ANY_LAYOUT) {
        /* Write and read with 2 descriptor layout */
        /* Write request */
        req.type = QVIRTIO_BLK_T_OUT;
        req.ioprio = 1;
        req.sector = 1;
        req.data = g_malloc0(512);
        strcpy(req.data, "TEST");

        req_addr = virtio_blk_request(alloc, &req, 512);

        g_free(req.data);

        free_head = qvirtqueue_add(vq, req_addr, 528, false, true);
        qvirtqueue_add(vq, req_addr + 528, 1, true, false);
        qvirtqueue_kick(bus, dev, vq, free_head);

        qvirtio_wait_queue_isr(bus, dev, vq, QVIRTIO_BLK_TIMEOUT_US);
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
        qvirtqueue_add(vq, req_addr + 16, 513, true, false);

        qvirtqueue_kick(bus, dev, vq, free_head);

        qvirtio_wait_queue_isr(bus, dev, vq, QVIRTIO_BLK_TIMEOUT_US);
        status = readb(req_addr + 528);
        g_assert_cmpint(status, ==, 0);

        data = g_malloc0(512);
        memread(req_addr + 16, data, 512);
        g_assert_cmpstr(data, ==, "TEST");
        g_free(data);

        guest_free(alloc, req_addr);
    }
}

static void pci_basic(void)
{
    QVirtioPCIDevice *dev;
    QPCIBus *bus;
    QVirtQueuePCI *vqpci;
    QGuestAllocator *alloc;
    void *addr;

    bus = pci_test_start();
    dev = virtio_blk_pci_init(bus, PCI_SLOT);

    alloc = pc_alloc_init();
    vqpci = (QVirtQueuePCI *)qvirtqueue_setup(&qvirtio_pci, &dev->vdev,
                                                                    alloc, 0);

    /* MSI-X is not enabled */
    addr = dev->addr + QVIRTIO_PCI_DEVICE_SPECIFIC_NO_MSIX;

    test_basic(&qvirtio_pci, &dev->vdev, alloc, &vqpci->vq,
                                                    (uint64_t)(uintptr_t)addr);

    /* End test */
    guest_free(alloc, vqpci->vq.desc);
    pc_alloc_uninit(alloc);
    qvirtio_pci_device_disable(dev);
    g_free(dev);
    qpci_free_pc(bus);
    test_end();
}

static void pci_indirect(void)
{
    QVirtioPCIDevice *dev;
    QPCIBus *bus;
    QVirtQueuePCI *vqpci;
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

    bus = pci_test_start();

    dev = virtio_blk_pci_init(bus, PCI_SLOT);

    /* MSI-X is not enabled */
    addr = dev->addr + QVIRTIO_PCI_DEVICE_SPECIFIC_NO_MSIX;

    capacity = qvirtio_config_readq(&qvirtio_pci, &dev->vdev,
                                                    (uint64_t)(uintptr_t)addr);
    g_assert_cmpint(capacity, ==, TEST_IMAGE_SIZE / 512);

    features = qvirtio_get_features(&qvirtio_pci, &dev->vdev);
    g_assert_cmphex(features & QVIRTIO_F_RING_INDIRECT_DESC, !=, 0);
    features = features & ~(QVIRTIO_F_BAD_FEATURE | QVIRTIO_F_RING_EVENT_IDX |
                                                            QVIRTIO_BLK_F_SCSI);
    qvirtio_set_features(&qvirtio_pci, &dev->vdev, features);

    alloc = pc_alloc_init();
    vqpci = (QVirtQueuePCI *)qvirtqueue_setup(&qvirtio_pci, &dev->vdev,
                                                                    alloc, 0);
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
    free_head = qvirtqueue_add_indirect(&vqpci->vq, indirect);
    qvirtqueue_kick(&qvirtio_pci, &dev->vdev, &vqpci->vq, free_head);

    qvirtio_wait_queue_isr(&qvirtio_pci, &dev->vdev, &vqpci->vq,
                           QVIRTIO_BLK_TIMEOUT_US);
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
    free_head = qvirtqueue_add_indirect(&vqpci->vq, indirect);
    qvirtqueue_kick(&qvirtio_pci, &dev->vdev, &vqpci->vq, free_head);

    qvirtio_wait_queue_isr(&qvirtio_pci, &dev->vdev, &vqpci->vq,
                           QVIRTIO_BLK_TIMEOUT_US);
    status = readb(req_addr + 528);
    g_assert_cmpint(status, ==, 0);

    data = g_malloc0(512);
    memread(req_addr + 16, data, 512);
    g_assert_cmpstr(data, ==, "TEST");
    g_free(data);

    g_free(indirect);
    guest_free(alloc, req_addr);

    /* End test */
    guest_free(alloc, vqpci->vq.desc);
    pc_alloc_uninit(alloc);
    qvirtio_pci_device_disable(dev);
    g_free(dev);
    qpci_free_pc(bus);
    test_end();
}

static void pci_config(void)
{
    QVirtioPCIDevice *dev;
    QPCIBus *bus;
    int n_size = TEST_IMAGE_SIZE / 2;
    void *addr;
    uint64_t capacity;

    bus = pci_test_start();

    dev = virtio_blk_pci_init(bus, PCI_SLOT);

    /* MSI-X is not enabled */
    addr = dev->addr + QVIRTIO_PCI_DEVICE_SPECIFIC_NO_MSIX;

    capacity = qvirtio_config_readq(&qvirtio_pci, &dev->vdev,
                                                    (uint64_t)(uintptr_t)addr);
    g_assert_cmpint(capacity, ==, TEST_IMAGE_SIZE / 512);

    qvirtio_set_driver_ok(&qvirtio_pci, &dev->vdev);

    qmp("{ 'execute': 'block_resize', 'arguments': { 'device': 'drive0', "
                                                    " 'size': %d } }", n_size);
    qvirtio_wait_config_isr(&qvirtio_pci, &dev->vdev, QVIRTIO_BLK_TIMEOUT_US);

    capacity = qvirtio_config_readq(&qvirtio_pci, &dev->vdev,
                                                    (uint64_t)(uintptr_t)addr);
    g_assert_cmpint(capacity, ==, n_size / 512);

    qvirtio_pci_device_disable(dev);
    g_free(dev);
    qpci_free_pc(bus);
    test_end();
}

static void pci_msix(void)
{
    QVirtioPCIDevice *dev;
    QPCIBus *bus;
    QVirtQueuePCI *vqpci;
    QGuestAllocator *alloc;
    QVirtioBlkReq req;
    int n_size = TEST_IMAGE_SIZE / 2;
    void *addr;
    uint64_t req_addr;
    uint64_t capacity;
    uint32_t features;
    uint32_t free_head;
    uint8_t status;
    char *data;

    bus = pci_test_start();
    alloc = pc_alloc_init();

    dev = virtio_blk_pci_init(bus, PCI_SLOT);
    qpci_msix_enable(dev->pdev);

    qvirtio_pci_set_msix_configuration_vector(dev, alloc, 0);

    /* MSI-X is enabled */
    addr = dev->addr + QVIRTIO_PCI_DEVICE_SPECIFIC_MSIX;

    capacity = qvirtio_config_readq(&qvirtio_pci, &dev->vdev,
                                                    (uint64_t)(uintptr_t)addr);
    g_assert_cmpint(capacity, ==, TEST_IMAGE_SIZE / 512);

    features = qvirtio_get_features(&qvirtio_pci, &dev->vdev);
    features = features & ~(QVIRTIO_F_BAD_FEATURE |
                            QVIRTIO_F_RING_INDIRECT_DESC |
                            QVIRTIO_F_RING_EVENT_IDX | QVIRTIO_BLK_F_SCSI);
    qvirtio_set_features(&qvirtio_pci, &dev->vdev, features);

    vqpci = (QVirtQueuePCI *)qvirtqueue_setup(&qvirtio_pci, &dev->vdev,
                                                                    alloc, 0);
    qvirtqueue_pci_msix_setup(dev, vqpci, alloc, 1);

    qvirtio_set_driver_ok(&qvirtio_pci, &dev->vdev);

    qmp("{ 'execute': 'block_resize', 'arguments': { 'device': 'drive0', "
                                                    " 'size': %d } }", n_size);

    qvirtio_wait_config_isr(&qvirtio_pci, &dev->vdev, QVIRTIO_BLK_TIMEOUT_US);

    capacity = qvirtio_config_readq(&qvirtio_pci, &dev->vdev,
                                                    (uint64_t)(uintptr_t)addr);
    g_assert_cmpint(capacity, ==, n_size / 512);

    /* Write request */
    req.type = QVIRTIO_BLK_T_OUT;
    req.ioprio = 1;
    req.sector = 0;
    req.data = g_malloc0(512);
    strcpy(req.data, "TEST");

    req_addr = virtio_blk_request(alloc, &req, 512);

    g_free(req.data);

    free_head = qvirtqueue_add(&vqpci->vq, req_addr, 16, false, true);
    qvirtqueue_add(&vqpci->vq, req_addr + 16, 512, false, true);
    qvirtqueue_add(&vqpci->vq, req_addr + 528, 1, true, false);
    qvirtqueue_kick(&qvirtio_pci, &dev->vdev, &vqpci->vq, free_head);

    qvirtio_wait_queue_isr(&qvirtio_pci, &dev->vdev, &vqpci->vq,
                           QVIRTIO_BLK_TIMEOUT_US);

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

    free_head = qvirtqueue_add(&vqpci->vq, req_addr, 16, false, true);
    qvirtqueue_add(&vqpci->vq, req_addr + 16, 512, true, true);
    qvirtqueue_add(&vqpci->vq, req_addr + 528, 1, true, false);

    qvirtqueue_kick(&qvirtio_pci, &dev->vdev, &vqpci->vq, free_head);


    qvirtio_wait_queue_isr(&qvirtio_pci, &dev->vdev, &vqpci->vq,
                           QVIRTIO_BLK_TIMEOUT_US);

    status = readb(req_addr + 528);
    g_assert_cmpint(status, ==, 0);

    data = g_malloc0(512);
    memread(req_addr + 16, data, 512);
    g_assert_cmpstr(data, ==, "TEST");
    g_free(data);

    guest_free(alloc, req_addr);

    /* End test */
    guest_free(alloc, vqpci->vq.desc);
    pc_alloc_uninit(alloc);
    qpci_msix_disable(dev->pdev);
    qvirtio_pci_device_disable(dev);
    g_free(dev);
    qpci_free_pc(bus);
    test_end();
}

static void pci_idx(void)
{
    QVirtioPCIDevice *dev;
    QPCIBus *bus;
    QVirtQueuePCI *vqpci;
    QGuestAllocator *alloc;
    QVirtioBlkReq req;
    void *addr;
    uint64_t req_addr;
    uint64_t capacity;
    uint32_t features;
    uint32_t free_head;
    uint8_t status;
    char *data;

    bus = pci_test_start();
    alloc = pc_alloc_init();

    dev = virtio_blk_pci_init(bus, PCI_SLOT);
    qpci_msix_enable(dev->pdev);

    qvirtio_pci_set_msix_configuration_vector(dev, alloc, 0);

    /* MSI-X is enabled */
    addr = dev->addr + QVIRTIO_PCI_DEVICE_SPECIFIC_MSIX;

    capacity = qvirtio_config_readq(&qvirtio_pci, &dev->vdev,
                                                    (uint64_t)(uintptr_t)addr);
    g_assert_cmpint(capacity, ==, TEST_IMAGE_SIZE / 512);

    features = qvirtio_get_features(&qvirtio_pci, &dev->vdev);
    features = features & ~(QVIRTIO_F_BAD_FEATURE |
                            QVIRTIO_F_RING_INDIRECT_DESC |
                            QVIRTIO_F_NOTIFY_ON_EMPTY | QVIRTIO_BLK_F_SCSI);
    qvirtio_set_features(&qvirtio_pci, &dev->vdev, features);

    vqpci = (QVirtQueuePCI *)qvirtqueue_setup(&qvirtio_pci, &dev->vdev,
                                                                    alloc, 0);
    qvirtqueue_pci_msix_setup(dev, vqpci, alloc, 1);

    qvirtio_set_driver_ok(&qvirtio_pci, &dev->vdev);

    /* Write request */
    req.type = QVIRTIO_BLK_T_OUT;
    req.ioprio = 1;
    req.sector = 0;
    req.data = g_malloc0(512);
    strcpy(req.data, "TEST");

    req_addr = virtio_blk_request(alloc, &req, 512);

    g_free(req.data);

    free_head = qvirtqueue_add(&vqpci->vq, req_addr, 16, false, true);
    qvirtqueue_add(&vqpci->vq, req_addr + 16, 512, false, true);
    qvirtqueue_add(&vqpci->vq, req_addr + 528, 1, true, false);
    qvirtqueue_kick(&qvirtio_pci, &dev->vdev, &vqpci->vq, free_head);

    qvirtio_wait_queue_isr(&qvirtio_pci, &dev->vdev, &vqpci->vq,
                           QVIRTIO_BLK_TIMEOUT_US);

    /* Write request */
    req.type = QVIRTIO_BLK_T_OUT;
    req.ioprio = 1;
    req.sector = 1;
    req.data = g_malloc0(512);
    strcpy(req.data, "TEST");

    req_addr = virtio_blk_request(alloc, &req, 512);

    g_free(req.data);

    /* Notify after processing the third request */
    qvirtqueue_set_used_event(&vqpci->vq, 2);
    free_head = qvirtqueue_add(&vqpci->vq, req_addr, 16, false, true);
    qvirtqueue_add(&vqpci->vq, req_addr + 16, 512, false, true);
    qvirtqueue_add(&vqpci->vq, req_addr + 528, 1, true, false);
    qvirtqueue_kick(&qvirtio_pci, &dev->vdev, &vqpci->vq, free_head);

    /* No notification expected */
    status = qvirtio_wait_status_byte_no_isr(&qvirtio_pci, &dev->vdev,
                                             &vqpci->vq, req_addr + 528,
                                             QVIRTIO_BLK_TIMEOUT_US);
    g_assert_cmpint(status, ==, 0);

    guest_free(alloc, req_addr);

    /* Read request */
    req.type = QVIRTIO_BLK_T_IN;
    req.ioprio = 1;
    req.sector = 1;
    req.data = g_malloc0(512);

    req_addr = virtio_blk_request(alloc, &req, 512);

    g_free(req.data);

    free_head = qvirtqueue_add(&vqpci->vq, req_addr, 16, false, true);
    qvirtqueue_add(&vqpci->vq, req_addr + 16, 512, true, true);
    qvirtqueue_add(&vqpci->vq, req_addr + 528, 1, true, false);

    qvirtqueue_kick(&qvirtio_pci, &dev->vdev, &vqpci->vq, free_head);

    qvirtio_wait_queue_isr(&qvirtio_pci, &dev->vdev, &vqpci->vq,
                           QVIRTIO_BLK_TIMEOUT_US);

    status = readb(req_addr + 528);
    g_assert_cmpint(status, ==, 0);

    data = g_malloc0(512);
    memread(req_addr + 16, data, 512);
    g_assert_cmpstr(data, ==, "TEST");
    g_free(data);

    guest_free(alloc, req_addr);

    /* End test */
    guest_free(alloc, vqpci->vq.desc);
    pc_alloc_uninit(alloc);
    qpci_msix_disable(dev->pdev);
    qvirtio_pci_device_disable(dev);
    g_free(dev);
    qpci_free_pc(bus);
    test_end();
}

static void pci_hotplug(void)
{
    QPCIBus *bus;
    QVirtioPCIDevice *dev;

    bus = pci_test_start();

    /* plug secondary disk */
    qpci_plug_device_test("virtio-blk-pci", "drv1", PCI_SLOT_HP,
                          "'drive': 'drive1'");

    dev = virtio_blk_pci_init(bus, PCI_SLOT_HP);
    g_assert(dev);
    qvirtio_pci_device_disable(dev);
    g_free(dev);

    /* unplug secondary disk */
    qpci_unplug_acpi_device_test("drv1", PCI_SLOT_HP);
    qpci_free_pc(bus);
    test_end();
}

static void mmio_basic(void)
{
    QVirtioMMIODevice *dev;
    QVirtQueue *vq;
    QGuestAllocator *alloc;
    int n_size = TEST_IMAGE_SIZE / 2;
    uint64_t capacity;

    arm_test_start();

    dev = qvirtio_mmio_init_device(MMIO_DEV_BASE_ADDR, MMIO_PAGE_SIZE);
    g_assert(dev != NULL);
    g_assert_cmphex(dev->vdev.device_type, ==, QVIRTIO_BLK_DEVICE_ID);

    qvirtio_reset(&qvirtio_mmio, &dev->vdev);
    qvirtio_set_acknowledge(&qvirtio_mmio, &dev->vdev);
    qvirtio_set_driver(&qvirtio_mmio, &dev->vdev);

    alloc = generic_alloc_init(MMIO_RAM_ADDR, MMIO_RAM_SIZE, MMIO_PAGE_SIZE);
    vq = qvirtqueue_setup(&qvirtio_mmio, &dev->vdev, alloc, 0);

    test_basic(&qvirtio_mmio, &dev->vdev, alloc, vq,
                            QVIRTIO_MMIO_DEVICE_SPECIFIC);

    qmp("{ 'execute': 'block_resize', 'arguments': { 'device': 'drive0', "
                                                    " 'size': %d } }", n_size);

    qvirtio_wait_queue_isr(&qvirtio_mmio, &dev->vdev, vq,
                           QVIRTIO_BLK_TIMEOUT_US);

    capacity = qvirtio_config_readq(&qvirtio_mmio, &dev->vdev,
                                                QVIRTIO_MMIO_DEVICE_SPECIFIC);
    g_assert_cmpint(capacity, ==, n_size / 512);

    /* End test */
    guest_free(alloc, vq->desc);
    generic_alloc_uninit(alloc);
    g_free(dev);
    test_end();
}

int main(int argc, char **argv)
{
    int ret;
    const char *arch = qtest_get_arch();

    g_test_init(&argc, &argv, NULL);

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        qtest_add_func("/virtio/blk/pci/basic", pci_basic);
        qtest_add_func("/virtio/blk/pci/indirect", pci_indirect);
        qtest_add_func("/virtio/blk/pci/config", pci_config);
        qtest_add_func("/virtio/blk/pci/msix", pci_msix);
        qtest_add_func("/virtio/blk/pci/idx", pci_idx);
        qtest_add_func("/virtio/blk/pci/hotplug", pci_hotplug);
    } else if (strcmp(arch, "arm") == 0) {
        qtest_add_func("/virtio/blk/mmio/basic", mmio_basic);
    }

    ret = g_test_run();

    return ret;
}
