/*
 * Renesas SH7751R R2D-PLUS emulation
 *
 * Copyright (c) 2007 Magnus Damm
 * Copyright (c) 2008 Paul Mundt
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
#include "devices.h"
#include "sysemu.h"
#include "boards.h"
#include "pci.h"
#include "net.h"
#include "sh7750_regs.h"

#define SDRAM_BASE 0x0c000000 /* Physical location of SDRAM: Area 3 */
#define SDRAM_SIZE 0x04000000

#define SM501_VRAM_SIZE 0x800000

#define PA_IRLMSK	0x00
#define PA_POWOFF	0x30
#define PA_VERREG	0x32
#define PA_OUTPORT	0x36

typedef struct {
    uint16_t bcr;
    uint16_t irlmsk;
    uint16_t irlmon;
    uint16_t cfctl;
    uint16_t cfpow;
    uint16_t dispctl;
    uint16_t sdmpow;
    uint16_t rtcce;
    uint16_t pcicd;
    uint16_t voyagerrts;
    uint16_t cfrst;
    uint16_t admrts;
    uint16_t extrst;
    uint16_t cfcdintclr;
    uint16_t keyctlclr;
    uint16_t pad0;
    uint16_t pad1;
    uint16_t powoff;
    uint16_t verreg;
    uint16_t inport;
    uint16_t outport;
    uint16_t bverreg;

/* output pin */
    qemu_irq irl;
} r2d_fpga_t;

enum r2d_fpga_irq {
    PCI_INTD, CF_IDE, CF_CD, PCI_INTC, SM501, KEY, RTC_A, RTC_T,
    SDCARD, PCI_INTA, PCI_INTB, EXT, TP,
    NR_IRQS
};

static const struct { short irl; uint16_t msk; } irqtab[NR_IRQS] = {
    [CF_IDE]	= {  1, 1<<9 },
    [CF_CD]	= {  2, 1<<8 },
    [PCI_INTA]	= {  9, 1<<14 },
    [PCI_INTB]	= { 10, 1<<13 },
    [PCI_INTC]	= {  3, 1<<12 },
    [PCI_INTD]	= {  0, 1<<11 },
    [SM501]	= {  4, 1<<10 },
    [KEY]	= {  5, 1<<6 },
    [RTC_A]	= {  6, 1<<5 },
    [RTC_T]	= {  7, 1<<4 },
    [SDCARD]	= {  8, 1<<7 },
    [EXT]	= { 11, 1<<0 },
    [TP]	= { 12, 1<<15 },
};

static void update_irl(r2d_fpga_t *fpga)
{
    int i, irl = 15;
    for (i = 0; i < NR_IRQS; i++)
        if (fpga->irlmon & fpga->irlmsk & irqtab[i].msk)
            if (irqtab[i].irl < irl)
                irl = irqtab[i].irl;
    qemu_set_irq(fpga->irl, irl ^ 15);
}

static void r2d_fpga_irq_set(void *opaque, int n, int level)
{
    r2d_fpga_t *fpga = opaque;
    if (level)
        fpga->irlmon |= irqtab[n].msk;
    else
        fpga->irlmon &= ~irqtab[n].msk;
    update_irl(fpga);
}

static uint32_t r2d_fpga_read(void *opaque, target_phys_addr_t addr)
{
    r2d_fpga_t *s = opaque;

    switch (addr) {
    case PA_IRLMSK:
        return s->irlmsk;
    case PA_OUTPORT:
	return s->outport;
    case PA_POWOFF:
	return s->powoff;
    case PA_VERREG:
	return 0x10;
    }

    return 0;
}

static void
r2d_fpga_write(void *opaque, target_phys_addr_t addr, uint32_t value)
{
    r2d_fpga_t *s = opaque;

    switch (addr) {
    case PA_IRLMSK:
        s->irlmsk = value;
        update_irl(s);
	break;
    case PA_OUTPORT:
	s->outport = value;
	break;
    case PA_POWOFF:
	s->powoff = value;
	break;
    case PA_VERREG:
	/* Discard writes */
	break;
    }
}

static CPUReadMemoryFunc *r2d_fpga_readfn[] = {
    r2d_fpga_read,
    r2d_fpga_read,
    NULL,
};

static CPUWriteMemoryFunc *r2d_fpga_writefn[] = {
    r2d_fpga_write,
    r2d_fpga_write,
    NULL,
};

static qemu_irq *r2d_fpga_init(target_phys_addr_t base, qemu_irq irl)
{
    int iomemtype;
    r2d_fpga_t *s;

    s = qemu_mallocz(sizeof(r2d_fpga_t));
    if (!s)
        return NULL;

    s->irl = irl;

    iomemtype = cpu_register_io_memory(0, r2d_fpga_readfn,
				       r2d_fpga_writefn, s);
    cpu_register_physical_memory(base, 0x40, iomemtype);
    return qemu_allocate_irqs(r2d_fpga_irq_set, s, NR_IRQS);
}

static void r2d_pci_set_irq(qemu_irq *p, int n, int l)
{
    qemu_set_irq(p[n], l);
}

static int r2d_pci_map_irq(PCIDevice *d, int irq_num)
{
    const int intx[] = { PCI_INTA, PCI_INTB, PCI_INTC, PCI_INTD };
    return intx[d->devfn >> 3];
}

static void r2d_init(ram_addr_t ram_size, int vga_ram_size,
              const char *boot_device,
	      const char *kernel_filename, const char *kernel_cmdline,
	      const char *initrd_filename, const char *cpu_model)
{
    CPUState *env;
    struct SH7750State *s;
    ram_addr_t sdram_addr, sm501_vga_ram_addr;
    qemu_irq *irq;
    PCIBus *pci;
    int i;

    if (!cpu_model)
        cpu_model = "SH7751R";

    env = cpu_init(cpu_model);
    if (!env) {
        fprintf(stderr, "Unable to find CPU definition\n");
        exit(1);
    }

    /* Allocate memory space */
    sdram_addr = qemu_ram_alloc(SDRAM_SIZE);
    cpu_register_physical_memory(SDRAM_BASE, SDRAM_SIZE, sdram_addr);
    /* Register peripherals */
    s = sh7750_init(env);
    irq = r2d_fpga_init(0x04000000, sh7750_irl(s));
    pci = sh_pci_register_bus(r2d_pci_set_irq, r2d_pci_map_irq, irq, 0, 4);

    sm501_vga_ram_addr = qemu_ram_alloc(SM501_VRAM_SIZE);
    sm501_init(0x10000000, sm501_vga_ram_addr, SM501_VRAM_SIZE,
	       serial_hds[2]);

    /* onboard CF (True IDE mode, Master only). */
    mmio_ide_init(0x14001000, 0x1400080c, irq[CF_IDE], 1,
        drives_table[drive_get_index(IF_IDE, 0, 0)].bdrv, NULL);

    /* NIC: rtl8139 on-board, and 2 slots. */
    pci_nic_init(pci, &nd_table[0], 2 << 3, "rtl8139");
    for (i = 1; i < nb_nics; i++)
        pci_nic_init(pci, &nd_table[i], -1, "ne2k_pci");

    /* Todo: register on board registers */
    {
      int kernel_size;
      /* initialization which should be done by firmware */
      uint32_t bcr1 = 1 << 3; /* cs3 SDRAM */
      uint16_t bcr2 = 3 << (3 * 2); /* cs3 32-bit */
      cpu_physical_memory_write(SH7750_BCR1_A7, (uint8_t *)&bcr1, 4);
      cpu_physical_memory_write(SH7750_BCR2_A7, (uint8_t *)&bcr2, 2);

      kernel_size = load_image(kernel_filename, phys_ram_base);

      if (kernel_size < 0) {
        fprintf(stderr, "qemu: could not load kernel '%s'\n", kernel_filename);
        exit(1);
      }

      env->pc = SDRAM_BASE | 0xa0000000; /* Start from P2 area */
    }
}

QEMUMachine r2d_machine = {
    .name = "r2d",
    .desc = "r2d-plus board",
    .init = r2d_init,
    .ram_require = (SDRAM_SIZE + SM501_VRAM_SIZE) | RAMSIZE_FIXED,
};
