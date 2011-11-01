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

static const uint32_t bars[] = {
    PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_1,
    PCI_BASE_ADDRESS_2, PCI_BASE_ADDRESS_3,
    PCI_BASE_ADDRESS_4, PCI_BASE_ADDRESS_5
    /*, PCI_ROM_ADDRESS*/
};

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
    addr = rtas_ld(args, 0) & 0xFF;
    val = pci_default_read_config(dev, addr, size);
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
    addr = rtas_ld(args, 0) & 0xFF;
    val = pci_default_read_config(dev, addr, size);
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
    addr = rtas_ld(args, 0) & 0xFF;
    pci_default_write_config(dev, addr, val, size);
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
    addr = rtas_ld(args, 0) & 0xFF;
    pci_default_write_config(dev, addr, val, size);
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

static int spapr_phb_init(SysBusDevice *s)
{
    sPAPRPHBState *phb = FROM_SYSBUS(sPAPRPHBState, s);
    int i;

    /* Initialize the LSI table */
    for (i = 0; i < SPAPR_PCI_NUM_LSI; i++) {
        qemu_irq qirq;
        uint32_t num;

        qirq = spapr_allocate_irq(0, &num);
        if (!qirq) {
            return -1;
        }

        phb->lsi_table[i].dt_irq = num;
        phb->lsi_table[i].qirq = qirq;
    }

    return 0;
}

static int spapr_main_pci_host_init(PCIDevice *d)
{
    return 0;
}

static PCIDeviceInfo spapr_main_pci_host_info = {
    .qdev.name = "spapr-pci-host-bridge",
    .qdev.size = sizeof(PCIDevice),
    .init      = spapr_main_pci_host_init,
};

static void spapr_register_devices(void)
{
    sysbus_register_dev("spapr-pci-host-bridge", sizeof(sPAPRPHBState),
                        spapr_phb_init);
    pci_qdev_register(&spapr_main_pci_host_info);
}

device_init(spapr_register_devices)

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

static MemoryRegionOps spapr_io_ops = {
    .endianness = DEVICE_LITTLE_ENDIAN,
    .read = spapr_io_read,
    .write = spapr_io_write
};

void spapr_create_phb(sPAPREnvironment *spapr,
                      const char *busname, uint64_t buid,
                      uint64_t mem_win_addr, uint64_t mem_win_size,
                      uint64_t io_win_addr)
{
    DeviceState *dev;
    SysBusDevice *s;
    sPAPRPHBState *phb;
    PCIBus *bus;
    char namebuf[strlen(busname)+11];

    dev = qdev_create(NULL, "spapr-pci-host-bridge");
    qdev_init_nofail(dev);
    s = sysbus_from_qdev(dev);
    phb = FROM_SYSBUS(sPAPRPHBState, s);

    phb->mem_win_addr = mem_win_addr;

    sprintf(namebuf, "%s-mem", busname);
    memory_region_init(&phb->memspace, namebuf, INT64_MAX);

    sprintf(namebuf, "%s-memwindow", busname);
    memory_region_init_alias(&phb->memwindow, namebuf, &phb->memspace,
                             SPAPR_PCI_MEM_WIN_BUS_OFFSET, mem_win_size);
    memory_region_add_subregion(get_system_memory(), mem_win_addr,
                                &phb->memwindow);

    phb->io_win_addr = io_win_addr;

    /* On ppc, we only have MMIO no specific IO space from the CPU
     * perspective.  In theory we ought to be able to embed the PCI IO
     * memory region direction in the system memory space.  However,
     * if any of the IO BAR subregions use the old_portio mechanism,
     * that won't be processed properly unless accessed from the
     * system io address space.  This hack to bounce things via
     * system_io works around the problem until all the users of
     * old_portion are updated */
    sprintf(namebuf, "%s-io", busname);
    memory_region_init(&phb->iospace, namebuf, SPAPR_PCI_IO_WIN_SIZE);
    /* FIXME: fix to support multiple PHBs */
    memory_region_add_subregion(get_system_io(), 0, &phb->iospace);

    sprintf(namebuf, "%s-iowindow", busname);
    memory_region_init_io(&phb->iowindow, &spapr_io_ops, phb,
                          namebuf, SPAPR_PCI_IO_WIN_SIZE);
    memory_region_add_subregion(get_system_memory(), io_win_addr,
                                &phb->iowindow);

    phb->host_state.bus = bus = pci_register_bus(&phb->busdev.qdev, busname,
                                                 pci_spapr_set_irq,
                                                 pci_spapr_map_irq,
                                                 phb,
                                                 &phb->memspace, &phb->iospace,
                                                 PCI_DEVFN(0, 0),
                                                 SPAPR_PCI_NUM_LSI);

    spapr_rtas_register("read-pci-config", rtas_read_pci_config);
    spapr_rtas_register("write-pci-config", rtas_write_pci_config);
    spapr_rtas_register("ibm,read-pci-config", rtas_ibm_read_pci_config);
    spapr_rtas_register("ibm,write-pci-config", rtas_ibm_write_pci_config);

    QLIST_INSERT_HEAD(&spapr->phbs, phb, list);

    /* pci_bus_set_mem_base(bus, mem_va_start - SPAPR_PCI_MEM_BAR_START); */
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

static uint32_t regtype_to_ss(uint8_t type)
{
    if (type & PCI_BASE_ADDRESS_MEM_TYPE_64) {
        return 3;
    }
    if (type == PCI_BASE_ADDRESS_SPACE_IO) {
        return 1;
    }
    return 2;
}

int spapr_populate_pci_devices(sPAPRPHBState *phb,
                               uint32_t xics_phandle,
                               void *fdt)
{
    PCIBus *bus = phb->host_state.bus;
    int bus_off, node_off = 0, devid, fn, i, n, devices;
    DeviceState *qdev;
    char nodename[256];
    struct {
        uint32_t hi;
        uint64_t addr;
        uint64_t size;
    } __attribute__((packed)) reg[PCI_NUM_REGIONS + 1],
          assigned_addresses[PCI_NUM_REGIONS];
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
        cpu_to_be32(b_ddddd(-1)|b_fff(-1)), 0x0, 0x0, 0x0};
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
    _FDT(fdt_setprop(fdt, bus_off, "interrupt-map-mask",
                     &interrupt_map_mask, sizeof(interrupt_map_mask)));

    /* Populate PCI devices and allocate IRQs */
    devices = 0;
    QTAILQ_FOREACH(qdev, &bus->qbus.children, sibling) {
        PCIDevice *dev = DO_UPCAST(PCIDevice, qdev, qdev);
        int irq_index = pci_spapr_map_irq(dev, 0);
        uint32_t *irqmap = interrupt_map[devices];
        uint8_t *config = dev->config;

        devid = dev->devfn >> 3;
        fn = dev->devfn & 7;

        sprintf(nodename, "pci@%u,%u", devid, fn);

        /* Allocate interrupt from the map */
        if (devid > bus->nirq)  {
            printf("Unexpected behaviour in spapr_populate_pci_devices,"
                    "wrong devid %u\n", devid);
            exit(-1);
        }
        irqmap[0] = cpu_to_be32(b_ddddd(devid)|b_fff(fn));
        irqmap[1] = 0;
        irqmap[2] = 0;
        irqmap[3] = 0;
        irqmap[4] = cpu_to_be32(xics_phandle);
        irqmap[5] = cpu_to_be32(phb->lsi_table[irq_index].dt_irq);
        irqmap[6] = cpu_to_be32(0x8);

        /* Add node to FDT */
        node_off = fdt_add_subnode(fdt, bus_off, nodename);
        if (node_off < 0) {
            return node_off;
        }

        _FDT(fdt_setprop_cell(fdt, node_off, "vendor-id",
                              pci_get_word(&config[PCI_VENDOR_ID])));
        _FDT(fdt_setprop_cell(fdt, node_off, "device-id",
                              pci_get_word(&config[PCI_DEVICE_ID])));
        _FDT(fdt_setprop_cell(fdt, node_off, "revision-id",
                              pci_get_byte(&config[PCI_REVISION_ID])));
        _FDT(fdt_setprop_cell(fdt, node_off, "class-code",
                              pci_get_long(&config[PCI_CLASS_REVISION]) >> 8));
        _FDT(fdt_setprop_cell(fdt, node_off, "subsystem-id",
                              pci_get_word(&config[PCI_SUBSYSTEM_ID])));
        _FDT(fdt_setprop_cell(fdt, node_off, "subsystem-vendor-id",
                              pci_get_word(&config[PCI_SUBSYSTEM_VENDOR_ID])));

        /* Config space region comes first */
        reg[0].hi = cpu_to_be32(
            b_n(0) |
            b_p(0) |
            b_t(0) |
            b_ss(0/*config*/) |
            b_bbbbbbbb(0) |
            b_ddddd(devid) |
            b_fff(fn));
        reg[0].addr = 0;
        reg[0].size = 0;

        n = 0;
        for (i = 0; i < PCI_NUM_REGIONS; ++i) {
            if (0 == dev->io_regions[i].size) {
                continue;
            }

            reg[n+1].hi = cpu_to_be32(
                b_n(0) |
                b_p(0) |
                b_t(0) |
                b_ss(regtype_to_ss(dev->io_regions[i].type)) |
                b_bbbbbbbb(0) |
                b_ddddd(devid) |
                b_fff(fn) |
                b_rrrrrrrr(bars[i]));
            reg[n+1].addr = 0;
            reg[n+1].size = cpu_to_be64(dev->io_regions[i].size);

            assigned_addresses[n].hi = cpu_to_be32(
                b_n(1) |
                b_p(0) |
                b_t(0) |
                b_ss(regtype_to_ss(dev->io_regions[i].type)) |
                b_bbbbbbbb(0) |
                b_ddddd(devid) |
                b_fff(fn) |
                b_rrrrrrrr(bars[i]));

            /*
             * Writing zeroes to assigned_addresses causes the guest kernel to
             * reassign BARs
             */
            assigned_addresses[n].addr = cpu_to_be64(dev->io_regions[i].addr);
            assigned_addresses[n].size = reg[n+1].size;

            ++n;
        }
        _FDT(fdt_setprop(fdt, node_off, "reg", reg, sizeof(reg[0])*(n+1)));
        _FDT(fdt_setprop(fdt, node_off, "assigned-addresses",
                         assigned_addresses,
                         sizeof(assigned_addresses[0])*(n)));
        _FDT(fdt_setprop_cell(fdt, node_off, "interrupts",
                              pci_get_byte(&config[PCI_INTERRUPT_PIN])));

        ++devices;
    }

    /* Write interrupt map */
    _FDT(fdt_setprop(fdt, bus_off, "interrupt-map", &interrupt_map,
                     devices * sizeof(interrupt_map[0])));

    return 0;
}
