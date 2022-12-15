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
 * version 2.1 of the License, or (at your option) any later version.
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
#include "libqtest-single.h"
#include "libqos/pci-pc.h"
#include "qemu/sockets.h"
#include "qemu/iov.h"
#include "qemu/module.h"
#include "qemu/bitops.h"
#include "libqos/libqos-malloc.h"
#include "libqos/e1000e.h"
#include "hw/net/e1000_regs.h"

static void e1000e_send_verify(QE1000E *d, int *test_sockets, QGuestAllocator *alloc)
{
    static const char test[] = "TEST";
    struct e1000_tx_desc descr;
    char buffer[64];
    int ret;
    uint32_t recv_len;

    /* Prepare test data buffer */
    uint64_t data = guest_alloc(alloc, sizeof(buffer));
    memwrite(data, test, sizeof(test));

    /* Prepare TX descriptor */
    memset(&descr, 0, sizeof(descr));
    descr.buffer_addr = cpu_to_le64(data);
    descr.lower.data = cpu_to_le32(E1000_TXD_CMD_RS   |
                                   E1000_TXD_CMD_EOP  |
                                   E1000_TXD_CMD_DEXT |
                                   E1000_TXD_DTYP_D   |
                                   sizeof(buffer));

    /* Put descriptor to the ring */
    e1000e_tx_ring_push(d, &descr);

    /* Wait for TX WB interrupt */
    e1000e_wait_isr(d, E1000E_TX0_MSG_ID);

    /* Check DD bit */
    g_assert_cmphex(le32_to_cpu(descr.upper.data) & E1000_TXD_STAT_DD, ==,
                    E1000_TXD_STAT_DD);

    /* Check data sent to the backend */
    ret = recv(test_sockets[0], &recv_len, sizeof(recv_len), 0);
    g_assert_cmpint(ret, == , sizeof(recv_len));
    ret = recv(test_sockets[0], buffer, sizeof(buffer), 0);
    g_assert_cmpint(ret, ==, sizeof(buffer));
    g_assert_cmpstr(buffer, == , test);

    /* Free test data buffer */
    guest_free(alloc, data);
}

static void e1000e_receive_verify(QE1000E *d, int *test_sockets, QGuestAllocator *alloc)
{
    union e1000_rx_desc_extended descr;

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

    char buffer[64];
    int ret;

    /* Send a dummy packet to device's socket*/
    ret = iov_send(test_sockets[0], iov, 2, 0, sizeof(len) + sizeof(test));
    g_assert_cmpint(ret, == , sizeof(test) + sizeof(len));

    /* Prepare test data buffer */
    uint64_t data = guest_alloc(alloc, sizeof(buffer));

    /* Prepare RX descriptor */
    memset(&descr, 0, sizeof(descr));
    descr.read.buffer_addr = cpu_to_le64(data);

    /* Put descriptor to the ring */
    e1000e_rx_ring_push(d, &descr);

    /* Wait for TX WB interrupt */
    e1000e_wait_isr(d, E1000E_RX0_MSG_ID);

    /* Check DD bit */
    g_assert_cmphex(le32_to_cpu(descr.wb.upper.status_error) &
        E1000_RXD_STAT_DD, ==, E1000_RXD_STAT_DD);

    /* Check data sent to the backend */
    memread(data, buffer, sizeof(buffer));
    g_assert_cmpstr(buffer, == , test);

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
    QE1000E_PCI *dev = obj;

    if (dev->pci_dev.bus->not_hotpluggable) {
        g_test_skip("pci bus does not support hotplug");
        return;
    }

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
