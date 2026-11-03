/*
 * BCM2838 PCIe Root Complex emulation
 *
 * Copyright (C) 2022 Ovchinnikov Vitalii <vitalii.ovchinnikov@auriga.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/core/irq.h"
#include "hw/pci-host/gpex.h"
#include "hw/pci/pci_host.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "hw/pci/pcie.h"
#include "hw/pci/pcie_aer.h"
#include "hw/arm/bcm2838_pcie.h"
#include "trace.h"

/*
 * The register bytes in root_port.regs[] are little-endian whatever the host
 * is, so access them with the ld*_le_p()/st*_le_p() helpers.
 */
static uint32_t bcm2838_pcie_ext_cfg_index(const uint8_t *root_regs)
{
    return ldl_le_p(root_regs + BCM2838_PCIE_EXT_CFG_INDEX
                    - PCIE_CONFIG_SPACE_SIZE);
}

/*
 * EXT_CFG_INDEX selects the bus/device/function (in ECAM address layout) that
 * the EXT_CFG_DATA window accesses; the offset within the window selects the
 * register in that function's config space.
 */
static PCIDevice *bcm2838_pcie_ext_cfg_dev(BCM2838PcieHostState *s,
                                           hwaddr data_offset,
                                           uint32_t *conf_offset)
{
    uint32_t mmcfg_addr = bcm2838_pcie_ext_cfg_index(s->root_port.regs)
        | PCIE_MMCFG_CONFOFFSET(data_offset);

    *conf_offset = PCIE_MMCFG_CONFOFFSET(mmcfg_addr);
    return pci_find_device(PCI_HOST_BRIDGE(s)->bus,
                           PCIE_MMCFG_BUS(mmcfg_addr),
                           PCIE_MMCFG_DEVFN(mmcfg_addr));
}

static uint64_t bcm2838_pcie_host_read(void *opaque, hwaddr offset,
                                       unsigned size) {
    uint64_t value = ~0;
    BCM2838PcieHostState *s = opaque;
    uint8_t *root_regs = s->root_port.regs;
    PCIDevice *pci_dev;
    uint32_t conf_offset;

    if (offset < PCIE_CONFIG_SPACE_SIZE) {
        /*
         * The first 4KB of the window is the root port's own PCI config
         * space (vendor/device ID, BARs, capabilities). Serve it from the
         * real PCIDevice, not from the raw regs[] shadow buffer, which is
         * only backing store for the controller registers above 4KB.
         */
        value = pci_host_config_read_common(PCI_DEVICE(&s->root_port),
                                            offset, PCIE_CONFIG_SPACE_SIZE,
                                            size);
    } else if (offset - PCIE_CONFIG_SPACE_SIZE + size
               <= sizeof(s->root_port.regs)) {
        switch (offset) {
        case BCM2838_PCIE_EXT_CFG_DATA
            ... BCM2838_PCIE_EXT_CFG_DATA + PCIE_CONFIG_SPACE_SIZE - 1:
            pci_dev = bcm2838_pcie_ext_cfg_dev(
                s, offset - BCM2838_PCIE_EXT_CFG_DATA, &conf_offset);
            if (pci_dev) {
                value = pci_host_config_read_common(
                    pci_dev, conf_offset, pci_config_size(pci_dev), size);
            }
            break;
        default:
            value = ldn_le_p(root_regs + offset - PCIE_CONFIG_SPACE_SIZE, size);
        }
    } else {
        qemu_log_mask(
            LOG_GUEST_ERROR,
            "%s: out-of-range access, %u bytes @ offset 0x%04" PRIx64 "\n",
            __func__, size, offset);
    }

    trace_bcm2838_pcie_host_read(size, offset, value);
    return value;
}

static void bcm2838_pcie_host_write(void *opaque, hwaddr offset,
                                    uint64_t value, unsigned size) {
    BCM2838PcieHostState *s = opaque;
    uint8_t *root_regs = s->root_port.regs;
    PCIDevice *pci_dev;
    uint32_t conf_offset;

    trace_bcm2838_pcie_host_write(size, offset, value);

    if (offset < PCIE_CONFIG_SPACE_SIZE) {
        /* Root port's own PCI config space -- see read path above */
        pci_host_config_write_common(PCI_DEVICE(&s->root_port), offset,
                                     PCIE_CONFIG_SPACE_SIZE, value, size);
    } else if (offset - PCIE_CONFIG_SPACE_SIZE + size
               <= sizeof(s->root_port.regs)) {
        switch (offset) {
        case BCM2838_PCIE_EXT_CFG_DATA
            ... BCM2838_PCIE_EXT_CFG_DATA + PCIE_CONFIG_SPACE_SIZE - 1:
            pci_dev = bcm2838_pcie_ext_cfg_dev(
                s, offset - BCM2838_PCIE_EXT_CFG_DATA, &conf_offset);
            if (pci_dev) {
                pci_host_config_write_common(
                    pci_dev, conf_offset, pci_config_size(pci_dev),
                    value, size);
            }
            break;
        default:
            stn_le_p(root_regs + offset - PCIE_CONFIG_SPACE_SIZE, size, value);
        }
    } else {
        qemu_log_mask(
            LOG_GUEST_ERROR,
            "%s: out-of-range access, %u bytes @ offset 0x%04" PRIx64 "\n",
            __func__, size, offset);
    }
}

static const MemoryRegionOps bcm2838_pcie_host_ops = {
    .read = bcm2838_pcie_host_read,
    .write = bcm2838_pcie_host_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {.max_access_size = 8},
};

static void bcm2838_pcie_host_set_irq(void *opaque, int irq_num, int level)
{
    BCM2838PcieHostState *s = opaque;

    qemu_set_irq(s->irq[irq_num], level);
}

static int bcm2838_pcie_host_map_irq(PCIDevice *pci_dev, int pin)
{
    /* Only slot 0 exists behind the root port: nothing to swizzle */
    return pin;
}

static void bcm2838_pcie_host_realize(DeviceState *dev, Error **errp)
{
    PCIHostState *pci = PCI_HOST_BRIDGE(dev);
    BCM2838PcieHostState *s = BCM2838_PCIE_HOST(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    int i;

    memory_region_init_io(&s->cfg_regs, OBJECT(s), &bcm2838_pcie_host_ops, s,
                          "bcm2838_pcie_cfg_regs", BCM2838_PCIE_REGS_SIZE);
    sysbus_init_mmio(sbd, &s->cfg_regs);

    /*
     * io_mmio and io_ioport are the PCI view of the PCI address space that we
     * pass to pci_register_root_bus(). The io_mmio_window and io_ioport_window
     * containers hold them as subregions and are what we expose as SysBus
     * MRs: accesses to addresses where no PCI device has been mapped read as
     * -1 and ignore writes.
     */
    memory_region_init(&s->io_mmio, OBJECT(s), "bcm2838_pcie_mmio", UINT64_MAX);
    memory_region_init(&s->io_ioport, OBJECT(s), "bcm2838_pcie_ioport",
                       64 * 1024);

    memory_region_init_io(&s->io_mmio_window, OBJECT(s),
                            &unassigned_io_ops, OBJECT(s),
                            "bcm2838_pcie_mmio_window", UINT64_MAX);
    memory_region_init_io(&s->io_ioport_window, OBJECT(s),
                            &unassigned_io_ops, OBJECT(s),
                            "bcm2838_pcie_ioport_window", 64 * 1024);

    memory_region_add_subregion(&s->io_mmio_window, 0, &s->io_mmio);
    memory_region_add_subregion(&s->io_ioport_window, 0, &s->io_ioport);
    sysbus_init_mmio(sbd, &s->io_mmio_window);
    sysbus_init_mmio(sbd, &s->io_ioport_window);

    for (i = 0; i < BCM2838_PCIE_NUM_IRQS; i++) {
        sysbus_init_irq(sbd, &s->irq[i]);
    }

    pci->bus = pci_register_root_bus(dev, "pcie.0", bcm2838_pcie_host_set_irq,
                                     bcm2838_pcie_host_map_irq, s, &s->io_mmio,
                                     &s->io_ioport, 0, BCM2838_PCIE_NUM_IRQS,
                                     TYPE_PCIE_BUS);
    qdev_realize(DEVICE(&s->root_port), BUS(pci->bus), &error_fatal);
}

static const char *bcm2838_pcie_host_root_bus_path(PCIHostState *host_bridge,
                                                   PCIBus *rootbus)
{
    return "0000:00";
}

static void bcm2838_pcie_host_class_init(ObjectClass *class, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIHostBridgeClass *hc = PCI_HOST_BRIDGE_CLASS(class);

    hc->root_bus_path = bcm2838_pcie_host_root_bus_path;
    dc->realize = bcm2838_pcie_host_realize;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->fw_name = "pci";
}

static void bcm2838_pcie_host_initfn(Object *obj)
{
    BCM2838PcieHostState *s = BCM2838_PCIE_HOST(obj);
    BCM2838PcieRootState *root = &s->root_port;

    object_initialize_child(obj, "root_port", root, TYPE_BCM2838_PCIE_ROOT);
    qdev_prop_set_int32(DEVICE(root), "addr", PCI_DEVFN(0, 0));
    qdev_prop_set_bit(DEVICE(root), "multifunction", false);
}

static const TypeInfo bcm2838_pcie_host_info = {
    .name       = TYPE_BCM2838_PCIE_HOST,
    .parent     = TYPE_PCIE_HOST_BRIDGE,
    .instance_size = sizeof(BCM2838PcieHostState),
    .instance_init = bcm2838_pcie_host_initfn,
    .class_init = bcm2838_pcie_host_class_init,
};

/*
 * RC root part (D0:F0)
 */

static void bcm2838_pcie_root_port_reset_hold(Object *obj, ResetType type)
{
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_GET_CLASS(obj);
    PCIDevice *dev = PCI_DEVICE(obj);
    BCM2838PcieRootState *s = BCM2838_PCIE_ROOT(dev);

    if (rpc->parent_phases.hold) {
        rpc->parent_phases.hold(obj, type);
    }

    memset(s->regs, 0xFF, sizeof(s->regs));
}

static void bcm2838_pcie_root_init(Object *obj)
{
    PCIBridge *br = PCI_BRIDGE(obj);
    br->bus_name = "pcie.1";
}

static const VMStateDescription vmstate_bcm2838_pcie_root = {
    .name = "bcm2838-pcie-root",
    .priority = MIG_PRI_PCI_BUS,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = pcie_cap_slot_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj.parent_obj.parent_obj, PCIESlot),
        VMSTATE_STRUCT(parent_obj.parent_obj.parent_obj.exp.aer_log,
                       PCIESlot, 0, vmstate_pcie_aer_log, PCIEAERLog),
        VMSTATE_UINT8_ARRAY(regs, BCM2838PcieRootState,
                            BCM2838_PCIE_REGS_SIZE - PCIE_CONFIG_SPACE_SIZE),
        VMSTATE_END_OF_LIST()
    }
};

static void bcm2838_pcie_root_class_init(ObjectClass *class, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);
    ResettableClass *rc = RESETTABLE_CLASS(class);
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_CLASS(class);

    dc->desc = "BCM2711 PCIe Bridge";
    dc->vmsd = &vmstate_bcm2838_pcie_root;

    resettable_class_set_parent_phases(rc, NULL,
                                       bcm2838_pcie_root_port_reset_hold,
                                       NULL, &rpc->parent_phases);

    /*
     * PCI-facing part of the host bridge, not usable without the host-facing
     * part, which can't be device_add'ed.
     */
    dc->user_creatable = false;
    k->vendor_id = BCM2838_PCIE_VENDOR_ID;
    k->device_id = BCM2838_PCIE_DEVICE_ID;
    k->revision = BCM2838_PCIE_REVISION;

    rpc->exp_offset = BCM2838_PCIE_EXP_CAP_OFFSET;
    rpc->aer_offset = BCM2838_PCIE_AER_CAP_OFFSET;
}

static const TypeInfo bcm2838_pcie_root_info = {
    .name = TYPE_BCM2838_PCIE_ROOT,
    .parent = TYPE_PCIE_ROOT_PORT,
    .instance_size = sizeof(BCM2838PcieRootState),
    .instance_init = bcm2838_pcie_root_init,
    .class_init = bcm2838_pcie_root_class_init,
};

static void bcm2838_pcie_register(void)
{
    type_register_static(&bcm2838_pcie_root_info);
    type_register_static(&bcm2838_pcie_host_info);
}

type_init(bcm2838_pcie_register)
