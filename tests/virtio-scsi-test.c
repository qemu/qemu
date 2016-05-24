/*
 * QTest testcase for VirtIO SCSI
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 * Copyright (c) 2015 Red Hat Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "block/scsi.h"
#include "libqos/virtio.h"
#include "libqos/virtio-pci.h"
#include "libqos/pci-pc.h"
#include "libqos/malloc.h"
#include "libqos/malloc-pc.h"
#include "libqos/malloc-generic.h"

#define PCI_SLOT                0x02
#define PCI_FN                  0x00
#define QVIRTIO_SCSI_TIMEOUT_US (1 * 1000 * 1000)
#define CDB_SIZE 32

#define MAX_NUM_QUEUES 64

typedef struct {
    QVirtioDevice *dev;
    QGuestAllocator *alloc;
    QPCIBus *bus;
    int num_queues;
    QVirtQueue *vq[MAX_NUM_QUEUES + 2];
} QVirtIOSCSI;

typedef struct {
    uint8_t lun[8];
    int64_t tag;
    uint8_t task_attr;
    uint8_t prio;
    uint8_t crn;
    uint8_t cdb[CDB_SIZE];
} QEMU_PACKED QVirtIOSCSICmdReq;

typedef struct {
    uint32_t sense_len;
    uint32_t resid;
    uint16_t status_qualifier;
    uint8_t status;
    uint8_t response;
    uint8_t sense[96];
} QEMU_PACKED QVirtIOSCSICmdResp;

static void qvirtio_scsi_start(const char *extra_opts)
{
    char *cmdline;

    cmdline = g_strdup_printf(
                "-drive id=drv0,if=none,file=/dev/null,format=raw "
                "-device virtio-scsi-pci,id=vs0 "
                "-device scsi-hd,bus=vs0.0,drive=drv0 %s",
                extra_opts ? : "");
    qtest_start(cmdline);
    g_free(cmdline);
}

static void qvirtio_scsi_stop(void)
{
    qtest_end();
}

static void qvirtio_scsi_pci_free(QVirtIOSCSI *vs)
{
    int i;

    for (i = 0; i < vs->num_queues + 2; i++) {
        guest_free(vs->alloc, vs->vq[i]->desc);
    }
    pc_alloc_uninit(vs->alloc);
    qvirtio_pci_device_disable(container_of(vs->dev, QVirtioPCIDevice, vdev));
    g_free(vs->dev);
    qpci_free_pc(vs->bus);
}

static uint64_t qvirtio_scsi_alloc(QVirtIOSCSI *vs, size_t alloc_size,
                                   const void *data)
{
    uint64_t addr;

    addr = guest_alloc(vs->alloc, alloc_size);
    if (data) {
        memwrite(addr, data, alloc_size);
    }

    return addr;
}

static uint8_t virtio_scsi_do_command(QVirtIOSCSI *vs, const uint8_t *cdb,
                                      const uint8_t *data_in,
                                      size_t data_in_len,
                                      uint8_t *data_out, size_t data_out_len,
                                      QVirtIOSCSICmdResp *resp_out)
{
    QVirtQueue *vq;
    QVirtIOSCSICmdReq req = { { 0 } };
    QVirtIOSCSICmdResp resp = { .response = 0xff, .status = 0xff };
    uint64_t req_addr, resp_addr, data_in_addr = 0, data_out_addr = 0;
    uint8_t response;
    uint32_t free_head;

    vq = vs->vq[2];

    req.lun[0] = 1; /* Select LUN */
    req.lun[1] = 1; /* Select target 1 */
    memcpy(req.cdb, cdb, CDB_SIZE);

    /* XXX: Fix endian if any multi-byte field in req/resp is used */

    /* Add request header */
    req_addr = qvirtio_scsi_alloc(vs, sizeof(req), &req);
    free_head = qvirtqueue_add(vq, req_addr, sizeof(req), false, true);

    if (data_out_len) {
        data_out_addr = qvirtio_scsi_alloc(vs, data_out_len, data_out);
        qvirtqueue_add(vq, data_out_addr, data_out_len, false, true);
    }

    /* Add response header */
    resp_addr = qvirtio_scsi_alloc(vs, sizeof(resp), &resp);
    qvirtqueue_add(vq, resp_addr, sizeof(resp), true, !!data_in_len);

    if (data_in_len) {
        data_in_addr = qvirtio_scsi_alloc(vs, data_in_len, data_in);
        qvirtqueue_add(vq, data_in_addr, data_in_len, true, false);
    }

    qvirtqueue_kick(&qvirtio_pci, vs->dev, vq, free_head);
    qvirtio_wait_queue_isr(&qvirtio_pci, vs->dev, vq, QVIRTIO_SCSI_TIMEOUT_US);

    response = readb(resp_addr + offsetof(QVirtIOSCSICmdResp, response));

    if (resp_out) {
        memread(resp_addr, resp_out, sizeof(*resp_out));
    }

    guest_free(vs->alloc, req_addr);
    guest_free(vs->alloc, resp_addr);
    guest_free(vs->alloc, data_in_addr);
    guest_free(vs->alloc, data_out_addr);
    return response;
}

static QVirtIOSCSI *qvirtio_scsi_pci_init(int slot)
{
    const uint8_t test_unit_ready_cdb[CDB_SIZE] = {};
    QVirtIOSCSI *vs;
    QVirtioPCIDevice *dev;
    QVirtIOSCSICmdResp resp;
    void *addr;
    int i;

    vs = g_new0(QVirtIOSCSI, 1);
    vs->alloc = pc_alloc_init();
    vs->bus = qpci_init_pc();

    dev = qvirtio_pci_device_find(vs->bus, QVIRTIO_SCSI_DEVICE_ID);
    vs->dev = (QVirtioDevice *)dev;
    g_assert(dev != NULL);
    g_assert_cmphex(vs->dev->device_type, ==, QVIRTIO_SCSI_DEVICE_ID);

    qvirtio_pci_device_enable(dev);
    qvirtio_reset(&qvirtio_pci, vs->dev);
    qvirtio_set_acknowledge(&qvirtio_pci, vs->dev);
    qvirtio_set_driver(&qvirtio_pci, vs->dev);

    addr = dev->addr + QVIRTIO_PCI_DEVICE_SPECIFIC_NO_MSIX;
    vs->num_queues = qvirtio_config_readl(&qvirtio_pci, vs->dev,
                                          (uint64_t)(uintptr_t)addr);

    g_assert_cmpint(vs->num_queues, <, MAX_NUM_QUEUES);

    for (i = 0; i < vs->num_queues + 2; i++) {
        vs->vq[i] = qvirtqueue_setup(&qvirtio_pci, vs->dev, vs->alloc, i);
    }

    /* Clear the POWER ON OCCURRED unit attention */
    g_assert_cmpint(virtio_scsi_do_command(vs, test_unit_ready_cdb,
                                           NULL, 0, NULL, 0, &resp),
                    ==, 0);
    g_assert_cmpint(resp.status, ==, CHECK_CONDITION);
    g_assert_cmpint(resp.sense[0], ==, 0x70); /* Fixed format sense buffer */
    g_assert_cmpint(resp.sense[2], ==, UNIT_ATTENTION);
    g_assert_cmpint(resp.sense[12], ==, 0x29); /* POWER ON */
    g_assert_cmpint(resp.sense[13], ==, 0x00);

    return vs;
}

/* Tests only initialization so far. TODO: Replace with functional tests */
static void pci_nop(void)
{
    qvirtio_scsi_start(NULL);
    qvirtio_scsi_stop();
}

static void hotplug(void)
{
    QDict *response;

    qvirtio_scsi_start("-drive id=drv1,if=none,file=/dev/null,format=raw");
    response = qmp("{\"execute\": \"device_add\","
                   " \"arguments\": {"
                   "   \"driver\": \"scsi-hd\","
                   "   \"id\": \"scsi-hd\","
                   "   \"drive\": \"drv1\""
                   "}}");

    g_assert(response);
    g_assert(!qdict_haskey(response, "error"));
    QDECREF(response);

    response = qmp("{\"execute\": \"device_del\","
                   " \"arguments\": {"
                   "   \"id\": \"scsi-hd\""
                   "}}");

    g_assert(response);
    g_assert(!qdict_haskey(response, "error"));
    g_assert(qdict_haskey(response, "event"));
    g_assert(!strcmp(qdict_get_str(response, "event"), "DEVICE_DELETED"));
    QDECREF(response);
    qvirtio_scsi_stop();
}

/* Test WRITE SAME with the lba not aligned */
static void test_unaligned_write_same(void)
{
    QVirtIOSCSI *vs;
    uint8_t buf1[512] = { 0 };
    uint8_t buf2[512] = { 1 };
    const uint8_t write_same_cdb_1[CDB_SIZE] = { 0x41, 0x00, 0x00, 0x00, 0x00,
                                               0x01, 0x00, 0x00, 0x02, 0x00 };
    const uint8_t write_same_cdb_2[CDB_SIZE] = { 0x41, 0x00, 0x00, 0x00, 0x00,
                                               0x01, 0x00, 0x33, 0x00, 0x00 };

    qvirtio_scsi_start("-drive file=blkdebug::null-co://,if=none,id=dr1"
                       ",format=raw,file.align=4k "
                       "-device scsi-disk,drive=dr1,lun=0,scsi-id=1");
    vs = qvirtio_scsi_pci_init(PCI_SLOT);

    g_assert_cmphex(0, ==,
        virtio_scsi_do_command(vs, write_same_cdb_1, NULL, 0, buf1, 512, NULL));

    g_assert_cmphex(0, ==,
        virtio_scsi_do_command(vs, write_same_cdb_2, NULL, 0, buf2, 512, NULL));

    qvirtio_scsi_pci_free(vs);
    qvirtio_scsi_stop();
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/virtio/scsi/pci/nop", pci_nop);
    qtest_add_func("/virtio/scsi/pci/hotplug", hotplug);
    qtest_add_func("/virtio/scsi/pci/scsi-disk/unaligned-write-same",
                   test_unaligned_write_same);

    ret = g_test_run();

    return ret;
}
