/*
 * QEMU sPAPR PCI host originated from Uninorth PCI host
 *
 * Copyright (c) 2011 Alexey Kardashevskiy, IBM Corporation.
 * Copyright (C) 2011 David Gibson, IBM Corporation.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "hw.h"
#include "pci.h"
#include "pci_host.h"
#include "hw/spapr.h"
#include "hw/spapr_pci.h"
#include "exec-memory.h"
#include <libfdt.h>

#include "hw/pci_internals.h"

static PCIDevice *find_dev(sPAPREnvironment *spapr,
                           uint64_t buid, uint32_t config_addr)
{
    DeviceState *qdev;
    int devfn = (config_addr >> 8) & 0xFF;
    sPAPRPHBState *phb;

    QLIST_FOREACH(phb, &spapr->phbs, list) {
        if (phb->buid != buid) {
            continue;
        }

        QTAILQ_FOREACH(qdev, &phb->host_state.bus->qbus.children, sibling) {
            PCIDevice *dev = (PCIDevice *)qdev;
            if (dev->devfn == devfn) {
                return dev;
            }
        }
    }

    return NULL;
}

static uint32_t rtas_pci_cfgaddr(uint32_t arg)
{
    return ((arg >> 20) & 0xf00) | (arg & 0xff);
}

static uint32_t rtas_read_pci_config_do(PCIDevice *pci_dev, uint32_t addr,
                                        uint32_t limit, uint32_t len)
{
    if ((addr + len) <= limit) {
        return pci_host_config_read_common(pci_dev, addr, limit, len);
    } else {
        return ~0x0;
    }
}

static void rtas_write_pci_config_do(PCIDevice *pci_dev, uint32_t addr,
                                     uint32_t limit, uint32_t val,
                                     uint32_t len)
{
    if ((addr + len) <= limit) {
        pci_host_config_write_common(pci_dev, addr, limit, val, len);
    }
}

static void rtas_ibm_read_pci_config(sPAPREnvironment *spapr,
                                     uint32_t token, uint32_t nargs,
                                     target_ulong args,
                                     uint32_t nret, target_ulong rets)
{
    uint32_t val, size, addr;
    uint64_t buid = ((uint64_t)rtas_ld(args, 1) << 32) | rtas_ld(args, 2);
    PCIDevice *dev = find_dev(spapr, buid, rtas_ld(args, 0));

    if (!dev) {
        rtas_st(rets, 0, -1);
        return;
    }
    size = rtas_ld(args, 3);
    addr = rtas_pci_cfgaddr(rtas_ld(args, 0));
    val = rtas_read_pci_config_do(dev, addr, pci_config_size(dev), size);
    rtas_st(rets, 0, 0);
    rtas_st(rets, 1, val);
}

static void rtas_read_pci_config(sPAPREnvironment *spapr,
                                 uint32_t token, uint32_t nargs,
                                 target_ulong args,
                                 uint32_t nret, target_ulong rets)
{
    uint32_t val, size, addr;
    PCIDevice *dev = find_dev(spapr, 0, rtas_ld(args, 0));

    if (!dev) {
        rtas_st(rets, 0, -1);
        return;
    }
    size = rtas_ld(args, 1);
    addr = rtas_pci_cfgaddr(rtas_ld(args, 0));
    val = rtas_read_pci_config_do(dev, addr, pci_config_size(dev), size);
    rtas_st(rets, 0, 0);
    rtas_st(rets, 1, val);
}

static void rtas_ibm_write_pci_config(sPAPREnvironment *spapr,
                                      uint32_t token, uint32_t nargs,
                                      target_ulong args,
                                      uint32_t nret, target_ulong rets)
{
    uint32_t val, size, addr;
    uint64_t buid = ((uint64_t)rtas_ld(args, 1) << 32) | rtas_ld(args, 2);
    PCIDevice *dev = find_dev(spapr, buid, rtas_ld(args, 0));

    if (!dev) {
        rtas_st(rets, 0, -1);
        return;
    }
    val = rtas_ld(args, 4);
    size = rtas_ld(args, 3);
    addr = rtas_pci_cfgaddr(rtas_ld(args, 0));
    rtas_write_pci_config_do(dev, addr, pci_config_size(dev), val, size);
    rtas_st(rets, 0, 0);
}

static void rtas_write_pci_config(sPAPREnvironment *spapr,
                                  uint32_t token, uint32_t nargs,
                                  target_ulong args,
                                  uint32_t nret, target_ulong rets)
{
    uint32_t val, size, addr;
    PCIDevice *dev = find_dev(spapr, 0, rtas_ld(args, 0));

    if (!dev) {
        rtas_st(rets, 0, -1);
        return;
    }
    val = rtas_ld(args, 2);
    size = rtas_ld(args, 1);
    addr = rtas_pci_cfgaddr(rtas_ld(args, 0));
    rtas_write_pci_config_do(dev, addr, pci_config_size(dev), val, size);
    rtas_st(rets, 0, 0);
}

static int pci_spapr_map_irq(PCIDevice *pci_dev, int irq_num)
{
    /*
     * Here we need to convert pci_dev + irq_num to some unique value
     * which is less than number of IRQs on the specific bus (now it
     * is 16).  At the moment irq_num == device_id (number of the
     * slot?)
     * FIXME: we should swizzle in fn and irq_num
     */
    return (pci_dev->devfn >> 3) % SPAPR_PCI_NUM_LSI;
}

static void pci_spapr_set_irq(void *opaque, int irq_num, int level)
{
    /*
     * Here we use the number returned by pci_spapr_map_irq to find a
     * corresponding qemu_irq.
     */
    sPAPRPHBState *phb = opaque;

    qemu_set_irq(phb->lsi_table[irq_num].qirq, level);
}

static uint64_t spapr_io_read(void *opaque, target_phys_addr_t addr,
                              unsigned size)
{
    switch (size) {
    case 1:
        return cpu_inb(addr);
    case 2:
        return cpu_inw(addr);
    case 4:
        return cpu_inl(addr);
    }
    assert(0);
}

static void spapr_io_write(void *opaque, target_phys_addr_t addr,
                           uint64_t data, unsigned size)
{
    switch (size) {
    case 1:
        cpu_outb(addr, data);
        return;
    case 2:
        cpu_outw(addr, data);
        return;
    case 4:
        cpu_outl(addr, data);
        return;
    }
    assert(0);
}

static const MemoryRegionOps spapr_io_ops = {
    .endianness = DEVICE_LITTLE_ENDIAN,
    .read = spapr_io_read,
    .write = spapr_io_write
};

/*
 * PHB PCI device
 */
static int spapr_phb_init(SysBusDevice *s)
{
    sPAPRPHBState *phb = FROM_SYSBUS(sPAPRPHBState, s);
    char *namebuf;
    int i;
    PCIBus *bus;

    phb->dtbusname = g_strdup_printf("pci@%" PRIx64, phb->buid);
    namebuf = alloca(strlen(phb->dtbusname) + 32);

    /* Initialize memory regions */
    sprintf(namebuf, "%s.mmio", phb->dtbusname);
    memory_region_init(&phb->memspace, namebuf, INT64_MAX);

    sprintf(namebuf, "%s.mmio-alias", phb->dtbusname);
    memory_region_init_alias(&phb->memwindow, namebuf, &phb->memspace,
                             SPAPR_PCI_MEM_WIN_BUS_OFFSET, phb->mem_win_size);
    memory_region_add_subregion(get_system_memory(), phb->mem_win_addr,
                                &phb->memwindow);

    /* On ppc, we only have MMIO no specific IO space from the CPU
     * perspective.  In theory we ought to be able to embed the PCI IO
     * memory region direction in the system memory space.  However,
     * if any of the IO BAR subregions use the old_portio mechanism,
     * that won't be processed properly unless accessed from the
     * system io address space.  This hack to bounce things via
     * system_io works around the problem until all the users of
     * old_portion are updated */
    sprintf(namebuf, "%s.io", phb->dtbusname);
    memory_region_init(&phb->iospace, namebuf, SPAPR_PCI_IO_WIN_SIZE);
    /* FIXME: fix to support multiple PHBs */
    memory_region_add_subregion(get_system_io(), 0, &phb->iospace);

    sprintf(namebuf, "%s.io-alias", phb->dtbusname);
    memory_region_init_io(&phb->iowindow, &spapr_io_ops, phb,
                          namebuf, SPAPR_PCI_IO_WIN_SIZE);
    memory_region_add_subregion(get_system_memory(), phb->io_win_addr,
                                &phb->iowindow);

    bus = pci_register_bus(&phb->busdev.qdev,
                           phb->busname ? phb->busname : phb->dtbusname,
                           pci_spapr_set_irq, pci_spapr_map_irq, phb,
                           &phb->memspace, &phb->iospace,
                           PCI_DEVFN(0, 0), SPAPR_PCI_NUM_LSI);
    phb->host_state.bus = bus;

    QLIST_INSERT_HEAD(&spapr->phbs, phb, list);

    /* Initialize the LSI table */
    for (i = 0; i < SPAPR_PCI_NUM_LSI; i++) {
        qemu_irq qirq;
        uint32_t num;

        qirq = spapr_allocate_lsi(0, &num);
        if (!qirq) {
            return -1;
        }

        phb->lsi_table[i].dt_irq = num;
        phb->lsi_table[i].qirq = qirq;
    }

    return 0;
}

static Property spapr_phb_properties[] = {
    DEFINE_PROP_HEX64("buid", sPAPRPHBState, buid, 0),
    DEFINE_PROP_STRING("busname", sPAPRPHBState, busname),
    DEFINE_PROP_HEX64("mem_win_addr", sPAPRPHBState, mem_win_addr, 0),
    DEFINE_PROP_HEX64("mem_win_size", sPAPRPHBState, mem_win_size, 0x20000000),
    DEFINE_PROP_HEX64("io_win_addr", sPAPRPHBState, io_win_addr, 0),
    DEFINE_PROP_HEX64("io_win_size", sPAPRPHBState, io_win_size, 0x10000),
    DEFINE_PROP_END_OF_LIST(),
};

static void spapr_phb_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sdc = SYS_BUS_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    sdc->init = spapr_phb_init;
    dc->props = spapr_phb_properties;

    spapr_rtas_register("read-pci-config", rtas_read_pci_config);
    spapr_rtas_register("write-pci-config", rtas_write_pci_config);
    spapr_rtas_register("ibm,read-pci-config", rtas_ibm_read_pci_config);
    spapr_rtas_register("ibm,write-pci-config", rtas_ibm_write_pci_config);
}

static TypeInfo spapr_phb_info = {
    .name          = "spapr-pci-host-bridge",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(sPAPRPHBState),
    .class_init    = spapr_phb_class_init,
};

void spapr_create_phb(sPAPREnvironment *spapr,
                      const char *busname, uint64_t buid,
                      uint64_t mem_win_addr, uint64_t mem_win_size,
                      uint64_t io_win_addr)
{
    DeviceState *dev;

    dev = qdev_create(NULL, spapr_phb_info.name);

    if (busname) {
        qdev_prop_set_string(dev, "busname", g_strdup(busname));
    }
    qdev_prop_set_uint64(dev, "buid", buid);
    qdev_prop_set_uint64(dev, "mem_win_addr", mem_win_addr);
    qdev_prop_set_uint64(dev, "mem_win_size", mem_win_size);
    qdev_prop_set_uint64(dev, "io_win_addr", io_win_addr);

    qdev_init_nofail(dev);
}

/* Macros to operate with address in OF binding to PCI */
#define b_x(x, p, l)    (((x) & ((1<<(l))-1)) << (p))
#define b_n(x)          b_x((x), 31, 1) /* 0 if relocatable */
#define b_p(x)          b_x((x), 30, 1) /* 1 if prefetchable */
#define b_t(x)          b_x((x), 29, 1) /* 1 if the address is aliased */
#define b_ss(x)         b_x((x), 24, 2) /* the space code */
#define b_bbbbbbbb(x)   b_x((x), 16, 8) /* bus number */
#define b_ddddd(x)      b_x((x), 11, 5) /* device number */
#define b_fff(x)        b_x((x), 8, 3)  /* function number */
#define b_rrrrrrrr(x)   b_x((x), 0, 8)  /* register number */

int spapr_populate_pci_devices(sPAPRPHBState *phb,
                               uint32_t xics_phandle,
                               void *fdt)
{
    PCIBus *bus = phb->host_state.bus;
    int bus_off, i;
    char nodename[256];
    uint32_t bus_range[] = { cpu_to_be32(0), cpu_to_be32(0xff) };
    struct {
        uint32_t hi;
        uint64_t child;
        uint64_t parent;
        uint64_t size;
    } __attribute__((packed)) ranges[] = {
        {
            cpu_to_be32(b_ss(1)), cpu_to_be64(0),
            cpu_to_be64(phb->io_win_addr),
            cpu_to_be64(memory_region_size(&phb->iospace)),
        },
        {
            cpu_to_be32(b_ss(2)), cpu_to_be64(SPAPR_PCI_MEM_WIN_BUS_OFFSET),
            cpu_to_be64(phb->mem_win_addr),
            cpu_to_be64(memory_region_size(&phb->memwindow)),
        },
    };
    uint64_t bus_reg[] = { cpu_to_be64(phb->buid), 0 };
    uint32_t interrupt_map_mask[] = {
        cpu_to_be32(b_ddddd(-1)|b_fff(0)), 0x0, 0x0, 0x0};
    uint32_t interrupt_map[bus->nirq][7];

    /* Start populating the FDT */
    sprintf(nodename, "pci@%" PRIx64, phb->buid);
    bus_off = fdt_add_subnode(fdt, 0, nodename);
    if (bus_off < 0) {
        return bus_off;
    }

#define _FDT(exp) \
    do { \
        int ret = (exp);                                           \
        if (ret < 0) {                                             \
            return ret;                                            \
        }                                                          \
    } while (0)

    /* Write PHB properties */
    _FDT(fdt_setprop_string(fdt, bus_off, "device_type", "pci"));
    _FDT(fdt_setprop_string(fdt, bus_off, "compatible", "IBM,Logical_PHB"));
    _FDT(fdt_setprop_cell(fdt, bus_off, "#address-cells", 0x3));
    _FDT(fdt_setprop_cell(fdt, bus_off, "#size-cells", 0x2));
    _FDT(fdt_setprop_cell(fdt, bus_off, "#interrupt-cells", 0x1));
    _FDT(fdt_setprop(fdt, bus_off, "used-by-rtas", NULL, 0));
    _FDT(fdt_setprop(fdt, bus_off, "bus-range", &bus_range, sizeof(bus_range)));
    _FDT(fdt_setprop(fdt, bus_off, "ranges", &ranges, sizeof(ranges)));
    _FDT(fdt_setprop(fdt, bus_off, "reg", &bus_reg, sizeof(bus_reg)));
    _FDT(fdt_setprop_cell(fdt, bus_off, "ibm,pci-config-space-type", 0x1));

    /* Build the interrupt-map, this must matches what is done
     * in pci_spapr_map_irq
     */
    _FDT(fdt_setprop(fdt, bus_off, "interrupt-map-mask",
                     &interrupt_map_mask, sizeof(interrupt_map_mask)));
    for (i = 0; i < 7; i++) {
        uint32_t *irqmap = interrupt_map[i];
        irqmap[0] = cpu_to_be32(b_ddddd(i)|b_fff(0));
        irqmap[1] = 0;
        irqmap[2] = 0;
        irqmap[3] = 0;
        irqmap[4] = cpu_to_be32(xics_phandle);
        irqmap[5] = cpu_to_be32(phb->lsi_table[i % SPAPR_PCI_NUM_LSI].dt_irq);
        irqmap[6] = cpu_to_be32(0x8);
    }
    /* Write interrupt map */
    _FDT(fdt_setprop(fdt, bus_off, "interrupt-map", &interrupt_map,
                     7 * sizeof(interrupt_map[0])));

    return 0;
}

static void register_types(void)
{
    type_register_static(&spapr_phb_info);
}
type_init(register_types)
