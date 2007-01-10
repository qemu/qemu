/*
 * QEMU Sun4m System Emulator
 * 
 * Copyright (c) 2003-2005 Fabrice Bellard
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

#define KERNEL_LOAD_ADDR     0x00004000
#define CMDLINE_ADDR         0x007ff000
#define INITRD_LOAD_ADDR     0x00800000
#define PROM_SIZE_MAX        (256 * 1024)
#define PROM_ADDR	     0xffd00000
#define PROM_FILENAME	     "openbios-sparc32"
#define PHYS_JJ_EEPROM	0x71200000	/* m48t08 */
#define PHYS_JJ_IDPROM_OFF	0x1FD8
#define PHYS_JJ_EEPROM_SIZE	0x2000
// IRQs are not PIL ones, but master interrupt controller register
// bits
#define PHYS_JJ_IOMMU	0x10000000	/* I/O MMU */
#define PHYS_JJ_TCX_FB	0x50000000	/* TCX frame buffer */
#define PHYS_JJ_SLAVIO	0x70000000	/* Slavio base */
#define PHYS_JJ_DMA     0x78400000      /* DMA controller */
#define PHYS_JJ_ESP     0x78800000      /* ESP SCSI */
#define PHYS_JJ_ESP_IRQ    18
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
#define PHYS_JJ_FDC	0x71400000	/* Floppy */
#define PHYS_JJ_FLOPPY_IRQ 22
#define PHYS_JJ_ME_IRQ 30		/* Module error, power fail */
#define PHYS_JJ_CS      0x6c000000      /* Crystal CS4231 */
#define PHYS_JJ_CS_IRQ  5

#define MAX_CPUS 16

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

static void nvram_set_word (m48t59_t *nvram, uint32_t addr, uint16_t value)
{
    m48t59_write(nvram, addr++, (value >> 8) & 0xff);
    m48t59_write(nvram, addr++, value & 0xff);
}

static void nvram_set_lword (m48t59_t *nvram, uint32_t addr, uint32_t value)
{
    m48t59_write(nvram, addr++, value >> 24);
    m48t59_write(nvram, addr++, (value >> 16) & 0xff);
    m48t59_write(nvram, addr++, (value >> 8) & 0xff);
    m48t59_write(nvram, addr++, value & 0xff);
}

static void nvram_set_string (m48t59_t *nvram, uint32_t addr,
                       const unsigned char *str, uint32_t max)
{
    unsigned int i;

    for (i = 0; i < max && str[i] != '\0'; i++) {
        m48t59_write(nvram, addr + i, str[i]);
    }
    m48t59_write(nvram, addr + max - 1, '\0');
}

static m48t59_t *nvram;

extern int nographic;

static void nvram_init(m48t59_t *nvram, uint8_t *macaddr, const char *cmdline,
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
    m48t59_write(nvram, 0x2D, smp_cpus & 0xff);
    m48t59_write(nvram, 0x2E, 0);
    m48t59_write(nvram, 0x2F, nographic & 0xff);
    nvram_set_lword(nvram,  0x30, RAM_size);
    m48t59_write(nvram, 0x34, boot_device & 0xff);
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
    m48t59_write(nvram, i++, 0x01);
    m48t59_write(nvram, i++, 0x80); /* Sun4m OBP */
    j = 0;
    m48t59_write(nvram, i++, macaddr[j++]);
    m48t59_write(nvram, i++, macaddr[j++]);
    m48t59_write(nvram, i++, macaddr[j++]);
    m48t59_write(nvram, i++, macaddr[j++]);
    m48t59_write(nvram, i++, macaddr[j++]);
    m48t59_write(nvram, i, macaddr[j]);

    /* Calculate checksum */
    for (i = 0x1fd8; i < 0x1fe7; i++) {
	tmp ^= m48t59_read(nvram, i);
    }
    m48t59_write(nvram, 0x1fe7, tmp);
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

void pic_set_irq_new(void *opaque, int irq, int level)
{
    pic_set_irq(irq, level);
}

void pic_set_irq_cpu(int irq, int level, unsigned int cpu)
{
    slavio_pic_set_irq_cpu(slavio_intctl, irq, level, cpu);
}

static void *slavio_misc;

void qemu_system_powerdown(void)
{
    slavio_set_power_fail(slavio_misc, 1);
}

static void main_cpu_reset(void *opaque)
{
    CPUState *env = opaque;
    cpu_reset(env);
}

/* Sun4m hardware initialisation */
static void sun4m_init(int ram_size, int vga_ram_size, int boot_device,
                       DisplayState *ds, const char **fd_filename, int snapshot,
                       const char *kernel_filename, const char *kernel_cmdline,
                       const char *initrd_filename)
{
    CPUState *env, *envs[MAX_CPUS];
    char buf[1024];
    int ret, linux_boot;
    unsigned int i;
    long vram_size = 0x100000, prom_offset, initrd_size, kernel_size;
    void *iommu, *dma, *main_esp, *main_lance = NULL;

    linux_boot = (kernel_filename != NULL);

    /* init CPUs */
    for(i = 0; i < smp_cpus; i++) {
        env = cpu_init();
        envs[i] = env;
        if (i != 0)
            env->halted = 1;
        register_savevm("cpu", i, 3, cpu_save, cpu_load, env);
        qemu_register_reset(main_cpu_reset, env);
    }
    /* allocate RAM */
    cpu_register_physical_memory(0, ram_size, 0);

    iommu = iommu_init(PHYS_JJ_IOMMU);
    slavio_intctl = slavio_intctl_init(PHYS_JJ_INTR0, PHYS_JJ_INTR_G);
    for(i = 0; i < smp_cpus; i++) {
        slavio_intctl_set_cpu(slavio_intctl, i, envs[i]);
    }
    dma = sparc32_dma_init(PHYS_JJ_DMA, PHYS_JJ_ESP_IRQ, PHYS_JJ_LE_IRQ, iommu, slavio_intctl);

    tcx_init(ds, PHYS_JJ_TCX_FB, phys_ram_base + ram_size, ram_size, vram_size, graphic_width, graphic_height);
    if (nd_table[0].vlan) {
        if (nd_table[0].model == NULL
            || strcmp(nd_table[0].model, "lance") == 0) {
            main_lance = lance_init(&nd_table[0], PHYS_JJ_LE, dma);
        } else {
            fprintf(stderr, "qemu: Unsupported NIC: %s\n", nd_table[0].model);
            exit (1);
        }
    }
    nvram = m48t59_init(0, PHYS_JJ_EEPROM, 0, PHYS_JJ_EEPROM_SIZE, 8);
    for (i = 0; i < MAX_CPUS; i++) {
        slavio_timer_init(PHYS_JJ_CLOCK + i * TARGET_PAGE_SIZE, PHYS_JJ_CLOCK_IRQ, 0, i);
    }
    slavio_timer_init(PHYS_JJ_CLOCK1, PHYS_JJ_CLOCK1_IRQ, 2, (unsigned int)-1);
    slavio_serial_ms_kbd_init(PHYS_JJ_MS_KBD, PHYS_JJ_MS_KBD_IRQ);
    // Slavio TTYA (base+4, Linux ttyS0) is the first Qemu serial device
    // Slavio TTYB (base+0, Linux ttyS1) is the second Qemu serial device
    slavio_serial_init(PHYS_JJ_SER, PHYS_JJ_SER_IRQ, serial_hds[1], serial_hds[0]);
    fdctrl_init(PHYS_JJ_FLOPPY_IRQ, 0, 1, PHYS_JJ_FDC, fd_table);
    main_esp = esp_init(bs_table, PHYS_JJ_ESP, dma);

    for (i = 0; i < MAX_DISKS; i++) {
        if (bs_table[i]) {
            esp_scsi_attach(main_esp, bs_table[i], i);
        }
    }

    slavio_misc = slavio_misc_init(PHYS_JJ_SLAVIO, PHYS_JJ_ME_IRQ);
    cs_init(PHYS_JJ_CS, PHYS_JJ_CS_IRQ, slavio_intctl);
    sparc32_dma_set_reset_data(dma, main_esp, main_lance);

    prom_offset = ram_size + vram_size;
    cpu_register_physical_memory(PROM_ADDR, 
                                 (PROM_SIZE_MAX + TARGET_PAGE_SIZE - 1) & TARGET_PAGE_MASK, 
                                 prom_offset | IO_MEM_ROM);

    snprintf(buf, sizeof(buf), "%s/%s", bios_dir, PROM_FILENAME);
    ret = load_elf(buf, 0, NULL);
    if (ret < 0) {
	fprintf(stderr, "qemu: could not load prom '%s'\n", 
		buf);
	exit(1);
    }

    kernel_size = 0;
    if (linux_boot) {
        kernel_size = load_elf(kernel_filename, -0xf0000000, NULL);
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

QEMUMachine sun4m_machine = {
    "sun4m",
    "Sun4m platform",
    sun4m_init,
};
