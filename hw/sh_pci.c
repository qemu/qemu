/*
 * SuperH on-chip PCIC emulation.
 *
 * Copyright (c) 2008 Takashi YOSHII
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
#include "sysbus.h"
#include "sh.h"
#include "pci.h"
#include "pci_host.h"
#include "bswap.h"
#include "exec-memory.h"

typedef struct SHPCIState {
    SysBusDevice busdev;
    PCIBus *bus;
    PCIDevice *dev;
    qemu_irq irq[4];
    int memconfig;
    uint32_t par;
    uint32_t mbr;
    uint32_t iobr;
} SHPCIState;

static void sh_pci_reg_write (void *p, target_phys_addr_t addr, uint32_t val)
{
    SHPCIState *pcic = p;
    switch(addr) {
    case 0 ... 0xfc:
        cpu_to_le32w((uint32_t*)(pcic->dev->config + addr), val);
        break;
    case 0x1c0:
        pcic->par = val;
        break;
    case 0x1c4:
        pcic->mbr = val & 0xff000001;
        break;
    case 0x1c8:
        if ((val & 0xfffc0000) != (pcic->iobr & 0xfffc0000)) {
            cpu_register_physical_memory(pcic->iobr & 0xfffc0000, 0x40000,
                                         IO_MEM_UNASSIGNED);
            pcic->iobr = val & 0xfffc0001;
            isa_mmio_init(pcic->iobr & 0xfffc0000, 0x40000);
        }
        break;
    case 0x220:
        pci_data_write(pcic->bus, pcic->par, val, 4);
        break;
    }
}

static uint32_t sh_pci_reg_read (void *p, target_phys_addr_t addr)
{
    SHPCIState *pcic = p;
    switch(addr) {
    case 0 ... 0xfc:
        return le32_to_cpup((uint32_t*)(pcic->dev->config + addr));
    case 0x1c0:
        return pcic->par;
    case 0x1c4:
        return pcic->mbr;
    case 0x1c8:
        return pcic->iobr;
    case 0x220:
        return pci_data_read(pcic->bus, pcic->par, 4);
    }
    return 0;
}

typedef struct {
    CPUReadMemoryFunc * const r[3];
    CPUWriteMemoryFunc * const w[3];
} MemOp;

static MemOp sh_pci_reg = {
    { NULL, NULL, sh_pci_reg_read },
    { NULL, NULL, sh_pci_reg_write },
};

static int sh_pci_map_irq(PCIDevice *d, int irq_num)
{
    return (d->devfn >> 3);
}

static void sh_pci_set_irq(void *opaque, int irq_num, int level)
{
    qemu_irq *pic = opaque;

    qemu_set_irq(pic[irq_num], level);
}

static void sh_pci_map(SysBusDevice *dev, target_phys_addr_t base)
{
    SHPCIState *s = FROM_SYSBUS(SHPCIState, dev);

    cpu_register_physical_memory(P4ADDR(base), 0x224, s->memconfig);
    cpu_register_physical_memory(A7ADDR(base), 0x224, s->memconfig);

    s->iobr = 0xfe240000;
    isa_mmio_init(s->iobr, 0x40000);
}

static int sh_pci_init_device(SysBusDevice *dev)
{
    SHPCIState *s;
    int i;

    s = FROM_SYSBUS(SHPCIState, dev);
    for (i = 0; i < 4; i++) {
        sysbus_init_irq(dev, &s->irq[i]);
    }
    s->bus = pci_register_bus(&s->busdev.qdev, "pci",
                              sh_pci_set_irq, sh_pci_map_irq,
                              s->irq,
                              get_system_memory(),
                              get_system_io(),
                              PCI_DEVFN(0, 0), 4);
    s->memconfig = cpu_register_io_memory(sh_pci_reg.r, sh_pci_reg.w,
                                          s, DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio_cb(dev, 0x224, sh_pci_map);
    s->dev = pci_create_simple(s->bus, PCI_DEVFN(0, 0), "sh_pci_host");
    return 0;
}

static int sh_pci_host_init(PCIDevice *d)
{
    pci_set_word(d->config + PCI_COMMAND, PCI_COMMAND_WAIT);
    pci_set_word(d->config + PCI_STATUS, PCI_STATUS_CAP_LIST |
                 PCI_STATUS_FAST_BACK | PCI_STATUS_DEVSEL_MEDIUM);
    return 0;
}

static PCIDeviceInfo sh_pci_host_info = {
    .qdev.name = "sh_pci_host",
    .qdev.size = sizeof(PCIDevice),
    .init      = sh_pci_host_init,
    .vendor_id = PCI_VENDOR_ID_HITACHI,
    .device_id = PCI_DEVICE_ID_HITACHI_SH7751R,
};

static void sh_pci_register_devices(void)
{
    sysbus_register_dev("sh_pci", sizeof(SHPCIState),
                        sh_pci_init_device);
    pci_qdev_register(&sh_pci_host_info);
}

device_init(sh_pci_register_devices)
