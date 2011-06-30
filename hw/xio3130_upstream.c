/*
 * xio3130_upstream.c
 * TI X3130 pci express upstream port switch
 *
 * Copyright (c) 2010 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "pci_ids.h"
#include "msi.h"
#include "pcie.h"
#include "xio3130_upstream.h"

#define PCI_DEVICE_ID_TI_XIO3130U       0x8232  /* upstream port */
#define XIO3130_REVISION                0x2
#define XIO3130_MSI_OFFSET              0x70
#define XIO3130_MSI_SUPPORTED_FLAGS     PCI_MSI_FLAGS_64BIT
#define XIO3130_MSI_NR_VECTOR           1
#define XIO3130_SSVID_OFFSET            0x80
#define XIO3130_SSVID_SVID              0
#define XIO3130_SSVID_SSID              0
#define XIO3130_EXP_OFFSET              0x90
#define XIO3130_AER_OFFSET              0x100

static void xio3130_upstream_write_config(PCIDevice *d, uint32_t address,
                                          uint32_t val, int len)
{
    pci_bridge_write_config(d, address, val, len);
    pcie_cap_flr_write_config(d, address, val, len);
    msi_write_config(d, address, val, len);
    pcie_aer_write_config(d, address, val, len);
}

static void xio3130_upstream_reset(DeviceState *qdev)
{
    PCIDevice *d = DO_UPCAST(PCIDevice, qdev, qdev);
    msi_reset(d);
    pci_bridge_reset(qdev);
    pcie_cap_deverr_reset(d);
}

static int xio3130_upstream_initfn(PCIDevice *d)
{
    PCIBridge* br = DO_UPCAST(PCIBridge, dev, d);
    PCIEPort *p = DO_UPCAST(PCIEPort, br, br);
    int rc;
    int tmp;

    rc = pci_bridge_initfn(d);
    if (rc < 0) {
        return rc;
    }

    pcie_port_init_reg(d);

    rc = msi_init(d, XIO3130_MSI_OFFSET, XIO3130_MSI_NR_VECTOR,
                  XIO3130_MSI_SUPPORTED_FLAGS & PCI_MSI_FLAGS_64BIT,
                  XIO3130_MSI_SUPPORTED_FLAGS & PCI_MSI_FLAGS_MASKBIT);
    if (rc < 0) {
        goto err_bridge;
    }
    rc = pci_bridge_ssvid_init(d, XIO3130_SSVID_OFFSET,
                               XIO3130_SSVID_SVID, XIO3130_SSVID_SSID);
    if (rc < 0) {
        goto err_bridge;
    }
    rc = pcie_cap_init(d, XIO3130_EXP_OFFSET, PCI_EXP_TYPE_UPSTREAM,
                       p->port);
    if (rc < 0) {
        goto err_msi;
    }
    pcie_cap_flr_init(d);
    pcie_cap_deverr_init(d);
    rc = pcie_aer_init(d, XIO3130_AER_OFFSET);
    if (rc < 0) {
        goto err;
    }

    return 0;

err:
    pcie_cap_exit(d);
err_msi:
    msi_uninit(d);
err_bridge:
    tmp =  pci_bridge_exitfn(d);
    assert(!tmp);
    return rc;
}

static int xio3130_upstream_exitfn(PCIDevice *d)
{
    pcie_aer_exit(d);
    pcie_cap_exit(d);
    msi_uninit(d);
    return pci_bridge_exitfn(d);
}

PCIEPort *xio3130_upstream_init(PCIBus *bus, int devfn, bool multifunction,
                             const char *bus_name, pci_map_irq_fn map_irq,
                             uint8_t port)
{
    PCIDevice *d;
    PCIBridge *br;
    DeviceState *qdev;

    d = pci_create_multifunction(bus, devfn, multifunction, "x3130-upstream");
    if (!d) {
        return NULL;
    }
    br = DO_UPCAST(PCIBridge, dev, d);

    qdev = &br->dev.qdev;
    pci_bridge_map_irq(br, bus_name, map_irq);
    qdev_prop_set_uint8(qdev, "port", port);
    qdev_init_nofail(qdev);

    return DO_UPCAST(PCIEPort, br, br);
}

static const VMStateDescription vmstate_xio3130_upstream = {
    .name = "xio3130-express-upstream-port",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCIE_DEVICE(br.dev, PCIEPort),
        VMSTATE_STRUCT(br.dev.exp.aer_log, PCIEPort, 0, vmstate_pcie_aer_log,
                       PCIEAERLog),
        VMSTATE_END_OF_LIST()
    }
};

static PCIDeviceInfo xio3130_upstream_info = {
    .qdev.name = "x3130-upstream",
    .qdev.desc = "TI X3130 Upstream Port of PCI Express Switch",
    .qdev.size = sizeof(PCIEPort),
    .qdev.reset = xio3130_upstream_reset,
    .qdev.vmsd = &vmstate_xio3130_upstream,

    .is_express = 1,
    .is_bridge = 1,
    .config_write = xio3130_upstream_write_config,
    .init = xio3130_upstream_initfn,
    .exit = xio3130_upstream_exitfn,
    .vendor_id = PCI_VENDOR_ID_TI,
    .device_id = PCI_DEVICE_ID_TI_XIO3130U,
    .revision = XIO3130_REVISION,

    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT8("port", PCIEPort, port, 0),
        DEFINE_PROP_UINT16("aer_log_max", PCIEPort, br.dev.exp.aer_log.log_max,
                           PCIE_AER_LOG_MAX_DEFAULT),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void xio3130_upstream_register(void)
{
    pci_qdev_register(&xio3130_upstream_info);
}

device_init(xio3130_upstream_register);


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 8
 *  indent-tab-mode: nil
 * End:
 */
