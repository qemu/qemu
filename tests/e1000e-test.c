 /*
 * QTest testcase for e1000e NIC
 *
 * Copyright (c) 2015 Ravello Systems LTD (http://ravellosystems.com)
 * Developed by Daynix Computing LTD (http://www.daynix.com)
 *
 * Authors:
 * Dmitry Fleytman <dmitry@daynix.com>
 * Leonid Bloch <leonid@daynix.com>
 * Yan Vugenfirer <yan@daynix.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */


#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu-common.h"
#include "libqos/pci-pc.h"
#include "qemu/sockets.h"
#include "qemu/iov.h"
#include "qemu/bitops.h"
#include "libqos/malloc.h"
#include "libqos/malloc-pc.h"
#include "libqos/malloc-generic.h"

#define E1000E_IMS      (0x00d0)

#define E1000E_STATUS   (0x0008)
#define E1000E_STATUS_LU BIT(1)
#define E1000E_STATUS_ASDV1000 BIT(9)

#define E1000E_CTRL     (0x0000)
#define E1000E_CTRL_RESET BIT(26)

#define E1000E_RCTL     (0x0100)
#define E1000E_RCTL_EN  BIT(1)
#define E1000E_RCTL_UPE BIT(3)
#define E1000E_RCTL_MPE BIT(4)

#define E1000E_RFCTL     (0x5008)
#define E1000E_RFCTL_EXTEN  BIT(15)

#define E1000E_TCTL     (0x0400)
#define E1000E_TCTL_EN  BIT(1)

#define E1000E_CTRL_EXT             (0x0018)
#define E1000E_CTRL_EXT_DRV_LOAD    BIT(28)
#define E1000E_CTRL_EXT_TXLSFLOW    BIT(22)

#define E1000E_RX0_MSG_ID           (0)
#define E1000E_TX0_MSG_ID           (1)
#define E1000E_OTHER_MSG_ID         (2)

#define E1000E_IVAR                 (0x00E4)
#define E1000E_IVAR_TEST_CFG        ((E1000E_RX0_MSG_ID << 0)    | BIT(3)  | \
                                     (E1000E_TX0_MSG_ID << 8)    | BIT(11) | \
                                     (E1000E_OTHER_MSG_ID << 16) | BIT(19) | \
                                     BIT(31))

#define E1000E_RING_LEN             (0x1000)
#define E1000E_TXD_LEN              (16)
#define E1000E_RXD_LEN              (16)

#define E1000E_TDBAL    (0x3800)
#define E1000E_TDBAH    (0x3804)
#define E1000E_TDLEN    (0x3808)
#define E1000E_TDH      (0x3810)
#define E1000E_TDT      (0x3818)

#define E1000E_RDBAL    (0x2800)
#define E1000E_RDBAH    (0x2804)
#define E1000E_RDLEN    (0x2808)
#define E1000E_RDH      (0x2810)
#define E1000E_RDT      (0x2818)

typedef struct e1000e_device {
    QPCIDevice *pci_dev;
    QPCIBar mac_regs;

    uint64_t tx_ring;
    uint64_t rx_ring;
} e1000e_device;

static int test_sockets[2];
static QGuestAllocator *test_alloc;
static QPCIBus *test_bus;

static void e1000e_pci_foreach_callback(QPCIDevice *dev, int devfn, void *data)
{
    QPCIDevice **res = data;

    g_assert_null(*res);
    *res = dev;
}

static QPCIDevice *e1000e_device_find(QPCIBus *bus)
{
    static const int e1000e_vendor_id = 0x8086;
    static const int e1000e_dev_id = 0x10D3;

    QPCIDevice *e1000e_dev = NULL;

    qpci_device_foreach(bus, e1000e_vendor_id, e1000e_dev_id,
        e1000e_pci_foreach_callback, &e1000e_dev);

    g_assert_nonnull(e1000e_dev);

    return e1000e_dev;
}

static void e1000e_macreg_write(e1000e_device *d, uint32_t reg, uint32_t val)
{
    qpci_io_writel(d->pci_dev, d->mac_regs, reg, val);
}

static uint32_t e1000e_macreg_read(e1000e_device *d, uint32_t reg)
{
    return qpci_io_readl(d->pci_dev, d->mac_regs, reg);
}

static void e1000e_device_init(QPCIBus *bus, e1000e_device *d)
{
    uint32_t val;

    d->pci_dev = e1000e_device_find(bus);

    /* Enable the device */
    qpci_device_enable(d->pci_dev);

    /* Map BAR0 (mac registers) */
    d->mac_regs = qpci_iomap(d->pci_dev, 0, NULL);

    /* Reset the device */
    val = e1000e_macreg_read(d, E1000E_CTRL);
    e1000e_macreg_write(d, E1000E_CTRL, val | E1000E_CTRL_RESET);

    /* Enable and configure MSI-X */
    qpci_msix_enable(d->pci_dev);
    e1000e_macreg_write(d, E1000E_IVAR, E1000E_IVAR_TEST_CFG);

    /* Check the device status - link and speed */
    val = e1000e_macreg_read(d, E1000E_STATUS);
    g_assert_cmphex(val & (E1000E_STATUS_LU | E1000E_STATUS_ASDV1000),
        ==, E1000E_STATUS_LU | E1000E_STATUS_ASDV1000);

    /* Initialize TX/RX logic */
    e1000e_macreg_write(d, E1000E_RCTL, 0);
    e1000e_macreg_write(d, E1000E_TCTL, 0);

    /* Notify the device that the driver is ready */
    val = e1000e_macreg_read(d, E1000E_CTRL_EXT);
    e1000e_macreg_write(d, E1000E_CTRL_EXT,
        val | E1000E_CTRL_EXT_DRV_LOAD | E1000E_CTRL_EXT_TXLSFLOW);

    /* Allocate and setup TX ring */
    d->tx_ring = guest_alloc(test_alloc, E1000E_RING_LEN);
    g_assert(d->tx_ring != 0);

    e1000e_macreg_write(d, E1000E_TDBAL, (uint32_t) d->tx_ring);
    e1000e_macreg_write(d, E1000E_TDBAH, (uint32_t) (d->tx_ring >> 32));
    e1000e_macreg_write(d, E1000E_TDLEN, E1000E_RING_LEN);
    e1000e_macreg_write(d, E1000E_TDT, 0);
    e1000e_macreg_write(d, E1000E_TDH, 0);

    /* Enable transmit */
    e1000e_macreg_write(d, E1000E_TCTL, E1000E_TCTL_EN);

    /* Allocate and setup RX ring */
    d->rx_ring = guest_alloc(test_alloc, E1000E_RING_LEN);
    g_assert(d->rx_ring != 0);

    e1000e_macreg_write(d, E1000E_RDBAL, (uint32_t)d->rx_ring);
    e1000e_macreg_write(d, E1000E_RDBAH, (uint32_t)(d->rx_ring >> 32));
    e1000e_macreg_write(d, E1000E_RDLEN, E1000E_RING_LEN);
    e1000e_macreg_write(d, E1000E_RDT, 0);
    e1000e_macreg_write(d, E1000E_RDH, 0);

    /* Enable receive */
    e1000e_macreg_write(d, E1000E_RFCTL, E1000E_RFCTL_EXTEN);
    e1000e_macreg_write(d, E1000E_RCTL, E1000E_RCTL_EN  |
                                        E1000E_RCTL_UPE |
                                        E1000E_RCTL_MPE);

    /* Enable all interrupts */
    e1000e_macreg_write(d, E1000E_IMS, 0xFFFFFFFF);
}

static void e1000e_tx_ring_push(e1000e_device *d, void *descr)
{
    uint32_t tail = e1000e_macreg_read(d, E1000E_TDT);
    uint32_t len = e1000e_macreg_read(d, E1000E_TDLEN) / E1000E_TXD_LEN;

    memwrite(d->tx_ring + tail * E1000E_TXD_LEN, descr, E1000E_TXD_LEN);
    e1000e_macreg_write(d, E1000E_TDT, (tail + 1) % len);

    /* Read WB data for the packet transmitted */
    memread(d->tx_ring + tail * E1000E_TXD_LEN, descr, E1000E_TXD_LEN);
}

static void e1000e_rx_ring_push(e1000e_device *d, void *descr)
{
    uint32_t tail = e1000e_macreg_read(d, E1000E_RDT);
    uint32_t len = e1000e_macreg_read(d, E1000E_RDLEN) / E1000E_RXD_LEN;

    memwrite(d->rx_ring + tail * E1000E_RXD_LEN, descr, E1000E_RXD_LEN);
    e1000e_macreg_write(d, E1000E_RDT, (tail + 1) % len);

    /* Read WB data for the packet received */
    memread(d->rx_ring + tail * E1000E_RXD_LEN, descr, E1000E_RXD_LEN);
}

static void e1000e_wait_isr(e1000e_device *d, uint16_t msg_id)
{
    guint64 end_time = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;

    do {
        if (qpci_msix_pending(d->pci_dev, msg_id)) {
            return;
        }
        clock_step(10000);
    } while (g_get_monotonic_time() < end_time);

    g_error("Timeout expired");
}

static void e1000e_send_verify(e1000e_device *d)
{
    struct {
        uint64_t buffer_addr;
        union {
            uint32_t data;
            struct {
                uint16_t length;
                uint8_t cso;
                uint8_t cmd;
            } flags;
        } lower;
        union {
            uint32_t data;
            struct {
                uint8_t status;
                uint8_t css;
                uint16_t special;
            } fields;
        } upper;
    } descr;

    static const uint32_t dtyp_data = BIT(20);
    static const uint32_t dtyp_ext  = BIT(29);
    static const uint32_t dcmd_rs   = BIT(27);
    static const uint32_t dcmd_eop  = BIT(24);
    static const uint32_t dsta_dd   = BIT(0);
    static const int data_len = 64;
    char buffer[64];
    int ret;
    uint32_t recv_len;

    /* Prepare test data buffer */
    uint64_t data = guest_alloc(test_alloc, data_len);
    memwrite(data, "TEST", 5);

    /* Prepare TX descriptor */
    memset(&descr, 0, sizeof(descr));
    descr.buffer_addr = cpu_to_le64(data);
    descr.lower.data = cpu_to_le32(dcmd_rs   |
                                   dcmd_eop  |
                                   dtyp_ext  |
                                   dtyp_data |
                                   data_len);

    /* Put descriptor to the ring */
    e1000e_tx_ring_push(d, &descr);

    /* Wait for TX WB interrupt */
    e1000e_wait_isr(d, E1000E_TX0_MSG_ID);

    /* Check DD bit */
    g_assert_cmphex(le32_to_cpu(descr.upper.data) & dsta_dd, ==, dsta_dd);

    /* Check data sent to the backend */
    ret = qemu_recv(test_sockets[0], &recv_len, sizeof(recv_len), 0);
    g_assert_cmpint(ret, == , sizeof(recv_len));
    qemu_recv(test_sockets[0], buffer, 64, 0);
    g_assert_cmpstr(buffer, == , "TEST");

    /* Free test data buffer */
    guest_free(test_alloc, data);
}

static void e1000e_receive_verify(e1000e_device *d)
{
    union {
        struct {
            uint64_t buffer_addr;
            uint64_t reserved;
        } read;
        struct {
            struct {
                uint32_t mrq;
                union {
                    uint32_t rss;
                    struct {
                        uint16_t ip_id;
                        uint16_t csum;
                    } csum_ip;
                } hi_dword;
            } lower;
            struct {
                uint32_t status_error;
                uint16_t length;
                uint16_t vlan;
            } upper;
        } wb;
    } descr;

    static const uint32_t esta_dd = BIT(0);

    char test[] = "TEST";
    int len = htonl(sizeof(test));
    struct iovec iov[] = {
        {
            .iov_base = &len,
            .iov_len = sizeof(len),
        },{
            .iov_base = test,
            .iov_len = sizeof(test),
        },
    };

    static const int data_len = 64;
    char buffer[64];
    int ret;

    /* Send a dummy packet to device's socket*/
    ret = iov_send(test_sockets[0], iov, 2, 0, sizeof(len) + sizeof(test));
    g_assert_cmpint(ret, == , sizeof(test) + sizeof(len));

    /* Prepare test data buffer */
    uint64_t data = guest_alloc(test_alloc, data_len);

    /* Prepare RX descriptor */
    memset(&descr, 0, sizeof(descr));
    descr.read.buffer_addr = cpu_to_le64(data);

    /* Put descriptor to the ring */
    e1000e_rx_ring_push(d, &descr);

    /* Wait for TX WB interrupt */
    e1000e_wait_isr(d, E1000E_RX0_MSG_ID);

    /* Check DD bit */
    g_assert_cmphex(le32_to_cpu(descr.wb.upper.status_error) &
        esta_dd, ==, esta_dd);

    /* Check data sent to the backend */
    memread(data, buffer, sizeof(buffer));
    g_assert_cmpstr(buffer, == , "TEST");

    /* Free test data buffer */
    guest_free(test_alloc, data);
}

static void e1000e_device_clear(QPCIBus *bus, e1000e_device *d)
{
    qpci_iounmap(d->pci_dev, d->mac_regs);
    qpci_msix_disable(d->pci_dev);
}

static void data_test_init(e1000e_device *d)
{
    char *cmdline;

    int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, test_sockets);
    g_assert_cmpint(ret, != , -1);

    cmdline = g_strdup_printf("-netdev socket,fd=%d,id=hs0 "
                              "-device e1000e,netdev=hs0", test_sockets[1]);
    g_assert_nonnull(cmdline);

    qtest_start(cmdline);
    g_free(cmdline);

    test_bus = qpci_init_pc(NULL);
    g_assert_nonnull(test_bus);

    test_alloc = pc_alloc_init();
    g_assert_nonnull(test_alloc);

    e1000e_device_init(test_bus, d);
}

static void data_test_clear(e1000e_device *d)
{
    e1000e_device_clear(test_bus, d);
    close(test_sockets[0]);
    pc_alloc_uninit(test_alloc);
    g_free(d->pci_dev);
    qpci_free_pc(test_bus);
    qtest_end();
}

static void test_e1000e_init(gconstpointer data)
{
    e1000e_device d;

    data_test_init(&d);
    data_test_clear(&d);
}

static void test_e1000e_tx(gconstpointer data)
{
    e1000e_device d;

    data_test_init(&d);
    e1000e_send_verify(&d);
    data_test_clear(&d);
}

static void test_e1000e_rx(gconstpointer data)
{
    e1000e_device d;

    data_test_init(&d);
    e1000e_receive_verify(&d);
    data_test_clear(&d);
}

static void test_e1000e_multiple_transfers(gconstpointer data)
{
    static const long iterations = 4 * 1024;
    long i;

    e1000e_device d;

    data_test_init(&d);

    for (i = 0; i < iterations; i++) {
        e1000e_send_verify(&d);
        e1000e_receive_verify(&d);
    }

    data_test_clear(&d);
}

static void test_e1000e_hotplug(gconstpointer data)
{
    static const uint8_t slot = 0x06;

    qtest_start("-device e1000e");

    qpci_plug_device_test("e1000e", "e1000e_net", slot, NULL);
    qpci_unplug_acpi_device_test("e1000e_net", slot);

    qtest_end();
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_data_func("e1000e/init", NULL, test_e1000e_init);
    qtest_add_data_func("e1000e/tx", NULL, test_e1000e_tx);
    qtest_add_data_func("e1000e/rx", NULL, test_e1000e_rx);
    qtest_add_data_func("e1000e/multiple_transfers", NULL,
        test_e1000e_multiple_transfers);
    qtest_add_data_func("e1000e/hotplug", NULL, test_e1000e_hotplug);

    return g_test_run();
}
