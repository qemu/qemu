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
#include "libqtest-single.h"
#include "qemu/bswap.h"
#include "qemu/module.h"
#include "standard-headers/linux/virtio_blk.h"
#include "standard-headers/linux/virtio_pci.h"
#include "libqos/qgraph.h"
#include "libqos/virtio-blk.h"

/* TODO actually test the results and get rid of this */
#define qmp_discard_response(...) qobject_unref(qmp(__VA_ARGS__))

#define TEST_IMAGE_SIZE         (64 * 1024 * 1024)
#define QVIRTIO_BLK_TIMEOUT_US  (30 * 1000 * 1000)
#define PCI_SLOT_HP             0x06

typedef struct QVirtioBlkReq {
    uint32_t type;
    uint32_t ioprio;
    uint64_t sector;
    char *data;
    uint8_t status;
} QVirtioBlkReq;


#ifdef HOST_WORDS_BIGENDIAN
const bool host_is_big_endian = true;
#else
const bool host_is_big_endian; /* false */
#endif

static void drive_destroy(void *path)
{
    unlink(path);
    g_free(path);
    qos_invalidate_command_line();
}

static char *drive_create(void)
{
    int fd, ret;
    char *t_path = g_strdup("/tmp/qtest.XXXXXX");

    /* Create a temporary raw image */
    fd = mkstemp(t_path);
    g_assert_cmpint(fd, >=, 0);
    ret = ftruncate(fd, TEST_IMAGE_SIZE);
    g_assert_cmpint(ret, ==, 0);
    close(fd);

    g_test_queue_destroy(drive_destroy, t_path);
    return t_path;
}

static inline void virtio_blk_fix_request(QVirtioDevice *d, QVirtioBlkReq *req)
{
    if (qvirtio_is_big_endian(d) != host_is_big_endian) {
        req->type = bswap32(req->type);
        req->ioprio = bswap32(req->ioprio);
        req->sector = bswap64(req->sector);
    }
}


static inline void virtio_blk_fix_dwz_hdr(QVirtioDevice *d,
    struct virtio_blk_discard_write_zeroes *dwz_hdr)
{
    if (qvirtio_is_big_endian(d) != host_is_big_endian) {
        dwz_hdr->sector = bswap64(dwz_hdr->sector);
        dwz_hdr->num_sectors = bswap32(dwz_hdr->num_sectors);
        dwz_hdr->flags = bswap32(dwz_hdr->flags);
    }
}

static uint64_t virtio_blk_request(QGuestAllocator *alloc, QVirtioDevice *d,
                                   QVirtioBlkReq *req, uint64_t data_size)
{
    uint64_t addr;
    uint8_t status = 0xFF;

    switch (req->type) {
    case VIRTIO_BLK_T_IN:
    case VIRTIO_BLK_T_OUT:
        g_assert_cmpuint(data_size % 512, ==, 0);
        break;
    case VIRTIO_BLK_T_DISCARD:
    case VIRTIO_BLK_T_WRITE_ZEROES:
        g_assert_cmpuint(data_size %
                         sizeof(struct virtio_blk_discard_write_zeroes), ==, 0);
        break;
    default:
        g_assert_cmpuint(data_size, ==, 0);
    }

    addr = guest_alloc(alloc, sizeof(*req) + data_size);

    virtio_blk_fix_request(d, req);

    memwrite(addr, req, 16);
    memwrite(addr + 16, req->data, data_size);
    memwrite(addr + 16 + data_size, &status, sizeof(status));

    return addr;
}

/* Returns the request virtqueue so the caller can perform further tests */
static QVirtQueue *test_basic(QVirtioDevice *dev, QGuestAllocator *alloc)
{
    QVirtioBlkReq req;
    uint64_t req_addr;
    uint64_t capacity;
    uint64_t features;
    uint32_t free_head;
    uint8_t status;
    char *data;
    QTestState *qts = global_qtest;
    QVirtQueue *vq;

    features = qvirtio_get_features(dev);
    features = features & ~(QVIRTIO_F_BAD_FEATURE |
                    (1u << VIRTIO_RING_F_INDIRECT_DESC) |
                    (1u << VIRTIO_RING_F_EVENT_IDX) |
                    (1u << VIRTIO_BLK_F_SCSI));
    qvirtio_set_features(dev, features);

    capacity = qvirtio_config_readq(dev, 0);
    g_assert_cmpint(capacity, ==, TEST_IMAGE_SIZE / 512);

    vq = qvirtqueue_setup(dev, alloc, 0);

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

    free_head = qvirtqueue_add(qts, vq, req_addr, 16, false, true);
    qvirtqueue_add(qts, vq, req_addr + 16, 512, false, true);
    qvirtqueue_add(qts, vq, req_addr + 528, 1, true, false);

    qvirtqueue_kick(qts, dev, vq, free_head);

    qvirtio_wait_used_elem(qts, dev, vq, free_head, NULL,
                           QVIRTIO_BLK_TIMEOUT_US);
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

    free_head = qvirtqueue_add(qts, vq, req_addr, 16, false, true);
    qvirtqueue_add(qts, vq, req_addr + 16, 512, true, true);
    qvirtqueue_add(qts, vq, req_addr + 528, 1, true, false);

    qvirtqueue_kick(qts, dev, vq, free_head);

    qvirtio_wait_used_elem(qts, dev, vq, free_head, NULL,
                           QVIRTIO_BLK_TIMEOUT_US);
    status = readb(req_addr + 528);
    g_assert_cmpint(status, ==, 0);

    data = g_malloc0(512);
    memread(req_addr + 16, data, 512);
    g_assert_cmpstr(data, ==, "TEST");
    g_free(data);

    guest_free(alloc, req_addr);

    if (features & (1u << VIRTIO_BLK_F_WRITE_ZEROES)) {
        struct virtio_blk_discard_write_zeroes dwz_hdr;
        void *expected;

        /*
         * WRITE_ZEROES request on the same sector of previous test where
         * we wrote "TEST".
         */
        req.type = VIRTIO_BLK_T_WRITE_ZEROES;
        req.data = (char *) &dwz_hdr;
        dwz_hdr.sector = 0;
        dwz_hdr.num_sectors = 1;
        dwz_hdr.flags = 0;

        virtio_blk_fix_dwz_hdr(dev, &dwz_hdr);

        req_addr = virtio_blk_request(alloc, dev, &req, sizeof(dwz_hdr));

        free_head = qvirtqueue_add(qts, vq, req_addr, 16, false, true);
        qvirtqueue_add(qts, vq, req_addr + 16, sizeof(dwz_hdr), false, true);
        qvirtqueue_add(qts, vq, req_addr + 16 + sizeof(dwz_hdr), 1, true,
                       false);

        qvirtqueue_kick(qts, dev, vq, free_head);

        qvirtio_wait_used_elem(qts, dev, vq, free_head, NULL,
                               QVIRTIO_BLK_TIMEOUT_US);
        status = readb(req_addr + 16 + sizeof(dwz_hdr));
        g_assert_cmpint(status, ==, 0);

        guest_free(alloc, req_addr);

        /* Read request to check if the sector contains all zeroes */
        req.type = VIRTIO_BLK_T_IN;
        req.ioprio = 1;
        req.sector = 0;
        req.data = g_malloc0(512);

        req_addr = virtio_blk_request(alloc, dev, &req, 512);

        g_free(req.data);

        free_head = qvirtqueue_add(qts, vq, req_addr, 16, false, true);
        qvirtqueue_add(qts, vq, req_addr + 16, 512, true, true);
        qvirtqueue_add(qts, vq, req_addr + 528, 1, true, false);

        qvirtqueue_kick(qts, dev, vq, free_head);

        qvirtio_wait_used_elem(qts, dev, vq, free_head, NULL,
                               QVIRTIO_BLK_TIMEOUT_US);
        status = readb(req_addr + 528);
        g_assert_cmpint(status, ==, 0);

        data = g_malloc(512);
        expected = g_malloc0(512);
        memread(req_addr + 16, data, 512);
        g_assert_cmpmem(data, 512, expected, 512);
        g_free(expected);
        g_free(data);

        guest_free(alloc, req_addr);
    }

    if (features & (1u << VIRTIO_BLK_F_DISCARD)) {
        struct virtio_blk_discard_write_zeroes dwz_hdr;

        req.type = VIRTIO_BLK_T_DISCARD;
        req.data = (char *) &dwz_hdr;
        dwz_hdr.sector = 0;
        dwz_hdr.num_sectors = 1;
        dwz_hdr.flags = 0;

        virtio_blk_fix_dwz_hdr(dev, &dwz_hdr);

        req_addr = virtio_blk_request(alloc, dev, &req, sizeof(dwz_hdr));

        free_head = qvirtqueue_add(qts, vq, req_addr, 16, false, true);
        qvirtqueue_add(qts, vq, req_addr + 16, sizeof(dwz_hdr), false, true);
        qvirtqueue_add(qts, vq, req_addr + 16 + sizeof(dwz_hdr), 1, true, false);

        qvirtqueue_kick(qts, dev, vq, free_head);

        qvirtio_wait_used_elem(qts, dev, vq, free_head, NULL,
                               QVIRTIO_BLK_TIMEOUT_US);
        status = readb(req_addr + 16 + sizeof(dwz_hdr));
        g_assert_cmpint(status, ==, 0);

        guest_free(alloc, req_addr);
    }

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

        free_head = qvirtqueue_add(qts, vq, req_addr, 528, false, true);
        qvirtqueue_add(qts, vq, req_addr + 528, 1, true, false);
        qvirtqueue_kick(qts, dev, vq, free_head);

        qvirtio_wait_used_elem(qts, dev, vq, free_head, NULL,
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

        free_head = qvirtqueue_add(qts, vq, req_addr, 16, false, true);
        qvirtqueue_add(qts, vq, req_addr + 16, 513, true, false);

        qvirtqueue_kick(qts, dev, vq, free_head);

        qvirtio_wait_used_elem(qts, dev, vq, free_head, NULL,
                               QVIRTIO_BLK_TIMEOUT_US);
        status = readb(req_addr + 528);
        g_assert_cmpint(status, ==, 0);

        data = g_malloc0(512);
        memread(req_addr + 16, data, 512);
        g_assert_cmpstr(data, ==, "TEST");
        g_free(data);

        guest_free(alloc, req_addr);
    }

    return vq;
}

static void basic(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtioBlk *blk_if = obj;
    QVirtQueue *vq;

    vq = test_basic(blk_if->vdev, t_alloc);
    qvirtqueue_cleanup(blk_if->vdev->bus, vq, t_alloc);

}

static void indirect(void *obj, void *u_data, QGuestAllocator *t_alloc)
{
    QVirtQueue *vq;
    QVirtioBlk *blk_if = obj;
    QVirtioDevice *dev = blk_if->vdev;
    QVirtioBlkReq req;
    QVRingIndirectDesc *indirect;
    uint64_t req_addr;
    uint64_t capacity;
    uint64_t features;
    uint32_t free_head;
    uint8_t status;
    char *data;
    QTestState *qts = global_qtest;

    features = qvirtio_get_features(dev);
    g_assert_cmphex(features & (1u << VIRTIO_RING_F_INDIRECT_DESC), !=, 0);
    features = features & ~(QVIRTIO_F_BAD_FEATURE |
                            (1u << VIRTIO_RING_F_EVENT_IDX) |
                            (1u << VIRTIO_BLK_F_SCSI));
    qvirtio_set_features(dev, features);

    capacity = qvirtio_config_readq(dev, 0);
    g_assert_cmpint(capacity, ==, TEST_IMAGE_SIZE / 512);

    vq = qvirtqueue_setup(dev, t_alloc, 0);
    qvirtio_set_driver_ok(dev);

    /* Write request */
    req.type = VIRTIO_BLK_T_OUT;
    req.ioprio = 1;
    req.sector = 0;
    req.data = g_malloc0(512);
    strcpy(req.data, "TEST");

    req_addr = virtio_blk_request(t_alloc, dev, &req, 512);

    g_free(req.data);

    indirect = qvring_indirect_desc_setup(qts, dev, t_alloc, 2);
    qvring_indirect_desc_add(dev, qts, indirect, req_addr, 528, false);
    qvring_indirect_desc_add(dev, qts, indirect, req_addr + 528, 1, true);
    free_head = qvirtqueue_add_indirect(qts, vq, indirect);
    qvirtqueue_kick(qts, dev, vq, free_head);

    qvirtio_wait_used_elem(qts, dev, vq, free_head, NULL,
                           QVIRTIO_BLK_TIMEOUT_US);
    status = readb(req_addr + 528);
    g_assert_cmpint(status, ==, 0);

    g_free(indirect);
    guest_free(t_alloc, req_addr);

    /* Read request */
    req.type = VIRTIO_BLK_T_IN;
    req.ioprio = 1;
    req.sector = 0;
    req.data = g_malloc0(512);
    strcpy(req.data, "TEST");

    req_addr = virtio_blk_request(t_alloc, dev, &req, 512);

    g_free(req.data);

    indirect = qvring_indirect_desc_setup(qts, dev, t_alloc, 2);
    qvring_indirect_desc_add(dev, qts, indirect, req_addr, 16, false);
    qvring_indirect_desc_add(dev, qts, indirect, req_addr + 16, 513, true);
    free_head = qvirtqueue_add_indirect(qts, vq, indirect);
    qvirtqueue_kick(qts, dev, vq, free_head);

    qvirtio_wait_used_elem(qts, dev, vq, free_head, NULL,
                           QVIRTIO_BLK_TIMEOUT_US);
    status = readb(req_addr + 528);
    g_assert_cmpint(status, ==, 0);

    data = g_malloc0(512);
    memread(req_addr + 16, data, 512);
    g_assert_cmpstr(data, ==, "TEST");
    g_free(data);

    g_free(indirect);
    guest_free(t_alloc, req_addr);
    qvirtqueue_cleanup(dev->bus, vq, t_alloc);
}

static void config(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtioBlk *blk_if = obj;
    QVirtioDevice *dev = blk_if->vdev;
    int n_size = TEST_IMAGE_SIZE / 2;
    uint64_t features;
    uint64_t capacity;

    features = qvirtio_get_features(dev);
    features = features & ~(QVIRTIO_F_BAD_FEATURE |
                            (1u << VIRTIO_RING_F_INDIRECT_DESC) |
                            (1u << VIRTIO_RING_F_EVENT_IDX) |
                            (1u << VIRTIO_BLK_F_SCSI));
    qvirtio_set_features(dev, features);

    capacity = qvirtio_config_readq(dev, 0);
    g_assert_cmpint(capacity, ==, TEST_IMAGE_SIZE / 512);

    qvirtio_set_driver_ok(dev);

    qmp_discard_response("{ 'execute': 'block_resize', "
                         " 'arguments': { 'device': 'drive0', "
                         " 'size': %d } }", n_size);
    qvirtio_wait_config_isr(dev, QVIRTIO_BLK_TIMEOUT_US);

    capacity = qvirtio_config_readq(dev, 0);
    g_assert_cmpint(capacity, ==, n_size / 512);
}

static void msix(void *obj, void *u_data, QGuestAllocator *t_alloc)
{
    QVirtQueue *vq;
    QVirtioBlkPCI *blk = obj;
    QVirtioPCIDevice *pdev = &blk->pci_vdev;
    QVirtioDevice *dev = &pdev->vdev;
    QVirtioBlkReq req;
    int n_size = TEST_IMAGE_SIZE / 2;
    uint64_t req_addr;
    uint64_t capacity;
    uint64_t features;
    uint32_t free_head;
    uint8_t status;
    char *data;
    QOSGraphObject *blk_object = obj;
    QPCIDevice *pci_dev = blk_object->get_driver(blk_object, "pci-device");
    QTestState *qts = global_qtest;

    if (qpci_check_buggy_msi(pci_dev)) {
        return;
    }

    qpci_msix_enable(pdev->pdev);
    qvirtio_pci_set_msix_configuration_vector(pdev, t_alloc, 0);

    features = qvirtio_get_features(dev);
    features = features & ~(QVIRTIO_F_BAD_FEATURE |
                            (1u << VIRTIO_RING_F_INDIRECT_DESC) |
                            (1u << VIRTIO_RING_F_EVENT_IDX) |
                            (1u << VIRTIO_BLK_F_SCSI));
    qvirtio_set_features(dev, features);

    capacity = qvirtio_config_readq(dev, 0);
    g_assert_cmpint(capacity, ==, TEST_IMAGE_SIZE / 512);

    vq = qvirtqueue_setup(dev, t_alloc, 0);
    qvirtqueue_pci_msix_setup(pdev, (QVirtQueuePCI *)vq, t_alloc, 1);

    qvirtio_set_driver_ok(dev);

    qmp_discard_response("{ 'execute': 'block_resize', "
                         " 'arguments': { 'device': 'drive0', "
                         " 'size': %d } }", n_size);

    qvirtio_wait_config_isr(dev, QVIRTIO_BLK_TIMEOUT_US);

    capacity = qvirtio_config_readq(dev, 0);
    g_assert_cmpint(capacity, ==, n_size / 512);

    /* Write request */
    req.type = VIRTIO_BLK_T_OUT;
    req.ioprio = 1;
    req.sector = 0;
    req.data = g_malloc0(512);
    strcpy(req.data, "TEST");

    req_addr = virtio_blk_request(t_alloc, dev, &req, 512);

    g_free(req.data);

    free_head = qvirtqueue_add(qts, vq, req_addr, 16, false, true);
    qvirtqueue_add(qts, vq, req_addr + 16, 512, false, true);
    qvirtqueue_add(qts, vq, req_addr + 528, 1, true, false);
    qvirtqueue_kick(qts, dev, vq, free_head);

    qvirtio_wait_used_elem(qts, dev, vq, free_head, NULL,
                           QVIRTIO_BLK_TIMEOUT_US);

    status = readb(req_addr + 528);
    g_assert_cmpint(status, ==, 0);

    guest_free(t_alloc, req_addr);

    /* Read request */
    req.type = VIRTIO_BLK_T_IN;
    req.ioprio = 1;
    req.sector = 0;
    req.data = g_malloc0(512);

    req_addr = virtio_blk_request(t_alloc, dev, &req, 512);

    g_free(req.data);

    free_head = qvirtqueue_add(qts, vq, req_addr, 16, false, true);
    qvirtqueue_add(qts, vq, req_addr + 16, 512, true, true);
    qvirtqueue_add(qts, vq, req_addr + 528, 1, true, false);

    qvirtqueue_kick(qts, dev, vq, free_head);


    qvirtio_wait_used_elem(qts, dev, vq, free_head, NULL,
                           QVIRTIO_BLK_TIMEOUT_US);

    status = readb(req_addr + 528);
    g_assert_cmpint(status, ==, 0);

    data = g_malloc0(512);
    memread(req_addr + 16, data, 512);
    g_assert_cmpstr(data, ==, "TEST");
    g_free(data);

    guest_free(t_alloc, req_addr);

    /* End test */
    qpci_msix_disable(pdev->pdev);
    qvirtqueue_cleanup(dev->bus, vq, t_alloc);
}

static void idx(void *obj, void *u_data, QGuestAllocator *t_alloc)
{
    QVirtQueue *vq;
    QVirtioBlkPCI *blk = obj;
    QVirtioPCIDevice *pdev = &blk->pci_vdev;
    QVirtioDevice *dev = &pdev->vdev;
    QVirtioBlkReq req;
    uint64_t req_addr;
    uint64_t capacity;
    uint64_t features;
    uint32_t free_head;
    uint32_t write_head;
    uint32_t desc_idx;
    uint8_t status;
    char *data;
    QOSGraphObject *blk_object = obj;
    QPCIDevice *pci_dev = blk_object->get_driver(blk_object, "pci-device");
    QTestState *qts = global_qtest;

    if (qpci_check_buggy_msi(pci_dev)) {
        return;
    }

    qpci_msix_enable(pdev->pdev);
    qvirtio_pci_set_msix_configuration_vector(pdev, t_alloc, 0);

    features = qvirtio_get_features(dev);
    features = features & ~(QVIRTIO_F_BAD_FEATURE |
                            (1u << VIRTIO_RING_F_INDIRECT_DESC) |
                            (1u << VIRTIO_F_NOTIFY_ON_EMPTY) |
                            (1u << VIRTIO_BLK_F_SCSI));
    qvirtio_set_features(dev, features);

    capacity = qvirtio_config_readq(dev, 0);
    g_assert_cmpint(capacity, ==, TEST_IMAGE_SIZE / 512);

    vq = qvirtqueue_setup(dev, t_alloc, 0);
    qvirtqueue_pci_msix_setup(pdev, (QVirtQueuePCI *)vq, t_alloc, 1);

    qvirtio_set_driver_ok(dev);

    /* Write request */
    req.type = VIRTIO_BLK_T_OUT;
    req.ioprio = 1;
    req.sector = 0;
    req.data = g_malloc0(512);
    strcpy(req.data, "TEST");

    req_addr = virtio_blk_request(t_alloc, dev, &req, 512);

    g_free(req.data);

    free_head = qvirtqueue_add(qts, vq, req_addr, 16, false, true);
    qvirtqueue_add(qts, vq, req_addr + 16, 512, false, true);
    qvirtqueue_add(qts, vq, req_addr + 528, 1, true, false);
    qvirtqueue_kick(qts, dev, vq, free_head);

    qvirtio_wait_used_elem(qts, dev, vq, free_head, NULL,
                           QVIRTIO_BLK_TIMEOUT_US);

    /* Write request */
    req.type = VIRTIO_BLK_T_OUT;
    req.ioprio = 1;
    req.sector = 1;
    req.data = g_malloc0(512);
    strcpy(req.data, "TEST");

    req_addr = virtio_blk_request(t_alloc, dev, &req, 512);

    g_free(req.data);

    /* Notify after processing the third request */
    qvirtqueue_set_used_event(qts, vq, 2);
    free_head = qvirtqueue_add(qts, vq, req_addr, 16, false, true);
    qvirtqueue_add(qts, vq, req_addr + 16, 512, false, true);
    qvirtqueue_add(qts, vq, req_addr + 528, 1, true, false);
    qvirtqueue_kick(qts, dev, vq, free_head);
    write_head = free_head;

    /* No notification expected */
    status = qvirtio_wait_status_byte_no_isr(qts, dev,
                                             vq, req_addr + 528,
                                             QVIRTIO_BLK_TIMEOUT_US);
    g_assert_cmpint(status, ==, 0);

    guest_free(t_alloc, req_addr);

    /* Read request */
    req.type = VIRTIO_BLK_T_IN;
    req.ioprio = 1;
    req.sector = 1;
    req.data = g_malloc0(512);

    req_addr = virtio_blk_request(t_alloc, dev, &req, 512);

    g_free(req.data);

    free_head = qvirtqueue_add(qts, vq, req_addr, 16, false, true);
    qvirtqueue_add(qts, vq, req_addr + 16, 512, true, true);
    qvirtqueue_add(qts, vq, req_addr + 528, 1, true, false);

    qvirtqueue_kick(qts, dev, vq, free_head);

    /* We get just one notification for both requests */
    qvirtio_wait_used_elem(qts, dev, vq, write_head, NULL,
                           QVIRTIO_BLK_TIMEOUT_US);
    g_assert(qvirtqueue_get_buf(qts, vq, &desc_idx, NULL));
    g_assert_cmpint(desc_idx, ==, free_head);

    status = readb(req_addr + 528);
    g_assert_cmpint(status, ==, 0);

    data = g_malloc0(512);
    memread(req_addr + 16, data, 512);
    g_assert_cmpstr(data, ==, "TEST");
    g_free(data);

    guest_free(t_alloc, req_addr);

    /* End test */
    qpci_msix_disable(pdev->pdev);

    qvirtqueue_cleanup(dev->bus, vq, t_alloc);
}

static void pci_hotplug(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtioPCIDevice *dev1 = obj;
    QVirtioPCIDevice *dev;
    QTestState *qts = dev1->pdev->bus->qts;

    /* plug secondary disk */
    qtest_qmp_device_add(qts, "virtio-blk-pci", "drv1",
                         "{'addr': %s, 'drive': 'drive1'}",
                         stringify(PCI_SLOT_HP) ".0");

    dev = virtio_pci_new(dev1->pdev->bus,
                         &(QPCIAddress) { .devfn = QPCI_DEVFN(PCI_SLOT_HP, 0) });
    g_assert_nonnull(dev);
    g_assert_cmpint(dev->vdev.device_type, ==, VIRTIO_ID_BLOCK);
    qvirtio_pci_device_disable(dev);
    qos_object_destroy((QOSGraphObject *)dev);

    /* unplug secondary disk */
    qpci_unplug_acpi_device_test(qts, "drv1", PCI_SLOT_HP);
}

/*
 * Check that setting the vring addr on a non-existent virtqueue does
 * not crash.
 */
static void test_nonexistent_virtqueue(void *obj, void *data,
                                       QGuestAllocator *t_alloc)
{
    QVirtioBlkPCI *blk = obj;
    QVirtioPCIDevice *pdev = &blk->pci_vdev;
    QPCIBar bar0;
    QPCIDevice *dev;

    dev = qpci_device_find(pdev->pdev->bus, QPCI_DEVFN(4, 0));
    g_assert(dev != NULL);
    qpci_device_enable(dev);

    bar0 = qpci_iomap(dev, 0, NULL);

    qpci_io_writeb(dev, bar0, VIRTIO_PCI_QUEUE_SEL, 2);
    qpci_io_writel(dev, bar0, VIRTIO_PCI_QUEUE_PFN, 1);


    g_free(dev);
}

static void resize(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtioBlk *blk_if = obj;
    QVirtioDevice *dev = blk_if->vdev;
    int n_size = TEST_IMAGE_SIZE / 2;
    uint64_t capacity;
    QVirtQueue *vq;
    QTestState *qts = global_qtest;

    vq = test_basic(dev, t_alloc);

    qmp_discard_response("{ 'execute': 'block_resize', "
                         " 'arguments': { 'device': 'drive0', "
                         " 'size': %d } }", n_size);

    qvirtio_wait_queue_isr(qts, dev, vq, QVIRTIO_BLK_TIMEOUT_US);

    capacity = qvirtio_config_readq(dev, 0);
    g_assert_cmpint(capacity, ==, n_size / 512);

    qvirtqueue_cleanup(dev->bus, vq, t_alloc);

}

static void *virtio_blk_test_setup(GString *cmd_line, void *arg)
{
    char *tmp_path = drive_create();

    g_string_append_printf(cmd_line,
                           " -drive if=none,id=drive0,file=%s,"
                           "format=raw,auto-read-only=off "
                           "-drive if=none,id=drive1,file=null-co://,"
                           "file.read-zeroes=on,format=raw ",
                           tmp_path);

    return arg;
}

static void register_virtio_blk_test(void)
{
    QOSGraphTestOptions opts = {
        .before = virtio_blk_test_setup,
    };

    qos_add_test("indirect", "virtio-blk", indirect, &opts);
    qos_add_test("config", "virtio-blk", config, &opts);
    qos_add_test("basic", "virtio-blk", basic, &opts);
    qos_add_test("resize", "virtio-blk", resize, &opts);

    /* tests just for virtio-blk-pci */
    qos_add_test("msix", "virtio-blk-pci", msix, &opts);
    qos_add_test("idx", "virtio-blk-pci", idx, &opts);
    qos_add_test("nxvirtq", "virtio-blk-pci",
                      test_nonexistent_virtqueue, &opts);
    qos_add_test("hotplug", "virtio-blk-pci", pci_hotplug, &opts);
}

libqos_init(register_virtio_blk_test);
