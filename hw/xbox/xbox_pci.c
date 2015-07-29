/*
 * QEMU Xbox PCI buses implementation
 *
 * Copyright (c) 2012 espes
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "hw/hw.h"
#include "qemu/range.h"
#include "hw/isa/isa.h"
#include "hw/sysbus.h"
#include "hw/loader.h"
#include "qemu/config-file.h"
#include "hw/i386/pc.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/pci_bridge.h"
#include "exec/address-spaces.h"
#include "qemu-common.h"

#include "hw/xbox/acpi_xbox.h"
#include "hw/xbox/amd_smbus.h"

#include "hw/xbox/xbox_pci.h"


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


//#define DEBUG

#ifdef DEBUG
# define XBOXPCI_DPRINTF(format, ...)     printf(format, ## __VA_ARGS__)
#else
# define XBOXPCI_DPRINTF(format, ...)     do { } while (0)
#endif



#define XBOX_NUM_INT_IRQS 8
#define XBOX_NUM_PIRQS    4

#define XBOX_NUM_PIC_IRQS 16

#define XBOX_LPC_ACPI_IRQ_ROUT 0x64
#define XBOX_LPC_PIRQ_ROUT     0x68
#define XBOX_LPC_INT_IRQ_ROUT  0x6C

static void xbox_lpc_set_irq(void *opaque, int pirq, int level)
{
    XBOX_LPCState *lpc = opaque;

    assert(pirq >= 0);
    assert(pirq < XBOX_NUM_INT_IRQS + XBOX_NUM_PIRQS);

    int pic_irq = 0;

    if (pirq < XBOX_NUM_INT_IRQS) {
        /* devices on the internal bus */
        uint32_t routing = pci_get_long(lpc->dev.config + XBOX_LPC_INT_IRQ_ROUT);
        pic_irq = (routing >> (pirq*4)) & 0xF;

        if (pic_irq == 0) {
            return;
        }
    } else {
        /* pirqs */
        pirq -= XBOX_NUM_INT_IRQS;
        pic_irq = lpc->dev.config[XBOX_LPC_PIRQ_ROUT + pirq];
    }

    if (pic_irq >= XBOX_NUM_PIC_IRQS) {
        return;
    }
    qemu_set_irq(lpc->pic[pic_irq], level);
}

static int xbox_lpc_map_irq(PCIDevice *pci_dev, int intx)
{
    int slot = PCI_SLOT(pci_dev->devfn);
    switch (slot) {
    /* devices on the internal bus */
    case 2: return 0; /* usb0 */
    case 3: return 1; /* usb1 */
    case 4: return 2; /* nic */
    case 5: return 3; /* apu */
    case 6: return 4; /* aci */
    case 9: return 6; /* ide */

    case 30: /* agp bridge -> PIRQC? */
        return XBOX_NUM_INT_IRQS + 2;
    default:
        /* don't actually know how this should work */
        assert(false);
        return XBOX_NUM_INT_IRQS + ((slot + intx) & 3);
    }
}

static void xbox_lpc_set_acpi_irq(void *opaque, int irq_num, int level)
{
    XBOX_LPCState *lpc = opaque;
    assert(irq_num == 0 || irq_num == 1);

    uint32_t routing = pci_get_long(lpc->dev.config + XBOX_LPC_ACPI_IRQ_ROUT);
    int irq = (routing >> (irq_num*8)) & 0xff;

    if (irq == 0 || irq >= XBOX_NUM_PIC_IRQS) {
        return;
    }
    qemu_set_irq(lpc->pic[irq], level);
}



void xbox_pci_init(qemu_irq *pic,
                   MemoryRegion *address_space_mem,
                   MemoryRegion *address_space_io,
                   MemoryRegion *pci_memory,
                   MemoryRegion *ram_memory,
                   PCIBus **out_host_bus,
                   ISABus **out_isa_bus,
                   i2c_bus **out_smbus,
                   PCIBus **out_agp_bus)
{
    DeviceState *host;
    PCIHostState *host_state;
    PCIBus *host_bus;
    PCIDevice *bridge;
    XBOX_PCIState *bridge_state;

    /* pci host bus */
    host = qdev_create(NULL, "xbox-pcihost");
    host_state = PCI_HOST_BRIDGE(host);

    host_bus = pci_bus_new(host, NULL,
                           pci_memory, address_space_io, 0, TYPE_PCI_BUS);
    host_state->bus = host_bus;
    qdev_init_nofail(host);

    bridge = pci_create_simple_multifunction(host_bus, PCI_DEVFN(0, 0),
                                             true, "xbox-pci");
    bridge_state = XBOX_PCI_DEVICE(bridge);
    bridge_state->ram_memory = ram_memory;
    bridge_state->pci_address_space = pci_memory;
    bridge_state->system_memory = address_space_mem;

    /* PCI hole */
    /* TODO: move to xbox-pci init */
    memory_region_init_alias(&bridge_state->pci_hole, OBJECT(bridge),
                             "pci-hole",
                             bridge_state->pci_address_space,
                             ram_size,
                             0x100000000ULL - ram_size);    
    memory_region_add_subregion(bridge_state->system_memory, ram_size,
                                &bridge_state->pci_hole);


    /* lpc bridge */
    PCIDevice *lpc = pci_create_simple_multifunction(host_bus, PCI_DEVFN(1, 0),
                                                     true, "xbox-lpc");
    XBOX_LPCState *lpc_state = XBOX_LPC_DEVICE(lpc);
    lpc_state->pic = pic;

    pci_bus_irqs(host_bus, xbox_lpc_set_irq, xbox_lpc_map_irq, lpc_state,
                 XBOX_NUM_INT_IRQS + XBOX_NUM_PIRQS);

    qemu_irq *acpi_irq = qemu_allocate_irqs(xbox_lpc_set_acpi_irq,
                                            lpc_state, 2);
    xbox_pm_init(lpc, &lpc_state->pm, acpi_irq[0]);
    //xbox_lpc_reset(&s->dev.qdev);


    /* smbus */
    PCIDevice *smbus = pci_create_simple_multifunction(host_bus, PCI_DEVFN(1, 1),
                                                       true, "xbox-smbus");

    XBOX_SMBState *smbus_state = XBOX_SMBUS_DEVICE(smbus);
    amd756_smbus_init(&smbus->qdev, &smbus_state->smb, acpi_irq[1]);


    /* AGP bus */
    PCIDevice *agp = pci_create_simple(host_bus, PCI_DEVFN(30, 0), "xbox-agp");
    //qdev = &br->dev.qdev;
    //qdev_init_nofail(qdev);
    PCIBus *agp_bus = pci_bridge_get_sec_bus(PCI_BRIDGE(agp));



    *out_host_bus = host_bus;
    *out_isa_bus = lpc_state->isa_bus;
    *out_smbus = smbus_state->smb.smbus;
    *out_agp_bus = agp_bus;
}


#define XBOX_SMBUS_BASE_BAR 1

static void xbox_smb_ioport_writeb(void *opaque, hwaddr addr,
                                   uint64_t val, unsigned size)
{
    XBOX_SMBState *s = opaque;

    uint64_t offset = addr - s->dev.io_regions[XBOX_SMBUS_BASE_BAR].addr;
    amd756_smb_ioport_writeb(&s->smb, offset, val);
}

static uint64_t xbox_smb_ioport_readb(void *opaque, hwaddr addr,
                                      unsigned size)
{
    XBOX_SMBState *s = opaque;

    uint64_t offset = addr - s->dev.io_regions[XBOX_SMBUS_BASE_BAR].addr;
    return amd756_smb_ioport_readb(&s->smb, offset);
}

static const MemoryRegionOps xbox_smbus_ops = {
    .read = xbox_smb_ioport_readb,
    .write = xbox_smb_ioport_writeb,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static int xbox_smbus_initfn(PCIDevice *dev)
{
    XBOX_SMBState *s = XBOX_SMBUS_DEVICE(dev);

    memory_region_init_io(&s->smb_bar, OBJECT(dev), &xbox_smbus_ops,
                          s, "xbox-smbus-bar", 32);
    pci_register_bar(dev, XBOX_SMBUS_BASE_BAR, PCI_BASE_ADDRESS_SPACE_IO,
                     &s->smb_bar);

    return 0;
}


static void xbox_smbus_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init         = xbox_smbus_initfn;
    k->vendor_id    = PCI_VENDOR_ID_NVIDIA;
    k->device_id    = PCI_DEVICE_ID_NVIDIA_NFORCE_SMBUS;
    k->revision     = 161;
    k->class_id     = PCI_CLASS_SERIAL_SMBUS;

    dc->desc        = "nForce PCI System Management";
    dc->no_user     = 1;
}

static const TypeInfo xbox_smbus_info = {
    .name = "xbox-smbus",
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(XBOX_SMBState),
    .class_init = xbox_smbus_class_init,
};



static int xbox_lpc_initfn(PCIDevice *d)
{
    XBOX_LPCState *s = XBOX_LPC_DEVICE(d);
    ISABus *isa_bus;

    isa_bus = isa_bus_new(&d->qdev, get_system_io());
    s->isa_bus = isa_bus;


    /* southbridge chip contains and controls bootrom image.
     * can't load it through loader.c because it overlaps with the bios...
     * We really should just commandeer the entire top 16Mb.
     */
    QemuOpts *machine_opts = qemu_opts_find(qemu_find_opts("machine"), NULL);
    if (machine_opts) {
        const char *bootrom_file = qemu_opt_get(machine_opts, "bootrom");

        int rc, fd = -1;
        if (bootrom_file) {
            char *filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bootrom_file);
            assert(filename);

            s->bootrom_size = get_image_size(filename);
            if (s->bootrom_size != 512) {
                fprintf(stderr, "MCPX bootrom should be 512 bytes, got %d\n",
                        s->bootrom_size);
                return -1;
            }

            fd = open(filename, O_RDONLY | O_BINARY);
            assert(fd >= 0);
            rc = read(fd, s->bootrom_data, s->bootrom_size);
            assert(rc == s->bootrom_size);

            close(fd);
        }
    }


    return 0;
}



static void xbox_lpc_reset(DeviceState *dev)
{
    PCIDevice *d = PCI_DEVICE(dev);
    XBOX_LPCState *s = XBOX_LPC_DEVICE(d);


    if (s->bootrom_size) {
        /* qemu's memory region shit is actually kinda broken -
         * Trying to execute off a non-page-aligned memory region
         * is fucked, so we can't just map in the bootrom.
         *
         * We need to be able to disable it at runtime, and
         * it shouldn't be visible ontop of the bios mirrors. It'll have to
         * be a hack.
         *
         * Be lazy for now and just write it ontop of the bios.
         *
         * (We do this here since loader.c loads roms into memory in a reset
         * handler, and here we /should/ be handled after it.)
         */

        hwaddr bootrom_addr = (uint32_t)(-s->bootrom_size);
        cpu_physical_memory_write_rom(bootrom_addr,
                                      s->bootrom_data,
                                      s->bootrom_size);
     }

}


#if 0
/* Xbox 1.1 uses a config register instead of a bar to set the pm base address */
#define XBOX_LPC_PMBASE 0x84
#define XBOX_LPC_PMBASE_ADDRESS_MASK 0xff00
#define XBOX_LPC_PMBASE_DEFAULT 0x1

static void xbox_lpc_pmbase_update(XBOX_LPCState *s)
{
    uint32_t pm_io_base = pci_get_long(s->dev.config + XBOX_LPC_PMBASE);
    pm_io_base &= XBOX_LPC_PMBASE_ADDRESS_MASK;

    xbox_pm_iospace_update(&s->pm, pm_io_base);
}

static void xbox_lpc_reset(DeviceState *dev)
{
    PCIDevice *d = PCI_DEVICE(dev);
    XBOX_LPCState *s = XBOX_LPC_DEVICE(d);

    pci_set_long(s->dev.config + XBOX_LPC_PMBASE, XBOX_LPC_PMBASE_DEFAULT);
    xbox_lpc_pmbase_update(s);
}

static void xbox_lpc_config_write(PCIDevice *dev,
                                    uint32_t addr, uint32_t val, int len)
{
    XBOX_LPCState *s = XBOX_LPC_DEVICE(dev);

    pci_default_write_config(dev, addr, val, len);
    if (ranges_overlap(addr, len, XBOX_LPC_PMBASE, 2)) {
        xbox_lpc_pmbase_update(s);
    }
}

static int xbox_lpc_post_load(void *opaque, int version_id)
{
    XBOX_LPCState *s = opaque;
    xbox_lpc_pmbase_update(s);
    return 0;
}

static const VMStateDescription vmstate_xbox_lpc = {
    .name = "XBOX LPC",
    .version_id = 1,
    .post_load = xbox_lpc_post_load,
};
#endif

static void xbox_lpc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->no_hotplug   = 1;
    k->init         = xbox_lpc_initfn;
    //k->config_write = xbox_lpc_config_write;
    k->vendor_id    = PCI_VENDOR_ID_NVIDIA;
    k->device_id    = PCI_DEVICE_ID_NVIDIA_NFORCE_LPC;
    k->revision     = 212;
    k->class_id     = PCI_CLASS_BRIDGE_ISA;

    dc->desc        = "nForce LPC Bridge";
    dc->no_user     = 1;
    dc->reset       = xbox_lpc_reset;
    //dc->vmsd        = &vmstate_xbox_lpc;
}

static const TypeInfo xbox_lpc_info = {
    .name = "xbox-lpc",
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(XBOX_LPCState),
    .class_init = xbox_lpc_class_init,
};




static int xbox_agp_initfn(PCIDevice *d)
{
    pci_set_word(d->config + PCI_PREF_MEMORY_BASE, PCI_PREF_RANGE_TYPE_32);
    pci_set_word(d->config + PCI_PREF_MEMORY_LIMIT, PCI_PREF_RANGE_TYPE_32);
    return pci_bridge_initfn(d, TYPE_PCI_BUS);
}

static void xbox_agp_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init         = xbox_agp_initfn;
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
    .parent        = TYPE_PCI_BRIDGE,
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

    memory_region_init_io(&s->conf_mem, OBJECT(dev),
                          &pci_host_conf_le_ops, s,
                          "pci-conf-idx", 4);
    sysbus_add_io(dev, CONFIG_ADDR, &s->conf_mem);
    sysbus_init_ioports(&s->busdev, CONFIG_ADDR, 4);

    memory_region_init_io(&s->data_mem, OBJECT(dev),
                          &pci_host_data_le_ops, s,
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

    type_register(&xbox_lpc_info);
    type_register(&xbox_smbus_info);
}

type_init(xboxpci_register_types)
