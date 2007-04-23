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

/*
 * Sun4m architecture was used in the following machines:
 *
 * SPARCserver 6xxMP/xx
 * SPARCclassic (SPARCclassic Server)(SPARCstation LC) (4/15), SPARCclassic X (4/10)
 * SPARCstation LX/ZX (4/30)
 * SPARCstation Voyager
 * SPARCstation 10/xx, SPARCserver 10/xx
 * SPARCstation 5, SPARCserver 5
 * SPARCstation 20/xx, SPARCserver 20
 * SPARCstation 4
 *
 * See for example: http://www.sunhelp.org/faq/sunref1.html
 */

#define KERNEL_LOAD_ADDR     0x00004000
#define CMDLINE_ADDR         0x007ff000
#define INITRD_LOAD_ADDR     0x00800000
#define PROM_SIZE_MAX        (256 * 1024)
#define PROM_ADDR	     0xffd00000
#define PROM_FILENAME	     "openbios-sparc32"

#define MAX_CPUS 16

struct hwdef {
    target_ulong iommu_base, slavio_base;
    target_ulong intctl_base, counter_base, nvram_base, ms_kb_base, serial_base;
    target_ulong fd_base;
    target_ulong dma_base, esp_base, le_base;
    target_ulong tcx_base, cs_base;
    long vram_size, nvram_size;
    // IRQ numbers are not PIL ones, but master interrupt controller register
    // bit numbers
    int intctl_g_intr, esp_irq, le_irq, cpu_irq, clock_irq, clock1_irq;
    int ser_irq, ms_kb_irq, fd_irq, me_irq, cs_irq;
    int machine_id; // For NVRAM
    uint32_t intbit_to_level[32];
};

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
		       int width, int height, int depth,
                       int machine_id)
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
    m48t59_write(nvram, i++, machine_id);
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

static void sun4m_hw_init(const struct hwdef *hwdef, int ram_size,
                          DisplayState *ds, const char *cpu_model)

{
    CPUState *env, *envs[MAX_CPUS];
    unsigned int i;
    void *iommu, *dma, *main_esp, *main_lance = NULL;
    const sparc_def_t *def;
    qemu_irq *slavio_irq;

    /* init CPUs */
    sparc_find_by_name(cpu_model, &def);
    if (def == NULL) {
        fprintf(stderr, "Unable to find Sparc CPU definition\n");
        exit(1);
    }
    for(i = 0; i < smp_cpus; i++) {
        env = cpu_init();
        cpu_sparc_register(env, def);
        envs[i] = env;
        if (i != 0)
            env->halted = 1;
        register_savevm("cpu", i, 3, cpu_save, cpu_load, env);
        qemu_register_reset(main_cpu_reset, env);
    }
    /* allocate RAM */
    cpu_register_physical_memory(0, ram_size, 0);

    iommu = iommu_init(hwdef->iommu_base);
    slavio_intctl = slavio_intctl_init(hwdef->intctl_base,
                                       hwdef->intctl_base + 0x10000,
                                       &hwdef->intbit_to_level[0],
                                       &slavio_irq);
    for(i = 0; i < smp_cpus; i++) {
        slavio_intctl_set_cpu(slavio_intctl, i, envs[i]);
    }
    dma = sparc32_dma_init(hwdef->dma_base, slavio_irq[hwdef->esp_irq],
                           slavio_irq[hwdef->le_irq], iommu);

    if (graphic_depth != 8 && graphic_depth != 24) {
        fprintf(stderr, "qemu: Unsupported depth: %d\n", graphic_depth);
        exit (1);
    }
    tcx_init(ds, hwdef->tcx_base, phys_ram_base + ram_size, ram_size,
             hwdef->vram_size, graphic_width, graphic_height, graphic_depth);
    if (nd_table[0].vlan) {
        if (nd_table[0].model == NULL
            || strcmp(nd_table[0].model, "lance") == 0) {
            main_lance = lance_init(&nd_table[0], hwdef->le_base, dma,
                                    slavio_irq[hwdef->le_irq]);
        } else {
            fprintf(stderr, "qemu: Unsupported NIC: %s\n", nd_table[0].model);
            exit (1);
        }
    }
    nvram = m48t59_init(slavio_irq[0], hwdef->nvram_base, 0,
                        hwdef->nvram_size, 8);
    for (i = 0; i < MAX_CPUS; i++) {
        slavio_timer_init(hwdef->counter_base + i * TARGET_PAGE_SIZE,
                          hwdef->clock_irq, 0, i, slavio_intctl);
    }
    slavio_timer_init(hwdef->counter_base + 0x10000, hwdef->clock1_irq, 2,
                      (unsigned int)-1, slavio_intctl);
    slavio_serial_ms_kbd_init(hwdef->ms_kb_base, slavio_irq[hwdef->ms_kb_irq]);
    // Slavio TTYA (base+4, Linux ttyS0) is the first Qemu serial device
    // Slavio TTYB (base+0, Linux ttyS1) is the second Qemu serial device
    slavio_serial_init(hwdef->serial_base, slavio_irq[hwdef->ser_irq],
                       serial_hds[1], serial_hds[0]);
    fdctrl_init(slavio_irq[hwdef->fd_irq], 0, 1, hwdef->fd_base, fd_table);
    main_esp = esp_init(bs_table, hwdef->esp_base, dma);

    for (i = 0; i < MAX_DISKS; i++) {
        if (bs_table[i]) {
            esp_scsi_attach(main_esp, bs_table[i], i);
        }
    }

    slavio_misc = slavio_misc_init(hwdef->slavio_base, 
                                   slavio_irq[hwdef->me_irq]);
    if (hwdef->cs_base != (target_ulong)-1)
        cs_init(hwdef->cs_base, hwdef->cs_irq, slavio_intctl);
    sparc32_dma_set_reset_data(dma, main_esp, main_lance);
}

static void sun4m_load_kernel(long vram_size, int ram_size, int boot_device,
                              const char *kernel_filename,
                              const char *kernel_cmdline,
                              const char *initrd_filename,
                              int machine_id)
{
    int ret, linux_boot;
    char buf[1024];
    unsigned int i;
    long prom_offset, initrd_size, kernel_size;

    linux_boot = (kernel_filename != NULL);

    prom_offset = ram_size + vram_size;
    cpu_register_physical_memory(PROM_ADDR, 
                                 (PROM_SIZE_MAX + TARGET_PAGE_SIZE - 1) & TARGET_PAGE_MASK, 
                                 prom_offset | IO_MEM_ROM);

    snprintf(buf, sizeof(buf), "%s/%s", bios_dir, PROM_FILENAME);
    ret = load_elf(buf, 0, NULL, NULL, NULL);
    if (ret < 0) {
	fprintf(stderr, "qemu: could not load prom '%s'\n", 
		buf);
	exit(1);
    }

    kernel_size = 0;
    if (linux_boot) {
        kernel_size = load_elf(kernel_filename, -0xf0000000, NULL, NULL, NULL);
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
    nvram_init(nvram, (uint8_t *)&nd_table[0].macaddr, kernel_cmdline,
               boot_device, ram_size, kernel_size, graphic_width,
               graphic_height, graphic_depth, machine_id);
}

static const struct hwdef hwdefs[] = {
    /* SS-5 */
    {
        .iommu_base   = 0x10000000,
        .tcx_base     = 0x50000000,
        .cs_base      = 0x6c000000,
        .slavio_base  = 0x71000000,
        .ms_kb_base   = 0x71000000,
        .serial_base  = 0x71100000,
        .nvram_base   = 0x71200000,
        .fd_base      = 0x71400000,
        .counter_base = 0x71d00000,
        .intctl_base  = 0x71e00000,
        .dma_base     = 0x78400000,
        .esp_base     = 0x78800000,
        .le_base      = 0x78c00000,
        .vram_size    = 0x00100000,
        .nvram_size   = 0x2000,
        .esp_irq = 18,
        .le_irq = 16,
        .clock_irq = 7,
        .clock1_irq = 19,
        .ms_kb_irq = 14,
        .ser_irq = 15,
        .fd_irq = 22,
        .me_irq = 30,
        .cs_irq = 5,
        .machine_id = 0x80,
        .intbit_to_level = {
            2, 3, 5, 7, 9, 11, 0, 14,	3, 5, 7, 9, 11, 13, 12, 12,
            6, 0, 4, 10, 8, 0, 11, 0,	0, 0, 0, 0, 15, 0, 15, 0,
        },
    },
    /* SS-10 */
    {
        .iommu_base   = 0xe0000000, // XXX Actually at 0xfe0000000ULL (36 bits)
        .tcx_base     = 0x20000000, // 0xe20000000ULL,
        .cs_base      = -1,
        .slavio_base  = 0xf1000000, // 0xff1000000ULL,
        .ms_kb_base   = 0xf1000000, // 0xff1000000ULL,
        .serial_base  = 0xf1100000, // 0xff1100000ULL,
        .nvram_base   = 0xf1200000, // 0xff1200000ULL,
        .fd_base      = 0xf1700000, // 0xff1700000ULL,
        .counter_base = 0xf1300000, // 0xff1300000ULL,
        .intctl_base  = 0xf1400000, // 0xff1400000ULL,
        .dma_base     = 0xf0400000, // 0xef0400000ULL,
        .esp_base     = 0xf0800000, // 0xef0800000ULL,
        .le_base      = 0xf0c00000, // 0xef0c00000ULL,
        .vram_size    = 0x00100000,
        .nvram_size   = 0x2000,
        .esp_irq = 18,
        .le_irq = 16,
        .clock_irq = 7,
        .clock1_irq = 19,
        .ms_kb_irq = 14,
        .ser_irq = 15,
        .fd_irq = 22,
        .me_irq = 30,
        .cs_irq = -1,
        .machine_id = 0x72,
        .intbit_to_level = {
            2, 3, 5, 7, 9, 11, 0, 14,	3, 5, 7, 9, 11, 13, 12, 12,
            6, 0, 4, 10, 8, 0, 11, 0,	0, 0, 0, 0, 15, 0, 15, 0,
        },
    },
};

static void sun4m_common_init(int ram_size, int boot_device, DisplayState *ds,
                              const char *kernel_filename, const char *kernel_cmdline,
                              const char *initrd_filename, const char *cpu_model,
                              unsigned int machine)
{
    sun4m_hw_init(&hwdefs[machine], ram_size, ds, cpu_model);

    sun4m_load_kernel(hwdefs[machine].vram_size, ram_size, boot_device,
                      kernel_filename, kernel_cmdline, initrd_filename,
                      hwdefs[machine].machine_id);
}

/* SPARCstation 5 hardware initialisation */
static void ss5_init(int ram_size, int vga_ram_size, int boot_device,
                       DisplayState *ds, const char **fd_filename, int snapshot,
                       const char *kernel_filename, const char *kernel_cmdline,
                       const char *initrd_filename, const char *cpu_model)
{
    if (cpu_model == NULL)
        cpu_model = "Fujitsu MB86904";
    sun4m_common_init(ram_size, boot_device, ds, kernel_filename,
                      kernel_cmdline, initrd_filename, cpu_model,
                      0);
}

/* SPARCstation 10 hardware initialisation */
static void ss10_init(int ram_size, int vga_ram_size, int boot_device,
                            DisplayState *ds, const char **fd_filename, int snapshot,
                            const char *kernel_filename, const char *kernel_cmdline,
                            const char *initrd_filename, const char *cpu_model)
{
    if (cpu_model == NULL)
        cpu_model = "TI SuperSparc II";
    sun4m_common_init(ram_size, boot_device, ds, kernel_filename,
                      kernel_cmdline, initrd_filename, cpu_model,
                      1);
}

QEMUMachine ss5_machine = {
    "SS-5",
    "Sun4m platform, SPARCstation 5",
    ss5_init,
};

QEMUMachine ss10_machine = {
    "SS-10",
    "Sun4m platform, SPARCstation 10",
    ss10_init,
};
