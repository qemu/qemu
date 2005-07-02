/*
 * QEMU Sun4u System Emulator
 * 
 * Copyright (c) 2005 Fabrice Bellard
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
#define PROM_FILENAMEB	     "proll-sparc64.bin"
#define PROM_FILENAMEE	     "proll-sparc64.elf"
#define PHYS_JJ_EEPROM	0x71200000	/* m48t08 */
#define PHYS_JJ_IDPROM_OFF	0x1FD8
#define PHYS_JJ_EEPROM_SIZE	0x2000
// IRQs are not PIL ones, but master interrupt controller register
// bits
#define PHYS_JJ_MS_KBD	0x71000000	/* Mouse and keyboard */
#define PHYS_JJ_MS_KBD_IRQ    14
#define PHYS_JJ_SER	0x71100000	/* Serial */
#define PHYS_JJ_SER_IRQ    15

/* TSC handling */

uint64_t cpu_get_tsc()
{
    return qemu_get_clock(vm_clock);
}

int DMA_get_channel_mode (int nchan)
{
    return 0;
}
int DMA_read_memory (int nchan, void *buf, int pos, int size)
{
    return 0;
}
int DMA_write_memory (int nchan, void *buf, int pos, int size)
{
    return 0;
}
void DMA_hold_DREQ (int nchan) {}
void DMA_release_DREQ (int nchan) {}
void DMA_schedule(int nchan) {}
void DMA_run (void) {}
void DMA_init (int high_page_enable) {}
void DMA_register_channel (int nchan,
                           DMA_transfer_handler transfer_handler,
                           void *opaque)
{
}

static void nvram_set_word (m48t08_t *nvram, uint32_t addr, uint16_t value)
{
    m48t08_write(nvram, addr++, (value >> 8) & 0xff);
    m48t08_write(nvram, addr++, value & 0xff);
}

static void nvram_set_lword (m48t08_t *nvram, uint32_t addr, uint32_t value)
{
    m48t08_write(nvram, addr++, value >> 24);
    m48t08_write(nvram, addr++, (value >> 16) & 0xff);
    m48t08_write(nvram, addr++, (value >> 8) & 0xff);
    m48t08_write(nvram, addr++, value & 0xff);
}

static void nvram_set_string (m48t08_t *nvram, uint32_t addr,
                       const unsigned char *str, uint32_t max)
{
    unsigned int i;

    for (i = 0; i < max && str[i] != '\0'; i++) {
        m48t08_write(nvram, addr + i, str[i]);
    }
    m48t08_write(nvram, addr + max - 1, '\0');
}

static m48t08_t *nvram;

extern int nographic;

static void nvram_init(m48t08_t *nvram, uint8_t *macaddr, const char *cmdline,
		       int boot_device, uint32_t RAM_size,
		       uint32_t kernel_size,
		       int width, int height, int depth)
{
    unsigned char tmp = 0;
    int i, j;

    // Try to match PPC NVRAM
    nvram_set_string(nvram, 0x00, "QEMU_BIOS", 16);
    nvram_set_lword(nvram,  0x10, 0x00000001); /* structure v1 */
    // NVRAM_size, arch not applicable
    m48t08_write(nvram, 0x2F, nographic & 0xff);
    nvram_set_lword(nvram,  0x30, RAM_size);
    m48t08_write(nvram, 0x34, boot_device & 0xff);
    nvram_set_lword(nvram,  0x38, KERNEL_LOAD_ADDR);
    nvram_set_lword(nvram,  0x3C, kernel_size);
    if (cmdline) {
	strcpy(phys_ram_base + CMDLINE_ADDR, cmdline);
	nvram_set_lword(nvram,  0x40, CMDLINE_ADDR);
        nvram_set_lword(nvram,  0x44, strlen(cmdline));
    }
    // initrd_image, initrd_size passed differently
    nvram_set_word(nvram,   0x54, width);
    nvram_set_word(nvram,   0x56, height);
    nvram_set_word(nvram,   0x58, depth);

    // Sun4m specific use
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

void pic_info()
{
}

void irq_info()
{
}

void pic_set_irq(int irq, int level)
{
}

void vga_update_display()
{
}

void vga_invalidate_display()
{
}

void vga_screen_dump(const char *filename)
{
}

void qemu_system_powerdown(void)
{
}

/* Sun4u hardware initialisation */
static void sun4u_init(int ram_size, int vga_ram_size, int boot_device,
             DisplayState *ds, const char **fd_filename, int snapshot,
             const char *kernel_filename, const char *kernel_cmdline,
             const char *initrd_filename)
{
    char buf[1024];
    int ret, linux_boot;
    unsigned int i;
    long vram_size = 0x100000, prom_offset, initrd_size, kernel_size;

    linux_boot = (kernel_filename != NULL);

    /* allocate RAM */
    cpu_register_physical_memory(0, ram_size, 0);

    nvram = m48t08_init(PHYS_JJ_EEPROM, PHYS_JJ_EEPROM_SIZE);
    // Slavio TTYA (base+4, Linux ttyS0) is the first Qemu serial device
    // Slavio TTYB (base+0, Linux ttyS1) is the second Qemu serial device
    slavio_serial_init(PHYS_JJ_SER, PHYS_JJ_SER_IRQ, serial_hds[1], serial_hds[0]);

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

    kernel_size = 0;
    if (linux_boot) {
        kernel_size = load_elf(kernel_filename, phys_ram_base + KERNEL_LOAD_ADDR);
        if (kernel_size < 0)
	    kernel_size = load_aout(kernel_filename, phys_ram_base + KERNEL_LOAD_ADDR);
	if (kernel_size < 0)
	    kernel_size = load_image(kernel_filename, phys_ram_base + KERNEL_LOAD_ADDR);
        if (kernel_size < 0) {
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
    nvram_init(nvram, (uint8_t *)&nd_table[0].macaddr, kernel_cmdline, boot_device, ram_size, kernel_size, graphic_width, graphic_height, graphic_depth);
}

QEMUMachine sun4u_machine = {
    "sun4u",
    "Sun4u platform",
    sun4u_init,
};
