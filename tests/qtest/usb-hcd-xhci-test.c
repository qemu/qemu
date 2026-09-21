/*
 * QTest testcase for USB xHCI controller
 *
 * Copyright (c) 2014 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"
#include "libqos/usb.h"
#include "libqos/malloc-pc.h"
#include "qobject/qdict.h"

/* capability registers */
#define XHCI_CAPLENGTH          0x00
#define XHCI_HCSPARAMS1         0x04
#define XHCI_DBOFF              0x14
#define XHCI_RTSOFF             0x18
/* operational registers */
#define XHCI_USBCMD             0x00
#define XHCI_USBSTS             0x04
#define XHCI_CRCR               0x18
#define XHCI_DCBAAP             0x30
#define XHCI_CONFIG             0x38
#define XHCI_PORTSC(n)          (0x400 + 0x10 * (n))
/* interrupter 0, relative to the runtime registers */
#define XHCI_ERSTSZ             0x28
#define XHCI_ERSTBA             0x30
#define XHCI_ERDP               0x38

#define USBCMD_RS               (1 << 0)
#define USBCMD_HCRST            (1 << 1)
#define USBSTS_HCH              (1 << 0)
#define PORTSC_CCS              (1 << 0)
#define PORTSC_PR               (1 << 4)
#define PORTSC_PP               (1 << 9)
#define CRCR_RCS                (1 << 0)
#define ERDP_EHB                (1 << 3)

#define TRB_C                   (1 << 0)
#define TRB_TR_IOC              (1 << 5)
#define TRB_TR_SIA              (1U << 31)
#define TRB_TYPE(t)             ((t) << 10)
#define TRB_GET_TYPE(control)   (((control) >> 10) & 0x3f)
#define TRB_GET_CCODE(status)   ((status) >> 24)
#define TRB_GET_SLOT(control)   ((control) >> 24)

#define TR_ISOCH                5
#define CR_ENABLE_SLOT          9
#define CR_ADDRESS_DEVICE       11
#define CR_CONFIGURE_ENDPOINT   12
#define ER_TRANSFER             32
#define ER_COMMAND_COMPLETE     33
#define CC_SUCCESS              1

#define EP_TYPE_ISOCH_OUT       1
#define EP_TYPE_CONTROL         4

#define XHCI_RING_TRBS          64
#define XHCI_MICROFRAME_NS      125000

typedef struct XHCITest {
    QTestState *qts;
    QGuestAllocator alloc;
    QPCIBus *bus;
    struct qhc hc;
    uint32_t oper, runtime, doorbell;
    uint64_t cmd_ring, event_ring, input_ctx;
    unsigned int cmd_idx, event_idx;
    unsigned int port, slot;
} XHCITest;

static void wait_device_deleted_event(QTestState *qtest, const char *id)
{
    QDict *resp, *data;
    const char *device;

    /*
     * Other devices might get removed along with the removed device. Skip
     * these. The device of interest will be the last one.
     */
    for (;;) {
        resp = qtest_qmp_eventwait_ref(qtest, "DEVICE_DELETED");
        data = qdict_get_qdict(resp, "data");
        device = data ? qdict_get_try_str(data, "device") : NULL;
        if (device && !strcmp(device, id)) {
            qobject_unref(resp);
            break;
        }
        qobject_unref(resp);
    }
}

/*
 * Regression test for the xHCI-PCI "host" strong-link reference cycle.
 *
 * The xHCI PCI wrapper embeds an xhci-core child whose strong "host" link
 * points back at the PCI device, forming a refcount cycle. If
 * usb_xhci_pci_exit() does not break that cycle, the device's refcount never
 * reaches 0 on unplug, device_finalize() never runs, and therefore the
 * DEVICE_DELETED event (emitted from device_finalize()) is never sent.
 *
 * This test hot-plugs an xHCI controller into an ACPI-hotpluggable bus,
 * requests its removal and waits for DEVICE_DELETED. Without the fix the event
 * is never delivered (device_finalize() is blocked), so the test would
 * hang/time out.
 */
static void test_xhci_unplug_finalize(void)
{
    QTestState *qtest;
    const char *arch = qtest_get_arch();

    if (strcmp(arch, "i386") != 0 && strcmp(arch, "x86_64") != 0) {
        g_test_skip("Test only runs on x86 (ACPI PCI hotplug)");
        return;
    }
    if (!qtest_has_device("nec-usb-xhci")) {
        g_test_skip("Device nec-usb-xhci not available");
        return;
    }

    qtest = qtest_initf("-machine pc");

    qtest_qmp_device_add(qtest, "nec-usb-xhci", "xhci-finalize", "{}");

    /*
     * Request device removal. As the guest is not running, the unplug request
     * won't be processed until the next system reset, which performs the
     * removal and triggers device_finalize() (and thus DEVICE_DELETED).
     */
    qtest_qmp_device_del_send(qtest, "xhci-finalize");
    qtest_system_reset_nowait(qtest);
    wait_device_deleted_event(qtest, "xhci-finalize");

    qtest_quit(qtest);
}

static void test_xhci_hotplug(void)
{
    usb_test_hotplug(global_qtest, "xhci", "1", NULL);
}

static void test_usb_uas_hotplug(void)
{
    QTestState *qts = global_qtest;

    qtest_qmp_device_add(qts, "usb-uas", "uas", "{}");
    qtest_qmp_device_add(qts, "scsi-hd", "scsihd", "{'drive': 'drive0'}");

    /* TODO:
        UAS HBA driver in libqos, to check that
        added disk is visible after BUS rescan
    */

    qtest_qmp_device_del(qts, "scsihd");
    qtest_qmp_device_del(qts, "uas");
}

static void test_usb_ccid_hotplug(void)
{
    QTestState *qts = global_qtest;

    qtest_qmp_device_add(qts, "usb-ccid", "ccid", "{}");
    qtest_qmp_device_del(qts, "ccid");
    /* check the device can be added again */
    qtest_qmp_device_add(qts, "usb-ccid", "ccid", "{}");
    qtest_qmp_device_del(qts, "ccid");
}

static uint32_t xhci_readl(XHCITest *x, uint32_t off)
{
    return qpci_io_readl(x->hc.dev, x->hc.bar, off);
}

static void xhci_writel(XHCITest *x, uint32_t off, uint32_t val)
{
    qpci_io_writel(x->hc.dev, x->hc.bar, off, val);
}

static void xhci_writeq(XHCITest *x, uint32_t off, uint64_t val)
{
    xhci_writel(x, off, val);
    xhci_writel(x, off + 4, val >> 32);
}

static uint64_t xhci_alloc_page(XHCITest *x)
{
    uint64_t addr = guest_alloc(&x->alloc, 0x1000);

    qtest_memset(x->qts, addr, 0, 0x1000);
    return addr;
}

static void xhci_write_trb(XHCITest *x, uint64_t addr, uint64_t parameter,
                           uint32_t status, uint32_t control)
{
    qtest_writeq(x->qts, addr, parameter);
    qtest_writel(x->qts, addr + 8, status);
    qtest_writel(x->qts, addr + 12, control);
}

/* Fetch the next event if there is one. Does not advance the clock. */
static bool xhci_next_event(XHCITest *x, uint32_t *status, uint32_t *control)
{
    uint64_t addr = x->event_ring + 16 * x->event_idx;
    uint32_t c = qtest_readl(x->qts, addr + 12);

    if (!(c & TRB_C)) {
        return false;
    }
    if (status) {
        *status = qtest_readl(x->qts, addr + 8);
    }
    if (control) {
        *control = c;
    }
    x->event_idx++;
    g_assert_cmpuint(x->event_idx, <, XHCI_RING_TRBS);
    xhci_writeq(x, x->runtime + XHCI_ERDP, (addr + 16) | ERDP_EHB);
    return true;
}

static unsigned int xhci_command(XHCITest *x, uint64_t parameter,
                                 uint32_t control)
{
    uint32_t status;

    g_assert_cmpuint(x->cmd_idx, <, XHCI_RING_TRBS);
    xhci_write_trb(x, x->cmd_ring + 16 * x->cmd_idx++, parameter, 0,
                   control | TRB_C);
    xhci_writel(x, x->doorbell, 0);

    g_assert_true(xhci_next_event(x, &status, &control));
    g_assert_cmpuint(TRB_GET_TYPE(control), ==, ER_COMMAND_COMPLETE);
    g_assert_cmpuint(TRB_GET_CCODE(status), ==, CC_SUCCESS);
    return TRB_GET_SLOT(control);
}

/*
 * Start qemu-xhci with one USB device, run the controller and bring the
 * device to the Addressed state.
 */
static void xhci_test_start(XHCITest *x, const char *usb_device)
{
    uint64_t dcbaa, erst, ep0_ring;
    unsigned int maxports;

    memset(x, 0, sizeof(*x));
    /* pit=off: a long clock step would run the i8254 timer all the way */
    x->qts = qtest_initf("-machine pc,pit=off -nodefaults "
                         "-device qemu-xhci,id=xhci,addr=04.0 %s", usb_device);
    pc_alloc_init(&x->alloc, x->qts, ALLOC_NO_FLAGS);
    x->bus = qpci_new_pc(x->qts, NULL);
    qusb_pci_init_one(x->bus, &x->hc, QPCI_DEVFN(4, 0), 0);

    x->oper = qpci_io_readb(x->hc.dev, x->hc.bar, XHCI_CAPLENGTH);
    x->runtime = xhci_readl(x, XHCI_RTSOFF) & ~0x1f;
    x->doorbell = xhci_readl(x, XHCI_DBOFF) & ~0x3;
    maxports = xhci_readl(x, XHCI_HCSPARAMS1) >> 24;

    xhci_writel(x, x->oper + XHCI_USBCMD, USBCMD_HCRST);
    g_assert_false(xhci_readl(x, x->oper + XHCI_USBCMD) & USBCMD_HCRST);

    dcbaa = xhci_alloc_page(x);
    erst = xhci_alloc_page(x);
    x->cmd_ring = xhci_alloc_page(x);
    x->event_ring = xhci_alloc_page(x);
    x->input_ctx = xhci_alloc_page(x);

    xhci_writel(x, x->oper + XHCI_CONFIG, 1);
    xhci_writeq(x, x->oper + XHCI_DCBAAP, dcbaa);
    qtest_writeq(x->qts, erst, x->event_ring);
    qtest_writel(x->qts, erst + 8, XHCI_RING_TRBS);
    xhci_writel(x, x->runtime + XHCI_ERSTSZ, 1);
    xhci_writeq(x, x->runtime + XHCI_ERSTBA, erst);
    xhci_writeq(x, x->runtime + XHCI_ERDP, x->event_ring | ERDP_EHB);
    xhci_writeq(x, x->oper + XHCI_CRCR, x->cmd_ring | CRCR_RCS);
    xhci_writel(x, x->oper + XHCI_USBCMD, USBCMD_RS);
    g_assert_false(xhci_readl(x, x->oper + XHCI_USBSTS) & USBSTS_HCH);

    for (x->port = 0; x->port < maxports; x->port++) {
        if (xhci_readl(x, x->oper + XHCI_PORTSC(x->port)) & PORTSC_CCS) {
            break;
        }
    }
    g_assert_cmpuint(x->port, <, maxports);
    xhci_writel(x, x->oper + XHCI_PORTSC(x->port), PORTSC_PP | PORTSC_PR);
    while (xhci_next_event(x, NULL, NULL)) {
        /* drop the port status change events */
    }

    x->slot = xhci_command(x, 0, TRB_TYPE(CR_ENABLE_SLOT));
    qtest_writeq(x->qts, dcbaa + 8 * x->slot, xhci_alloc_page(x));

    /* input control context: add slot and ep0 */
    qtest_writel(x->qts, x->input_ctx + 0x04, 0x3);
    /* slot context: one context entry, root hub port */
    qtest_writel(x->qts, x->input_ctx + 0x20, 1 << 27);
    qtest_writel(x->qts, x->input_ctx + 0x24, (x->port + 1) << 16);
    /* ep0 context */
    ep0_ring = xhci_alloc_page(x);
    qtest_writel(x->qts, x->input_ctx + 0x44,
                 (64 << 16) | (EP_TYPE_CONTROL << 3));
    qtest_writeq(x->qts, x->input_ctx + 0x48, ep0_ring | 1);
    xhci_command(x, x->input_ctx,
                 TRB_TYPE(CR_ADDRESS_DEVICE) | (x->slot << 24));
}

/* Returns the address of the transfer ring. */
static uint64_t xhci_configure_ep(XHCITest *x, unsigned int epid,
                                  unsigned int type, unsigned int interval,
                                  unsigned int max_packet)
{
    uint64_t ring = xhci_alloc_page(x);
    uint64_t epctx = x->input_ctx + 0x20 * (epid + 1);

    qtest_memset(x->qts, x->input_ctx, 0, 0x1000);
    qtest_writel(x->qts, x->input_ctx + 0x04, (1 << epid) | 1);
    qtest_writel(x->qts, x->input_ctx + 0x20, epid << 27);
    qtest_writel(x->qts, x->input_ctx + 0x24, (x->port + 1) << 16);
    qtest_writel(x->qts, epctx + 0x00, interval << 16);
    qtest_writel(x->qts, epctx + 0x04, (max_packet << 16) | (type << 3));
    qtest_writeq(x->qts, epctx + 0x08, ring | 1);
    xhci_command(x, x->input_ctx,
                 TRB_TYPE(CR_CONFIGURE_ENDPOINT) | (x->slot << 24));
    return ring;
}

static void xhci_test_end(XHCITest *x)
{
    g_free(x->hc.dev);
    qpci_free_pc(x->bus);
    alloc_destroy(&x->alloc);
    qtest_quit(x->qts);
}

static bool xhci_test_supported(const char *usb_device)
{
    const char *arch = qtest_get_arch();

    if (strcmp(arch, "i386") != 0 && strcmp(arch, "x86_64") != 0) {
        g_test_skip("Test only runs on x86 (pc machine)");
        return false;
    }
    if (!qtest_has_device("qemu-xhci") || !qtest_has_device(usb_device)) {
        g_test_skip("Devices not available");
        return false;
    }
    return true;
}

/*
 * An isoch TD with SIA set is run at the next interval boundary. That has to
 * hold once the microframe index no longer fits in 32 bits as well.
 */
static void test_xhci_isoch_mfindex_32bit(void)
{
    const unsigned int interval = 6;
    uint32_t control;
    uint64_t ring;
    XHCITest x;

    if (!xhci_test_supported("usb-audio")) {
        return;
    }

    xhci_test_start(&x, "-audiodev none,id=snd0 "
                        "-device usb-audio,audiodev=snd0");
    ring = xhci_configure_ep(&x, 2, EP_TYPE_ISOCH_OUT, interval, 64);

    /* Go past 2^32 microframes and stop off an interval boundary. */
    qtest_clock_step(x.qts, (1ULL << 32) * XHCI_MICROFRAME_NS);
    qtest_clock_step(x.qts, 5 * XHCI_MICROFRAME_NS);

    xhci_write_trb(&x, ring, xhci_alloc_page(&x), 64,
                   TRB_TYPE(TR_ISOCH) | TRB_TR_SIA | TRB_TR_IOC | TRB_C);
    xhci_writel(&x, x.doorbell + 4 * x.slot, 2);
    g_assert_false(xhci_next_event(&x, NULL, NULL));

    /*
     * The streaming interface has not been enabled, so usb-audio stalls the
     * TD. What matters is when that happens.
     */
    qtest_clock_step(x.qts, XHCI_MICROFRAME_NS << interval);
    g_assert_true(xhci_next_event(&x, NULL, &control));
    g_assert_cmpuint(TRB_GET_TYPE(control), ==, ER_TRANSFER);

    xhci_test_end(&x);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/xhci/pci/hotplug", test_xhci_hotplug);
    qtest_add_func("/xhci/pci/unplug/finalize", test_xhci_unplug_finalize);
    if (qtest_has_device("usb-uas")) {
        qtest_add_func("/xhci/pci/hotplug/usb-uas", test_usb_uas_hotplug);
    }
    if (qtest_has_device("usb-ccid")) {
        qtest_add_func("/xhci/pci/hotplug/usb-ccid", test_usb_ccid_hotplug);
    }
    qtest_add_func("/xhci/pci/isoch/mfindex-32bit",
                   test_xhci_isoch_mfindex_32bit);

    qtest_start("-device nec-usb-xhci,id=xhci"
                " -drive id=drive0,if=none,file=null-co://,"
                "file.read-zeroes=on,format=raw");
    ret = g_test_run();
    qtest_end();

    return ret;
}
