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
#include "m48t59.h"

#define KERNEL_LOAD_ADDR     0x00404000
#define CMDLINE_ADDR         0x003ff000
#define INITRD_LOAD_ADDR     0x00300000
#define PROM_SIZE_MAX        (512 * 1024)
#define PROM_ADDR	     0x1fff0000000ULL
#define APB_SPECIAL_BASE     0x1fe00000000ULL
#define APB_MEM_BASE	     0x1ff00000000ULL
#define VGA_BASE	     (APB_MEM_BASE + 0x400000ULL)
#define PROM_FILENAME	     "openbios-sparc64"
#define NVRAM_SIZE           0x2000

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

/* NVRAM helpers */
void NVRAM_set_byte (m48t59_t *nvram, uint32_t addr, uint8_t value)
{
    m48t59_write(nvram, addr, value);
}

uint8_t NVRAM_get_byte (m48t59_t *nvram, uint32_t addr)
{
    return m48t59_read(nvram, addr);
}

void NVRAM_set_word (m48t59_t *nvram, uint32_t addr, uint16_t value)
{
    m48t59_write(nvram, addr, value >> 8);
    m48t59_write(nvram, addr + 1, value & 0xFF);
}

uint16_t NVRAM_get_word (m48t59_t *nvram, uint32_t addr)
{
    uint16_t tmp;

    tmp = m48t59_read(nvram, addr) << 8;
    tmp |= m48t59_read(nvram, addr + 1);

    return tmp;
}

void NVRAM_set_lword (m48t59_t *nvram, uint32_t addr, uint32_t value)
{
    m48t59_write(nvram, addr, value >> 24);
    m48t59_write(nvram, addr + 1, (value >> 16) & 0xFF);
    m48t59_write(nvram, addr + 2, (value >> 8) & 0xFF);
    m48t59_write(nvram, addr + 3, value & 0xFF);
}

uint32_t NVRAM_get_lword (m48t59_t *nvram, uint32_t addr)
{
    uint32_t tmp;

    tmp = m48t59_read(nvram, addr) << 24;
    tmp |= m48t59_read(nvram, addr + 1) << 16;
    tmp |= m48t59_read(nvram, addr + 2) << 8;
    tmp |= m48t59_read(nvram, addr + 3);

    return tmp;
}

void NVRAM_set_string (m48t59_t *nvram, uint32_t addr,
                       const unsigned char *str, uint32_t max)
{
    int i;

    for (i = 0; i < max && str[i] != '\0'; i++) {
        m48t59_write(nvram, addr + i, str[i]);
    }
    m48t59_write(nvram, addr + max - 1, '\0');
}

int NVRAM_get_string (m48t59_t *nvram, uint8_t *dst, uint16_t addr, int max)
{
    int i;

    memset(dst, 0, max);
    for (i = 0; i < max; i++) {
        dst[i] = NVRAM_get_byte(nvram, addr + i);
        if (dst[i] == '\0')
            break;
    }

    return i;
}

static uint16_t NVRAM_crc_update (uint16_t prev, uint16_t value)
{
    uint16_t tmp;
    uint16_t pd, pd1, pd2;

    tmp = prev >> 8;
    pd = prev ^ value;
    pd1 = pd & 0x000F;
    pd2 = ((pd >> 4) & 0x000F) ^ pd1;
    tmp ^= (pd1 << 3) | (pd1 << 8);
    tmp ^= pd2 | (pd2 << 7) | (pd2 << 12);

    return tmp;
}

uint16_t NVRAM_compute_crc (m48t59_t *nvram, uint32_t start, uint32_t count)
{
    uint32_t i;
    uint16_t crc = 0xFFFF;
    int odd;

    odd = count & 1;
    count &= ~1;
    for (i = 0; i != count; i++) {
	crc = NVRAM_crc_update(crc, NVRAM_get_word(nvram, start + i));
    }
    if (odd) {
	crc = NVRAM_crc_update(crc, NVRAM_get_byte(nvram, start + i) << 8);
    }

    return crc;
}

extern int nographic;

int sun4u_NVRAM_set_params (m48t59_t *nvram, uint16_t NVRAM_size,
                          const unsigned char *arch,
                          uint32_t RAM_size, int boot_device,
                          uint32_t kernel_image, uint32_t kernel_size,
                          const char *cmdline,
                          uint32_t initrd_image, uint32_t initrd_size,
                          uint32_t NVRAM_image,
                          int width, int height, int depth)
{
    uint16_t crc;

    /* Set parameters for Open Hack'Ware BIOS */
    NVRAM_set_string(nvram, 0x00, "QEMU_BIOS", 16);
    NVRAM_set_lword(nvram,  0x10, 0x00000002); /* structure v2 */
    NVRAM_set_word(nvram,   0x14, NVRAM_size);
    NVRAM_set_string(nvram, 0x20, arch, 16);
    NVRAM_set_byte(nvram,   0x2f, nographic & 0xff);
    NVRAM_set_lword(nvram,  0x30, RAM_size);
    NVRAM_set_byte(nvram,   0x34, boot_device);
    NVRAM_set_lword(nvram,  0x38, kernel_image);
    NVRAM_set_lword(nvram,  0x3C, kernel_size);
    if (cmdline) {
        /* XXX: put the cmdline in NVRAM too ? */
        strcpy(phys_ram_base + CMDLINE_ADDR, cmdline);
        NVRAM_set_lword(nvram,  0x40, CMDLINE_ADDR);
        NVRAM_set_lword(nvram,  0x44, strlen(cmdline));
    } else {
        NVRAM_set_lword(nvram,  0x40, 0);
        NVRAM_set_lword(nvram,  0x44, 0);
    }
    NVRAM_set_lword(nvram,  0x48, initrd_image);
    NVRAM_set_lword(nvram,  0x4C, initrd_size);
    NVRAM_set_lword(nvram,  0x50, NVRAM_image);

    NVRAM_set_word(nvram,   0x54, width);
    NVRAM_set_word(nvram,   0x56, height);
    NVRAM_set_word(nvram,   0x58, depth);
    crc = NVRAM_compute_crc(nvram, 0x00, 0xF8);
    NVRAM_set_word(nvram,  0xFC, crc);

    return 0;
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

void pic_set_irq_new(void *opaque, int irq, int level)
{
}

void qemu_system_powerdown(void)
{
}

static void main_cpu_reset(void *opaque)
{
    CPUState *env = opaque;
    cpu_reset(env);
}

static const int ide_iobase[2] = { 0x1f0, 0x170 };
static const int ide_iobase2[2] = { 0x3f6, 0x376 };
static const int ide_irq[2] = { 14, 15 };

static const int serial_io[MAX_SERIAL_PORTS] = { 0x3f8, 0x2f8, 0x3e8, 0x2e8 };
static const int serial_irq[MAX_SERIAL_PORTS] = { 4, 3, 4, 3 };

static const int parallel_io[MAX_PARALLEL_PORTS] = { 0x378, 0x278, 0x3bc };
static const int parallel_irq[MAX_PARALLEL_PORTS] = { 7, 7, 7 };

static fdctrl_t *floppy_controller;

/* Sun4u hardware initialisation */
static void sun4u_init(int ram_size, int vga_ram_size, int boot_device,
             DisplayState *ds, const char **fd_filename, int snapshot,
             const char *kernel_filename, const char *kernel_cmdline,
             const char *initrd_filename)
{
    CPUState *env;
    char buf[1024];
    m48t59_t *nvram;
    int ret, linux_boot;
    unsigned int i;
    long prom_offset, initrd_size, kernel_size;
    PCIBus *pci_bus;

    linux_boot = (kernel_filename != NULL);

    env = cpu_init();
    register_savevm("cpu", 0, 3, cpu_save, cpu_load, env);
    qemu_register_reset(main_cpu_reset, env);

    /* allocate RAM */
    cpu_register_physical_memory(0, ram_size, 0);

    prom_offset = ram_size + vga_ram_size;
    cpu_register_physical_memory(PROM_ADDR, 
                                 (PROM_SIZE_MAX + TARGET_PAGE_SIZE) & TARGET_PAGE_MASK, 
                                 prom_offset | IO_MEM_ROM);

    snprintf(buf, sizeof(buf), "%s/%s", bios_dir, PROM_FILENAME);
    ret = load_elf(buf, 0, NULL);
    if (ret < 0) {
	fprintf(stderr, "qemu: could not load prom '%s'\n", 
		buf);
	exit(1);
    }

    kernel_size = 0;
    initrd_size = 0;
    if (linux_boot) {
        /* XXX: put correct offset */
        kernel_size = load_elf(kernel_filename, 0, NULL);
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
    pci_bus = pci_apb_init(APB_SPECIAL_BASE, APB_MEM_BASE, NULL);
    isa_mem_base = VGA_BASE;
    pci_cirrus_vga_init(pci_bus, ds, phys_ram_base + ram_size, ram_size, vga_ram_size);

    for(i = 0; i < MAX_SERIAL_PORTS; i++) {
        if (serial_hds[i]) {
            serial_init(&pic_set_irq_new, NULL,
                        serial_io[i], serial_irq[i], serial_hds[i]);
        }
    }

    for(i = 0; i < MAX_PARALLEL_PORTS; i++) {
        if (parallel_hds[i]) {
            parallel_init(parallel_io[i], parallel_irq[i], parallel_hds[i]);
        }
    }

    for(i = 0; i < nb_nics; i++) {
        if (!nd_table[i].model)
            nd_table[i].model = "ne2k_pci";
	pci_nic_init(pci_bus, &nd_table[i]);
    }

    pci_cmd646_ide_init(pci_bus, bs_table, 1);
    kbd_init();
    floppy_controller = fdctrl_init(6, 2, 0, 0x3f0, fd_table);
    nvram = m48t59_init(8, 0, 0x0074, NVRAM_SIZE, 59);
    sun4u_NVRAM_set_params(nvram, NVRAM_SIZE, "Sun4u", ram_size, boot_device,
                         KERNEL_LOAD_ADDR, kernel_size,
                         kernel_cmdline,
                         INITRD_LOAD_ADDR, initrd_size,
                         /* XXX: need an option to load a NVRAM image */
                         0,
                         graphic_width, graphic_height, graphic_depth);

}

QEMUMachine sun4u_machine = {
    "sun4u",
    "Sun4u platform",
    sun4u_init,
};
