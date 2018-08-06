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
#include "libqtest.h"
#include "libqos/libqos-pc.h"
#include "libqos/libqos-spapr.h"
#include "libqos/virtio.h"
#include "libqos/virtio-pci.h"
#include "libqos/virtio-mmio.h"
#include "libqos/malloc-generic.h"
#include "qapi/qmp/qdict.h"
#include "qemu/bswap.h"
#include "standard-headers/linux/virtio_ids.h"
#include "standard-headers/linux/virtio_config.h"
#include "standard-headers/linux/virtio_ring.h"
#include "standard-headers/linux/virtio_blk.h"
#include "standard-headers/linux/virtio_pci.h"

/* TODO actually test the results and get rid of this */
#define qmp_discard_response(...) qobject_unref(qmp(__VA_ARGS__))

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

static QOSState *pci_test_start(void)
{
    QOSState *qs;
    const char *arch = qtest_get_arch();
    char *tmp_path;
    const char *cmd = "-drive if=none,id=drive0,file=%s,format=raw "
                      "-drive if=none,id=drive1,file=null-co://,format=raw "
                      "-device virtio-blk-pci,id=drv0,drive=drive0,"
                      "addr=%x.%x";

    tmp_path = drive_create();

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        qs = qtest_pc_boot(cmd, tmp_path, PCI_SLOT, PCI_FN);
    } else if (strcmp(arch, "ppc64") == 0) {
        qs = qtest_spapr_boot(cmd, tmp_path, PCI_SLOT, PCI_FN);
    } else {
        g_printerr("virtio-blk tests are only available on x86 or ppc64\n");
        exit(EXIT_FAILURE);
    }
    global_qtest = qs->qts;
    unlink(tmp_path);
    g_free(tmp_path);
    return qs;
}

static void arm_test_start(void)
{
    char *tmp_path;

    tmp_path = drive_create();

    global_qtest = qtest_initf("-machine virt "
                               "-drive if=none,id=drive0,file=%s,format=raw "
                               "-device virtio-blk-device,drive=drive0",
                               tmp_path);
    unlink(tmp_path);
    g_free(tmp_path);
}

static void test_end(void)
{
    qtest_end();
}

static QVirtioPCIDevice *virtio_blk_pci_init(QPCIBus *bus, int slot)
{
    QVirtioPCIDevice *dev;

    dev = qvirtio_pci_device_find_slot(bus, VIRTIO_ID_BLOCK, slot);
    g_assert(dev != NULL);
    g_assert_cmphex(dev->vdev.device_type, ==, VIRTIO_ID_BLOCK);
    g_assert_cmphex(dev->pdev->devfn, ==, ((slot << 3) | PCI_FN));

    qvirtio_pci_device_enable(dev);
    qvirtio_reset(&dev->vdev);
    qvirtio_set_acknowledge(&dev->vdev);
    qvirtio_set_driver(&dev->vdev);

    return dev;
}

static inline void virtio_blk_fix_request(QVirtioDevice *d, QVirtioBlkReq *req)
{
#ifdef HOST_WORDS_BIGENDIAN
    const bool host_is_big_endian = true;
#else
    const bool host_is_big_endian = false;
#endif

    if (qvirtio_is_big_endian(d) != host_is_big_endian) {
        req->type = bswap32(req->type);
        req->ioprio = bswap32(req->ioprio);
        req->sector = bswap64(req->sector);
    }
}

static uint64_t virtio_blk_request(QGuestAllocator *alloc, QVirtioDevice *d,
                                   QVirtioBlkReq *req, uint64_t data_size)
{
    uint64_t addr;
    uint8_t status = 0xFF;

    g_assert_cmpuint(data_size % 512, ==, 0);
    addr = guest_alloc(alloc, sizeof(*req) + data_size);

    virtio_blk_fix_request(d, req);

    memwrite(addr, req, 16);
    memwrite(addr + 16, req->data, data_size);
    memwrite(addr + 16 + data_size, &status, sizeof(status));

    return addr;
}

static void test_basic(QVirtioDevice *dev, QGuestAllocator *alloc,
                       QVirtQueue *vq)
{
    QVirtioBlkReq req;
    uint64_t req_addr;
    uint64_t capacity;
    uint32_t features;
    uint32_t free_head;
    uint8_t status;
    char *data;

    capacity = qvirtio_config_readq(dev, 0);

    g_assert_cmpint(capacity, ==, TEST_IMAGE_SIZE / 512);

    features = qvirtio_get_features(dev);
    features = features & ~(QVIRTIO_F_BAD_FEATURE |
                    (1u << VIRTIO_RING_F_INDIRECT_DESC) |
                    (1u << VIRTIO_RING_F_EVENT_IDX) |
                    (1u << VIRTIO_BLK_F_SCSI));
    qvirtio_set_features(dev, features);

    qvirtio_set_driver_ok(dev);

    /* Write and read with 3 descriptor layout */
    /* Write request */
    req.type = VIRTIO_BLK_T_OUT;
    req.ioprio = 1;
    req.sector = 0;
    req.data = g_malloc0(512);
    strcpy(req.data, "TEST");

    req_addr = virtio_blk_request(alloc, dev, &req, 512);

    g_free(req.data);

    free_head = qvirtqueue_add(vq, req_addr, 16, false, true);
    qvirtqueue_add(vq, req_addr + 16, 512, false, true);
    qvirtqueue_add(vq, req_addr + 528, 1, true, false);

    qvirtqueue_kick(dev, vq, free_head);

    qvirtio_wait_used_elem(dev, vq, free_head, NULL, QVIRTIO_BLK_TIMEOUT_US);
    status = readb(req_addr + 528);
    g_assert_cmpint(status, ==, 0);

    guest_free(alloc, req_addr);

    /* Read request */
    req.type = VIRTIO_BLK_T_IN;
    req.ioprio = 1;
    req.sector = 0;
    req.data = g_malloc0(512);

    req_addr = virtio_blk_request(alloc, dev, &req, 512);

    g_free(req.data);

    free_head = qvirtqueue_add(vq, req_addr, 16, false, true);
    qvirtqueue_add(vq, req_addr + 16, 512, true, true);
    qvirtqueue_add(vq, req_addr + 528, 1, true, false);

    qvirtqueue_kick(dev, vq, free_head);

    qvirtio_wait_used_elem(dev, vq, free_head, NULL, QVIRTIO_BLK_TIMEOUT_US);
    status = readb(req_addr + 528);
    g_assert_cmpint(status, ==, 0);

    data = g_malloc0(512);
    memread(req_addr + 16, data, 512);
    g_assert_cmpstr(data, ==, "TEST");
    g_free(data);

    guest_free(alloc, req_addr);

    if (features & (1u << VIRTIO_F_ANY_LAYOUT)) {
        /* Write and read with 2 descriptor layout */
        /* Write request */
        req.type = VIRTIO_BLK_T_OUT;
        req.ioprio = 1;
        req.sector = 1;
        req.data = g_malloc0(512);
        strcpy(req.data, "TEST");

        req_addr = virtio_blk_request(alloc, dev, &req, 512);

        g_free(req.data);

        free_head = qvirtqueue_add(vq, req_addr, 528, false, true);
        qvirtqueue_add(vq, req_addr + 528, 1, true, false);
        qvirtqueue_kick(dev, vq, free_head);

        qvirtio_wait_used_elem(dev, vq, free_head, NULL,
                               QVIRTIO_BLK_TIMEOUT_US);
        status = readb(req_addr + 528);
        g_assert_cmpint(status, ==, 0);

        guest_free(alloc, req_addr);

        /* Read request */
        req.type = VIRTIO_BLK_T_IN;
        req.ioprio = 1;
        req.sector = 1;
        req.data = g_malloc0(512);

        req_addr = virtio_blk_request(alloc, dev, &req, 512);

        g_free(req.data);

        free_head = qvirtqueue_add(vq, req_addr, 16, false, true);
        qvirtqueue_add(vq, req_addr + 16, 513, true, false);

        qvirtqueue_kick(dev, vq, free_head);

        qvirtio_wait_used_elem(dev, vq, free_head, NULL,
                               QVIRTIO_BLK_TIMEOUT_US);
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
    QOSState *qs;
    QVirtQueuePCI *vqpci;

    qs = pci_test_start();
    dev = virtio_blk_pci_init(qs->pcibus, PCI_SLOT);

    vqpci = (QVirtQueuePCI *)qvirtqueue_setup(&dev->vdev, qs->alloc, 0);

    test_basic(&dev->vdev, qs->alloc, &vqpci->vq);

    /* End test */
    qvirtqueue_cleanup(dev->vdev.bus, &vqpci->vq, qs->alloc);
    qvirtio_pci_device_disable(dev);
    qvirtio_pci_device_free(dev);
    qtest_shutdown(qs);
}

static void pci_indirect(void)
{
    QVirtioPCIDevice *dev;
    QVirtQueuePCI *vqpci;
    QOSState *qs;
    QVirtioBlkReq req;
    QVRingIndirectDesc *indirect;
    uint64_t req_addr;
    uint64_t capacity;
    uint32_t features;
    uint32_t free_head;
    uint8_t status;
    char *data;

    qs = pci_test_start();

    dev = virtio_blk_pci_init(qs->pcibus, PCI_SLOT);

    capacity = qvirtio_config_readq(&dev->vdev, 0);
    g_assert_cmpint(capacity, ==, TEST_IMAGE_SIZE / 512);

    features = qvirtio_get_features(&dev->vdev);
    g_assert_cmphex(features & (1u << VIRTIO_RING_F_INDIRECT_DESC), !=, 0);
    features = features & ~(QVIRTIO_F_BAD_FEATURE |
                            (1u << VIRTIO_RING_F_EVENT_IDX) |
                            (1u << VIRTIO_BLK_F_SCSI));
    qvirtio_set_features(&dev->vdev, features);

    vqpci = (QVirtQueuePCI *)qvirtqueue_setup(&dev->vdev, qs->alloc, 0);
    qvirtio_set_driver_ok(&dev->vdev);

    /* Write request */
    req.type = VIRTIO_BLK_T_OUT;
    req.ioprio = 1;
    req.sector = 0;
    req.data = g_malloc0(512);
    strcpy(req.data, "TEST");

    req_addr = virtio_blk_request(qs->alloc, &dev->vdev, &req, 512);

    g_free(req.data);

    indirect = qvring_indirect_desc_setup(&dev->vdev, qs->alloc, 2);
    qvring_indirect_desc_add(indirect, req_addr, 528, false);
    qvring_indirect_desc_add(indirect, req_addr + 528, 1, true);
    free_head = qvirtqueue_add_indirect(&vqpci->vq, indirect);
    qvirtqueue_kick(&dev->vdev, &vqpci->vq, free_head);

    qvirtio_wait_used_elem(&dev->vdev, &vqpci->vq, free_head, NULL,
                           QVIRTIO_BLK_TIMEOUT_US);
    status = readb(req_addr + 528);
    g_assert_cmpint(status, ==, 0);

    g_free(indirect);
    guest_free(qs->alloc, req_addr);

    /* Read request */
    req.type = VIRTIO_BLK_T_IN;
    req.ioprio = 1;
    req.sector = 0;
    req.data = g_malloc0(512);
    strcpy(req.data, "TEST");

    req_addr = virtio_blk_request(qs->alloc, &dev->vdev, &req, 512);

    g_free(req.data);

    indirect = qvring_indirect_desc_setup(&dev->vdev, qs->alloc, 2);
    qvring_indirect_desc_add(indirect, req_addr, 16, false);
    qvring_indirect_desc_add(indirect, req_addr + 16, 513, true);
    free_head = qvirtqueue_add_indirect(&vqpci->vq, indirect);
    qvirtqueue_kick(&dev->vdev, &vqpci->vq, free_head);

    qvirtio_wait_used_elem(&dev->vdev, &vqpci->vq, free_head, NULL,
                           QVIRTIO_BLK_TIMEOUT_US);
    status = readb(req_addr + 528);
    g_assert_cmpint(status, ==, 0);

    data = g_malloc0(512);
    memread(req_addr + 16, data, 512);
    g_assert_cmpstr(data, ==, "TEST");
    g_free(data);

    g_free(indirect);
    guest_free(qs->alloc, req_addr);

    /* End test */
    qvirtqueue_cleanup(dev->vdev.bus, &vqpci->vq, qs->alloc);
    qvirtio_pci_device_disable(dev);
    qvirtio_pci_device_free(dev);
    qtest_shutdown(qs);
}

static void pci_config(void)
{
    QVirtioPCIDevice *dev;
    QOSState *qs;
    int n_size = TEST_IMAGE_SIZE / 2;
    uint64_t capacity;

    qs = pci_test_start();

    dev = virtio_blk_pci_init(qs->pcibus, PCI_SLOT);

    capacity = qvirtio_config_readq(&dev->vdev, 0);
    g_assert_cmpint(capacity, ==, TEST_IMAGE_SIZE / 512);

    qvirtio_set_driver_ok(&dev->vdev);

    qmp_discard_response("{ 'execute': 'block_resize', "
                         " 'arguments': { 'device': 'drive0', "
                         " 'size': %d } }", n_size);
    qvirtio_wait_config_isr(&dev->vdev, QVIRTIO_BLK_TIMEOUT_US);

    capacity = qvirtio_config_readq(&dev->vdev, 0);
    g_assert_cmpint(capacity, ==, n_size / 512);

    qvirtio_pci_device_disable(dev);
    qvirtio_pci_device_free(dev);

    qtest_shutdown(qs);
}

static void pci_msix(void)
{
    QVirtioPCIDevice *dev;
    QOSState *qs;
    QVirtQueuePCI *vqpci;
    QVirtioBlkReq req;
    int n_size = TEST_IMAGE_SIZE / 2;
    uint64_t req_addr;
    uint64_t capacity;
    uint32_t features;
    uint32_t free_head;
    uint8_t status;
    char *data;

    qs = pci_test_start();

    dev = virtio_blk_pci_init(qs->pcibus, PCI_SLOT);
    qpci_msix_enable(dev->pdev);

    qvirtio_pci_set_msix_configuration_vector(dev, qs->alloc, 0);

    capacity = qvirtio_config_readq(&dev->vdev, 0);
    g_assert_cmpint(capacity, ==, TEST_IMAGE_SIZE / 512);

    features = qvirtio_get_features(&dev->vdev);
    features = features & ~(QVIRTIO_F_BAD_FEATURE |
                            (1u << VIRTIO_RING_F_INDIRECT_DESC) |
                            (1u << VIRTIO_RING_F_EVENT_IDX) |
                            (1u << VIRTIO_BLK_F_SCSI));
    qvirtio_set_features(&dev->vdev, features);

    vqpci = (QVirtQueuePCI *)qvirtqueue_setup(&dev->vdev, qs->alloc, 0);
    qvirtqueue_pci_msix_setup(dev, vqpci, qs->alloc, 1);

    qvirtio_set_driver_ok(&dev->vdev);

    qmp_discard_response("{ 'execute': 'block_resize', "
                         " 'arguments': { 'device': 'drive0', "
                         " 'size': %d } }", n_size);

    qvirtio_wait_config_isr(&dev->vdev, QVIRTIO_BLK_TIMEOUT_US);

    capacity = qvirtio_config_readq(&dev->vdev, 0);
    g_assert_cmpint(capacity, ==, n_size / 512);

    /* Write request */
    req.type = VIRTIO_BLK_T_OUT;
    req.ioprio = 1;
    req.sector = 0;
    req.data = g_malloc0(512);
    strcpy(req.data, "TEST");

    req_addr = virtio_blk_request(qs->alloc, &dev->vdev, &req, 512);

    g_free(req.data);

    free_head = qvirtqueue_add(&vqpci->vq, req_addr, 16, false, true);
    qvirtqueue_add(&vqpci->vq, req_addr + 16, 512, false, true);
    qvirtqueue_add(&vqpci->vq, req_addr + 528, 1, true, false);
    qvirtqueue_kick(&dev->vdev, &vqpci->vq, free_head);

    qvirtio_wait_used_elem(&dev->vdev, &vqpci->vq, free_head, NULL,
                           QVIRTIO_BLK_TIMEOUT_US);

    status = readb(req_addr + 528);
    g_assert_cmpint(status, ==, 0);

    guest_free(qs->alloc, req_addr);

    /* Read request */
    req.type = VIRTIO_BLK_T_IN;
    req.ioprio = 1;
    req.sector = 0;
    req.data = g_malloc0(512);

    req_addr = virtio_blk_request(qs->alloc, &dev->vdev, &req, 512);

    g_free(req.data);

    free_head = qvirtqueue_add(&vqpci->vq, req_addr, 16, false, true);
    qvirtqueue_add(&vqpci->vq, req_addr + 16, 512, true, true);
    qvirtqueue_add(&vqpci->vq, req_addr + 528, 1, true, false);

    qvirtqueue_kick(&dev->vdev, &vqpci->vq, free_head);


    qvirtio_wait_used_elem(&dev->vdev, &vqpci->vq, free_head, NULL,
                           QVIRTIO_BLK_TIMEOUT_US);

    status = readb(req_addr + 528);
    g_assert_cmpint(status, ==, 0);

    data = g_malloc0(512);
    memread(req_addr + 16, data, 512);
    g_assert_cmpstr(data, ==, "TEST");
    g_free(data);

    guest_free(qs->alloc, req_addr);

    /* End test */
    qvirtqueue_cleanup(dev->vdev.bus, &vqpci->vq, qs->alloc);
    qpci_msix_disable(dev->pdev);
    qvirtio_pci_device_disable(dev);
    qvirtio_pci_device_free(dev);
    qtest_shutdown(qs);
}

static void pci_idx(void)
{
    QVirtioPCIDevice *dev;
    QOSState *qs;
    QVirtQueuePCI *vqpci;
    QVirtioBlkReq req;
    uint64_t req_addr;
    uint64_t capacity;
    uint32_t features;
    uint32_t free_head;
    uint32_t write_head;
    uint32_t desc_idx;
    uint8_t status;
    char *data;

    qs = pci_test_start();

    dev = virtio_blk_pci_init(qs->pcibus, PCI_SLOT);
    qpci_msix_enable(dev->pdev);

    qvirtio_pci_set_msix_configuration_vector(dev, qs->alloc, 0);

    capacity = qvirtio_config_readq(&dev->vdev, 0);
    g_assert_cmpint(capacity, ==, TEST_IMAGE_SIZE / 512);

    features = qvirtio_get_features(&dev->vdev);
    features = features & ~(QVIRTIO_F_BAD_FEATURE |
                            (1u << VIRTIO_RING_F_INDIRECT_DESC) |
                            (1u << VIRTIO_F_NOTIFY_ON_EMPTY) |
                            (1u << VIRTIO_BLK_F_SCSI));
    qvirtio_set_features(&dev->vdev, features);

    vqpci = (QVirtQueuePCI *)qvirtqueue_setup(&dev->vdev, qs->alloc, 0);
    qvirtqueue_pci_msix_setup(dev, vqpci, qs->alloc, 1);

    qvirtio_set_driver_ok(&dev->vdev);

    /* Write request */
    req.type = VIRTIO_BLK_T_OUT;
    req.ioprio = 1;
    req.sector = 0;
    req.data = g_malloc0(512);
    strcpy(req.data, "TEST");

    req_addr = virtio_blk_request(qs->alloc, &dev->vdev, &req, 512);

    g_free(req.data);

    free_head = qvirtqueue_add(&vqpci->vq, req_addr, 16, false, true);
    qvirtqueue_add(&vqpci->vq, req_addr + 16, 512, false, true);
    qvirtqueue_add(&vqpci->vq, req_addr + 528, 1, true, false);
    qvirtqueue_kick(&dev->vdev, &vqpci->vq, free_head);

    qvirtio_wait_used_elem(&dev->vdev, &vqpci->vq, free_head, NULL,
                           QVIRTIO_BLK_TIMEOUT_US);

    /* Write request */
    req.type = VIRTIO_BLK_T_OUT;
    req.ioprio = 1;
    req.sector = 1;
    req.data = g_malloc0(512);
    strcpy(req.data, "TEST");

    req_addr = virtio_blk_request(qs->alloc, &dev->vdev, &req, 512);

    g_free(req.data);

    /* Notify after processing the third request */
    qvirtqueue_set_used_event(&vqpci->vq, 2);
    free_head = qvirtqueue_add(&vqpci->vq, req_addr, 16, false, true);
    qvirtqueue_add(&vqpci->vq, req_addr + 16, 512, false, true);
    qvirtqueue_add(&vqpci->vq, req_addr + 528, 1, true, false);
    qvirtqueue_kick(&dev->vdev, &vqpci->vq, free_head);
    write_head = free_head;

    /* No notification expected */
    status = qvirtio_wait_status_byte_no_isr(&dev->vdev,
                                             &vqpci->vq, req_addr + 528,
                                             QVIRTIO_BLK_TIMEOUT_US);
    g_assert_cmpint(status, ==, 0);

    guest_free(qs->alloc, req_addr);

    /* Read request */
    req.type = VIRTIO_BLK_T_IN;
    req.ioprio = 1;
    req.sector = 1;
    req.data = g_malloc0(512);

    req_addr = virtio_blk_request(qs->alloc, &dev->vdev, &req, 512);

    g_free(req.data);

    free_head = qvirtqueue_add(&vqpci->vq, req_addr, 16, false, true);
    qvirtqueue_add(&vqpci->vq, req_addr + 16, 512, true, true);
    qvirtqueue_add(&vqpci->vq, req_addr + 528, 1, true, false);

    qvirtqueue_kick(&dev->vdev, &vqpci->vq, free_head);

    /* We get just one notification for both requests */
    qvirtio_wait_used_elem(&dev->vdev, &vqpci->vq, write_head, NULL,
                           QVIRTIO_BLK_TIMEOUT_US);
    g_assert(qvirtqueue_get_buf(&vqpci->vq, &desc_idx, NULL));
    g_assert_cmpint(desc_idx, ==, free_head);

    status = readb(req_addr + 528);
    g_assert_cmpint(status, ==, 0);

    data = g_malloc0(512);
    memread(req_addr + 16, data, 512);
    g_assert_cmpstr(data, ==, "TEST");
    g_free(data);

    guest_free(qs->alloc, req_addr);

    /* End test */
    qvirtqueue_cleanup(dev->vdev.bus, &vqpci->vq, qs->alloc);
    qpci_msix_disable(dev->pdev);
    qvirtio_pci_device_disable(dev);
    qvirtio_pci_device_free(dev);
    qtest_shutdown(qs);
}

static void pci_hotplug(void)
{
    QVirtioPCIDevice *dev;
    QOSState *qs;
    const char *arch = qtest_get_arch();

    qs = pci_test_start();

    /* plug secondary disk */
    qtest_qmp_device_add("virtio-blk-pci", "drv1",
                         "{'addr': %s, 'drive': 'drive1'}",
                         stringify(PCI_SLOT_HP));

    dev = virtio_blk_pci_init(qs->pcibus, PCI_SLOT_HP);
    g_assert(dev);
    qvirtio_pci_device_disable(dev);
    qvirtio_pci_device_free(dev);

    /* unplug secondary disk */
    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        qpci_unplug_acpi_device_test("drv1", PCI_SLOT_HP);
    }
    qtest_shutdown(qs);
}

/*
 * Check that setting the vring addr on a non-existent virtqueue does
 * not crash.
 */
static void test_nonexistent_virtqueue(void)
{
    QPCIBar bar0;
    QOSState *qs;
    QPCIDevice *dev;

    qs = pci_test_start();
    dev = qpci_device_find(qs->pcibus, QPCI_DEVFN(4, 0));
    g_assert(dev != NULL);

    qpci_device_enable(dev);
    bar0 = qpci_iomap(dev, 0, NULL);

    qpci_io_writeb(dev, bar0, VIRTIO_PCI_QUEUE_SEL, 2);
    qpci_io_writel(dev, bar0, VIRTIO_PCI_QUEUE_PFN, 1);

    g_free(dev);
    qtest_shutdown(qs);
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
    g_assert_cmphex(dev->vdev.device_type, ==, VIRTIO_ID_BLOCK);

    qvirtio_reset(&dev->vdev);
    qvirtio_set_acknowledge(&dev->vdev);
    qvirtio_set_driver(&dev->vdev);

    alloc = generic_alloc_init(MMIO_RAM_ADDR, MMIO_RAM_SIZE, MMIO_PAGE_SIZE);
    vq = qvirtqueue_setup(&dev->vdev, alloc, 0);

    test_basic(&dev->vdev, alloc, vq);

    qmp_discard_response("{ 'execute': 'block_resize', "
                         " 'arguments': { 'device': 'drive0', "
                         " 'size': %d } }", n_size);

    qvirtio_wait_queue_isr(&dev->vdev, vq, QVIRTIO_BLK_TIMEOUT_US);

    capacity = qvirtio_config_readq(&dev->vdev, 0);
    g_assert_cmpint(capacity, ==, n_size / 512);

    /* End test */
    qvirtqueue_cleanup(dev->vdev.bus, vq, alloc);
    g_free(dev);
    generic_alloc_uninit(alloc);
    test_end();
}

int main(int argc, char **argv)
{
    const char *arch = qtest_get_arch();

    g_test_init(&argc, &argv, NULL);

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0 ||
        strcmp(arch, "ppc64") == 0) {
        qtest_add_func("/virtio/blk/pci/basic", pci_basic);
        qtest_add_func("/virtio/blk/pci/indirect", pci_indirect);
        qtest_add_func("/virtio/blk/pci/config", pci_config);
        qtest_add_func("/virtio/blk/pci/nxvirtq", test_nonexistent_virtqueue);
        if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
            qtest_add_func("/virtio/blk/pci/msix", pci_msix);
            qtest_add_func("/virtio/blk/pci/idx", pci_idx);
        }
        qtest_add_func("/virtio/blk/pci/hotplug", pci_hotplug);
    } else if (strcmp(arch, "arm") == 0) {
        qtest_add_func("/virtio/blk/mmio/basic", mmio_basic);
    }

    return g_test_run();
}
