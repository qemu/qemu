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
#include "bswap.h"

typedef struct {
    PCIBus *bus;
    PCIDevice *dev;
    uint32_t regbase;
    uint32_t iopbase;
    uint32_t membase;
    uint32_t par;
    uint32_t mbr;
    uint32_t iobr;
} SHPCIC;

static void sh_pci_reg_write (void *p, target_phys_addr_t addr, uint32_t val)
{
    SHPCIC *pcic = p;
    addr -= pcic->regbase;
    switch(addr) {
    case 0 ... 0xfc:
        cpu_to_le32w((uint32_t*)(pcic->dev->config + addr), val);
        break;
    case 0x1c0:
        pcic->par = val;
        break;
    case 0x1c4:
        pcic->mbr = val;
        break;
    case 0x1c8:
        pcic->iobr = val;
        break;
    case 0x220:
        pci_data_write(pcic->bus, pcic->par, val, 4);
        break;
    }
}

static uint32_t sh_pci_reg_read (void *p, target_phys_addr_t addr)
{
    SHPCIC *pcic = p;
    addr -= pcic->regbase;
    switch(addr) {
    case 0 ... 0xfc:
        return le32_to_cpup((uint32_t*)(pcic->dev->config + addr));
    case 0x1c0:
        return pcic->par;
    case 0x220:
        return pci_data_read(pcic->bus, pcic->par, 4);
    }
    return 0;
}

static void sh_pci_data_write (SHPCIC *pcic, target_phys_addr_t addr,
                               uint32_t val, int size)
{
    pci_data_write(pcic->bus, addr - pcic->membase + pcic->mbr, val, size);
}

static uint32_t sh_pci_mem_read (SHPCIC *pcic, target_phys_addr_t addr,
                                 int size)
{
    return pci_data_read(pcic->bus, addr - pcic->membase + pcic->mbr, size);
}

static void sh_pci_writeb (void *p, target_phys_addr_t addr, uint32_t val)
{
    sh_pci_data_write(p, addr, val, 1);
}

static void sh_pci_writew (void *p, target_phys_addr_t addr, uint32_t val)
{
    sh_pci_data_write(p, addr, val, 2);
}

static void sh_pci_writel (void *p, target_phys_addr_t addr, uint32_t val)
{
    sh_pci_data_write(p, addr, val, 4);
}

static uint32_t sh_pci_readb (void *p, target_phys_addr_t addr)
{
    return sh_pci_mem_read(p, addr, 1);
}

static uint32_t sh_pci_readw (void *p, target_phys_addr_t addr)
{
    return sh_pci_mem_read(p, addr, 2);
}

static uint32_t sh_pci_readl (void *p, target_phys_addr_t addr)
{
    return sh_pci_mem_read(p, addr, 4);
}

static int sh_pci_addr2port(SHPCIC *pcic, target_phys_addr_t addr)
{
    return addr - pcic->iopbase + pcic->iobr;
}

static void sh_pci_outb (void *p, target_phys_addr_t addr, uint32_t val)
{
    cpu_outb(NULL, sh_pci_addr2port(p, addr), val);
}

static void sh_pci_outw (void *p, target_phys_addr_t addr, uint32_t val)
{
    cpu_outw(NULL, sh_pci_addr2port(p, addr), val);
}

static void sh_pci_outl (void *p, target_phys_addr_t addr, uint32_t val)
{
    cpu_outl(NULL, sh_pci_addr2port(p, addr), val);
}

static uint32_t sh_pci_inb (void *p, target_phys_addr_t addr)
{
    return cpu_inb(NULL, sh_pci_addr2port(p, addr));
}

static uint32_t sh_pci_inw (void *p, target_phys_addr_t addr)
{
    return cpu_inw(NULL, sh_pci_addr2port(p, addr));
}

static uint32_t sh_pci_inl (void *p, target_phys_addr_t addr)
{
    return cpu_inl(NULL, sh_pci_addr2port(p, addr));
}

typedef struct {
    CPUReadMemoryFunc *r[3];
    CPUWriteMemoryFunc *w[3];
} MemOp;

static MemOp sh_pci_reg = {
    { NULL, NULL, sh_pci_reg_read },
    { NULL, NULL, sh_pci_reg_write },
};

static MemOp sh_pci_mem = {
    { sh_pci_readb, sh_pci_readw, sh_pci_readl },
    { sh_pci_writeb, sh_pci_writew, sh_pci_writel },
};

static MemOp sh_pci_iop = {
    { sh_pci_inb, sh_pci_inw, sh_pci_inl },
    { sh_pci_outb, sh_pci_outw, sh_pci_outl },
};

PCIBus *sh_pci_register_bus(pci_set_irq_fn set_irq, pci_map_irq_fn map_irq,
                            qemu_irq *pic, int devfn_min, int nirq)
{
    SHPCIC *p;
    int mem, reg, iop;

    p = qemu_mallocz(sizeof(SHPCIC));
    p->bus = pci_register_bus(set_irq, map_irq, pic, devfn_min, nirq);

    p->dev = pci_register_device(p->bus, "SH PCIC", sizeof(PCIDevice),
                                 -1, NULL, NULL);
    p->regbase = 0x1e200000;
    p->iopbase = 0x1e240000;
    p->membase = 0xfd000000;
    reg = cpu_register_io_memory(0, sh_pci_reg.r, sh_pci_reg.w, p);
    mem = cpu_register_io_memory(0, sh_pci_mem.r, sh_pci_mem.w, p);
    iop = cpu_register_io_memory(0, sh_pci_iop.r, sh_pci_iop.w, p);
    cpu_register_physical_memory(p->regbase, 0x224, reg);
    cpu_register_physical_memory(p->iopbase, 0x40000, iop);
    cpu_register_physical_memory(p->membase, 0x1000000, mem);

    p->dev->config[0x00] = 0x54; // HITACHI
    p->dev->config[0x01] = 0x10; //
    p->dev->config[0x02] = 0x0e; // SH7751R
    p->dev->config[0x03] = 0x35; //
    p->dev->config[0x04] = 0x80;
    p->dev->config[0x05] = 0x00;
    p->dev->config[0x06] = 0x90;
    p->dev->config[0x07] = 0x02;

    return p->bus;
}

