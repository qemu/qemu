/*
 * libqos driver framework
 *
 * Copyright (c) 2022-2023 Red Hat, Inc.
 * Copyright (c) 2018 Emanuele Giuseppe Esposito <e.emanuelegiuseppe@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#include "qemu/osdep.h"
#include "hw/net/igb_regs.h"
#include "hw/net/mii.h"
#include "hw/pci/pci_ids.h"
#include "../libqtest.h"
#include "pci-pc.h"
#include "qemu/sockets.h"
#include "qemu/iov.h"
#include "qemu/module.h"
#include "qemu/bitops.h"
#include "libqos-malloc.h"
#include "qgraph.h"
#include "e1000e.h"

#define IGB_IVAR_TEST_CFG \
    ((E1000E_RX0_MSG_ID | E1000_IVAR_VALID) << (igb_ivar_entry_rx(0) * 8)   | \
     ((E1000E_TX0_MSG_ID | E1000_IVAR_VALID) << (igb_ivar_entry_tx(0) * 8)))

#define E1000E_RING_LEN (0x1000)

static void e1000e_foreach_callback(QPCIDevice *dev, int devfn, void *data)
{
    QPCIDevice *res = data;
    memcpy(res, dev, sizeof(QPCIDevice));
    g_free(dev);
}

static void e1000e_pci_destructor(QOSGraphObject *obj)
{
    QE1000E_PCI *epci = (QE1000E_PCI *) obj;
    qpci_iounmap(&epci->pci_dev, epci->mac_regs);
    qpci_msix_disable(&epci->pci_dev);
}

static void igb_pci_start_hw(QOSGraphObject *obj)
{
    static const uint8_t address[] = E1000E_ADDRESS;
    QE1000E_PCI *d = (QE1000E_PCI *) obj;
    uint32_t val;

    /* Enable the device */
    qpci_device_enable(&d->pci_dev);

    /* Reset the device */
    val = e1000e_macreg_read(&d->e1000e, E1000_CTRL);
    e1000e_macreg_write(&d->e1000e, E1000_CTRL, val | E1000_CTRL_RST | E1000_CTRL_SLU);

    /* Setup link */
    e1000e_macreg_write(&d->e1000e, E1000_MDIC,
                        MII_BMCR_AUTOEN | MII_BMCR_ANRESTART |
                        (MII_BMCR << E1000_MDIC_REG_SHIFT) |
                        (1 << E1000_MDIC_PHY_SHIFT) |
                        E1000_MDIC_OP_WRITE);

    qtest_clock_step(d->pci_dev.bus->qts, 900000000);

    /* Enable and configure MSI-X */
    qpci_msix_enable(&d->pci_dev);
    e1000e_macreg_write(&d->e1000e, E1000_IVAR0, IGB_IVAR_TEST_CFG);

    /* Check the device link status */
    val = e1000e_macreg_read(&d->e1000e, E1000_STATUS);
    g_assert_cmphex(val & E1000_STATUS_LU, ==, E1000_STATUS_LU);

    /* Initialize TX/RX logic */
    e1000e_macreg_write(&d->e1000e, E1000_RCTL, 0);
    e1000e_macreg_write(&d->e1000e, E1000_TCTL, 0);

    e1000e_macreg_write(&d->e1000e, E1000_TDBAL(0),
                           (uint32_t) d->e1000e.tx_ring);
    e1000e_macreg_write(&d->e1000e, E1000_TDBAH(0),
                           (uint32_t) (d->e1000e.tx_ring >> 32));
    e1000e_macreg_write(&d->e1000e, E1000_TDLEN(0), E1000E_RING_LEN);
    e1000e_macreg_write(&d->e1000e, E1000_TDT(0), 0);
    e1000e_macreg_write(&d->e1000e, E1000_TDH(0), 0);

    /* Enable transmit */
    e1000e_macreg_write(&d->e1000e, E1000_TCTL, E1000_TCTL_EN);

    e1000e_macreg_write(&d->e1000e, E1000_RDBAL(0),
                           (uint32_t)d->e1000e.rx_ring);
    e1000e_macreg_write(&d->e1000e, E1000_RDBAH(0),
                           (uint32_t)(d->e1000e.rx_ring >> 32));
    e1000e_macreg_write(&d->e1000e, E1000_RDLEN(0), E1000E_RING_LEN);
    e1000e_macreg_write(&d->e1000e, E1000_RDT(0), 0);
    e1000e_macreg_write(&d->e1000e, E1000_RDH(0), 0);
    e1000e_macreg_write(&d->e1000e, E1000_RA,
                        ldl_le_p(address));
    e1000e_macreg_write(&d->e1000e, E1000_RA + 4,
                        E1000_RAH_AV | E1000_RAH_POOL_1 |
                        lduw_le_p(address + 4));

    /* Set supported receive descriptor mode */
    e1000e_macreg_write(&d->e1000e,
                        E1000_SRRCTL(0),
                        E1000_SRRCTL_DESCTYPE_ADV_ONEBUF);

    /* Enable receive */
    e1000e_macreg_write(&d->e1000e, E1000_RFCTL, E1000_RFCTL_EXTEN);
    e1000e_macreg_write(&d->e1000e, E1000_RCTL, E1000_RCTL_EN);

    /* Enable all interrupts */
    e1000e_macreg_write(&d->e1000e, E1000_GPIE,  E1000_GPIE_MSIX_MODE);
    e1000e_macreg_write(&d->e1000e, E1000_IMS,  0xFFFFFFFF);
    e1000e_macreg_write(&d->e1000e, E1000_EIMS, 0xFFFFFFFF);

}

static void *igb_pci_get_driver(void *obj, const char *interface)
{
    QE1000E_PCI *epci = obj;
    if (!g_strcmp0(interface, "igb-if")) {
        return &epci->e1000e;
    }

    /* implicit contains */
    if (!g_strcmp0(interface, "pci-device")) {
        return &epci->pci_dev;
    }

    fprintf(stderr, "%s not present in igb\n", interface);
    g_assert_not_reached();
}

static void *igb_pci_create(void *pci_bus, QGuestAllocator *alloc, void *addr)
{
    QE1000E_PCI *d = g_new0(QE1000E_PCI, 1);
    QPCIBus *bus = pci_bus;
    QPCIAddress *address = addr;

    qpci_device_foreach(bus, address->vendor_id, address->device_id,
                        e1000e_foreach_callback, &d->pci_dev);

    /* Map BAR0 (mac registers) */
    d->mac_regs = qpci_iomap(&d->pci_dev, 0, NULL);

    /* Allocate and setup TX ring */
    d->e1000e.tx_ring = guest_alloc(alloc, E1000E_RING_LEN);
    g_assert(d->e1000e.tx_ring != 0);

    /* Allocate and setup RX ring */
    d->e1000e.rx_ring = guest_alloc(alloc, E1000E_RING_LEN);
    g_assert(d->e1000e.rx_ring != 0);

    d->obj.get_driver = igb_pci_get_driver;
    d->obj.start_hw = igb_pci_start_hw;
    d->obj.destructor = e1000e_pci_destructor;

    return &d->obj;
}

static void igb_register_nodes(void)
{
    QPCIAddress addr = {
        .vendor_id = PCI_VENDOR_ID_INTEL,
        .device_id = E1000_DEV_ID_82576,
    };

    /*
     * FIXME: every test using this node needs to setup a -netdev socket,id=hs0
     * otherwise QEMU is not going to start
     */
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "netdev=hs0",
    };
    add_qpci_address(&opts, &addr);

    qos_node_create_driver("igb", igb_pci_create);
    qos_node_consumes("igb", "pci-bus", &opts);
}

libqos_init(igb_register_nodes);
