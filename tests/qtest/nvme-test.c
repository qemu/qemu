/*
 * QTest testcase for NVMe
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <glib/gstdio.h>
#include "qemu/bswap.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "libqtest.h"
#include "libqtest-single.h"
#include "libqos/qgraph.h"
#include "libqos/pci.h"
#include "block/nvme.h"

typedef struct QNvme QNvme;

struct QNvme {
    QOSGraphObject obj;
    QPCIDevice dev;
};

static void *nvme_get_driver(void *obj, const char *interface)
{
    QNvme *nvme = obj;

    if (!g_strcmp0(interface, "pci-device")) {
        return &nvme->dev;
    }

    fprintf(stderr, "%s not present in nvme\n", interface);
    g_assert_not_reached();
}

static void *nvme_create(void *pci_bus, QGuestAllocator *alloc, void *addr)
{
    QNvme *nvme = g_new0(QNvme, 1);
    QPCIBus *bus = pci_bus;

    qpci_device_init(&nvme->dev, bus, addr);
    nvme->obj.get_driver = nvme_get_driver;

    return &nvme->obj;
}

/* This used to cause a NULL pointer dereference.  */
static void nvmetest_oob_cmb_test(void *obj, void *data, QGuestAllocator *alloc)
{
    const int cmb_bar_size = 2 * MiB;
    QNvme *nvme = obj;
    QPCIDevice *pdev = &nvme->dev;
    QPCIBar bar;

    qpci_device_enable(pdev);
    bar = qpci_iomap(pdev, 2, NULL);

    qpci_io_writel(pdev, bar, 0, 0xccbbaa99);
    g_assert_cmpint(qpci_io_readb(pdev, bar, 0), ==, 0x99);
    g_assert_cmpint(qpci_io_readw(pdev, bar, 0), ==, 0xaa99);

    /* Test partially out-of-bounds accesses.  */
    qpci_io_writel(pdev, bar, cmb_bar_size - 1, 0x44332211);
    g_assert_cmpint(qpci_io_readb(pdev, bar, cmb_bar_size - 1), ==, 0x11);
    g_assert_cmpint(qpci_io_readw(pdev, bar, cmb_bar_size - 1), !=, 0x2211);
    g_assert_cmpint(qpci_io_readl(pdev, bar, cmb_bar_size - 1), !=, 0x44332211);
}

static void nvmetest_reg_read_test(void *obj, void *data, QGuestAllocator *alloc)
{
    QNvme *nvme = obj;
    QPCIDevice *pdev = &nvme->dev;
    QPCIBar bar;
    uint32_t cap_lo, cap_hi;
    uint64_t cap;

    qpci_device_enable(pdev);
    bar = qpci_iomap(pdev, 0, NULL);

    cap_lo = qpci_io_readl(pdev, bar, 0x0);
    g_assert_cmpint(NVME_CAP_MQES(cap_lo), ==, 0x7ff);

    cap_hi = qpci_io_readl(pdev, bar, 0x4);
    g_assert_cmpint(NVME_CAP_MPSMAX((uint64_t)cap_hi << 32), ==, 0x4);

    cap = qpci_io_readq(pdev, bar, 0x0);
    g_assert_cmpint(NVME_CAP_MQES(cap), ==, 0x7ff);
    g_assert_cmpint(NVME_CAP_MPSMAX(cap), ==, 0x4);

    qpci_iounmap(pdev, bar);
}

static void nvmetest_pmr_reg_test(void *obj, void *data, QGuestAllocator *alloc)
{
    QNvme *nvme = obj;
    QPCIDevice *pdev = &nvme->dev;
    QPCIBar pmr_bar, nvme_bar;
    uint32_t pmrcap, pmrsts;

    qpci_device_enable(pdev);
    pmr_bar = qpci_iomap(pdev, 4, NULL);

    /* Without Enabling PMRCTL check bar enablemet */
    qpci_io_writel(pdev, pmr_bar, 0, 0xccbbaa99);
    g_assert_cmpint(qpci_io_readb(pdev, pmr_bar, 0), !=, 0x99);
    g_assert_cmpint(qpci_io_readw(pdev, pmr_bar, 0), !=, 0xaa99);

    /* Map NVMe Bar Register to Enable the Mem Region */
    nvme_bar = qpci_iomap(pdev, 0, NULL);

    pmrcap = qpci_io_readl(pdev, nvme_bar, 0xe00);
    g_assert_cmpint(NVME_PMRCAP_RDS(pmrcap), ==, 0x1);
    g_assert_cmpint(NVME_PMRCAP_WDS(pmrcap), ==, 0x1);
    g_assert_cmpint(NVME_PMRCAP_BIR(pmrcap), ==, 0x4);
    g_assert_cmpint(NVME_PMRCAP_PMRWBM(pmrcap), ==, 0x2);
    g_assert_cmpint(NVME_PMRCAP_CMSS(pmrcap), ==, 0x1);

    /* Enable PMRCTRL */
    qpci_io_writel(pdev, nvme_bar, 0xe04, 0x1);

    qpci_io_writel(pdev, pmr_bar, 0, 0x44332211);
    g_assert_cmpint(qpci_io_readb(pdev, pmr_bar, 0), ==, 0x11);
    g_assert_cmpint(qpci_io_readw(pdev, pmr_bar, 0), ==, 0x2211);
    g_assert_cmpint(qpci_io_readl(pdev, pmr_bar, 0), ==, 0x44332211);

    pmrsts = qpci_io_readl(pdev, nvme_bar, 0xe08);
    g_assert_cmpint(NVME_PMRSTS_NRDY(pmrsts), ==, 0x0);

    /* Disable PMRCTRL */
    qpci_io_writel(pdev, nvme_bar, 0xe04, 0x0);

    qpci_io_writel(pdev, pmr_bar, 0, 0x88776655);
    g_assert_cmpint(qpci_io_readb(pdev, pmr_bar, 0), !=, 0x55);
    g_assert_cmpint(qpci_io_readw(pdev, pmr_bar, 0), !=, 0x6655);
    g_assert_cmpint(qpci_io_readl(pdev, pmr_bar, 0), !=, 0x88776655);

    pmrsts = qpci_io_readl(pdev, nvme_bar, 0xe08);
    g_assert_cmpint(NVME_PMRSTS_NRDY(pmrsts), ==, 0x1);

    qpci_iounmap(pdev, nvme_bar);
    qpci_iounmap(pdev, pmr_bar);
}

typedef struct nvme_ctrl nvme_ctrl;

typedef struct nvme_queue {
    nvme_ctrl *ctrl;
    uint64_t doorbell;
    uint32_t size;
} nvme_queue;

typedef struct nvme_cq {
    nvme_queue common;
    uint64_t phys_cqe; /* NvmeCqe* */
    uint16_t head;
    uint8_t phase;
} nvme_cq;

typedef struct nvme_sq {
    nvme_queue common;
    uint64_t phys_sqe; /* NvmeCmd* */
    nvme_cq *cq;
    uint16_t head;
    uint16_t tail;
} nvme_sq;

struct nvme_ctrl {
    QGuestAllocator *alloc;
    QPCIDevice *pdev;
    QPCIBar bar;

    uint32_t db_stride;

    nvme_sq admin_sq;
    nvme_cq admin_cq;
};

#define PHYS_ADDR_OF_FIELD(T, base_phys_addr, field) \
    ((uint64_t)&((T *)(base_phys_addr))->field)

#define PHYS_ADDR_OF(T, base_phys_addr, accessor) \
    ((uint64_t)&((T *)(base_phys_addr))accessor)

static void nvme_init_queue_common(nvme_ctrl *ctrl, nvme_queue *q,
                                   uint16_t db_idx, uint32_t size)
{
    q->ctrl = ctrl;
    q->doorbell = (sizeof(NvmeBar) + db_idx * ctrl->db_stride);
    g_test_message(" q %p db_idx %u doorbell 0x%" PRIx64, q, db_idx, q->doorbell);
    q->size = size;
}

static void nvme_init_sq(nvme_ctrl *ctrl, nvme_sq *sq, uint16_t db_idx,
                         uint32_t size, nvme_cq *cq)
{
    nvme_init_queue_common(ctrl, &sq->common, db_idx, size);

    sq->phys_sqe = guest_alloc(ctrl->alloc, sizeof(NvmeCmd) * size);
    g_assert(sq->phys_sqe);

    g_test_message("sq %p db_idx %u sqe 0x%" PRIx64, sq, db_idx, sq->phys_sqe);
    sq->cq = cq;
    sq->head = 0;
    sq->tail = 0;
}

static void nvme_init_cq(nvme_ctrl *ctrl, nvme_cq *cq, uint16_t db_idx,
                         uint32_t size)
{
    nvme_init_queue_common(ctrl, &cq->common, db_idx, size);

    cq->phys_cqe = guest_alloc(ctrl->alloc, sizeof(NvmeCqe) * size);
    g_assert(cq->phys_cqe);

    g_test_message("cq %p db_idx %u cqe 0x%" PRIx64, cq, db_idx, cq->phys_cqe);
    cq->head = 0;
    cq->phase = 1;
}

static int nvme_cqe_pending(nvme_cq *cq)
{
    uint16_t status;

    qtest_memread(cq->common.ctrl->pdev->bus->qts,
                  PHYS_ADDR_OF(NvmeCqe, cq->phys_cqe, [cq->head].status),
                  &status, sizeof(status));
    return (le16_to_cpu(status) & 1) == cq->phase;
}

static int nvme_is_cqe_success(NvmeCqe *cqe)
{
    return (le16_to_cpu(cqe->status) >> 1) == NVME_SUCCESS;
}

static NvmeCqe nvme_handle_cqe(nvme_sq *sq)
{
    nvme_cq *cq = sq->cq;
    uint64_t phys_cqe = PHYS_ADDR_OF(
                            NvmeCqe, cq->phys_cqe, [cq->head]); /* NvmeCqe* */
    NvmeCqe cqe;
    uint16_t cq_next_head;

    g_assert(nvme_cqe_pending(cq));

    qtest_memread(sq->common.ctrl->pdev->bus->qts, phys_cqe, &cqe, sizeof(cqe));

    cq_next_head = (cq->head + 1) % cq->common.size;
    g_test_message("cq %p head %u -> %u", cq, cq->head, cq_next_head);
    if (cq_next_head < cq->head) {
        cq->phase ^= 1;
    }
    cq->head = cq_next_head;

    if (le16_to_cpu(cqe.sq_head) != sq->head) {
        sq->head = le16_to_cpu(cqe.sq_head);
        g_test_message("sq %p head = %u", sq, sq->head);
    }

    qpci_io_writel(cq->common.ctrl->pdev, cq->common.ctrl->bar,
                   cq->common.doorbell, cq->head);

    return cqe;
}

static NvmeCqe nvme_wait(nvme_sq *sq)
{
    int i;
    bool ready = false;

    for (i = 0; i < 10; i++) {
        if (nvme_cqe_pending(sq->cq)) {
            ready = true;
            break;
        }

        g_usleep(1000);
    }

    g_assert(ready);

    return nvme_handle_cqe(sq);
}

static uint64_t nvme_get_next_sqe(nvme_sq *sq, uint8_t opcode,
                                  uint16_t cid, uint64_t prp1)
{
    uint64_t phys_sqe = PHYS_ADDR_OF(NvmeCmd, sq->phys_sqe, [sq->tail]);

    if (((sq->tail + 1) % sq->common.size) == sq->head) {
        /* no space in SQ */
        g_test_message("%s head %d tail %d", __func__, sq->head, sq->tail);
        g_assert_not_reached();
        return 0;
    }

    qtest_memset(sq->common.ctrl->pdev->bus->qts,
                 phys_sqe, 0, sizeof(NvmeCmd));

    #define GUEST_MEM_WRITE(phys_addr, val) \
        qtest_memwrite(sq->common.ctrl->pdev->bus->qts, phys_addr, &val, sizeof(val))

    GUEST_MEM_WRITE(PHYS_ADDR_OF_FIELD(NvmeCmd, phys_sqe, opcode), opcode);
    cid = cpu_to_le16(cid);
    GUEST_MEM_WRITE(PHYS_ADDR_OF_FIELD(NvmeCmd, phys_sqe, cid), cid);
    prp1 = cpu_to_le64(prp1);
    GUEST_MEM_WRITE(PHYS_ADDR_OF_FIELD(NvmeCmd, phys_sqe, dptr.prp1), prp1);

    #undef GUEST_MEM_WRITE

    g_test_message("sq %p next_sqe %u sqe 0x%" PRIx64, sq, sq->tail, phys_sqe);
    return phys_sqe;
}

static void nvme_commit_sqe(nvme_sq *sq)
{
    g_test_message("sq %p commit sqe tail %u", sq, sq->tail);
    sq->tail = (sq->tail + 1) % sq->common.size;
    qpci_io_writel(sq->common.ctrl->pdev, sq->common.ctrl->bar,
                   sq->common.doorbell, sq->tail);
}

static uint64_t nvme_admin_identify_ctrl(nvme_ctrl *ctrl,
                                         uint16_t cid, bool no_wait)
{
    uint64_t phys_cmd_identify; /* NvmeCmd* */
    uint64_t phys_identify; /* NvmeIdCtrl* */
    NvmeCqe cqe;

    g_test_message("sending req cid %u no_wait %d", cid, no_wait);

    phys_identify = guest_alloc(ctrl->alloc, sizeof(NvmeIdCtrl));
    g_assert(phys_identify);

    phys_cmd_identify = nvme_get_next_sqe(&ctrl->admin_sq,
                                          NVME_ADM_CMD_IDENTIFY, cid,
                                          phys_identify);
    g_assert(phys_cmd_identify);

    #define GUEST_MEM_WRITE(fn, phys_addr, val) \
        fn(ctrl->pdev->bus->qts, phys_addr, (val))

    GUEST_MEM_WRITE(qtest_writel,
                    PHYS_ADDR_OF_FIELD(NvmeCmd, phys_cmd_identify, nsid), 0);
    GUEST_MEM_WRITE(qtest_writeb,
                    PHYS_ADDR_OF_FIELD(NvmeIdentify, phys_cmd_identify, cns),
                    NVME_ID_CNS_CTRL);

    #undef GUEST_MEM_WRITE

    nvme_commit_sqe(&ctrl->admin_sq);

    if (no_wait) {
        return phys_identify;
    }

    cqe = nvme_wait(&ctrl->admin_sq);
    g_assert(nvme_is_cqe_success(&cqe));
    g_assert(le16_to_cpu(cqe.cid) == cid);

    return phys_identify;
}

static void nvme_wait_ready(nvme_ctrl *ctrl, int val)
{
    int i;

    for (i = 0; i < 10; i++) {
        uint32_t csts = qpci_io_readl(ctrl->pdev, ctrl->bar, NVME_REG_CSTS);
        g_test_message("%s: csts %x", __func__, csts);

        if (NVME_CSTS_RDY(csts) == val) {
            return;
        }

        g_usleep(1000);
    }

    g_assert_not_reached();
}

static void test_migrate_setup_nvme_ctrl(nvme_ctrl *ctrl)
{
    uint64_t cap;

    /* disable controller */
    qpci_io_writel(ctrl->pdev, ctrl->bar, NVME_REG_CC, 0);
    nvme_wait_ready(ctrl, 0);

    cap = qpci_io_readq(ctrl->pdev, ctrl->bar, NVME_REG_CAP);
    ctrl->db_stride = 4 << NVME_CAP_DSTRD(cap);

    nvme_init_cq(ctrl, &ctrl->admin_cq, 1, 2 /* CQEs num */);
    nvme_init_sq(ctrl, &ctrl->admin_sq, 0, 4 /* SQEs num */, &ctrl->admin_cq);

    qpci_io_writel(ctrl->pdev, ctrl->bar, NVME_REG_AQA,
        ((ctrl->admin_cq.common.size - 1) << AQA_ACQS_SHIFT) |
        ((ctrl->admin_sq.common.size - 1) << AQA_ASQS_SHIFT)
    );

    qpci_io_writeq(ctrl->pdev, ctrl->bar,
                   NVME_REG_ASQ, (uint64_t)ctrl->admin_sq.phys_sqe);
    qpci_io_writeq(ctrl->pdev, ctrl->bar,
                   NVME_REG_ACQ, (uint64_t)ctrl->admin_cq.phys_cqe);

    /* enable controller */
    {
        uint32_t cc = 0;
        NVME_SET_CC_EN(cc, 1);
        qpci_io_writel(ctrl->pdev, ctrl->bar, NVME_REG_CC, cc);
    }

    nvme_wait_ready(ctrl, 1);
}

typedef struct test_migrate_req {
    uint16_t cid;
    bool handle_cqe;
    uint64_t phys_identify; /* NvmeIdCtrl* */
} test_migrate_req;

static void test_migrate_send_nvme_reqs(nvme_ctrl *ctrl, test_migrate_req *reqs,
                                        int num)
{
    int i;

    for (i = 0; i < num; i++) {
        reqs[i].phys_identify = nvme_admin_identify_ctrl(ctrl, reqs[i].cid,
                                                         !reqs[i].handle_cqe);
        g_assert(reqs[i].phys_identify);

        if (reqs[i].handle_cqe) {
            guest_free(ctrl->alloc, reqs[i].phys_identify);
        }
    }
}

static void test_migrate_check_nvme(nvme_ctrl *ctrl,
                                    test_migrate_req *reqs, int num)
{
    int i;

    for (i = 0; i < num; i++) {
        NvmeCqe cqe;

        if (reqs[i].handle_cqe) {
            continue;
        }

        cqe = nvme_wait(&ctrl->admin_sq);
        g_assert(nvme_is_cqe_success(&cqe));

        g_assert_cmpint(le16_to_cpu(cqe.cid), ==, reqs[i].cid);

        #define GUEST_MEM_READB(phys_addr) \
                    qtest_readb(ctrl->pdev->bus->qts, (phys_addr))

        g_assert_cmpint(GUEST_MEM_READB(
            PHYS_ADDR_OF_FIELD(NvmeIdCtrl, reqs[i].phys_identify, ieee[0])),
            ==, 0x0);
        g_assert_cmpint(GUEST_MEM_READB(
            PHYS_ADDR_OF_FIELD(NvmeIdCtrl, reqs[i].phys_identify, ieee[1])),
            ==, 0x54);
        g_assert_cmpint(GUEST_MEM_READB(
            PHYS_ADDR_OF_FIELD(NvmeIdCtrl, reqs[i].phys_identify, ieee[2])),
            ==, 0x52);

        #undef GUEST_MEM_READB

        guest_free(ctrl->alloc, reqs[i].phys_identify);
    }
}

static void test_migrate(void *obj, void *data, QGuestAllocator *alloc)
{
    g_autofree gchar *tmpfs = NULL;
    GError *err = NULL;
    g_autofree gchar *mig_path = NULL;
    g_autofree gchar *uri = NULL;
    GString *dest_cmdline;
    QTestState *to;
    QDict *rsp;
    QNvme *nvme = obj;
    QPCIDevice *pdev = &nvme->dev;
    g_autofree nvme_ctrl *ctrl = NULL;
    test_migrate_req test_reqs[] = {
        { 123, true },
        { 456, false },
        { 300, false },
        { 333, false }
    };

    /* create temporary dir and prepare unix socket path for migration */
    tmpfs = g_dir_make_tmp("nvme-test-XXXXXX", &err);
    if (!tmpfs) {
        g_test_message("Can't create temporary directory in %s: %s",
                       g_get_tmp_dir(), err->message);
        g_error_free(err);
    }
    g_assert(tmpfs);

    mig_path = g_strdup_printf("%s/socket.mig", tmpfs);
    uri = g_strdup_printf("unix:%s", mig_path);

    /* enable NVMe PCI device */
    qpci_device_enable(pdev);

    ctrl = g_malloc0(sizeof(*ctrl));
    ctrl->alloc = alloc;
    ctrl->pdev = pdev;
    ctrl->bar = qpci_iomap(ctrl->pdev, 0, NULL);
    g_assert(pdev->bus->qts == global_qtest);

    test_migrate_setup_nvme_ctrl(ctrl);
    test_migrate_send_nvme_reqs(ctrl, test_reqs, ARRAY_SIZE(test_reqs));

    dest_cmdline = g_string_new(qos_get_current_command_line());
    g_string_append_printf(dest_cmdline, " -incoming %s", uri);

    /* Create destination VM */
    to = qtest_init(dest_cmdline->str);

    /* Get access to PCI device from destination VM */
    nvme = qos_allocate_objects(to, &ctrl->alloc);
    pdev = &nvme->dev;
    qpci_migrate_fixup(pdev, ctrl->pdev);
    ctrl->pdev = pdev;
    g_assert(pdev->bus->qts == to);

    /* Migrate VM */
    rsp = qmp("{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    g_assert(qdict_haskey(rsp, "return"));
    qobject_unref(rsp);

    /* Wait when source VM is stopped */
    qmp_eventwait("STOP");

    /* Copy guest physical memory allocator state */
    migrate_allocator(alloc, ctrl->alloc);

    /* Wait for destination VM to become alive */
    qtest_qmp_eventwait(to, "RESUME");

    test_migrate_check_nvme(ctrl, test_reqs, ARRAY_SIZE(test_reqs));

    qpci_iounmap(ctrl->pdev, ctrl->bar);

    qtest_quit(to);
    g_unlink(mig_path);
    g_rmdir(tmpfs);
    g_string_free(dest_cmdline, true);
}

static void nvme_register_nodes(void)
{
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "addr=04.0,drive=drv0,serial=foo",
        .before_cmd_line = "-drive id=drv0,if=none,file=null-co://,"
                           "file.read-zeroes=on,format=raw "
                           "-object memory-backend-ram,id=pmr0,"
                           "share=on,size=16",
    };

    add_qpci_address(&opts, &(QPCIAddress) { .devfn = QPCI_DEVFN(4, 0) });

    qos_node_create_driver("nvme", nvme_create);
    qos_node_consumes("nvme", "pci-bus", &opts);
    qos_node_produces("nvme", "pci-device");

    qos_add_test("oob-cmb-access", "nvme", nvmetest_oob_cmb_test, &(QOSGraphTestOptions) {
        .edge.extra_device_opts = "cmb_size_mb=2"
    });

    qos_add_test("pmr-test-access", "nvme", nvmetest_pmr_reg_test,
                 &(QOSGraphTestOptions) {
        .edge.extra_device_opts = "pmrdev=pmr0"
    });

    qos_add_test("reg-read", "nvme", nvmetest_reg_read_test, NULL);

    qos_add_test("migrate", "nvme", test_migrate, NULL);
}

libqos_init(nvme_register_nodes);
