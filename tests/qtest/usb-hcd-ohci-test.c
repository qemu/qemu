/*
 * QTest testcase for USB OHCI controller
 *
 * Copyright (c) 2014 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "libqtest-single.h"
#include "qemu/module.h"
#include "libqos/usb.h"
#include "libqos/qgraph.h"
#include "libqos/pci.h"

typedef struct QOHCI_PCI QOHCI_PCI;

struct QOHCI_PCI {
    QOSGraphObject obj;
    QPCIDevice dev;
};

#define HC_CONTROL          0x04
#define HC_COMMAND_STATUS   0x08
#define HC_INTERRUPT_STATUS 0x0c
#define HC_HCCA             0x18
#define HC_CONTROL_HEAD_ED  0x20
#define HC_DONE_HEAD        0x30
#define HC_FM_NUMBER        0x3c
#define HC_RH_PORT_STATUS   0x54
#define HC_CONTROL_PLE      (1 << 2)
#define HC_CONTROL_IE       (1 << 3)
#define HC_CONTROL_CLE      (1 << 4)
#define HC_CONTROL_STATE    (3 << 6)
#define HC_OPERATIONAL      (2 << 6)
#define HC_CLF              (1 << 1)
#define HC_UE               (1 << 4)
#define HC_WD               (1 << 1)
#define PORT_CCS            (1 << 0)
#define PORT_PES            (1 << 1)
#define PORT_RESET          (1 << 4)
#define TD_CC_NA            (15U << 28)
#define TD_CC(flags)        ((flags) >> 28)
#define TD_DP_SETUP         (0 << 19)
#define TD_DP_OUT           (1 << 19)
#define TD_DP_IN            (2 << 19)
#define ED_DIR_OUT          (1 << 11)
#define ED_DIR_IN           (2 << 11)
#define ED_ISO              (1 << 15)
#define ED_HEAD_HALT        (1 << 0)
#define CC_NOERROR          0
#define CC_STALL            4
#define CC_NOTACCESSED      15
#define FRAME_NS            1000000

typedef struct OHCITransferTest {
    QTestState *qts;
    QPCIDevice *pci;
    QPCIBar bar;
    uint64_t hcca;
    uint64_t desc;
    uint64_t data;
} OHCITransferTest;

enum OHCITransferScenario {
    OHCI_TEST_CONTROL,
    OHCI_TEST_ISO,
    OHCI_TEST_ISO_TO_CONTROL,
    OHCI_TEST_GENERAL_TO_ISO,
};

#define CTRL_ED(t)          ((t)->desc)
#define CTRL_SETUP_TD(t)    ((t)->desc + 0x20)
#define CTRL_DATA_TD(t)     ((t)->desc + 0x40)
#define CTRL_STATUS_TD(t)   ((t)->desc + 0x60)
#define CTRL_TAIL_TD(t)     ((t)->desc + 0x80)
#define PERIODIC_ED(t)      ((t)->desc + 0x100)
#define PERIODIC_TD(t)      ((t)->desc + 0x120)
#define PERIODIC_TAIL(t)    ((t)->desc + 0x140)
#define SETUP_BUF(t)        ((t)->data)
#define CTRL_BUF(t)         ((t)->data + 0x100)
#define ISO_BUF(t)          ((t)->data + 0x200)

static uint32_t ohci_readl(OHCITransferTest *t, unsigned int reg)
{
    return qpci_io_readl(t->pci, t->bar, reg);
}

static void ohci_writel(OHCITransferTest *t, unsigned int reg, uint32_t val)
{
    qpci_io_writel(t->pci, t->bar, reg, val);
}

/* OHCI DMA structures are little-endian, regardless of the guest CPU. */
static uint32_t ohci_mem_readl(OHCITransferTest *t, uint64_t addr)
{
    uint32_t value;

    qtest_memread(t->qts, addr, &value, sizeof(value));
    return le32_to_cpu(value);
}

static uint16_t ohci_mem_readw(OHCITransferTest *t, uint64_t addr)
{
    uint16_t value;

    qtest_memread(t->qts, addr, &value, sizeof(value));
    return le16_to_cpu(value);
}

static void ohci_mem_writel(OHCITransferTest *t, uint64_t addr, uint32_t value)
{
    value = cpu_to_le32(value);
    qtest_memwrite(t->qts, addr, &value, sizeof(value));
}

static void ohci_mem_writew(OHCITransferTest *t, uint64_t addr, uint16_t value)
{
    value = cpu_to_le16(value);
    qtest_memwrite(t->qts, addr, &value, sizeof(value));
}

static void ohci_write_td(OHCITransferTest *t, uint64_t addr, uint32_t flags,
                          uint32_t cbp, uint32_t next, uint32_t be)
{
    ohci_mem_writel(t, addr, flags | TD_CC_NA);
    ohci_mem_writel(t, addr + 4, cbp);
    ohci_mem_writel(t, addr + 8, next);
    ohci_mem_writel(t, addr + 12, be);
}

static void ohci_write_ed(OHCITransferTest *t, uint64_t addr, uint32_t flags,
                          uint32_t head, uint32_t tail)
{
    ohci_mem_writel(t, addr, flags);
    ohci_mem_writel(t, addr + 4, tail);
    ohci_mem_writel(t, addr + 8, head);
    ohci_mem_writel(t, addr + 12, 0);
}

static void ohci_step(OHCITransferTest *t)
{
    qtest_clock_step(t->qts, FRAME_NS);
    g_assert_cmphex(ohci_readl(t, HC_INTERRUPT_STATUS) & HC_UE, ==, 0);
}

static void ohci_drain_done(OHCITransferTest *t, const uint32_t *expected,
                            unsigned int expected_count)
{
    bool seen[3] = { false };
    unsigned int seen_count = 0;
    unsigned int i;

    g_assert_cmpuint(expected_count, <=, G_N_ELEMENTS(seen));
    for (i = 0; i < 10; i++) {
        uint32_t done = ohci_mem_readl(t, t->hcca + 0x84);

        if (expected_count == 1 && (done & ~0xfU)) {
            g_assert_cmphex(done & ~0xfU, ==, expected[0]);
        }

        while (done & ~0xfU) {
            uint32_t td = done & ~0xfU;
            unsigned int j;

            for (j = 0; j < expected_count; j++) {
                if (expected[j] == td) {
                    break;
                }
            }
            g_assert_cmpuint(j, <, expected_count);
            g_assert_false(seen[j]);
            seen[j] = true;
            seen_count++;
            done = ohci_mem_readl(t, td + 8);
        }

        if (seen_count == expected_count &&
            ohci_mem_readl(t, t->hcca + 0x84) == 0 &&
            !(ohci_readl(t, HC_INTERRUPT_STATUS) & HC_WD) &&
            ohci_readl(t, HC_DONE_HEAD) == 0) {
            break;
        }

        /* Consume this HCCA batch before allowing another writeback. */
        ohci_mem_writel(t, t->hcca + 0x84, 0);
        ohci_writel(t, HC_INTERRUPT_STATUS, HC_WD);
        ohci_step(t);
    }

    g_assert_cmpuint(i, <, 10);
    g_assert_cmpuint(seen_count, ==, expected_count);
    g_assert_cmphex(ohci_mem_readl(t, t->hcca + 0x84), ==, 0);
    g_assert_cmphex(ohci_readl(t, HC_DONE_HEAD), ==, 0);
    g_assert_cmphex(ohci_readl(t, HC_INTERRUPT_STATUS) & HC_WD, ==, 0);
}

static void ohci_transfer_init(OHCITransferTest *t, QOHCI_PCI *obj,
                               QGuestAllocator *alloc)
{
    unsigned int i;

    t->qts = global_qtest;
    t->pci = &obj->dev;
    qpci_device_enable(t->pci);
    t->bar = qpci_iomap(t->pci, 0, NULL);
    t->hcca = guest_alloc(alloc, 0x1000);
    t->desc = guest_alloc(alloc, 0x1000);
    t->data = guest_alloc(alloc, 0x1000);
    qtest_memset(t->qts, t->hcca, 0, 0x1000);
    qtest_memset(t->qts, t->desc, 0, 0x1000);
    qtest_memset(t->qts, t->data, 0, 0x1000);

    g_assert_cmphex(ohci_readl(t, HC_RH_PORT_STATUS) & PORT_CCS, ==,
                    PORT_CCS);
    ohci_writel(t, HC_RH_PORT_STATUS, PORT_RESET);
    g_assert_cmphex(ohci_readl(t, HC_RH_PORT_STATUS) & PORT_PES, ==,
                    PORT_PES);
    ohci_writel(t, HC_HCCA, t->hcca);
    ohci_writel(t, HC_CONTROL_HEAD_ED, CTRL_ED(t));
    ohci_writel(t, HC_CONTROL,
                HC_OPERATIONAL | HC_CONTROL_CLE | HC_CONTROL_PLE |
                HC_CONTROL_IE);
    g_assert_cmphex(ohci_readl(t, HC_CONTROL) &
                    (HC_CONTROL_STATE | HC_CONTROL_CLE | HC_CONTROL_PLE |
                     HC_CONTROL_IE), ==,
                    HC_OPERATIONAL | HC_CONTROL_CLE | HC_CONTROL_PLE |
                    HC_CONTROL_IE);

    for (i = 0; i < 32; i++) {
        ohci_mem_writel(t, t->hcca + 4 * i, 0);
    }
}

static void ohci_control(OHCITransferTest *t, const uint8_t setup[8],
                         unsigned int data_len)
{
    uint64_t status = data_len ? CTRL_STATUS_TD(t) : CTRL_DATA_TD(t);
    uint32_t done[3] = { CTRL_SETUP_TD(t), CTRL_DATA_TD(t), status };
    unsigned int done_count = data_len ? 3 : 2;
    unsigned int i;

    qtest_memwrite(t->qts, SETUP_BUF(t), setup, 8);
    qtest_memset(t->qts, CTRL_BUF(t), 0xa5, 64);
    ohci_write_td(t, CTRL_SETUP_TD(t), TD_DP_SETUP,
                  SETUP_BUF(t), data_len ? CTRL_DATA_TD(t) : status,
                  SETUP_BUF(t) + 7);
    if (data_len) {
        ohci_write_td(t, CTRL_DATA_TD(t), TD_DP_IN | (3 << 24),
                      CTRL_BUF(t), status, CTRL_BUF(t) + data_len - 1);
    }
    ohci_write_td(t, status, (data_len ? TD_DP_OUT : TD_DP_IN) | (3 << 24),
                  0, CTRL_TAIL_TD(t), 0);
    ohci_write_ed(t, CTRL_ED(t), 64 << 16, CTRL_SETUP_TD(t),
                  CTRL_TAIL_TD(t));
    ohci_writel(t, HC_COMMAND_STATUS, HC_CLF);

    for (i = 0; i < 10; i++) {
        ohci_step(t);
        if ((ohci_mem_readl(t, CTRL_ED(t) + 8) & ~15U) ==
            CTRL_TAIL_TD(t)) {
            break;
        }
    }
    g_assert_cmpuint(i, <, 10);
    g_assert_cmphex(ohci_mem_readl(t, CTRL_ED(t) + 8) & ED_HEAD_HALT,
                    ==, 0);
    g_assert_cmpuint(TD_CC(ohci_mem_readl(t, CTRL_SETUP_TD(t))), ==,
                     CC_NOERROR);
    if (data_len) {
        g_assert_cmpuint(TD_CC(ohci_mem_readl(t, CTRL_DATA_TD(t))), ==,
                         CC_NOERROR);
    }
    g_assert_cmpuint(TD_CC(ohci_mem_readl(t, status)), ==, CC_NOERROR);
    /* Do not reuse a TD while its NextTD still links the done queue. */
    ohci_drain_done(t, done, done_count);
}

static void ohci_get_device_descriptor(OHCITransferTest *t)
{
    const uint8_t setup[8] = { 0x80, 6, 0, 1, 0, 0, 18, 0 };
    uint8_t desc[18];

    ohci_control(t, setup, sizeof(desc));
    qtest_memread(t->qts, CTRL_BUF(t), desc, sizeof(desc));
    g_assert_cmpuint(desc[0], ==, 18);
    g_assert_cmpuint(desc[1], ==, 1);
    g_assert_cmpuint(desc[7], ==, 64);
    g_assert_cmpuint(desc[17], ==, 1);
}

static void ohci_enable_audio_stream(OHCITransferTest *t)
{
    const uint8_t configure[8] = { 0, 9, 1, 0, 0, 0, 0, 0 };
    const uint8_t interface[8] = { 1, 11, 1, 0, 1, 0, 0, 0 };

    ohci_control(t, configure, 0);
    ohci_control(t, interface, 0);
}

static void ohci_periodic(OHCITransferTest *t, bool iso, unsigned int ep,
                          bool in, unsigned int len)
{
    uint32_t flags = ep << 7 | len << 16 |
                     (in ? ED_DIR_IN : ED_DIR_OUT) | (iso ? ED_ISO : 0);
    uint32_t td = PERIODIC_TD(t);
    uint32_t buf = ISO_BUF(t);
    unsigned int i;

    qtest_memset(t->qts, buf, 0, len);
    if (iso) {
        uint16_t frame = (ohci_readl(t, HC_FM_NUMBER) + 2) & 0xffff;

        ohci_mem_writel(t, td, TD_CC_NA | frame);
        ohci_mem_writel(t, td + 4, buf);
        ohci_mem_writel(t, td + 8, PERIODIC_TAIL(t));
        ohci_mem_writel(t, td + 12, buf + len - 1);
        ohci_mem_writew(t, td + 16, 0xe000 | (buf & 0xfff));
        for (i = 1; i < 8; i++) {
            ohci_mem_writew(t, td + 16 + 2 * i, 0xe000);
        }
    } else {
        ohci_write_td(t, td, in ? TD_DP_IN : TD_DP_OUT,
                      buf, PERIODIC_TAIL(t), buf + len - 1);
    }
    ohci_write_ed(t, PERIODIC_ED(t), flags, td, PERIODIC_TAIL(t));
    for (i = 0; i < 32; i++) {
        ohci_mem_writel(t, t->hcca + 4 * i, PERIODIC_ED(t));
    }
}

static void ohci_run_valid_iso(OHCITransferTest *t)
{
    uint32_t expected[] = { PERIODIC_TD(t) };
    uint32_t frame;
    uint32_t head;
    uint32_t td_flags;
    uint16_t psw;
    unsigned int i;

    ohci_periodic(t, true, 1, false, 192);
    frame = ohci_readl(t, HC_FM_NUMBER) & 0xffff;
    for (i = 0; i < 5; i++) {
        ohci_step(t);
    }
    g_assert_cmpuint((ohci_readl(t, HC_FM_NUMBER) - frame) & 0xffff, >=, 5);
    head = ohci_mem_readl(t, PERIODIC_ED(t) + 8);
    td_flags = ohci_mem_readl(t, PERIODIC_TD(t));
    g_assert_cmphex(head & ~15U, ==, PERIODIC_TAIL(t));
    g_assert_cmpuint(TD_CC(td_flags), ==, CC_NOERROR);
    psw = ohci_mem_readw(t, PERIODIC_TD(t) + 16);
    g_assert_cmpuint(psw >> 12, ==, CC_NOERROR);
    g_assert_cmpuint(psw & 0xfff, ==, 0);
    ohci_drain_done(t, expected, G_N_ELEMENTS(expected));
}

static void test_ohci_transfer(void *obj, void *data, QGuestAllocator *alloc)
{
    OHCITransferTest t;
    unsigned int scenario = GPOINTER_TO_UINT(data);
    uint32_t head, td_flags, psw, frame, done;
    unsigned int i;

    ohci_transfer_init(&t, obj, alloc);
    ohci_get_device_descriptor(&t);
    if (scenario == OHCI_TEST_CONTROL) {
        qpci_iounmap(t.pci, t.bar);
        guest_free(alloc, t.data);
        guest_free(alloc, t.desc);
        guest_free(alloc, t.hcca);
        return;
    }
    if (scenario == OHCI_TEST_ISO_TO_CONTROL) {
        const uint8_t setup[8] = { 0x80, 6, 0, 1, 0, 0, 18, 0 };
        uint32_t expected[] = { CTRL_SETUP_TD(&t) };

        qtest_memwrite(t.qts, SETUP_BUF(&t), setup, 8);
        ohci_write_td(&t, CTRL_SETUP_TD(&t), TD_DP_SETUP,
                      SETUP_BUF(&t), CTRL_TAIL_TD(&t), SETUP_BUF(&t) + 7);
        ohci_write_ed(&t, CTRL_ED(&t), 64 << 16, CTRL_SETUP_TD(&t),
                      CTRL_TAIL_TD(&t));
        ohci_writel(&t, HC_COMMAND_STATUS, HC_CLF);
        ohci_step(&t);
        g_assert_cmpuint(TD_CC(ohci_mem_readl(&t, CTRL_SETUP_TD(&t))), ==,
                         CC_NOERROR);
        ohci_drain_done(&t, expected, G_N_ELEMENTS(expected));
        ohci_periodic(&t, true, 0, true, 18);
    } else {
        ohci_enable_audio_stream(&t);
        if (scenario == OHCI_TEST_GENERAL_TO_ISO) {
            /* Prove this configured endpoint and periodic schedule work. */
            ohci_run_valid_iso(&t);
            g_assert_cmphex(ohci_mem_readl(&t, t.hcca + 0x84), ==, 0);
            g_assert_cmphex(ohci_readl(&t, HC_DONE_HEAD), ==, 0);
            g_assert_cmphex(ohci_readl(&t, HC_INTERRUPT_STATUS) & HC_WD, ==,
                            0);
            ohci_periodic(&t, false, 1, false, 192);
        } else {
            ohci_periodic(&t, true, 1, false, 192);
        }
    }

    frame = ohci_readl(&t, HC_FM_NUMBER) & 0xffff;
    done = ohci_mem_readl(&t, t.hcca + 0x84);
    for (i = 0; i < 5; i++) {
        ohci_step(&t);
    }
    g_assert_cmpuint((ohci_readl(&t, HC_FM_NUMBER) - frame) & 0xffff, >=, 5);
    head = ohci_mem_readl(&t, PERIODIC_ED(&t) + 8);
    td_flags = ohci_mem_readl(&t, PERIODIC_TD(&t));
    if (scenario == OHCI_TEST_GENERAL_TO_ISO) {
        g_assert_cmphex(ohci_mem_readl(&t, t.hcca + 0x84), ==, done);
        g_assert_cmphex(ohci_readl(&t, HC_DONE_HEAD), ==, 0);
        g_assert_cmphex(ohci_readl(&t, HC_INTERRUPT_STATUS) & HC_WD, ==, 0);
        g_assert_cmphex(head & ~15U, ==, PERIODIC_TD(&t));
        g_assert_cmphex(head & ED_HEAD_HALT, ==, 0);
        g_assert_cmpuint(TD_CC(td_flags), ==, CC_NOTACCESSED);
        g_assert_cmphex(ohci_mem_readl(&t, PERIODIC_TD(&t) + 4), ==,
                        ISO_BUF(&t));
    } else {
        uint32_t expected[] = { PERIODIC_TD(&t) };

        g_assert_cmphex(head & ~15U, ==, PERIODIC_TAIL(&t));
        /* The iTD CC does not report a packet-level STALL. */
        g_assert_cmpuint(TD_CC(td_flags), ==, CC_NOERROR);
        psw = ohci_mem_readw(&t, PERIODIC_TD(&t) + 16);
        g_assert_cmpuint(psw >> 12, ==,
                         scenario == OHCI_TEST_ISO_TO_CONTROL ? CC_STALL :
                         CC_NOERROR);
        g_assert_cmpuint(psw & 0xfff, ==, 0);
        ohci_drain_done(&t, expected, G_N_ELEMENTS(expected));
    }
    qpci_iounmap(t.pci, t.bar);
    guest_free(alloc, t.data);
    guest_free(alloc, t.desc);
    guest_free(alloc, t.hcca);
}

static void test_ohci_hotplug(void *obj, void *data, QGuestAllocator *alloc)
{
    usb_test_hotplug(global_qtest, "ohci", "1", NULL);
}

static void *ohci_pci_get_driver(void *obj, const char *interface)
{
    QOHCI_PCI *ohci_pci = obj;

    if (!g_strcmp0(interface, "pci-device")) {
        return &ohci_pci->dev;
    }

    fprintf(stderr, "%s not present in pci-ohci\n", interface);
    g_assert_not_reached();
}

static void *ohci_pci_create(void *pci_bus, QGuestAllocator *alloc, void *addr)
{
    QOHCI_PCI *ohci_pci = g_new0(QOHCI_PCI, 1);
    qpci_device_init(&ohci_pci->dev, pci_bus, addr);
    ohci_pci->obj.get_driver = ohci_pci_get_driver;

    return &ohci_pci->obj;
}

static void ohci_pci_register_nodes(void)
{
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "addr=04.0,id=ohci",
    };
    add_qpci_address(&opts, &(QPCIAddress) { .devfn = QPCI_DEVFN(4, 0) });

    qos_node_create_driver("pci-ohci", ohci_pci_create);
    qos_node_consumes("pci-ohci", "pci-bus", &opts);
    qos_node_produces("pci-ohci", "pci-device");
}

libqos_init(ohci_pci_register_nodes);

static void register_ohci_pci_test(void)
{
    QOSGraphTestOptions opts = {
        /* The descriptors and buffers use guest physical addresses for DMA. */
        .edge.after_cmd_line = "-global pci-host-bridge.bypass-iommu=on "
                               "-audiodev none,id=snd0 "
                               "-device usb-audio,audiodev=snd0,"
                               "bus=ohci.0,port=1",
    };

    qos_add_test("ohci_pci-test-hotplug", "pci-ohci", test_ohci_hotplug, NULL);
    if (!qtest_has_device("usb-audio")) {
        return;
    }
    opts.arg = GUINT_TO_POINTER(OHCI_TEST_CONTROL);
    qos_add_test("ohci_pci-control", "pci-ohci", test_ohci_transfer, &opts);
    opts.arg = GUINT_TO_POINTER(OHCI_TEST_ISO);
    qos_add_test("ohci_pci-iso", "pci-ohci", test_ohci_transfer, &opts);
    opts.arg = GUINT_TO_POINTER(OHCI_TEST_ISO_TO_CONTROL);
    qos_add_test("ohci_pci-iso-to-control", "pci-ohci", test_ohci_transfer,
                 &opts);
    opts.arg = GUINT_TO_POINTER(OHCI_TEST_GENERAL_TO_ISO);
    qos_add_test("ohci_pci-general-to-iso", "pci-ohci", test_ohci_transfer,
                 &opts);
}

libqos_init(register_ohci_pci_test);
