/*
 * Copyright (c) 2018, Impinj, Inc.
 *
 * Designware PCIe IP block emulation
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
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "hw/pci/msi.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pci_host.h"
#include "hw/pci/pcie_port.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "hw/irq.h"
#include "hw/pci-host/designware.h"

#define DESIGNWARE_PCIE_PORT_LINK_CONTROL          0x710
#define DESIGNWARE_PCIE_PHY_DEBUG_R1               0x72C
#define DESIGNWARE_PCIE_PHY_DEBUG_R1_XMLH_LINK_UP  BIT(4)
#define DESIGNWARE_PCIE_LINK_WIDTH_SPEED_CONTROL   0x80C
#define DESIGNWARE_PCIE_PORT_LOGIC_SPEED_CHANGE    BIT(17)
#define DESIGNWARE_PCIE_MSI_ADDR_LO                0x820
#define DESIGNWARE_PCIE_MSI_ADDR_HI                0x824
#define DESIGNWARE_PCIE_MSI_INTR0_ENABLE           0x828
#define DESIGNWARE_PCIE_MSI_INTR0_MASK             0x82C
#define DESIGNWARE_PCIE_MSI_INTR0_STATUS           0x830
#define DESIGNWARE_PCIE_ATU_VIEWPORT               0x900
#define DESIGNWARE_PCIE_ATU_REGION_INBOUND         BIT(31)
#define DESIGNWARE_PCIE_ATU_CR1                    0x904
#define DESIGNWARE_PCIE_ATU_TYPE_MEM               (0x0 << 0)
#define DESIGNWARE_PCIE_ATU_CR2                    0x908
#define DESIGNWARE_PCIE_ATU_ENABLE                 BIT(31)
#define DESIGNWARE_PCIE_ATU_LOWER_BASE             0x90C
#define DESIGNWARE_PCIE_ATU_UPPER_BASE             0x910
#define DESIGNWARE_PCIE_ATU_LIMIT                  0x914
#define DESIGNWARE_PCIE_ATU_LOWER_TARGET           0x918
#define DESIGNWARE_PCIE_ATU_BUS(x)                 (((x) >> 24) & 0xff)
#define DESIGNWARE_PCIE_ATU_DEVFN(x)               (((x) >> 16) & 0xff)
#define DESIGNWARE_PCIE_ATU_UPPER_TARGET           0x91C

#define DESIGNWARE_PCIE_IRQ_MSI                    3

static DesignwarePCIEHost *
designware_pcie_root_to_host(DesignwarePCIERoot *root)
{
    BusState *bus = qdev_get_parent_bus(DEVICE(root));
    return DESIGNWARE_PCIE_HOST(bus->parent);
}

static uint64_t designware_pcie_root_msi_read(void *opaque, hwaddr addr,
                                              unsigned size)
{
    /*
     * Attempts to read from the MSI address are undefined in
     * the PCI specifications. For this hardware, the datasheet
     * specifies that a read from the magic address is simply not
     * intercepted by the MSI controller, and will go out to the
     * AHB/AXI bus like any other PCI-device-initiated DMA read.
     * This is not trivial to implement in QEMU, so since
     * well-behaved guests won't ever ask a PCI device to DMA from
     * this address we just log the missing functionality.
     */
    qemu_log_mask(LOG_UNIMP, "%s not implemented\n", __func__);
    return 0;
}

static void designware_pcie_root_msi_write(void *opaque, hwaddr addr,
                                           uint64_t val, unsigned len)
{
    DesignwarePCIERoot *root = DESIGNWARE_PCIE_ROOT(opaque);
    DesignwarePCIEHost *host = designware_pcie_root_to_host(root);

    root->msi.intr[0].status |= BIT(val) & root->msi.intr[0].enable;

    if (root->msi.intr[0].status & ~root->msi.intr[0].mask) {
        qemu_set_irq(host->pci.irqs[DESIGNWARE_PCIE_IRQ_MSI], 1);
    }
}

static const MemoryRegionOps designware_pci_host_msi_ops = {
    .read = designware_pcie_root_msi_read,
    .write = designware_pcie_root_msi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void designware_pcie_root_update_msi_mapping(DesignwarePCIERoot *root)

{
    MemoryRegion *mem   = &root->msi.iomem;
    const uint64_t base = root->msi.base;
    const bool enable   = root->msi.intr[0].enable;

    memory_region_set_address(mem, base);
    memory_region_set_enabled(mem, enable);
}

static DesignwarePCIEViewport *
designware_pcie_root_get_current_viewport(DesignwarePCIERoot *root)
{
    const unsigned int idx = root->atu_viewport & 0xF;
    const unsigned int dir =
        !!(root->atu_viewport & DESIGNWARE_PCIE_ATU_REGION_INBOUND);
    return &root->viewports[dir][idx];
}

static uint32_t
designware_pcie_root_config_read(PCIDevice *d, uint32_t address, int len)
{
    DesignwarePCIERoot *root = DESIGNWARE_PCIE_ROOT(d);
    DesignwarePCIEViewport *viewport =
        designware_pcie_root_get_current_viewport(root);

    uint32_t val;

    switch (address) {
    case DESIGNWARE_PCIE_PORT_LINK_CONTROL:
        /*
         * Linux guest uses this register only to configure number of
         * PCIE lane (which in our case is irrelevant) and doesn't
         * really care about the value it reads from this register
         */
        val = 0xDEADBEEF;
        break;

    case DESIGNWARE_PCIE_LINK_WIDTH_SPEED_CONTROL:
        /*
         * To make sure that any code in guest waiting for speed
         * change does not time out we always report
         * PORT_LOGIC_SPEED_CHANGE as set
         */
        val = DESIGNWARE_PCIE_PORT_LOGIC_SPEED_CHANGE;
        break;

    case DESIGNWARE_PCIE_MSI_ADDR_LO:
        val = root->msi.base;
        break;

    case DESIGNWARE_PCIE_MSI_ADDR_HI:
        val = root->msi.base >> 32;
        break;

    case DESIGNWARE_PCIE_MSI_INTR0_ENABLE:
        val = root->msi.intr[0].enable;
        break;

    case DESIGNWARE_PCIE_MSI_INTR0_MASK:
        val = root->msi.intr[0].mask;
        break;

    case DESIGNWARE_PCIE_MSI_INTR0_STATUS:
        val = root->msi.intr[0].status;
        break;

    case DESIGNWARE_PCIE_PHY_DEBUG_R1:
        val = DESIGNWARE_PCIE_PHY_DEBUG_R1_XMLH_LINK_UP;
        break;

    case DESIGNWARE_PCIE_ATU_VIEWPORT:
        val = root->atu_viewport;
        break;

    case DESIGNWARE_PCIE_ATU_LOWER_BASE:
        val = viewport->base;
        break;

    case DESIGNWARE_PCIE_ATU_UPPER_BASE:
        val = viewport->base >> 32;
        break;

    case DESIGNWARE_PCIE_ATU_LOWER_TARGET:
        val = viewport->target;
        break;

    case DESIGNWARE_PCIE_ATU_UPPER_TARGET:
        val = viewport->target >> 32;
        break;

    case DESIGNWARE_PCIE_ATU_LIMIT:
        val = viewport->limit;
        break;

    case DESIGNWARE_PCIE_ATU_CR1:
    case DESIGNWARE_PCIE_ATU_CR2:
        val = viewport->cr[(address - DESIGNWARE_PCIE_ATU_CR1) /
                           sizeof(uint32_t)];
        break;

    default:
        val = pci_default_read_config(d, address, len);
        break;
    }

    return val;
}

static uint64_t designware_pcie_root_data_access(void *opaque, hwaddr addr,
                                                 uint64_t *val, unsigned len)
{
    DesignwarePCIEViewport *viewport = opaque;
    DesignwarePCIERoot *root = viewport->root;

    const uint8_t busnum = DESIGNWARE_PCIE_ATU_BUS(viewport->target);
    const uint8_t devfn  = DESIGNWARE_PCIE_ATU_DEVFN(viewport->target);
    PCIBus    *pcibus    = pci_get_bus(PCI_DEVICE(root));
    PCIDevice *pcidev    = pci_find_device(pcibus, busnum, devfn);

    if (pcidev) {
        addr &= pci_config_size(pcidev) - 1;

        if (val) {
            pci_host_config_write_common(pcidev, addr,
                                         pci_config_size(pcidev),
                                         *val, len);
        } else {
            return pci_host_config_read_common(pcidev, addr,
                                               pci_config_size(pcidev),
                                               len);
        }
    }

    return UINT64_MAX;
}

static uint64_t designware_pcie_root_data_read(void *opaque, hwaddr addr,
                                               unsigned len)
{
    return designware_pcie_root_data_access(opaque, addr, NULL, len);
}

static void designware_pcie_root_data_write(void *opaque, hwaddr addr,
                                            uint64_t val, unsigned len)
{
    designware_pcie_root_data_access(opaque, addr, &val, len);
}

static const MemoryRegionOps designware_pci_host_conf_ops = {
    .read = designware_pcie_root_data_read,
    .write = designware_pcie_root_data_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void designware_pcie_update_viewport(DesignwarePCIERoot *root,
                                            DesignwarePCIEViewport *viewport)
{
    const uint64_t target = viewport->target;
    const uint64_t base   = viewport->base;
    const uint64_t size   = (uint64_t)viewport->limit - base + 1;
    const bool enabled    = viewport->cr[1] & DESIGNWARE_PCIE_ATU_ENABLE;

    MemoryRegion *current, *other;

    if (viewport->cr[0] == DESIGNWARE_PCIE_ATU_TYPE_MEM) {
        current = &viewport->mem;
        other   = &viewport->cfg;
        memory_region_set_alias_offset(current, target);
    } else {
        current = &viewport->cfg;
        other   = &viewport->mem;
    }

    /*
     * An outbound viewport can be reconfigure from being MEM to CFG,
     * to account for that we disable the "other" memory region that
     * becomes unused due to that fact.
     */
    memory_region_set_enabled(other, false);
    if (enabled) {
        memory_region_set_size(current, size);
        memory_region_set_address(current, base);
    }
    memory_region_set_enabled(current, enabled);
}

static void designware_pcie_root_config_write(PCIDevice *d, uint32_t address,
                                              uint32_t val, int len)
{
    DesignwarePCIERoot *root = DESIGNWARE_PCIE_ROOT(d);
    DesignwarePCIEHost *host = designware_pcie_root_to_host(root);
    DesignwarePCIEViewport *viewport =
        designware_pcie_root_get_current_viewport(root);

    switch (address) {
    case DESIGNWARE_PCIE_PORT_LINK_CONTROL:
    case DESIGNWARE_PCIE_LINK_WIDTH_SPEED_CONTROL:
    case DESIGNWARE_PCIE_PHY_DEBUG_R1:
        /* No-op */
        break;

    case DESIGNWARE_PCIE_MSI_ADDR_LO:
        root->msi.base &= 0xFFFFFFFF00000000ULL;
        root->msi.base |= val;
        designware_pcie_root_update_msi_mapping(root);
        break;

    case DESIGNWARE_PCIE_MSI_ADDR_HI:
        root->msi.base &= 0x00000000FFFFFFFFULL;
        root->msi.base |= (uint64_t)val << 32;
        designware_pcie_root_update_msi_mapping(root);
        break;

    case DESIGNWARE_PCIE_MSI_INTR0_ENABLE:
        root->msi.intr[0].enable = val;
        designware_pcie_root_update_msi_mapping(root);
        break;

    case DESIGNWARE_PCIE_MSI_INTR0_MASK:
        root->msi.intr[0].mask = val;
        break;

    case DESIGNWARE_PCIE_MSI_INTR0_STATUS:
        root->msi.intr[0].status ^= val;
        if (!root->msi.intr[0].status) {
            qemu_set_irq(host->pci.irqs[DESIGNWARE_PCIE_IRQ_MSI], 0);
        }
        break;

    case DESIGNWARE_PCIE_ATU_VIEWPORT:
        root->atu_viewport = val;
        break;

    case DESIGNWARE_PCIE_ATU_LOWER_BASE:
        viewport->base &= 0xFFFFFFFF00000000ULL;
        viewport->base |= val;
        break;

    case DESIGNWARE_PCIE_ATU_UPPER_BASE:
        viewport->base &= 0x00000000FFFFFFFFULL;
        viewport->base |= (uint64_t)val << 32;
        break;

    case DESIGNWARE_PCIE_ATU_LOWER_TARGET:
        viewport->target &= 0xFFFFFFFF00000000ULL;
        viewport->target |= val;
        break;

    case DESIGNWARE_PCIE_ATU_UPPER_TARGET:
        viewport->target &= 0x00000000FFFFFFFFULL;
        viewport->target |= val;
        break;

    case DESIGNWARE_PCIE_ATU_LIMIT:
        viewport->limit = val;
        break;

    case DESIGNWARE_PCIE_ATU_CR1:
        viewport->cr[0] = val;
        break;
    case DESIGNWARE_PCIE_ATU_CR2:
        viewport->cr[1] = val;
        designware_pcie_update_viewport(root, viewport);
        break;

    default:
        pci_bridge_write_config(d, address, val, len);
        break;
    }
}

static char *designware_pcie_viewport_name(const char *direction,
                                           unsigned int i,
                                           const char *type)
{
    return g_strdup_printf("PCI %s Viewport %u [%s]",
                           direction, i, type);
}

static void designware_pcie_root_realize(PCIDevice *dev, Error **errp)
{
    DesignwarePCIERoot *root = DESIGNWARE_PCIE_ROOT(dev);
    DesignwarePCIEHost *host = designware_pcie_root_to_host(root);
    MemoryRegion *address_space = &host->pci.memory;
    PCIBridge *br = PCI_BRIDGE(dev);
    DesignwarePCIEViewport *viewport;
    /*
     * Dummy values used for initial configuration of MemoryRegions
     * that belong to a given viewport
     */
    const hwaddr dummy_offset = 0;
    const uint64_t dummy_size = 4;
    size_t i;

    br->bus_name  = "dw-pcie";

    pci_set_word(dev->config + PCI_COMMAND,
                 PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);

    pci_config_set_interrupt_pin(dev->config, 1);
    pci_bridge_initfn(dev, TYPE_PCIE_BUS);

    pcie_port_init_reg(dev);

    pcie_cap_init(dev, 0x70, PCI_EXP_TYPE_ROOT_PORT,
                  0, &error_fatal);

    msi_nonbroken = true;
    msi_init(dev, 0x50, 32, true, true, &error_fatal);

    for (i = 0; i < DESIGNWARE_PCIE_NUM_VIEWPORTS; i++) {
        MemoryRegion *source, *destination, *mem;
        const char *direction;
        char *name;

        viewport = &root->viewports[DESIGNWARE_PCIE_VIEWPORT_INBOUND][i];
        viewport->inbound = true;
        viewport->base    = 0x0000000000000000ULL;
        viewport->target  = 0x0000000000000000ULL;
        viewport->limit   = UINT32_MAX;
        viewport->cr[0]   = DESIGNWARE_PCIE_ATU_TYPE_MEM;

        source      = &host->pci.address_space_root;
        destination = get_system_memory();
        direction   = "Inbound";

        /*
         * Configure MemoryRegion implementing PCI -> CPU memory
         * access
         */
        mem  = &viewport->mem;
        name = designware_pcie_viewport_name(direction, i, "MEM");
        memory_region_init_alias(mem, OBJECT(root), name, destination,
                                 dummy_offset, dummy_size);
        memory_region_add_subregion_overlap(source, dummy_offset, mem, -1);
        memory_region_set_enabled(mem, false);
        g_free(name);

        viewport = &root->viewports[DESIGNWARE_PCIE_VIEWPORT_OUTBOUND][i];
        viewport->root    = root;
        viewport->inbound = false;
        viewport->base    = 0x0000000000000000ULL;
        viewport->target  = 0x0000000000000000ULL;
        viewport->limit   = UINT32_MAX;
        viewport->cr[0]   = DESIGNWARE_PCIE_ATU_TYPE_MEM;

        destination = &host->pci.memory;
        direction   = "Outbound";
        source      = get_system_memory();

        /*
         * Configure MemoryRegion implementing CPU -> PCI memory
         * access
         */
        mem  = &viewport->mem;
        name = designware_pcie_viewport_name(direction, i, "MEM");
        memory_region_init_alias(mem, OBJECT(root), name, destination,
                                 dummy_offset, dummy_size);
        memory_region_add_subregion(source, dummy_offset, mem);
        memory_region_set_enabled(mem, false);
        g_free(name);

        /*
         * Configure MemoryRegion implementing access to configuration
         * space
         */
        mem  = &viewport->cfg;
        name = designware_pcie_viewport_name(direction, i, "CFG");
        memory_region_init_io(&viewport->cfg, OBJECT(root),
                              &designware_pci_host_conf_ops,
                              viewport, name, dummy_size);
        memory_region_add_subregion(source, dummy_offset, mem);
        memory_region_set_enabled(mem, false);
        g_free(name);
    }

    /*
     * If no inbound iATU windows are configured, HW defaults to
     * letting inbound TLPs to pass in. We emulate that by exlicitly
     * configuring first inbound window to cover all of target's
     * address space.
     *
     * NOTE: This will not work correctly for the case when first
     * configured inbound window is window 0
     */
    viewport = &root->viewports[DESIGNWARE_PCIE_VIEWPORT_INBOUND][0];
    viewport->cr[1] = DESIGNWARE_PCIE_ATU_ENABLE;
    designware_pcie_update_viewport(root, viewport);

    memory_region_init_io(&root->msi.iomem, OBJECT(root),
                          &designware_pci_host_msi_ops,
                          root, "pcie-msi", 0x4);
    /*
     * We initially place MSI interrupt I/O region a adress 0 and
     * disable it. It'll be later moved to correct offset and enabled
     * in designware_pcie_root_update_msi_mapping() as a part of
     * initialization done by guest OS
     */
    memory_region_add_subregion(address_space, dummy_offset, &root->msi.iomem);
    memory_region_set_enabled(&root->msi.iomem, false);
}

static void designware_pcie_set_irq(void *opaque, int irq_num, int level)
{
    DesignwarePCIEHost *host = DESIGNWARE_PCIE_HOST(opaque);

    qemu_set_irq(host->pci.irqs[irq_num], level);
}

static const char *
designware_pcie_host_root_bus_path(PCIHostState *host_bridge, PCIBus *rootbus)
{
    return "0000:00";
}

static const VMStateDescription vmstate_designware_pcie_msi_bank = {
    .name = "designware-pcie-msi-bank",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(enable, DesignwarePCIEMSIBank),
        VMSTATE_UINT32(mask, DesignwarePCIEMSIBank),
        VMSTATE_UINT32(status, DesignwarePCIEMSIBank),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_designware_pcie_msi = {
    .name = "designware-pcie-msi",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(base, DesignwarePCIEMSI),
        VMSTATE_STRUCT_ARRAY(intr,
                             DesignwarePCIEMSI,
                             DESIGNWARE_PCIE_NUM_MSI_BANKS,
                             1,
                             vmstate_designware_pcie_msi_bank,
                             DesignwarePCIEMSIBank),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_designware_pcie_viewport = {
    .name = "designware-pcie-viewport",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(base, DesignwarePCIEViewport),
        VMSTATE_UINT64(target, DesignwarePCIEViewport),
        VMSTATE_UINT32(limit, DesignwarePCIEViewport),
        VMSTATE_UINT32_ARRAY(cr, DesignwarePCIEViewport, 2),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_designware_pcie_root = {
    .name = "designware-pcie-root",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBridge),
        VMSTATE_UINT32(atu_viewport, DesignwarePCIERoot),
        VMSTATE_STRUCT_2DARRAY(viewports,
                               DesignwarePCIERoot,
                               2,
                               DESIGNWARE_PCIE_NUM_VIEWPORTS,
                               1,
                               vmstate_designware_pcie_viewport,
                               DesignwarePCIEViewport),
        VMSTATE_STRUCT(msi,
                       DesignwarePCIERoot,
                       1,
                       vmstate_designware_pcie_msi,
                       DesignwarePCIEMSI),
        VMSTATE_END_OF_LIST()
    }
};

static void designware_pcie_root_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);

    k->vendor_id = PCI_VENDOR_ID_SYNOPSYS;
    k->device_id = 0xABCD;
    k->revision = 0;
    k->class_id = PCI_CLASS_BRIDGE_PCI;
    k->exit = pci_bridge_exitfn;
    k->realize = designware_pcie_root_realize;
    k->config_read = designware_pcie_root_config_read;
    k->config_write = designware_pcie_root_config_write;

    dc->reset = pci_bridge_reset;
    /*
     * PCI-facing part of the host bridge, not usable without the
     * host-facing part, which can't be device_add'ed, yet.
     */
    dc->user_creatable = false;
    dc->vmsd = &vmstate_designware_pcie_root;
}

static uint64_t designware_pcie_host_mmio_read(void *opaque, hwaddr addr,
                                               unsigned int size)
{
    PCIHostState *pci = PCI_HOST_BRIDGE(opaque);
    PCIDevice *device = pci_find_device(pci->bus, 0, 0);

    return pci_host_config_read_common(device,
                                       addr,
                                       pci_config_size(device),
                                       size);
}

static void designware_pcie_host_mmio_write(void *opaque, hwaddr addr,
                                            uint64_t val, unsigned int size)
{
    PCIHostState *pci = PCI_HOST_BRIDGE(opaque);
    PCIDevice *device = pci_find_device(pci->bus, 0, 0);

    return pci_host_config_write_common(device,
                                        addr,
                                        pci_config_size(device),
                                        val, size);
}

static const MemoryRegionOps designware_pci_mmio_ops = {
    .read       = designware_pcie_host_mmio_read,
    .write      = designware_pcie_host_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        /*
         * Our device would not work correctly if the guest was doing
         * unaligned access. This might not be a limitation on the real
         * device but in practice there is no reason for a guest to access
         * this device unaligned.
         */
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static AddressSpace *designware_pcie_host_set_iommu(PCIBus *bus, void *opaque,
                                                    int devfn)
{
    DesignwarePCIEHost *s = DESIGNWARE_PCIE_HOST(opaque);

    return &s->pci.address_space;
}

static void designware_pcie_host_realize(DeviceState *dev, Error **errp)
{
    PCIHostState *pci = PCI_HOST_BRIDGE(dev);
    DesignwarePCIEHost *s = DESIGNWARE_PCIE_HOST(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    size_t i;

    for (i = 0; i < ARRAY_SIZE(s->pci.irqs); i++) {
        sysbus_init_irq(sbd, &s->pci.irqs[i]);
    }

    memory_region_init_io(&s->mmio,
                          OBJECT(s),
                          &designware_pci_mmio_ops,
                          s,
                          "pcie.reg", 4 * 1024);
    sysbus_init_mmio(sbd, &s->mmio);

    memory_region_init(&s->pci.io, OBJECT(s), "pcie-pio", 16);
    memory_region_init(&s->pci.memory, OBJECT(s),
                       "pcie-bus-memory",
                       UINT64_MAX);

    pci->bus = pci_register_root_bus(dev, "pcie",
                                     designware_pcie_set_irq,
                                     pci_swizzle_map_irq_fn,
                                     s,
                                     &s->pci.memory,
                                     &s->pci.io,
                                     0, 4,
                                     TYPE_PCIE_BUS);

    memory_region_init(&s->pci.address_space_root,
                       OBJECT(s),
                       "pcie-bus-address-space-root",
                       UINT64_MAX);
    memory_region_add_subregion(&s->pci.address_space_root,
                                0x0, &s->pci.memory);
    address_space_init(&s->pci.address_space,
                       &s->pci.address_space_root,
                       "pcie-bus-address-space");
    pci_setup_iommu(pci->bus, designware_pcie_host_set_iommu, s);

    qdev_realize(DEVICE(&s->root), BUS(pci->bus), &error_fatal);
}

static const VMStateDescription vmstate_designware_pcie_host = {
    .name = "designware-pcie-host",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(root,
                       DesignwarePCIEHost,
                       1,
                       vmstate_designware_pcie_root,
                       DesignwarePCIERoot),
        VMSTATE_END_OF_LIST()
    }
};

static void designware_pcie_host_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIHostBridgeClass *hc = PCI_HOST_BRIDGE_CLASS(klass);

    hc->root_bus_path = designware_pcie_host_root_bus_path;
    dc->realize = designware_pcie_host_realize;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->fw_name = "pci";
    dc->vmsd = &vmstate_designware_pcie_host;
}

static void designware_pcie_host_init(Object *obj)
{
    DesignwarePCIEHost *s = DESIGNWARE_PCIE_HOST(obj);
    DesignwarePCIERoot *root = &s->root;

    object_initialize_child(obj, "root", root, TYPE_DESIGNWARE_PCIE_ROOT);
    qdev_prop_set_int32(DEVICE(root), "addr", PCI_DEVFN(0, 0));
    qdev_prop_set_bit(DEVICE(root), "multifunction", false);
}

static const TypeInfo designware_pcie_root_info = {
    .name = TYPE_DESIGNWARE_PCIE_ROOT,
    .parent = TYPE_PCI_BRIDGE,
    .instance_size = sizeof(DesignwarePCIERoot),
    .class_init = designware_pcie_root_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { }
    },
};

static const TypeInfo designware_pcie_host_info = {
    .name       = TYPE_DESIGNWARE_PCIE_HOST,
    .parent     = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(DesignwarePCIEHost),
    .instance_init = designware_pcie_host_init,
    .class_init = designware_pcie_host_class_init,
};

static void designware_pcie_register(void)
{
    type_register_static(&designware_pcie_root_info);
    type_register_static(&designware_pcie_host_info);
}
type_init(designware_pcie_register)
