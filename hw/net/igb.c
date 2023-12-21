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
#include "qemu/units.h"
#include "net/eth.h"
#include "net/net.h"
#include "net/tap.h"
#include "qemu/module.h"
#include "qemu/range.h"
#include "sysemu/sysemu.h"
#include "hw/hw.h"
#include "hw/net/mii.h"
#include "hw/pci/pci.h"
#include "hw/pci/pcie.h"
#include "hw/pci/pcie_sriov.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

#include "igb_common.h"
#include "igb_core.h"

#include "trace.h"
#include "qapi/error.h"
#include "qom/object.h"

#define TYPE_IGB "igb"
OBJECT_DECLARE_SIMPLE_TYPE(IGBState, IGB)

struct IGBState {
    PCIDevice parent_obj;
    NICState *nic;
    NICConf conf;

    MemoryRegion mmio;
    MemoryRegion flash;
    MemoryRegion io;
    MemoryRegion msix;

    uint32_t ioaddr;

    IGBCore core;
    bool has_flr;
};

#define IGB_CAP_SRIOV_OFFSET    (0x160)
#define IGB_VF_OFFSET           (0x80)
#define IGB_VF_STRIDE           (2)

#define E1000E_MMIO_IDX     0
#define E1000E_FLASH_IDX    1
#define E1000E_IO_IDX       2
#define E1000E_MSIX_IDX     3

#define E1000E_MMIO_SIZE    (128 * KiB)
#define E1000E_FLASH_SIZE   (128 * KiB)
#define E1000E_IO_SIZE      (32)
#define E1000E_MSIX_SIZE    (16 * KiB)

static void igb_write_config(PCIDevice *dev, uint32_t addr,
    uint32_t val, int len)
{
    IGBState *s = IGB(dev);

    trace_igb_write_config(addr, val, len);
    pci_default_write_config(dev, addr, val, len);
    if (s->has_flr) {
        pcie_cap_flr_write_config(dev, addr, val, len);
    }

    if (range_covers_byte(addr, len, PCI_COMMAND) &&
        (dev->config[PCI_COMMAND] & PCI_COMMAND_MASTER)) {
        igb_start_recv(&s->core);
    }
}

uint64_t
igb_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    IGBState *s = opaque;
    return igb_core_read(&s->core, addr, size);
}

void
igb_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    IGBState *s = opaque;
    igb_core_write(&s->core, addr, val, size);
}

void igb_vf_reset(void *opaque, uint16_t vfn)
{
    IGBState *s = opaque;
    igb_core_vf_reset(&s->core, vfn);
}

static bool
igb_io_get_reg_index(IGBState *s, uint32_t *idx)
{
    if (s->ioaddr < 0x1FFFF) {
        *idx = s->ioaddr;
        return true;
    }

    if (s->ioaddr < 0x7FFFF) {
        trace_e1000e_wrn_io_addr_undefined(s->ioaddr);
        return false;
    }

    if (s->ioaddr < 0xFFFFF) {
        trace_e1000e_wrn_io_addr_flash(s->ioaddr);
        return false;
    }

    trace_e1000e_wrn_io_addr_unknown(s->ioaddr);
    return false;
}

static uint64_t
igb_io_read(void *opaque, hwaddr addr, unsigned size)
{
    IGBState *s = opaque;
    uint32_t idx = 0;
    uint64_t val;

    switch (addr) {
    case E1000_IOADDR:
        trace_e1000e_io_read_addr(s->ioaddr);
        return s->ioaddr;
    case E1000_IODATA:
        if (igb_io_get_reg_index(s, &idx)) {
            val = igb_core_read(&s->core, idx, sizeof(val));
            trace_e1000e_io_read_data(idx, val);
            return val;
        }
        return 0;
    default:
        trace_e1000e_wrn_io_read_unknown(addr);
        return 0;
    }
}

static void
igb_io_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    IGBState *s = opaque;
    uint32_t idx = 0;

    switch (addr) {
    case E1000_IOADDR:
        trace_e1000e_io_write_addr(val);
        s->ioaddr = (uint32_t) val;
        return;
    case E1000_IODATA:
        if (igb_io_get_reg_index(s, &idx)) {
            trace_e1000e_io_write_data(idx, val);
            igb_core_write(&s->core, idx, val, sizeof(val));
        }
        return;
    default:
        trace_e1000e_wrn_io_write_unknown(addr);
        return;
    }
}

static const MemoryRegionOps mmio_ops = {
    .read = igb_mmio_read,
    .write = igb_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps io_ops = {
    .read = igb_io_read,
    .write = igb_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static bool
igb_nc_can_receive(NetClientState *nc)
{
    IGBState *s = qemu_get_nic_opaque(nc);
    return igb_can_receive(&s->core);
}

static ssize_t
igb_nc_receive_iov(NetClientState *nc, const struct iovec *iov, int iovcnt)
{
    IGBState *s = qemu_get_nic_opaque(nc);
    return igb_receive_iov(&s->core, iov, iovcnt);
}

static ssize_t
igb_nc_receive(NetClientState *nc, const uint8_t *buf, size_t size)
{
    IGBState *s = qemu_get_nic_opaque(nc);
    return igb_receive(&s->core, buf, size);
}

static void
igb_set_link_status(NetClientState *nc)
{
    IGBState *s = qemu_get_nic_opaque(nc);
    igb_core_set_link_status(&s->core);
}

static NetClientInfo net_igb_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .can_receive = igb_nc_can_receive,
    .receive = igb_nc_receive,
    .receive_iov = igb_nc_receive_iov,
    .link_status_changed = igb_set_link_status,
};

/*
 * EEPROM (NVM) contents documented in section 6.1, table 6-1:
 * and in 6.10 Software accessed words.
 */
static const uint16_t igb_eeprom_template[] = {
  /*        Address        |Compat.|OEM sp.| ImRev |    OEM sp.    */
    0x0000, 0x0000, 0x0000, 0x0d34, 0xffff, 0x2010, 0xffff, 0xffff,
  /*      PBA      |ICtrl1 | SSID  | SVID  | DevID |-------|ICtrl2 */
    0x1040, 0xffff, 0x002b, 0x0000, 0x8086, 0x10c9, 0x0000, 0x70c3,
  /* SwPin0| DevID | EESZ  |-------|ICtrl3 |PCI-tc | MSIX  | APtr  */
    0x0004, 0x10c9, 0x5c00, 0x0000, 0x2880, 0x0014, 0x4a40, 0x0060,
  /* PCIe Init. Conf 1,2,3 |PCICtrl| LD1,3 |DDevID |DevRev | LD0,2 */
    0x6cfb, 0xc7b0, 0x0abe, 0x0403, 0x0783, 0x10a6, 0x0001, 0x0602,
  /* SwPin1| FunC  |LAN-PWR|ManHwC |ICtrl3 | IOVct |VDevID |-------*/
    0x0004, 0x0020, 0x0000, 0x004a, 0x2080, 0x00f5, 0x10ca, 0x0000,
  /*---------------| LD1,3 | LD0,2 | ROEnd | ROSta | Wdog  | VPD   */
    0x0000, 0x0000, 0x4784, 0x4602, 0x0000, 0x0000, 0x1000, 0xffff,
  /* PCSet0| Ccfg0 |PXEver |IBAcap |PCSet1 | Ccfg1 |iSCVer | ??    */
    0x0100, 0x4000, 0x131f, 0x4013, 0x0100, 0x4000, 0xffff, 0xffff,
  /* PCSet2| Ccfg2 |PCSet3 | Ccfg3 | ??    |AltMacP| ??    |CHKSUM */
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0x00e0, 0xffff, 0x0000,
  /* NC-SIC */
    0x0003,
};

static void igb_core_realize(IGBState *s)
{
    s->core.owner = &s->parent_obj;
    s->core.owner_nic = s->nic;
}

static void
igb_init_msix(IGBState *s)
{
    int i, res;

    res = msix_init(PCI_DEVICE(s), IGB_MSIX_VEC_NUM,
                    &s->msix,
                    E1000E_MSIX_IDX, 0,
                    &s->msix,
                    E1000E_MSIX_IDX, 0x2000,
                    0x70, NULL);

    if (res < 0) {
        trace_e1000e_msix_init_fail(res);
    } else {
        for (i = 0; i < IGB_MSIX_VEC_NUM; i++) {
            msix_vector_use(PCI_DEVICE(s), i);
        }
    }
}

static void
igb_cleanup_msix(IGBState *s)
{
    msix_unuse_all_vectors(PCI_DEVICE(s));
    msix_uninit(PCI_DEVICE(s), &s->msix, &s->msix);
}

static void
igb_init_net_peer(IGBState *s, PCIDevice *pci_dev, uint8_t *macaddr)
{
    DeviceState *dev = DEVICE(pci_dev);
    NetClientState *nc;
    int i;

    s->nic = qemu_new_nic(&net_igb_info, &s->conf,
        object_get_typename(OBJECT(s)), dev->id, &dev->mem_reentrancy_guard, s);

    s->core.max_queue_num = s->conf.peers.queues ? s->conf.peers.queues - 1 : 0;

    trace_e1000e_mac_set_permanent(MAC_ARG(macaddr));
    memcpy(s->core.permanent_mac, macaddr, sizeof(s->core.permanent_mac));

    qemu_format_nic_info_str(qemu_get_queue(s->nic), macaddr);

    /* Setup virtio headers */
    for (i = 0; i < s->conf.peers.queues; i++) {
        nc = qemu_get_subqueue(s->nic, i);
        if (!nc->peer || !qemu_has_vnet_hdr(nc->peer)) {
            trace_e1000e_cfg_support_virtio(false);
            return;
        }
    }

    trace_e1000e_cfg_support_virtio(true);
    s->core.has_vnet = true;

    for (i = 0; i < s->conf.peers.queues; i++) {
        nc = qemu_get_subqueue(s->nic, i);
        qemu_set_vnet_hdr_len(nc->peer, sizeof(struct virtio_net_hdr));
        qemu_using_vnet_hdr(nc->peer, true);
    }
}

static int
igb_add_pm_capability(PCIDevice *pdev, uint8_t offset, uint16_t pmc)
{
    Error *local_err = NULL;
    int ret = pci_add_capability(pdev, PCI_CAP_ID_PM, offset,
                                 PCI_PM_SIZEOF, &local_err);

    if (local_err) {
        error_report_err(local_err);
        return ret;
    }

    pci_set_word(pdev->config + offset + PCI_PM_PMC,
                 PCI_PM_CAP_VER_1_1 |
                 pmc);

    pci_set_word(pdev->wmask + offset + PCI_PM_CTRL,
                 PCI_PM_CTRL_STATE_MASK |
                 PCI_PM_CTRL_PME_ENABLE |
                 PCI_PM_CTRL_DATA_SEL_MASK);

    pci_set_word(pdev->w1cmask + offset + PCI_PM_CTRL,
                 PCI_PM_CTRL_PME_STATUS);

    return ret;
}

static void igb_pci_realize(PCIDevice *pci_dev, Error **errp)
{
    IGBState *s = IGB(pci_dev);
    uint8_t *macaddr;
    int ret;

    trace_e1000e_cb_pci_realize();

    pci_dev->config_write = igb_write_config;

    pci_dev->config[PCI_CACHE_LINE_SIZE] = 0x10;
    pci_dev->config[PCI_INTERRUPT_PIN] = 1;

    /* Define IO/MMIO regions */
    memory_region_init_io(&s->mmio, OBJECT(s), &mmio_ops, s,
                          "igb-mmio", E1000E_MMIO_SIZE);
    pci_register_bar(pci_dev, E1000E_MMIO_IDX,
                     PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);

    /*
     * We provide a dummy implementation for the flash BAR
     * for drivers that may theoretically probe for its presence.
     */
    memory_region_init(&s->flash, OBJECT(s),
                       "igb-flash", E1000E_FLASH_SIZE);
    pci_register_bar(pci_dev, E1000E_FLASH_IDX,
                     PCI_BASE_ADDRESS_SPACE_MEMORY, &s->flash);

    memory_region_init_io(&s->io, OBJECT(s), &io_ops, s,
                          "igb-io", E1000E_IO_SIZE);
    pci_register_bar(pci_dev, E1000E_IO_IDX,
                     PCI_BASE_ADDRESS_SPACE_IO, &s->io);

    memory_region_init(&s->msix, OBJECT(s), "igb-msix",
                       E1000E_MSIX_SIZE);
    pci_register_bar(pci_dev, E1000E_MSIX_IDX,
                     PCI_BASE_ADDRESS_MEM_TYPE_64, &s->msix);

    /* Create networking backend */
    qemu_macaddr_default_if_unset(&s->conf.macaddr);
    macaddr = s->conf.macaddr.a;

    /* Add PCI capabilities in reverse order */
    assert(pcie_endpoint_cap_init(pci_dev, 0xa0) > 0);

    igb_init_msix(s);

    ret = msi_init(pci_dev, 0x50, 1, true, true, NULL);
    if (ret) {
        trace_e1000e_msi_init_fail(ret);
    }

    if (igb_add_pm_capability(pci_dev, 0x40, PCI_PM_CAP_DSI) < 0) {
        hw_error("Failed to initialize PM capability");
    }

    /* PCIe extended capabilities (in order) */
    if (s->has_flr) {
        pcie_cap_flr_init(pci_dev);
    }

    if (pcie_aer_init(pci_dev, 1, 0x100, 0x40, errp) < 0) {
        hw_error("Failed to initialize AER capability");
    }

    pcie_ari_init(pci_dev, 0x150);

    pcie_sriov_pf_init(pci_dev, IGB_CAP_SRIOV_OFFSET, TYPE_IGBVF,
        IGB_82576_VF_DEV_ID, IGB_MAX_VF_FUNCTIONS, IGB_MAX_VF_FUNCTIONS,
        IGB_VF_OFFSET, IGB_VF_STRIDE);

    pcie_sriov_pf_init_vf_bar(pci_dev, IGBVF_MMIO_BAR_IDX,
        PCI_BASE_ADDRESS_MEM_TYPE_64 | PCI_BASE_ADDRESS_MEM_PREFETCH,
        IGBVF_MMIO_SIZE);
    pcie_sriov_pf_init_vf_bar(pci_dev, IGBVF_MSIX_BAR_IDX,
        PCI_BASE_ADDRESS_MEM_TYPE_64 | PCI_BASE_ADDRESS_MEM_PREFETCH,
        IGBVF_MSIX_SIZE);

    igb_init_net_peer(s, pci_dev, macaddr);

    /* Initialize core */
    igb_core_realize(s);

    igb_core_pci_realize(&s->core,
                         igb_eeprom_template,
                         sizeof(igb_eeprom_template),
                         macaddr);
}

static void igb_pci_uninit(PCIDevice *pci_dev)
{
    IGBState *s = IGB(pci_dev);

    trace_e1000e_cb_pci_uninit();

    igb_core_pci_uninit(&s->core);

    pcie_sriov_pf_exit(pci_dev);
    pcie_cap_exit(pci_dev);

    qemu_del_nic(s->nic);

    igb_cleanup_msix(s);
    msi_uninit(pci_dev);
}

static void igb_qdev_reset_hold(Object *obj)
{
    PCIDevice *d = PCI_DEVICE(obj);
    IGBState *s = IGB(obj);

    trace_e1000e_cb_qdev_reset_hold();

    pcie_sriov_pf_disable_vfs(d);
    igb_core_reset(&s->core);
}

static int igb_pre_save(void *opaque)
{
    IGBState *s = opaque;

    trace_e1000e_cb_pre_save();

    igb_core_pre_save(&s->core);

    return 0;
}

static int igb_post_load(void *opaque, int version_id)
{
    IGBState *s = opaque;

    trace_e1000e_cb_post_load();
    return igb_core_post_load(&s->core);
}

static const VMStateDescription igb_vmstate_tx_ctx = {
    .name = "igb-tx-ctx",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(vlan_macip_lens, struct e1000_adv_tx_context_desc),
        VMSTATE_UINT32(seqnum_seed, struct e1000_adv_tx_context_desc),
        VMSTATE_UINT32(type_tucmd_mlhl, struct e1000_adv_tx_context_desc),
        VMSTATE_UINT32(mss_l4len_idx, struct e1000_adv_tx_context_desc),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription igb_vmstate_tx = {
    .name = "igb-tx",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(ctx, struct igb_tx, 2, 0, igb_vmstate_tx_ctx,
                             struct e1000_adv_tx_context_desc),
        VMSTATE_UINT32(first_cmd_type_len, struct igb_tx),
        VMSTATE_UINT32(first_olinfo_status, struct igb_tx),
        VMSTATE_BOOL(first, struct igb_tx),
        VMSTATE_BOOL(skip_cp, struct igb_tx),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription igb_vmstate_intr_timer = {
    .name = "igb-intr-timer",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_TIMER_PTR(timer, IGBIntrDelayTimer),
        VMSTATE_BOOL(running, IGBIntrDelayTimer),
        VMSTATE_END_OF_LIST()
    }
};

#define VMSTATE_IGB_INTR_DELAY_TIMER(_f, _s)                        \
    VMSTATE_STRUCT(_f, _s, 0,                                       \
                   igb_vmstate_intr_timer, IGBIntrDelayTimer)

#define VMSTATE_IGB_INTR_DELAY_TIMER_ARRAY(_f, _s, _num)            \
    VMSTATE_STRUCT_ARRAY(_f, _s, _num, 0,                           \
                         igb_vmstate_intr_timer, IGBIntrDelayTimer)

static const VMStateDescription igb_vmstate = {
    .name = "igb",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = igb_pre_save,
    .post_load = igb_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, IGBState),
        VMSTATE_MSIX(parent_obj, IGBState),

        VMSTATE_UINT32(ioaddr, IGBState),
        VMSTATE_UINT8(core.rx_desc_len, IGBState),
        VMSTATE_UINT16_ARRAY(core.eeprom, IGBState, IGB_EEPROM_SIZE),
        VMSTATE_UINT16_ARRAY(core.phy, IGBState, MAX_PHY_REG_ADDRESS + 1),
        VMSTATE_UINT32_ARRAY(core.mac, IGBState, E1000E_MAC_SIZE),
        VMSTATE_UINT8_ARRAY(core.permanent_mac, IGBState, ETH_ALEN),

        VMSTATE_IGB_INTR_DELAY_TIMER_ARRAY(core.eitr, IGBState,
                                           IGB_INTR_NUM),

        VMSTATE_UINT32_ARRAY(core.eitr_guest_value, IGBState, IGB_INTR_NUM),

        VMSTATE_STRUCT_ARRAY(core.tx, IGBState, IGB_NUM_QUEUES, 0,
                             igb_vmstate_tx, struct igb_tx),

        VMSTATE_INT64(core.timadj, IGBState),

        VMSTATE_END_OF_LIST()
    }
};

static Property igb_properties[] = {
    DEFINE_NIC_PROPERTIES(IGBState, conf),
    DEFINE_PROP_BOOL("x-pcie-flr-init", IGBState, has_flr, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void igb_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    ResettableClass *rc = RESETTABLE_CLASS(class);
    PCIDeviceClass *c = PCI_DEVICE_CLASS(class);

    c->realize = igb_pci_realize;
    c->exit = igb_pci_uninit;
    c->vendor_id = PCI_VENDOR_ID_INTEL;
    c->device_id = E1000_DEV_ID_82576;
    c->revision = 1;
    c->class_id = PCI_CLASS_NETWORK_ETHERNET;

    rc->phases.hold = igb_qdev_reset_hold;

    dc->desc = "Intel 82576 Gigabit Ethernet Controller";
    dc->vmsd = &igb_vmstate;

    device_class_set_props(dc, igb_properties);
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static void igb_instance_init(Object *obj)
{
    IGBState *s = IGB(obj);
    device_add_bootindex_property(obj, &s->conf.bootindex,
                                  "bootindex", "/ethernet-phy@0",
                                  DEVICE(obj));
}

static const TypeInfo igb_info = {
    .name = TYPE_IGB,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(IGBState),
    .class_init = igb_class_init,
    .instance_init = igb_instance_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { }
    },
};

static void igb_register_types(void)
{
    type_register_static(&igb_info);
}

type_init(igb_register_types)
