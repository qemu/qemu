/*
 * QEMU Sun4m System Emulator
 * 
 * Copyright (c) 2003-2004 Fabrice Bellard
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
#include "vl.h"
#include "m48t08.h"

#define KERNEL_LOAD_ADDR     0x00004000
#define MMU_CONTEXT_TBL      0x00003000
#define MMU_L1PTP            (MMU_CONTEXT_TBL + 0x0400)
#define MMU_L2PTP            (MMU_CONTEXT_TBL + 0x0800)
#define PROM_ADDR	     0xffd04000
#define PROM_FILENAMEB	     "proll.bin"
#define PROM_FILENAMEE	     "proll.elf"
#define PROLL_MAGIC_ADDR 0x20000000
#define PHYS_JJ_EEPROM	0x71200000	/* [2000] MK48T08 */
#define PHYS_JJ_IDPROM_OFF	0x1FD8
#define PHYS_JJ_EEPROM_SIZE	0x2000
#define PHYS_JJ_IOMMU	0x10000000	/* First page of sun4m IOMMU */
#define PHYS_JJ_TCX_FB	0x50800000	/* Start address, frame buffer body */
#define PHYS_JJ_TCX_0E	0x5E000000	/* Top address, one byte used. */
#define PHYS_JJ_IOMMU	0x10000000	/* First page of sun4m IOMMU */
#define PHYS_JJ_LEDMA   0x78400010      /* ledma, off by 10 from unused SCSI */
#define PHYS_JJ_LE      0x78C00000      /* LANCE, typical sun4m */
#define PHYS_JJ_LE_IRQ  6
#define PHYS_JJ_CLOCK	0x71D00000
#define PHYS_JJ_CLOCK_IRQ  10
#define PHYS_JJ_CLOCK1	0x71D10000
#define PHYS_JJ_CLOCK1_IRQ  14
#define PHYS_JJ_INTR0	0x71E00000	/* CPU0 interrupt control registers */
#define PHYS_JJ_INTR_G	0x71E10000	/* Master interrupt control registers */

/* TSC handling */

uint64_t cpu_get_tsc()
{
    return qemu_get_clock(vm_clock);
}

void DMA_run() {}
void SB16_run() {}
int serial_can_receive(SerialState *s) { return 0; }
void serial_receive_byte(SerialState *s, int ch) {}
void serial_receive_break(SerialState *s) {}

static m48t08_t *nvram;

/* Sun4m hardware initialisation */
void sun4m_init(int ram_size, int vga_ram_size, int boot_device,
             DisplayState *ds, const char **fd_filename, int snapshot,
             const char *kernel_filename, const char *kernel_cmdline,
             const char *initrd_filename)
{
    char buf[1024];
    int ret, linux_boot;
    unsigned long bios_offset;

    linux_boot = (kernel_filename != NULL);

    /* allocate RAM */
    cpu_register_physical_memory(0, ram_size, 0);
    bios_offset = ram_size;

    iommu_init(PHYS_JJ_IOMMU);
    sched_init(PHYS_JJ_INTR0, PHYS_JJ_INTR_G);
    tcx_init(ds, PHYS_JJ_TCX_FB);
    lance_init(&nd_table[0], PHYS_JJ_LE_IRQ, PHYS_JJ_LE, PHYS_JJ_LEDMA);
    nvram = m48t08_init(PHYS_JJ_EEPROM, PHYS_JJ_EEPROM_SIZE, &nd_table[0].macaddr);
    timer_init(PHYS_JJ_CLOCK, PHYS_JJ_CLOCK_IRQ);
    timer_init(PHYS_JJ_CLOCK1, PHYS_JJ_CLOCK1_IRQ);
    magic_init(kernel_filename, phys_ram_base + KERNEL_LOAD_ADDR, PROLL_MAGIC_ADDR);

    /* We load Proll as the kernel and start it. It will issue a magic
       IO to load the real kernel */
    if (linux_boot) {
	snprintf(buf, sizeof(buf), "%s/%s", bios_dir, PROM_FILENAMEB);
        ret = load_kernel(buf, 
		       phys_ram_base + KERNEL_LOAD_ADDR);
        if (ret < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n", 
                    buf);
            exit(1);
        }
    }
    /* Setup a MMU entry for entire address space */
    stl_raw(phys_ram_base + MMU_CONTEXT_TBL, (MMU_L1PTP >> 4) | 1);
    stl_raw(phys_ram_base + MMU_L1PTP, (MMU_L2PTP >> 4) | 1);
    stl_raw(phys_ram_base + MMU_L1PTP + (0x01 << 2), (MMU_L2PTP >> 4) | 1); // 01.. == 00..
    stl_raw(phys_ram_base + MMU_L1PTP + (0xff << 2), (MMU_L2PTP >> 4) | 1); // ff.. == 00..
    stl_raw(phys_ram_base + MMU_L1PTP + (0xf0 << 2), (MMU_L2PTP >> 4) | 1); // f0.. == 00..
    /* 3 = U:RWX S:RWX */
    stl_raw(phys_ram_base + MMU_L2PTP, (3 << PTE_ACCESS_SHIFT) | 2);
    stl_raw(phys_ram_base + MMU_L2PTP, ((0x01 << PTE_PPN_SHIFT) >> 4 ) | (3 << PTE_ACCESS_SHIFT) | 2);
}
