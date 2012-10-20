/*
 * QEMU Xbox PCI buses implementation
 *
 * Copyright (c) 2012 espes
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */
#include "hw.h"
#include "range.h"
#include "isa.h"
#include "sysbus.h"
#include "pc.h"
#include "pci.h"
#include "pci_bridge.h"
#include "pci_internals.h"
#include "exec-memory.h"
#include "acpi_mcpx.h"
#include "amd_smbus.h"
#include "qemu-common.h"

#include "xbox_pci.h"


 /*
  * xbox chipset based on nForce 420, which was based on AMD-760
  * 
  * http://support.amd.com/us/ChipsetMotherboard_TechDocs/24494.pdf
  * http://support.amd.com/us/ChipsetMotherboard_TechDocs/24416.pdf
  * http://support.amd.com/us/ChipsetMotherboard_TechDocs/24467.pdf
  *
  * http://support.amd.com/us/ChipsetMotherboard_TechDocs/24462.pdf
  *
  * - 'NV2A' combination northbridge/gpu
  * - 'MCPX' combination southbridge/apu
  */


#define DEBUG

#ifdef DEBUG
# define XBOXPCI_DPRINTF(format, ...)     printf(format, ## __VA_ARGS__)
#else
# define XBOXPCI_DPRINTF(format, ...)     do { } while (0)
#endif





PCIBus *xbox_pci_init(DeviceState **xbox_pci_hostp,
                      qemu_irq *pic,
                      MemoryRegion *address_space_mem,
                      MemoryRegion *address_space_io,
                      MemoryRegion *pci_memory,
                      MemoryRegion *ram_memory)

{
    DeviceState *dev;
    PCIHostState *hostState;
    PCIBus *hostBus;
    PCIDevice *bridgeDev;
    XBOX_PCIState *bridge;

    /* pci host bus */
    dev = qdev_create(NULL, "xbox-pcihost");
    hostState = PCI_HOST_BRIDGE(dev);
    hostState->address_space = address_space_mem;


    hostBus = pci_bus_new(dev, NULL, pci_memory,
                          address_space_io, 0);
    hostState->bus = hostBus;

    //pci_bus_irqs(b, piix3_set_irq, pci_slot_get_pirq, piix3,
    //            PIIX_NUM_PIRQS);

    qdev_init_nofail(dev);

    bridgeDev = pci_create_simple_multifunction(hostBus, PCI_DEVFN(0, 0),
                                                   true, "xbox-pci");
    bridge = XBOX_PCI_DEVICE(bridgeDev);
    bridge->ram_memory = ram_memory;
    bridge->pci_address_space = pci_memory;
    bridge->system_memory = address_space_mem;

    /* PCI hole */
    memory_region_init_alias(&bridge->pci_hole, "pci-hole",
                             bridge->pci_address_space,
                             ram_size,
                             0x100000000ULL - ram_size);    
    memory_region_add_subregion(bridge->system_memory, ram_size,
                                &bridge->pci_hole);


    *xbox_pci_hostp = dev;
    return hostBus;
}


PCIBus *xbox_agp_init(DeviceState *host, PCIBus *bus)
{
    PCIDevice *d;
    PCIBridge *br;
    //DeviceState *qdev;

    /* AGP bus */
    d = pci_create_simple(bus, PCI_DEVFN(30, 0), "xbox-agp");
    if (!d) {
        return NULL;
    }

    br = DO_UPCAST(PCIBridge, dev, d);
    //qdev = &br->dev.qdev;
    //qdev_init_nofail(qdev);

    return pci_bridge_get_sec_bus(br);
}


ISABus *mcpx_lpc_init(DeviceState *host, PCIBus *bus)
{
    PCIDevice *d;
    MCPX_LPCState *s;
    //qemu_irq *sci_irq;

    d = pci_create_simple_multifunction(bus, PCI_DEVFN(1, 0),
                                        true, "mcpx-lpc");

    s = MCPX_LPC_DEVICE(d);

    //sci_irq = qemu_allocate_irqs(mcpx_set_sci, &s->irq_state, 1);
    mcpx_pm_init(&s->pm /*, sci_irq[0]*/);
    //mcpx_lpc_reset(&s->dev.qdev);

    return s->isa_bus;
}


i2c_bus *mcpx_smbus_init(DeviceState *host, PCIBus *bus)
{
    PCIDevice *d;
    MCPX_SMBState *s;
    
    d = pci_create_simple_multifunction(bus, PCI_DEVFN(1, 1),
                                        true, "mcpx-smbus");

    s = MCPX_SMBUS_DEVICE(d);

    return s->smb.smbus;
}




#define MCPX_SMBUS_BASE_BAR 1

static void mcpx_smb_ioport_writeb(void *opaque, target_phys_addr_t addr,
                                   uint64_t val, unsigned size)
{
    MCPX_SMBState *s = opaque;

    uint64_t offset = addr - s->dev.io_regions[MCPX_SMBUS_BASE_BAR].addr;
    amd756_smb_ioport_writeb(&s->smb, offset, val);
}

static uint64_t mcpx_smb_ioport_readb(void *opaque, target_phys_addr_t addr,
                                      unsigned size)
{
    MCPX_SMBState *s = opaque;

    uint64_t offset = addr - s->dev.io_regions[MCPX_SMBUS_BASE_BAR].addr;
    return amd756_smb_ioport_readb(&s->smb, offset);
}

static const MemoryRegionOps mcpx_smbus_ops = {
    .read = mcpx_smb_ioport_readb,
    .write = mcpx_smb_ioport_writeb,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static int mcpx_smbus_initfn(PCIDevice *dev)
{
    MCPX_SMBState *s = MCPX_SMBUS_DEVICE(dev);

    memory_region_init_io(&s->smb_bar, &mcpx_smbus_ops,
                          s, "mcpx-smbus-bar", 32);
    pci_register_bar(dev, MCPX_SMBUS_BASE_BAR, PCI_BASE_ADDRESS_SPACE_IO,
                     &s->smb_bar);
    amd756_smbus_init(&dev->qdev, &s->smb);

    return 0;
}


static void mcpx_smbus_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init         = mcpx_smbus_initfn;
    k->vendor_id    = PCI_VENDOR_ID_NVIDIA;
    k->device_id    = PCI_DEVICE_ID_NVIDIA_NFORCE_SMBUS;
    k->revision     = 161;
    k->class_id     = PCI_CLASS_SERIAL_SMBUS;

    dc->desc        = "nForce PCI System Management";
    dc->no_user     = 1;
}

static const TypeInfo mcpx_smbus_info = {
    .name = "mcpx-smbus",
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(MCPX_SMBState),
    .class_init = mcpx_smbus_class_init,
};





#define MCPX_LPC_PMBASE 0x84
#define MCPX_LPC_PMBASE_ADDRESS_MASK 0xff00
#define MCPX_LPC_PMBASE_DEFAULT 0x1

static int mcpx_lpc_initfn(PCIDevice *d)
{
    MCPX_LPCState *lpc = MCPX_LPC_DEVICE(d);
    ISABus *isa_bus;

    isa_bus = isa_bus_new(&d->qdev, get_system_io());
    lpc->isa_bus = isa_bus;

    return 0;
}

static void mcpx_lpc_pmbase_update(MCPX_LPCState *s)
{
    uint32_t pm_io_base = pci_get_long(s->dev.config + MCPX_LPC_PMBASE);
    pm_io_base &= MCPX_LPC_PMBASE_ADDRESS_MASK;

    mcpx_pm_iospace_update(&s->pm, pm_io_base);
}

static void mcpx_lpc_reset(DeviceState *dev)
{
    PCIDevice *d = PCI_DEVICE(dev);
    MCPX_LPCState *s = MCPX_LPC_DEVICE(d);

    pci_set_long(s->dev.config + MCPX_LPC_PMBASE, MCPX_LPC_PMBASE_DEFAULT);
    mcpx_lpc_pmbase_update(s);
}

static void mcpx_lpc_config_write(PCIDevice *dev,
                                    uint32_t addr, uint32_t val, int len)
{
    MCPX_LPCState *s = MCPX_LPC_DEVICE(dev);

    pci_default_write_config(dev, addr, val, len);
    if (ranges_overlap(addr, len, MCPX_LPC_PMBASE, 2)) {
        mcpx_lpc_pmbase_update(s);
    }
}

static int mcpx_lpc_post_load(void *opaque, int version_id)
{
    MCPX_LPCState *s = opaque;
    mcpx_lpc_pmbase_update(s);
    return 0;
}

static const VMStateDescription vmstate_mcpx_lpc = {
    .name = "MCPX LPC",
    .version_id = 1,
    .post_load = mcpx_lpc_post_load,
};

static void mcpx_lpc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->no_hotplug   = 1;
    k->init         = mcpx_lpc_initfn;
    k->config_write = mcpx_lpc_config_write;
    k->vendor_id    = PCI_VENDOR_ID_NVIDIA;
    k->device_id    = PCI_DEVICE_ID_NVIDIA_NFORCE_LPC;
    k->revision     = 212;
    k->class_id     = PCI_CLASS_BRIDGE_ISA;

    dc->desc        = "nForce LPC Bridge";
    dc->no_user     = 1;
    dc->reset       = mcpx_lpc_reset;
    dc->vmsd        = &vmstate_mcpx_lpc;
}

static const TypeInfo mcpx_lpc_info = {
    .name = "mcpx-lpc",
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(MCPX_LPCState),
    .class_init = mcpx_lpc_class_init,
};




static void xbox_agp_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init         = pci_bridge_initfn;
    k->exit         = pci_bridge_exitfn;
    k->config_write = pci_bridge_write_config;
    k->is_bridge    = 1;
    k->vendor_id    = PCI_VENDOR_ID_NVIDIA;
    k->device_id    = PCI_DEVICE_ID_NVIDIA_NFORCE_AGP;
    k->revision     = 161;

    dc->desc        = "nForce AGP to PCI Bridge";
    dc->reset       = pci_bridge_reset;
}

static const TypeInfo xbox_agp_info = {
    .name          = "xbox-agp",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIBridge),
    .class_init    = xbox_agp_class_init,
};






static int xbox_pci_initfn(PCIDevice *d)
{
    //XBOX_PCIState *s = DO_UPCAST(XBOX_PCIState, dev, dev);

    return 0;
}

static void xbox_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->no_hotplug = 1;
    k->init = xbox_pci_initfn;
    //k->config_write = xbox_pci_write_config;
    k->vendor_id = PCI_VENDOR_ID_NVIDIA;
    k->device_id = PCI_DEVICE_ID_NVIDIA_XBOX_PCHB;
    k->revision = 161;
    k->class_id = PCI_CLASS_BRIDGE_HOST;

    dc->desc = "Xbox PCI Host";
    dc->no_user = 1;
}

static const TypeInfo xbox_pci_info = {
    .name          = "xbox-pci",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(XBOX_PCIState),
    .class_init    = xbox_pci_class_init,
};



#define CONFIG_ADDR 0xcf8
#define CONFIG_DATA 0xcfc

static int xbox_pcihost_initfn(SysBusDevice *dev)
{
    PCIHostState *s = PCI_HOST_BRIDGE(dev);

    memory_region_init_io(&s->conf_mem, &pci_host_conf_le_ops, s,
                          "pci-conf-idx", 4);
    sysbus_add_io(dev, CONFIG_ADDR, &s->conf_mem);
    sysbus_init_ioports(&s->busdev, CONFIG_ADDR, 4);

    memory_region_init_io(&s->data_mem, &pci_host_data_le_ops, s,
                          "pci-conf-data", 4);
    sysbus_add_io(dev, CONFIG_DATA, &s->data_mem);
    sysbus_init_ioports(&s->busdev, CONFIG_DATA, 4);

    return 0;
}


static void xbox_pcihost_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = xbox_pcihost_initfn;
    dc->no_user = 1;
}

static const TypeInfo xbox_pcihost_info = {
    .name          = "xbox-pcihost",
    .parent        = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(PCIHostState),
    .class_init    = xbox_pcihost_class_init,
};


static void xboxpci_register_types(void)
{
    type_register(&xbox_pcihost_info);
    type_register(&xbox_pci_info);
    type_register(&xbox_agp_info);

    type_register(&mcpx_lpc_info);
    type_register(&mcpx_smbus_info);
}

type_init(xboxpci_register_types)
