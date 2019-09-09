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
#include "qemu-common.h"
#include "libqtest-single.h"
#include "qemu-common.h"
#include "libqos/pci-pc.h"
#include "qemu/sockets.h"
#include "qemu/iov.h"
#include "qemu/module.h"
#include "qemu/bitops.h"
#include "libqos/malloc.h"
#include "libqos/e1000e.h"

static void e1000e_send_verify(QE1000E *d, int *test_sockets, QGuestAllocator *alloc)
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
    uint64_t data = guest_alloc(alloc, data_len);
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
    guest_free(alloc, data);
}

static void e1000e_receive_verify(QE1000E *d, int *test_sockets, QGuestAllocator *alloc)
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
    uint64_t data = guest_alloc(alloc, data_len);

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
    guest_free(alloc, data);
}

static void test_e1000e_init(void *obj, void *data, QGuestAllocator * alloc)
{
    /* init does nothing */
}

static void test_e1000e_tx(void *obj, void *data, QGuestAllocator * alloc)
{
    QE1000E_PCI *e1000e = obj;
    QE1000E *d = &e1000e->e1000e;
    QOSGraphObject *e_object = obj;
    QPCIDevice *dev = e_object->get_driver(e_object, "pci-device");

    /* FIXME: add spapr support */
    if (qpci_check_buggy_msi(dev)) {
        return;
    }

    e1000e_send_verify(d, data, alloc);
}

static void test_e1000e_rx(void *obj, void *data, QGuestAllocator * alloc)
{
    QE1000E_PCI *e1000e = obj;
    QE1000E *d = &e1000e->e1000e;
    QOSGraphObject *e_object = obj;
    QPCIDevice *dev = e_object->get_driver(e_object, "pci-device");

    /* FIXME: add spapr support */
    if (qpci_check_buggy_msi(dev)) {
        return;
    }

    e1000e_receive_verify(d, data, alloc);
}

static void test_e1000e_multiple_transfers(void *obj, void *data,
                                           QGuestAllocator *alloc)
{
    static const long iterations = 4 * 1024;
    long i;

    QE1000E_PCI *e1000e = obj;
    QE1000E *d = &e1000e->e1000e;
    QOSGraphObject *e_object = obj;
    QPCIDevice *dev = e_object->get_driver(e_object, "pci-device");

    /* FIXME: add spapr support */
    if (qpci_check_buggy_msi(dev)) {
        return;
    }

    for (i = 0; i < iterations; i++) {
        e1000e_send_verify(d, data, alloc);
        e1000e_receive_verify(d, data, alloc);
    }

}

static void test_e1000e_hotplug(void *obj, void *data, QGuestAllocator * alloc)
{
    QTestState *qts = global_qtest;  /* TODO: get rid of global_qtest here */

    qtest_qmp_device_add(qts, "e1000e", "e1000e_net", "{'addr': '0x06'}");
    qpci_unplug_acpi_device_test(qts, "e1000e_net", 0x06);
}

static void data_test_clear(void *sockets)
{
    int *test_sockets = sockets;

    close(test_sockets[0]);
    qos_invalidate_command_line();
    close(test_sockets[1]);
    g_free(test_sockets);
}

static void *data_test_init(GString *cmd_line, void *arg)
{
    int *test_sockets = g_new(int, 2);
    int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, test_sockets);
    g_assert_cmpint(ret, != , -1);

    g_string_append_printf(cmd_line, " -netdev socket,fd=%d,id=hs0 ",
                           test_sockets[1]);

    g_test_queue_destroy(data_test_clear, test_sockets);
    return test_sockets;
}

static void register_e1000e_test(void)
{
    QOSGraphTestOptions opts = {
        .before = data_test_init,
    };

    qos_add_test("init", "e1000e", test_e1000e_init, &opts);
    qos_add_test("tx", "e1000e", test_e1000e_tx, &opts);
    qos_add_test("rx", "e1000e", test_e1000e_rx, &opts);
    qos_add_test("multiple_transfers", "e1000e",
                      test_e1000e_multiple_transfers, &opts);
    qos_add_test("hotplug", "e1000e", test_e1000e_hotplug, &opts);
}

libqos_init(register_e1000e_test);
