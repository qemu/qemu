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
#define ROMVEC_DATA          (MMU_CONTEXT_TBL + 0x1800)
#define PROM_ADDR	     0xffd04000
#define PROM_FILENAME	     "proll.bin"
#define PHYS_JJ_EEPROM	0x71200000	/* [2000] MK48T08 */
#define PHYS_JJ_IDPROM_OFF	0x1FD8
#define PHYS_JJ_EEPROM_SIZE	0x2000

/* TSC handling */

uint64_t cpu_get_tsc()
{
    return qemu_get_clock(vm_clock);
}

void DMA_run() {}
void SB16_run() {}
void vga_invalidate_display() {}
void vga_screen_dump(const char *filename) {}
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
    int ret, linux_boot, bios_size;
    unsigned long bios_offset;

    linux_boot = (kernel_filename != NULL);

    /* allocate RAM */
    cpu_register_physical_memory(0, ram_size, 0);
    bios_offset = ram_size;

    iommu_init();
    sched_init();
    tcx_init(ds);
    lance_init(&nd_table[0], 6);
    nvram = m48t08_init(PHYS_JJ_EEPROM, PHYS_JJ_EEPROM_SIZE);

    magic_init(kernel_filename, phys_ram_base + KERNEL_LOAD_ADDR);

#if 0
    snprintf(buf, sizeof(buf), "%s/%s", bios_dir, PROM_FILENAME);
    bios_size = get_image_size(buf);
    ret = load_image(buf, phys_ram_base + bios_offset);
    if (ret != bios_size) {
        fprintf(stderr, "qemu: could not load prom '%s'\n", buf);
        exit(1);
    }
    cpu_register_physical_memory(PROM_ADDR, 
                                 bios_size, bios_offset | IO_MEM_ROM);
#endif

    /* We load Proll as the kernel and start it. It will issue a magic
       IO to load the real kernel */
    if (linux_boot) {
	snprintf(buf, sizeof(buf), "%s/%s", bios_dir, PROM_FILENAME);
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
#if 0
    stl_raw(phys_ram_base + MMU_L1PTP + (0x50 << 2), (MMU_L2PTP >> 4) | 1); // frame buffer at 50..
#endif
    stl_raw(phys_ram_base + MMU_L1PTP + (0xff << 2), (MMU_L2PTP >> 4) | 1); // ff.. == 00..
    /* 3 = U:RWX S:RWX */
    stl_raw(phys_ram_base + MMU_L2PTP, (3 << PTE_ACCESS_SHIFT) | 2);
#if 0
    stl_raw(phys_ram_base + MMU_L2PTP + 0x84, (PHYS_JJ_TCX_FB >> 4) \
	    | (3 << PTE_ACCESS_SHIFT) | 2); // frame buf
    stl_raw(phys_ram_base + MMU_L2PTP + 0x88, (PHYS_JJ_TCX_FB >> 4) \
	    | (3 << PTE_ACCESS_SHIFT) | 2); // frame buf
    stl_raw(phys_ram_base + MMU_L2PTP + 0x140, (PHYS_JJ_TCX_FB >> 4) \
	    | (3 << PTE_ACCESS_SHIFT) | 2); // frame buf
    // "Empirical constant"
    stl_raw(phys_ram_base + ROMVEC_DATA, 0x10010407);

    // Version: V3 prom
    stl_raw(phys_ram_base + ROMVEC_DATA + 4, 3);

    stl_raw(phys_ram_base + ROMVEC_DATA + 0x1c, ROMVEC_DATA+0x400);
    stl_raw(phys_ram_base + ROMVEC_DATA + 0x400, ROMVEC_DATA+0x404);
    stl_raw(phys_ram_base + ROMVEC_DATA + 0x404, 0x81c3e008); // retl
    stl_raw(phys_ram_base + ROMVEC_DATA + 0x408, 0x01000000); // nop
#endif
}
