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
#include "hw.h"
#include "sh.h"
#include "pci.h"
#include "pci_host.h"
#include "sh_pci.h"
#include "bswap.h"

typedef struct {
    PCIBus *bus;
    PCIDevice *dev;
    uint32_t par;
    uint32_t mbr;
    uint32_t iobr;
} SHPCIC;

static void sh_pci_reg_write (void *p, target_phys_addr_t addr, uint32_t val)
{
    SHPCIC *pcic = p;
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
            isa_mmio_init(pcic->iobr & 0xfffc0000, 0x40000, 0);
        }
        break;
    case 0x220:
        pci_data_write(pcic->bus, pcic->par, val, 4);
        break;
    }
}

static uint32_t sh_pci_reg_read (void *p, target_phys_addr_t addr)
{
    SHPCIC *pcic = p;
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

PCIBus *sh_pci_register_bus(pci_set_irq_fn set_irq, pci_map_irq_fn map_irq,
                            void *opaque, int devfn_min, int nirq)
{
    SHPCIC *p;
    int reg;

    p = qemu_mallocz(sizeof(SHPCIC));
    p->bus = pci_register_bus(NULL, "pci",
                              set_irq, map_irq, opaque, devfn_min, nirq);

    p->dev = pci_register_device(p->bus, "SH PCIC", sizeof(PCIDevice),
                                 -1, NULL, NULL);
    reg = cpu_register_io_memory(sh_pci_reg.r, sh_pci_reg.w, p);
    cpu_register_physical_memory(0x1e200000, 0x224, reg);
    cpu_register_physical_memory(0xfe200000, 0x224, reg);

    p->iobr = 0xfe240000;
    isa_mmio_init(p->iobr, 0x40000, 0);

    pci_config_set_vendor_id(p->dev->config, PCI_VENDOR_ID_HITACHI);
    pci_config_set_device_id(p->dev->config, PCI_DEVICE_ID_HITACHI_SH7751R);
    p->dev->config[0x04] = 0x80;
    p->dev->config[0x05] = 0x00;
    p->dev->config[0x06] = 0x90;
    p->dev->config[0x07] = 0x02;

    return p->bus;
}
