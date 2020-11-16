/*
* QEMU INTEL 82574 GbE NIC emulation
*
* Software developer's manuals:
* http://www.intel.com/content/dam/doc/datasheet/82574l-gbe-controller-datasheet.pdf
*
* Copyright (c) 2015 Ravello Systems LTD (http://ravellosystems.com)
* Developed by Daynix Computing LTD (http://www.daynix.com)
*
* Authors:
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
#include "net/net.h"
#include "net/tap.h"
#include "qemu/module.h"
#include "qemu/range.h"
#include "sysemu/sysemu.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

#include "e1000_regs.h"

#include "e1000x_common.h"
#include "e1000e_core.h"

#include "trace.h"
#include "qapi/error.h"
#include "qom/object.h"

#define TYPE_E1000E "e1000e"
OBJECT_DECLARE_SIMPLE_TYPE(E1000EState, E1000E)

struct E1000EState {
    PCIDevice parent_obj;
    NICState *nic;
    NICConf conf;

    MemoryRegion mmio;
    MemoryRegion flash;
    MemoryRegion io;
    MemoryRegion msix;

    uint32_t ioaddr;

    uint16_t subsys_ven;
    uint16_t subsys;

    uint16_t subsys_ven_used;
    uint16_t subsys_used;

    bool disable_vnet;

    E1000ECore core;

};

#define E1000E_MMIO_IDX     0
#define E1000E_FLASH_IDX    1
#define E1000E_IO_IDX       2
#define E1000E_MSIX_IDX     3

#define E1000E_MMIO_SIZE    (128 * KiB)
#define E1000E_FLASH_SIZE   (128 * KiB)
#define E1000E_IO_SIZE      (32)
#define E1000E_MSIX_SIZE    (16 * KiB)

#define E1000E_MSIX_TABLE   (0x0000)
#define E1000E_MSIX_PBA     (0x2000)

static uint64_t
e1000e_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    E1000EState *s = opaque;
    return e1000e_core_read(&s->core, addr, size);
}

static void
e1000e_mmio_write(void *opaque, hwaddr addr,
                   uint64_t val, unsigned size)
{
    E1000EState *s = opaque;
    e1000e_core_write(&s->core, addr, val, size);
}

static bool
e1000e_io_get_reg_index(E1000EState *s, uint32_t *idx)
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
e1000e_io_read(void *opaque, hwaddr addr, unsigned size)
{
    E1000EState *s = opaque;
    uint32_t idx = 0;
    uint64_t val;

    switch (addr) {
    case E1000_IOADDR:
        trace_e1000e_io_read_addr(s->ioaddr);
        return s->ioaddr;
    case E1000_IODATA:
        if (e1000e_io_get_reg_index(s, &idx)) {
            val = e1000e_core_read(&s->core, idx, sizeof(val));
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
e1000e_io_write(void *opaque, hwaddr addr,
                uint64_t val, unsigned size)
{
    E1000EState *s = opaque;
    uint32_t idx = 0;

    switch (addr) {
    case E1000_IOADDR:
        trace_e1000e_io_write_addr(val);
        s->ioaddr = (uint32_t) val;
        return;
    case E1000_IODATA:
        if (e1000e_io_get_reg_index(s, &idx)) {
            trace_e1000e_io_write_data(idx, val);
            e1000e_core_write(&s->core, idx, val, sizeof(val));
        }
        return;
    default:
        trace_e1000e_wrn_io_write_unknown(addr);
        return;
    }
}

static const MemoryRegionOps mmio_ops = {
    .read = e1000e_mmio_read,
    .write = e1000e_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps io_ops = {
    .read = e1000e_io_read,
    .write = e1000e_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static bool
e1000e_nc_can_receive(NetClientState *nc)
{
    E1000EState *s = qemu_get_nic_opaque(nc);
    return e1000e_can_receive(&s->core);
}

static ssize_t
e1000e_nc_receive_iov(NetClientState *nc, const struct iovec *iov, int iovcnt)
{
    E1000EState *s = qemu_get_nic_opaque(nc);
    return e1000e_receive_iov(&s->core, iov, iovcnt);
}

static ssize_t
e1000e_nc_receive(NetClientState *nc, const uint8_t *buf, size_t size)
{
    E1000EState *s = qemu_get_nic_opaque(nc);
    return e1000e_receive(&s->core, buf, size);
}

static void
e1000e_set_link_status(NetClientState *nc)
{
    E1000EState *s = qemu_get_nic_opaque(nc);
    e1000e_core_set_link_status(&s->core);
}

static NetClientInfo net_e1000e_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .can_receive = e1000e_nc_can_receive,
    .receive = e1000e_nc_receive,
    .receive_iov = e1000e_nc_receive_iov,
    .link_status_changed = e1000e_set_link_status,
};

/*
* EEPROM (NVM) contents documented in Table 36, section 6.1
* and generally 6.1.2 Software accessed words.
*/
static const uint16_t e1000e_eeprom_template[64] = {
  /*        Address        |    Compat.    | ImVer |   Compat.     */
    0x0000, 0x0000, 0x0000, 0x0420, 0xf746, 0x2010, 0xffff, 0xffff,
  /*      PBA      |ICtrl1 | SSID  | SVID  | DevID |-------|ICtrl2 */
    0x0000, 0x0000, 0x026b, 0x0000, 0x8086, 0x0000, 0x0000, 0x8058,
  /*    NVM words 1,2,3    |-------------------------------|PCI-EID*/
    0x0000, 0x2001, 0x7e7c, 0xffff, 0x1000, 0x00c8, 0x0000, 0x2704,
  /* PCIe Init. Conf 1,2,3 |PCICtrl|PHY|LD1|-------| RevID | LD0,2 */
    0x6cc9, 0x3150, 0x070e, 0x460b, 0x2d84, 0x0100, 0xf000, 0x0706,
  /* FLPAR |FLANADD|LAN-PWR|FlVndr |ICtrl3 |APTSMBA|APTRxEP|APTSMBC*/
    0x6000, 0x0080, 0x0f04, 0x7fff, 0x4f01, 0xc600, 0x0000, 0x20ff,
  /* APTIF | APTMC |APTuCP |LSWFWID|MSWFWID|NC-SIMC|NC-SIC | VPDP  */
    0x0028, 0x0003, 0x0000, 0x0000, 0x0000, 0x0003, 0x0000, 0xffff,
  /*                            SW Section                         */
    0x0100, 0xc000, 0x121c, 0xc007, 0xffff, 0xffff, 0xffff, 0xffff,
  /*                      SW Section                       |CHKSUM */
    0xffff, 0xffff, 0xffff, 0xffff, 0x0000, 0x0120, 0xffff, 0x0000,
};

static void e1000e_core_realize(E1000EState *s)
{
    s->core.owner = &s->parent_obj;
    s->core.owner_nic = s->nic;
}

static void
e1000e_unuse_msix_vectors(E1000EState *s, int num_vectors)
{
    int i;
    for (i = 0; i < num_vectors; i++) {
        msix_vector_unuse(PCI_DEVICE(s), i);
    }
}

static bool
e1000e_use_msix_vectors(E1000EState *s, int num_vectors)
{
    int i;
    for (i = 0; i < num_vectors; i++) {
        int res = msix_vector_use(PCI_DEVICE(s), i);
        if (res < 0) {
            trace_e1000e_msix_use_vector_fail(i, res);
            e1000e_unuse_msix_vectors(s, i);
            return false;
        }
    }
    return true;
}

static void
e1000e_init_msix(E1000EState *s)
{
    PCIDevice *d = PCI_DEVICE(s);
    int res = msix_init(PCI_DEVICE(s), E1000E_MSIX_VEC_NUM,
                        &s->msix,
                        E1000E_MSIX_IDX, E1000E_MSIX_TABLE,
                        &s->msix,
                        E1000E_MSIX_IDX, E1000E_MSIX_PBA,
                        0xA0, NULL);

    if (res < 0) {
        trace_e1000e_msix_init_fail(res);
    } else {
        if (!e1000e_use_msix_vectors(s, E1000E_MSIX_VEC_NUM)) {
            msix_uninit(d, &s->msix, &s->msix);
        }
    }
}

static void
e1000e_cleanup_msix(E1000EState *s)
{
    if (msix_present(PCI_DEVICE(s))) {
        e1000e_unuse_msix_vectors(s, E1000E_MSIX_VEC_NUM);
        msix_uninit(PCI_DEVICE(s), &s->msix, &s->msix);
    }
}

static void
e1000e_init_net_peer(E1000EState *s, PCIDevice *pci_dev, uint8_t *macaddr)
{
    DeviceState *dev = DEVICE(pci_dev);
    NetClientState *nc;
    int i;

    s->nic = qemu_new_nic(&net_e1000e_info, &s->conf,
        object_get_typename(OBJECT(s)), dev->id, s);

    s->core.max_queue_num = s->conf.peers.queues ? s->conf.peers.queues - 1 : 0;

    trace_e1000e_mac_set_permanent(MAC_ARG(macaddr));
    memcpy(s->core.permanent_mac, macaddr, sizeof(s->core.permanent_mac));

    qemu_format_nic_info_str(qemu_get_queue(s->nic), macaddr);

    /* Setup virtio headers */
    if (s->disable_vnet) {
        s->core.has_vnet = false;
        trace_e1000e_cfg_support_virtio(false);
        return;
    } else {
        s->core.has_vnet = true;
    }

    for (i = 0; i < s->conf.peers.queues; i++) {
        nc = qemu_get_subqueue(s->nic, i);
        if (!nc->peer || !qemu_has_vnet_hdr(nc->peer)) {
            s->core.has_vnet = false;
            trace_e1000e_cfg_support_virtio(false);
            return;
        }
    }

    trace_e1000e_cfg_support_virtio(true);

    for (i = 0; i < s->conf.peers.queues; i++) {
        nc = qemu_get_subqueue(s->nic, i);
        qemu_set_vnet_hdr_len(nc->peer, sizeof(struct virtio_net_hdr));
        qemu_using_vnet_hdr(nc->peer, true);
    }
}

static inline uint64_t
e1000e_gen_dsn(uint8_t *mac)
{
    return (uint64_t)(mac[5])        |
           (uint64_t)(mac[4])  << 8  |
           (uint64_t)(mac[3])  << 16 |
           (uint64_t)(0x00FF)  << 24 |
           (uint64_t)(0x00FF)  << 32 |
           (uint64_t)(mac[2])  << 40 |
           (uint64_t)(mac[1])  << 48 |
           (uint64_t)(mac[0])  << 56;
}

static int
e1000e_add_pm_capability(PCIDevice *pdev, uint8_t offset, uint16_t pmc)
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

static void e1000e_write_config(PCIDevice *pci_dev, uint32_t address,
                                uint32_t val, int len)
{
    E1000EState *s = E1000E(pci_dev);

    pci_default_write_config(pci_dev, address, val, len);

    if (range_covers_byte(address, len, PCI_COMMAND) &&
        (pci_dev->config[PCI_COMMAND] & PCI_COMMAND_MASTER)) {
        e1000e_start_recv(&s->core);
    }
}

static void e1000e_pci_realize(PCIDevice *pci_dev, Error **errp)
{
    static const uint16_t e1000e_pmrb_offset = 0x0C8;
    static const uint16_t e1000e_pcie_offset = 0x0E0;
    static const uint16_t e1000e_aer_offset =  0x100;
    static const uint16_t e1000e_dsn_offset =  0x140;
    E1000EState *s = E1000E(pci_dev);
    uint8_t *macaddr;
    int ret;

    trace_e1000e_cb_pci_realize();

    pci_dev->config_write = e1000e_write_config;

    pci_dev->config[PCI_CACHE_LINE_SIZE] = 0x10;
    pci_dev->config[PCI_INTERRUPT_PIN] = 1;

    pci_set_word(pci_dev->config + PCI_SUBSYSTEM_VENDOR_ID, s->subsys_ven);
    pci_set_word(pci_dev->config + PCI_SUBSYSTEM_ID, s->subsys);

    s->subsys_ven_used = s->subsys_ven;
    s->subsys_used = s->subsys;

    /* Define IO/MMIO regions */
    memory_region_init_io(&s->mmio, OBJECT(s), &mmio_ops, s,
                          "e1000e-mmio", E1000E_MMIO_SIZE);
    pci_register_bar(pci_dev, E1000E_MMIO_IDX,
                     PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);

    /*
     * We provide a dummy implementation for the flash BAR
     * for drivers that may theoretically probe for its presence.
     */
    memory_region_init(&s->flash, OBJECT(s),
                       "e1000e-flash", E1000E_FLASH_SIZE);
    pci_register_bar(pci_dev, E1000E_FLASH_IDX,
                     PCI_BASE_ADDRESS_SPACE_MEMORY, &s->flash);

    memory_region_init_io(&s->io, OBJECT(s), &io_ops, s,
                          "e1000e-io", E1000E_IO_SIZE);
    pci_register_bar(pci_dev, E1000E_IO_IDX,
                     PCI_BASE_ADDRESS_SPACE_IO, &s->io);

    memory_region_init(&s->msix, OBJECT(s), "e1000e-msix",
                       E1000E_MSIX_SIZE);
    pci_register_bar(pci_dev, E1000E_MSIX_IDX,
                     PCI_BASE_ADDRESS_SPACE_MEMORY, &s->msix);

    /* Create networking backend */
    qemu_macaddr_default_if_unset(&s->conf.macaddr);
    macaddr = s->conf.macaddr.a;

    e1000e_init_msix(s);

    if (pcie_endpoint_cap_v1_init(pci_dev, e1000e_pcie_offset) < 0) {
        hw_error("Failed to initialize PCIe capability");
    }

    ret = msi_init(PCI_DEVICE(s), 0xD0, 1, true, false, NULL);
    if (ret) {
        trace_e1000e_msi_init_fail(ret);
    }

    if (e1000e_add_pm_capability(pci_dev, e1000e_pmrb_offset,
                                  PCI_PM_CAP_DSI) < 0) {
        hw_error("Failed to initialize PM capability");
    }

    if (pcie_aer_init(pci_dev, PCI_ERR_VER, e1000e_aer_offset,
                      PCI_ERR_SIZEOF, NULL) < 0) {
        hw_error("Failed to initialize AER capability");
    }

    pcie_dev_ser_num_init(pci_dev, e1000e_dsn_offset,
                          e1000e_gen_dsn(macaddr));

    e1000e_init_net_peer(s, pci_dev, macaddr);

    /* Initialize core */
    e1000e_core_realize(s);

    e1000e_core_pci_realize(&s->core,
                            e1000e_eeprom_template,
                            sizeof(e1000e_eeprom_template),
                            macaddr);
}

static void e1000e_pci_uninit(PCIDevice *pci_dev)
{
    E1000EState *s = E1000E(pci_dev);

    trace_e1000e_cb_pci_uninit();

    e1000e_core_pci_uninit(&s->core);

    pcie_aer_exit(pci_dev);
    pcie_cap_exit(pci_dev);

    qemu_del_nic(s->nic);

    e1000e_cleanup_msix(s);
    msi_uninit(pci_dev);
}

static void e1000e_qdev_reset(DeviceState *dev)
{
    E1000EState *s = E1000E(dev);

    trace_e1000e_cb_qdev_reset();

    e1000e_core_reset(&s->core);
}

static int e1000e_pre_save(void *opaque)
{
    E1000EState *s = opaque;

    trace_e1000e_cb_pre_save();

    e1000e_core_pre_save(&s->core);

    return 0;
}

static int e1000e_post_load(void *opaque, int version_id)
{
    E1000EState *s = opaque;

    trace_e1000e_cb_post_load();

    if ((s->subsys != s->subsys_used) ||
        (s->subsys_ven != s->subsys_ven_used)) {
        fprintf(stderr,
            "ERROR: Cannot migrate while device properties "
            "(subsys/subsys_ven) differ");
        return -1;
    }

    return e1000e_core_post_load(&s->core);
}

static const VMStateDescription e1000e_vmstate_tx = {
    .name = "e1000e-tx",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(sum_needed, struct e1000e_tx),
        VMSTATE_UINT8(props.ipcss, struct e1000e_tx),
        VMSTATE_UINT8(props.ipcso, struct e1000e_tx),
        VMSTATE_UINT16(props.ipcse, struct e1000e_tx),
        VMSTATE_UINT8(props.tucss, struct e1000e_tx),
        VMSTATE_UINT8(props.tucso, struct e1000e_tx),
        VMSTATE_UINT16(props.tucse, struct e1000e_tx),
        VMSTATE_UINT8(props.hdr_len, struct e1000e_tx),
        VMSTATE_UINT16(props.mss, struct e1000e_tx),
        VMSTATE_UINT32(props.paylen, struct e1000e_tx),
        VMSTATE_INT8(props.ip, struct e1000e_tx),
        VMSTATE_INT8(props.tcp, struct e1000e_tx),
        VMSTATE_BOOL(props.tse, struct e1000e_tx),
        VMSTATE_BOOL(cptse, struct e1000e_tx),
        VMSTATE_BOOL(skip_cp, struct e1000e_tx),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription e1000e_vmstate_intr_timer = {
    .name = "e1000e-intr-timer",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_TIMER_PTR(timer, E1000IntrDelayTimer),
        VMSTATE_BOOL(running, E1000IntrDelayTimer),
        VMSTATE_END_OF_LIST()
    }
};

#define VMSTATE_E1000E_INTR_DELAY_TIMER(_f, _s)                     \
    VMSTATE_STRUCT(_f, _s, 0,                                       \
                   e1000e_vmstate_intr_timer, E1000IntrDelayTimer)

#define VMSTATE_E1000E_INTR_DELAY_TIMER_ARRAY(_f, _s, _num)         \
    VMSTATE_STRUCT_ARRAY(_f, _s, _num, 0,                           \
                         e1000e_vmstate_intr_timer, E1000IntrDelayTimer)

static const VMStateDescription e1000e_vmstate = {
    .name = "e1000e",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = e1000e_pre_save,
    .post_load = e1000e_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, E1000EState),
        VMSTATE_MSIX(parent_obj, E1000EState),

        VMSTATE_UINT32(ioaddr, E1000EState),
        VMSTATE_UINT32(core.rxbuf_min_shift, E1000EState),
        VMSTATE_UINT8(core.rx_desc_len, E1000EState),
        VMSTATE_UINT32_ARRAY(core.rxbuf_sizes, E1000EState,
                             E1000_PSRCTL_BUFFS_PER_DESC),
        VMSTATE_UINT32(core.rx_desc_buf_size, E1000EState),
        VMSTATE_UINT16_ARRAY(core.eeprom, E1000EState, E1000E_EEPROM_SIZE),
        VMSTATE_UINT16_2DARRAY(core.phy, E1000EState,
                               E1000E_PHY_PAGES, E1000E_PHY_PAGE_SIZE),
        VMSTATE_UINT32_ARRAY(core.mac, E1000EState, E1000E_MAC_SIZE),
        VMSTATE_UINT8_ARRAY(core.permanent_mac, E1000EState, ETH_ALEN),

        VMSTATE_UINT32(core.delayed_causes, E1000EState),

        VMSTATE_UINT16(subsys, E1000EState),
        VMSTATE_UINT16(subsys_ven, E1000EState),

        VMSTATE_E1000E_INTR_DELAY_TIMER(core.rdtr, E1000EState),
        VMSTATE_E1000E_INTR_DELAY_TIMER(core.radv, E1000EState),
        VMSTATE_E1000E_INTR_DELAY_TIMER(core.raid, E1000EState),
        VMSTATE_E1000E_INTR_DELAY_TIMER(core.tadv, E1000EState),
        VMSTATE_E1000E_INTR_DELAY_TIMER(core.tidv, E1000EState),

        VMSTATE_E1000E_INTR_DELAY_TIMER(core.itr, E1000EState),
        VMSTATE_BOOL(core.itr_intr_pending, E1000EState),

        VMSTATE_E1000E_INTR_DELAY_TIMER_ARRAY(core.eitr, E1000EState,
                                              E1000E_MSIX_VEC_NUM),
        VMSTATE_BOOL_ARRAY(core.eitr_intr_pending, E1000EState,
                           E1000E_MSIX_VEC_NUM),

        VMSTATE_UINT32(core.itr_guest_value, E1000EState),
        VMSTATE_UINT32_ARRAY(core.eitr_guest_value, E1000EState,
                             E1000E_MSIX_VEC_NUM),

        VMSTATE_UINT16(core.vet, E1000EState),

        VMSTATE_STRUCT_ARRAY(core.tx, E1000EState, E1000E_NUM_QUEUES, 0,
                             e1000e_vmstate_tx, struct e1000e_tx),
        VMSTATE_END_OF_LIST()
    }
};

static PropertyInfo e1000e_prop_disable_vnet,
                    e1000e_prop_subsys_ven,
                    e1000e_prop_subsys;

static Property e1000e_properties[] = {
    DEFINE_NIC_PROPERTIES(E1000EState, conf),
    DEFINE_PROP_SIGNED("disable_vnet_hdr", E1000EState, disable_vnet, false,
                        e1000e_prop_disable_vnet, bool),
    DEFINE_PROP_SIGNED("subsys_ven", E1000EState, subsys_ven,
                        PCI_VENDOR_ID_INTEL,
                        e1000e_prop_subsys_ven, uint16_t),
    DEFINE_PROP_SIGNED("subsys", E1000EState, subsys, 0,
                        e1000e_prop_subsys, uint16_t),
    DEFINE_PROP_END_OF_LIST(),
};

static void e1000e_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *c = PCI_DEVICE_CLASS(class);

    c->realize = e1000e_pci_realize;
    c->exit = e1000e_pci_uninit;
    c->vendor_id = PCI_VENDOR_ID_INTEL;
    c->device_id = E1000_DEV_ID_82574L;
    c->revision = 0;
    c->romfile = "efi-e1000e.rom";
    c->class_id = PCI_CLASS_NETWORK_ETHERNET;

    dc->desc = "Intel 82574L GbE Controller";
    dc->reset = e1000e_qdev_reset;
    dc->vmsd = &e1000e_vmstate;

    e1000e_prop_disable_vnet = qdev_prop_uint8;
    e1000e_prop_disable_vnet.description = "Do not use virtio headers, "
                                           "perform SW offloads emulation "
                                           "instead";

    e1000e_prop_subsys_ven = qdev_prop_uint16;
    e1000e_prop_subsys_ven.description = "PCI device Subsystem Vendor ID";

    e1000e_prop_subsys = qdev_prop_uint16;
    e1000e_prop_subsys.description = "PCI device Subsystem ID";

    device_class_set_props(dc, e1000e_properties);
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static void e1000e_instance_init(Object *obj)
{
    E1000EState *s = E1000E(obj);
    device_add_bootindex_property(obj, &s->conf.bootindex,
                                  "bootindex", "/ethernet-phy@0",
                                  DEVICE(obj));
}

static const TypeInfo e1000e_info = {
    .name = TYPE_E1000E,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(E1000EState),
    .class_init = e1000e_class_init,
    .instance_init = e1000e_instance_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { }
    },
};

static void e1000e_register_types(void)
{
    type_register_static(&e1000e_info);
}

type_init(e1000e_register_types)
