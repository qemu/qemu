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
#define CMDLINE_ADDR         0x007ff000
#define INITRD_LOAD_ADDR     0x00800000
#define PROM_ADDR	     0xffd00000
#define PROM_FILENAMEB	     "proll.bin"
#define PROM_FILENAMEE	     "proll.elf"
#define PHYS_JJ_EEPROM	0x71200000	/* m48t08 */
#define PHYS_JJ_IDPROM_OFF	0x1FD8
#define PHYS_JJ_EEPROM_SIZE	0x2000
// IRQs are not PIL ones, but master interrupt controller register
// bits
#define PHYS_JJ_IOMMU	0x10000000	/* I/O MMU */
#define PHYS_JJ_TCX_FB	0x50800000	/* Start address, frame buffer body */
#define PHYS_JJ_LEDMA   0x78400010      /* Lance DMA controller */
#define PHYS_JJ_LE      0x78C00000      /* Lance ethernet */
#define PHYS_JJ_LE_IRQ     16
#define PHYS_JJ_CLOCK	0x71D00000      /* Per-CPU timer/counter, L14 */
#define PHYS_JJ_CLOCK_IRQ  7
#define PHYS_JJ_CLOCK1	0x71D10000      /* System timer/counter, L10 */
#define PHYS_JJ_CLOCK1_IRQ 19
#define PHYS_JJ_INTR0	0x71E00000	/* Per-CPU interrupt control registers */
#define PHYS_JJ_INTR_G	0x71E10000	/* Master interrupt control registers */
#define PHYS_JJ_MS_KBD	0x71000000	/* Mouse and keyboard */
#define PHYS_JJ_MS_KBD_IRQ    14
#define PHYS_JJ_SER	0x71100000	/* Serial */
#define PHYS_JJ_SER_IRQ    15
#define PHYS_JJ_SCSI_IRQ   18
#define PHYS_JJ_FDC	0x71400000	/* Floppy */
#define PHYS_JJ_FLOPPY_IRQ 22

/* TSC handling */

uint64_t cpu_get_tsc()
{
    return qemu_get_clock(vm_clock);
}

void DMA_run() {}

static m48t08_t *nvram;

static void nvram_init(m48t08_t *nvram, uint8_t *macaddr, const char *cmdline)
{
    unsigned char tmp = 0;
    int i, j;

    i = 0x40;
    if (cmdline) {
	uint32_t cmdline_len;

	strcpy(phys_ram_base + CMDLINE_ADDR, cmdline);
	m48t08_write(nvram, i++, CMDLINE_ADDR >> 24);
	m48t08_write(nvram, i++, (CMDLINE_ADDR >> 16) & 0xff);
	m48t08_write(nvram, i++, (CMDLINE_ADDR >> 8) & 0xff);
	m48t08_write(nvram, i++, CMDLINE_ADDR & 0xff);

	cmdline_len = strlen(cmdline);
	m48t08_write(nvram, i++, cmdline_len >> 24);
	m48t08_write(nvram, i++, (cmdline_len >> 16) & 0xff);
	m48t08_write(nvram, i++, (cmdline_len >> 8) & 0xff);
	m48t08_write(nvram, i++, cmdline_len & 0xff);
    }

    i = 0x1fd8;
    m48t08_write(nvram, i++, 0x01);
    m48t08_write(nvram, i++, 0x80); /* Sun4m OBP */
    j = 0;
    m48t08_write(nvram, i++, macaddr[j++]);
    m48t08_write(nvram, i++, macaddr[j++]);
    m48t08_write(nvram, i++, macaddr[j++]);
    m48t08_write(nvram, i++, macaddr[j++]);
    m48t08_write(nvram, i++, macaddr[j++]);
    m48t08_write(nvram, i, macaddr[j]);

    /* Calculate checksum */
    for (i = 0x1fd8; i < 0x1fe7; i++) {
	tmp ^= m48t08_read(nvram, i);
    }
    m48t08_write(nvram, 0x1fe7, tmp);
}

static void *slavio_intctl;

void pic_info()
{
    slavio_pic_info(slavio_intctl);
}

void irq_info()
{
    slavio_irq_info(slavio_intctl);
}

void pic_set_irq(int irq, int level)
{
    slavio_pic_set_irq(slavio_intctl, irq, level);
}

static void *tcx;

void vga_update_display()
{
    tcx_update_display(tcx);
}

void vga_invalidate_display()
{
    tcx_invalidate_display(tcx);
}

void vga_screen_dump(const char *filename)
{
    tcx_screen_dump(tcx, filename);
}

static void *iommu;

uint32_t iommu_translate(uint32_t addr)
{
    return iommu_translate_local(iommu, addr);
}

/* Sun4m hardware initialisation */
void sun4m_init(int ram_size, int vga_ram_size, int boot_device,
             DisplayState *ds, const char **fd_filename, int snapshot,
             const char *kernel_filename, const char *kernel_cmdline,
             const char *initrd_filename)
{
    char buf[1024];
    int ret, linux_boot;
    unsigned int i;
    unsigned long vram_size = 0x100000, prom_offset, initrd_size;

    linux_boot = (kernel_filename != NULL);

    /* allocate RAM */
    cpu_register_physical_memory(0, ram_size, 0);

    iommu = iommu_init(PHYS_JJ_IOMMU);
    slavio_intctl = slavio_intctl_init(PHYS_JJ_INTR0, PHYS_JJ_INTR_G);
    tcx = tcx_init(ds, PHYS_JJ_TCX_FB, phys_ram_base + ram_size, ram_size, vram_size);
    lance_init(&nd_table[0], PHYS_JJ_LE_IRQ, PHYS_JJ_LE, PHYS_JJ_LEDMA);
    nvram = m48t08_init(PHYS_JJ_EEPROM, PHYS_JJ_EEPROM_SIZE);
    nvram_init(nvram, (uint8_t *)&nd_table[0].macaddr, kernel_cmdline);
    slavio_timer_init(PHYS_JJ_CLOCK, PHYS_JJ_CLOCK_IRQ, PHYS_JJ_CLOCK1, PHYS_JJ_CLOCK1_IRQ);
    slavio_serial_ms_kbd_init(PHYS_JJ_MS_KBD, PHYS_JJ_MS_KBD_IRQ);
    slavio_serial_init(PHYS_JJ_SER, PHYS_JJ_SER_IRQ, serial_hds[0], serial_hds[1]);
    fdctrl_init(PHYS_JJ_FLOPPY_IRQ, 0, 1, PHYS_JJ_FDC, fd_table);

    prom_offset = ram_size + vram_size;

    snprintf(buf, sizeof(buf), "%s/%s", bios_dir, PROM_FILENAMEE);
    ret = load_elf(buf, phys_ram_base + prom_offset);
    if (ret < 0) {
	snprintf(buf, sizeof(buf), "%s/%s", bios_dir, PROM_FILENAMEB);
	ret = load_image(buf, phys_ram_base + prom_offset);
    }
    if (ret < 0) {
	fprintf(stderr, "qemu: could not load prom '%s'\n", 
		buf);
	exit(1);
    }
    cpu_register_physical_memory(PROM_ADDR, (ret + TARGET_PAGE_SIZE) & TARGET_PAGE_MASK, 
                                 prom_offset | IO_MEM_ROM);

    if (linux_boot) {
        ret = load_elf(kernel_filename, phys_ram_base + KERNEL_LOAD_ADDR);
        if (ret < 0)
	    ret = load_aout(kernel_filename, phys_ram_base + KERNEL_LOAD_ADDR);
	if (ret < 0)
	    ret = load_image(kernel_filename, phys_ram_base + KERNEL_LOAD_ADDR);
        if (ret < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n", 
                    kernel_filename);
	    exit(1);
        }

        /* load initrd */
        initrd_size = 0;
        if (initrd_filename) {
            initrd_size = load_image(initrd_filename, phys_ram_base + INITRD_LOAD_ADDR);
            if (initrd_size < 0) {
                fprintf(stderr, "qemu: could not load initial ram disk '%s'\n", 
                        initrd_filename);
                exit(1);
            }
        }
        if (initrd_size > 0) {
	    for (i = 0; i < 64 * TARGET_PAGE_SIZE; i += TARGET_PAGE_SIZE) {
		if (ldl_raw(phys_ram_base + KERNEL_LOAD_ADDR + i)
		    == 0x48647253) { // HdrS
		    stl_raw(phys_ram_base + KERNEL_LOAD_ADDR + i + 16, INITRD_LOAD_ADDR);
		    stl_raw(phys_ram_base + KERNEL_LOAD_ADDR + i + 20, initrd_size);
		    break;
		}
	    }
        }
    }
}
