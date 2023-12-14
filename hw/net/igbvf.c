/*
 * QEMU Intel 82576 SR/IOV Ethernet Controller Emulation
 *
 * Datasheet:
 * https://www.intel.com/content/dam/www/public/us/en/documents/datasheets/82576eg-gbe-datasheet.pdf
 *
 * Copyright (c) 2020-2023 Red Hat, Inc.
 * Copyright (c) 2015 Ravello Systems LTD (http://ravellosystems.com)
 * Developed by Daynix Computing LTD (http://www.daynix.com)
 *
 * Authors:
 * Akihiko Odaki <akihiko.odaki@daynix.com>
 * Gal Hammmer <gal.hammer@sap.com>
 * Marcel Apfelbaum <marcel.apfelbaum@gmail.com>
 * Dmitry Fleytman <dmitry@daynix.com>
 * Leonid Bloch <leonid@daynix.com>
 * Yan Vugenfirer <yan@daynix.com>
 *
 * Based on work done by:
 * Nir Peleg, Tutis Systems Ltd. for Qumranet Inc.
 * Copyright (c) 2008 Qumranet
 * Based on work done by:
 * Copyright (c) 2007 Dan Aloni
 * Copyright (c) 2004 Antony T Curtis
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
#include "hw/hw.h"
#include "hw/net/mii.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/pcie.h"
#include "hw/pci/msix.h"
#include "net/eth.h"
#include "net/net.h"
#include "igb_common.h"
#include "igb_core.h"
#include "trace.h"
#include "qapi/error.h"

OBJECT_DECLARE_SIMPLE_TYPE(IgbVfState, IGBVF)

struct IgbVfState {
    PCIDevice parent_obj;

    MemoryRegion mmio;
    MemoryRegion msix;
};

static hwaddr vf_to_pf_addr(hwaddr addr, uint16_t vfn, bool write)
{
    switch (addr) {
    case E1000_CTRL:
    case E1000_CTRL_DUP:
        return E1000_PVTCTRL(vfn);
    case E1000_EICS:
        return E1000_PVTEICS(vfn);
    case E1000_EIMS:
        return E1000_PVTEIMS(vfn);
    case E1000_EIMC:
        return E1000_PVTEIMC(vfn);
    case E1000_EIAC:
        return E1000_PVTEIAC(vfn);
    case E1000_EIAM:
        return E1000_PVTEIAM(vfn);
    case E1000_EICR:
        return E1000_PVTEICR(vfn);
    case E1000_EITR(0):
    case E1000_EITR(1):
    case E1000_EITR(2):
        return E1000_EITR(22) + (addr - E1000_EITR(0)) - vfn * 0xC;
    case E1000_IVAR0:
        return E1000_VTIVAR + vfn * 4;
    case E1000_IVAR_MISC:
        return E1000_VTIVAR_MISC + vfn * 4;
    case 0x0F04: /* PBACL */
        return E1000_PBACLR;
    case 0x0F0C: /* PSRTYPE */
        return E1000_PSRTYPE(vfn);
    case E1000_V2PMAILBOX(0):
        return E1000_V2PMAILBOX(vfn);
    case E1000_VMBMEM(0) ... E1000_VMBMEM(0) + 0x3F:
        return addr + vfn * 0x40;
    case E1000_RDBAL_A(0):
        return E1000_RDBAL(vfn);
    case E1000_RDBAL_A(1):
        return E1000_RDBAL(vfn + IGB_MAX_VF_FUNCTIONS);
    case E1000_RDBAH_A(0):
        return E1000_RDBAH(vfn);
    case E1000_RDBAH_A(1):
        return E1000_RDBAH(vfn + IGB_MAX_VF_FUNCTIONS);
    case E1000_RDLEN_A(0):
        return E1000_RDLEN(vfn);
    case E1000_RDLEN_A(1):
        return E1000_RDLEN(vfn + IGB_MAX_VF_FUNCTIONS);
    case E1000_SRRCTL_A(0):
        return E1000_SRRCTL(vfn);
    case E1000_SRRCTL_A(1):
        return E1000_SRRCTL(vfn + IGB_MAX_VF_FUNCTIONS);
    case E1000_RDH_A(0):
        return E1000_RDH(vfn);
    case E1000_RDH_A(1):
        return E1000_RDH(vfn + IGB_MAX_VF_FUNCTIONS);
    case E1000_RXCTL_A(0):
        return E1000_RXCTL(vfn);
    case E1000_RXCTL_A(1):
        return E1000_RXCTL(vfn + IGB_MAX_VF_FUNCTIONS);
    case E1000_RDT_A(0):
        return E1000_RDT(vfn);
    case E1000_RDT_A(1):
        return E1000_RDT(vfn + IGB_MAX_VF_FUNCTIONS);
    case E1000_RXDCTL_A(0):
        return E1000_RXDCTL(vfn);
    case E1000_RXDCTL_A(1):
        return E1000_RXDCTL(vfn + IGB_MAX_VF_FUNCTIONS);
    case E1000_RQDPC_A(0):
        return E1000_RQDPC(vfn);
    case E1000_RQDPC_A(1):
        return E1000_RQDPC(vfn + IGB_MAX_VF_FUNCTIONS);
    case E1000_TDBAL_A(0):
        return E1000_TDBAL(vfn);
    case E1000_TDBAL_A(1):
        return E1000_TDBAL(vfn + IGB_MAX_VF_FUNCTIONS);
    case E1000_TDBAH_A(0):
        return E1000_TDBAH(vfn);
    case E1000_TDBAH_A(1):
        return E1000_TDBAH(vfn + IGB_MAX_VF_FUNCTIONS);
    case E1000_TDLEN_A(0):
        return E1000_TDLEN(vfn);
    case E1000_TDLEN_A(1):
        return E1000_TDLEN(vfn + IGB_MAX_VF_FUNCTIONS);
    case E1000_TDH_A(0):
        return E1000_TDH(vfn);
    case E1000_TDH_A(1):
        return E1000_TDH(vfn + IGB_MAX_VF_FUNCTIONS);
    case E1000_TXCTL_A(0):
        return E1000_TXCTL(vfn);
    case E1000_TXCTL_A(1):
        return E1000_TXCTL(vfn + IGB_MAX_VF_FUNCTIONS);
    case E1000_TDT_A(0):
        return E1000_TDT(vfn);
    case E1000_TDT_A(1):
        return E1000_TDT(vfn + IGB_MAX_VF_FUNCTIONS);
    case E1000_TXDCTL_A(0):
        return E1000_TXDCTL(vfn);
    case E1000_TXDCTL_A(1):
        return E1000_TXDCTL(vfn + IGB_MAX_VF_FUNCTIONS);
    case E1000_TDWBAL_A(0):
        return E1000_TDWBAL(vfn);
    case E1000_TDWBAL_A(1):
        return E1000_TDWBAL(vfn + IGB_MAX_VF_FUNCTIONS);
    case E1000_TDWBAH_A(0):
        return E1000_TDWBAH(vfn);
    case E1000_TDWBAH_A(1):
        return E1000_TDWBAH(vfn + IGB_MAX_VF_FUNCTIONS);
    case E1000_VFGPRC:
        return E1000_PVFGPRC(vfn);
    case E1000_VFGPTC:
        return E1000_PVFGPTC(vfn);
    case E1000_VFGORC:
        return E1000_PVFGORC(vfn);
    case E1000_VFGOTC:
        return E1000_PVFGOTC(vfn);
    case E1000_VFMPRC:
        return E1000_PVFMPRC(vfn);
    case E1000_VFGPRLBC:
        return E1000_PVFGPRLBC(vfn);
    case E1000_VFGPTLBC:
        return E1000_PVFGPTLBC(vfn);
    case E1000_VFGORLBC:
        return E1000_PVFGORLBC(vfn);
    case E1000_VFGOTLBC:
        return E1000_PVFGOTLBC(vfn);
    case E1000_STATUS:
    case E1000_FRTIMER:
        if (write) {
            return HWADDR_MAX;
        }
        /* fallthrough */
    case 0x34E8: /* PBTWAC */
    case 0x24E8: /* PBRWAC */
        return addr;
    }

    trace_igbvf_wrn_io_addr_unknown(addr);

    return HWADDR_MAX;
}

static void igbvf_write_config(PCIDevice *dev, uint32_t addr, uint32_t val,
    int len)
{
    trace_igbvf_write_config(addr, val, len);
    pci_default_write_config(dev, addr, val, len);
    if (object_property_get_bool(OBJECT(pcie_sriov_get_pf(dev)),
                                 "x-pcie-flr-init", &error_abort)) {
        pcie_cap_flr_write_config(dev, addr, val, len);
    }
}

static uint64_t igbvf_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIDevice *vf = PCI_DEVICE(opaque);
    PCIDevice *pf = pcie_sriov_get_pf(vf);

    addr = vf_to_pf_addr(addr, pcie_sriov_vf_number(vf), false);
    return addr == HWADDR_MAX ? 0 : igb_mmio_read(pf, addr, size);
}

static void igbvf_mmio_write(void *opaque, hwaddr addr, uint64_t val,
    unsigned size)
{
    PCIDevice *vf = PCI_DEVICE(opaque);
    PCIDevice *pf = pcie_sriov_get_pf(vf);

    addr = vf_to_pf_addr(addr, pcie_sriov_vf_number(vf), true);
    if (addr != HWADDR_MAX) {
        igb_mmio_write(pf, addr, val, size);
    }
}

static const MemoryRegionOps mmio_ops = {
    .read = igbvf_mmio_read,
    .write = igbvf_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void igbvf_pci_realize(PCIDevice *dev, Error **errp)
{
    IgbVfState *s = IGBVF(dev);
    int ret;
    int i;

    dev->config_write = igbvf_write_config;

    memory_region_init_io(&s->mmio, OBJECT(dev), &mmio_ops, s, "igbvf-mmio",
        IGBVF_MMIO_SIZE);
    pcie_sriov_vf_register_bar(dev, IGBVF_MMIO_BAR_IDX, &s->mmio);

    memory_region_init(&s->msix, OBJECT(dev), "igbvf-msix", IGBVF_MSIX_SIZE);
    pcie_sriov_vf_register_bar(dev, IGBVF_MSIX_BAR_IDX, &s->msix);

    ret = msix_init(dev, IGBVF_MSIX_VEC_NUM, &s->msix, IGBVF_MSIX_BAR_IDX, 0,
        &s->msix, IGBVF_MSIX_BAR_IDX, 0x2000, 0x70, errp);
    if (ret) {
        return;
    }

    for (i = 0; i < IGBVF_MSIX_VEC_NUM; i++) {
        msix_vector_use(dev, i);
    }

    if (pcie_endpoint_cap_init(dev, 0xa0) < 0) {
        hw_error("Failed to initialize PCIe capability");
    }

    if (object_property_get_bool(OBJECT(pcie_sriov_get_pf(dev)),
                                 "x-pcie-flr-init", &error_abort)) {
        pcie_cap_flr_init(dev);
    }

    if (pcie_aer_init(dev, 1, 0x100, 0x40, errp) < 0) {
        hw_error("Failed to initialize AER capability");
    }

    pcie_ari_init(dev, 0x150);
}

static void igbvf_qdev_reset_hold(Object *obj)
{
    PCIDevice *vf = PCI_DEVICE(obj);

    igb_vf_reset(pcie_sriov_get_pf(vf), pcie_sriov_vf_number(vf));
}

static void igbvf_pci_uninit(PCIDevice *dev)
{
    IgbVfState *s = IGBVF(dev);

    pcie_aer_exit(dev);
    pcie_cap_exit(dev);
    msix_unuse_all_vectors(dev);
    msix_uninit(dev, &s->msix, &s->msix);
}

static void igbvf_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *c = PCI_DEVICE_CLASS(class);
    ResettableClass *rc = RESETTABLE_CLASS(class);

    c->realize = igbvf_pci_realize;
    c->exit = igbvf_pci_uninit;
    c->vendor_id = PCI_VENDOR_ID_INTEL;
    c->device_id = E1000_DEV_ID_82576_VF;
    c->revision = 1;
    c->class_id = PCI_CLASS_NETWORK_ETHERNET;

    rc->phases.hold = igbvf_qdev_reset_hold;

    dc->desc = "Intel 82576 Virtual Function";
    dc->user_creatable = false;

    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static const TypeInfo igbvf_info = {
    .name = TYPE_IGBVF,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(IgbVfState),
    .class_init = igbvf_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { }
    },
};

static void igb_register_types(void)
{
    type_register_static(&igbvf_info);
}

type_init(igb_register_types)
