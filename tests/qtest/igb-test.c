/*
 * QTest testcase for igb NIC
 *
 * Copyright (c) 2022-2023 Red Hat, Inc.
 * Copyright (c) 2015 Ravello Systems LTD (http://ravellosystems.com)
 * Developed by Daynix Computing LTD (http://www.daynix.com)
 *
 * Authors:
 * Akihiko Odaki <akihiko.odaki@daynix.com>
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
#include "net/eth.h"
#include "qemu/sockets.h"
#include "qemu/iov.h"
#include "qemu/module.h"
#include "qemu/bitops.h"
#include "libqos/libqos-malloc.h"
#include "libqos/e1000e.h"
#include "hw/net/igb_regs.h"

#ifndef _WIN32

static const struct eth_header packet = {
    .h_dest = E1000E_ADDRESS,
    .h_source = E1000E_ADDRESS,
};

static void igb_send_verify(QE1000E *d, int *test_sockets, QGuestAllocator *alloc)
{
    union e1000_adv_tx_desc descr;
    char buffer[64];
    int ret;
    uint32_t recv_len;

    /* Prepare test data buffer */
    uint64_t data = guest_alloc(alloc, sizeof(buffer));
    memwrite(data, &packet, sizeof(packet));

    /* Prepare TX descriptor */
    memset(&descr, 0, sizeof(descr));
    descr.read.buffer_addr = cpu_to_le64(data);
    descr.read.cmd_type_len = cpu_to_le32(E1000_TXD_CMD_RS   |
                                          E1000_TXD_CMD_EOP  |
                                          E1000_TXD_DTYP_D   |
                                          sizeof(buffer));

    /* Put descriptor to the ring */
    e1000e_tx_ring_push(d, &descr);

    /* Wait for TX WB interrupt */
    e1000e_wait_isr(d, E1000E_TX0_MSG_ID);

    /* Check DD bit */
    g_assert_cmphex(le32_to_cpu(descr.wb.status) & E1000_TXD_STAT_DD, ==,
                    E1000_TXD_STAT_DD);

    /* Check data sent to the backend */
    ret = recv(test_sockets[0], &recv_len, sizeof(recv_len), 0);
    g_assert_cmpint(ret, == , sizeof(recv_len));
    ret = recv(test_sockets[0], buffer, sizeof(buffer), 0);
    g_assert_cmpint(ret, ==, sizeof(buffer));
    g_assert_false(memcmp(buffer, &packet, sizeof(packet)));

    /* Free test data buffer */
    guest_free(alloc, data);
}

static void igb_receive_verify(QE1000E *d, int *test_sockets, QGuestAllocator *alloc)
{
    union e1000_adv_rx_desc descr;

    struct eth_header test_iov = packet;
    int len = htonl(sizeof(packet));
    struct iovec iov[] = {
        {
            .iov_base = &len,
            .iov_len = sizeof(len),
        },{
            .iov_base = &test_iov,
            .iov_len = sizeof(packet),
        },
    };

    char buffer[64];
    int ret;

    /* Send a dummy packet to device's socket*/
    ret = iov_send(test_sockets[0], iov, 2, 0, sizeof(len) + sizeof(packet));
    g_assert_cmpint(ret, == , sizeof(packet) + sizeof(len));

    /* Prepare test data buffer */
    uint64_t data = guest_alloc(alloc, sizeof(buffer));

    /* Prepare RX descriptor */
    memset(&descr, 0, sizeof(descr));
    descr.read.pkt_addr = cpu_to_le64(data);

    /* Put descriptor to the ring */
    e1000e_rx_ring_push(d, &descr);

    /* Wait for TX WB interrupt */
    e1000e_wait_isr(d, E1000E_RX0_MSG_ID);

    /* Check DD bit */
    g_assert_cmphex(le32_to_cpu(descr.wb.upper.status_error) &
        E1000_RXD_STAT_DD, ==, E1000_RXD_STAT_DD);

    /* Check data sent to the backend */
    memread(data, buffer, sizeof(buffer));
    g_assert_false(memcmp(buffer, &packet, sizeof(packet)));

    /* Free test data buffer */
    guest_free(alloc, data);
}

static void test_e1000e_init(void *obj, void *data, QGuestAllocator * alloc)
{
    /* init does nothing */
}

static void test_igb_tx(void *obj, void *data, QGuestAllocator * alloc)
{
    QE1000E_PCI *e1000e = obj;
    QE1000E *d = &e1000e->e1000e;
    QOSGraphObject *e_object = obj;
    QPCIDevice *dev = e_object->get_driver(e_object, "pci-device");

    /* FIXME: add spapr support */
    if (qpci_check_buggy_msi(dev)) {
        return;
    }

    igb_send_verify(d, data, alloc);
}

static void test_igb_rx(void *obj, void *data, QGuestAllocator * alloc)
{
    QE1000E_PCI *e1000e = obj;
    QE1000E *d = &e1000e->e1000e;
    QOSGraphObject *e_object = obj;
    QPCIDevice *dev = e_object->get_driver(e_object, "pci-device");

    /* FIXME: add spapr support */
    if (qpci_check_buggy_msi(dev)) {
        return;
    }

    igb_receive_verify(d, data, alloc);
}

static void test_igb_multiple_transfers(void *obj, void *data,
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
        igb_send_verify(d, data, alloc);
        igb_receive_verify(d, data, alloc);
    }

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

#endif

static void *data_test_init_no_socket(GString *cmd_line, void *arg)
{
    g_string_append(cmd_line, " -netdev hubport,hubid=0,id=hs0 ");
    return arg;
}

static void test_igb_hotplug(void *obj, void *data, QGuestAllocator * alloc)
{
    QTestState *qts = global_qtest;  /* TODO: get rid of global_qtest here */
    QE1000E_PCI *dev = obj;

    if (dev->pci_dev.bus->not_hotpluggable) {
        g_test_skip("pci bus does not support hotplug");
        return;
    }

    qtest_qmp_device_add(qts, "igb", "igb_net", "{'addr': '0x06'}");
    qpci_unplug_acpi_device_test(qts, "igb_net", 0x06);
}

static void register_igb_test(void)
{
    QOSGraphTestOptions opts = { 0 };

#ifndef _WIN32
    opts.before = data_test_init,
    qos_add_test("init", "igb", test_e1000e_init, &opts);
    qos_add_test("tx", "igb", test_igb_tx, &opts);
    qos_add_test("rx", "igb", test_igb_rx, &opts);
    qos_add_test("multiple_transfers", "igb",
                 test_igb_multiple_transfers, &opts);
#endif

    opts.before = data_test_init_no_socket;
    qos_add_test("hotplug", "igb", test_igb_hotplug, &opts);
}

libqos_init(register_igb_test);
