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
//#define DEBUG_IRQ

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

#ifdef DEBUG_IRQ
#define DPRINTF(fmt, args...)                           \
    do { printf("CPUIRQ: " fmt , ##args); } while (0)
#else
#define DPRINTF(fmt, args...)
#endif

#define KERNEL_LOAD_ADDR     0x00004000
#define CMDLINE_ADDR         0x007ff000
#define INITRD_LOAD_ADDR     0x00800000
#define PROM_SIZE_MAX        (512 * 1024)
#define PROM_PADDR           0xff0000000ULL
#define PROM_VADDR           0xffd00000
#define PROM_FILENAME        "openbios-sparc32"

#define MAX_CPUS 16
#define MAX_PILS 16

struct hwdef {
    target_phys_addr_t iommu_base, slavio_base;
    target_phys_addr_t intctl_base, counter_base, nvram_base, ms_kb_base;
    target_phys_addr_t serial_base, fd_base;
    target_phys_addr_t dma_base, esp_base, le_base;
    target_phys_addr_t tcx_base, cs_base, power_base;
    long vram_size, nvram_size;
    // IRQ numbers are not PIL ones, but master interrupt controller register
    // bit numbers
    int intctl_g_intr, esp_irq, le_irq, clock_irq, clock1_irq;
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

static uint32_t nvram_set_var (m48t59_t *nvram, uint32_t addr,
                                const unsigned char *str)
{
    uint32_t len;

    len = strlen(str) + 1;
    nvram_set_string(nvram, addr, str, len);

    return addr + len;
}

static void nvram_finish_partition (m48t59_t *nvram, uint32_t start,
                                    uint32_t end)
{
    unsigned int i, sum;

    // Length divided by 16
    m48t59_write(nvram, start + 2, ((end - start) >> 12) & 0xff);
    m48t59_write(nvram, start + 3, ((end - start) >> 4) & 0xff);
    // Checksum
    sum = m48t59_read(nvram, start);
    for (i = 0; i < 14; i++) {
        sum += m48t59_read(nvram, start + 2 + i);
        sum = (sum + ((sum & 0xff00) >> 8)) & 0xff;
    }
    m48t59_write(nvram, start + 1, sum & 0xff);
}

extern int nographic;

static void nvram_init(m48t59_t *nvram, uint8_t *macaddr, const char *cmdline,
                       const char *boot_device, uint32_t RAM_size,
                       uint32_t kernel_size,
                       int width, int height, int depth,
                       int machine_id)
{
    unsigned char tmp = 0;
    unsigned int i, j;
    uint32_t start, end;

    // Try to match PPC NVRAM
    nvram_set_string(nvram, 0x00, "QEMU_BIOS", 16);
    nvram_set_lword(nvram,  0x10, 0x00000001); /* structure v1 */
    // NVRAM_size, arch not applicable
    m48t59_write(nvram, 0x2D, smp_cpus & 0xff);
    m48t59_write(nvram, 0x2E, 0);
    m48t59_write(nvram, 0x2F, nographic & 0xff);
    nvram_set_lword(nvram,  0x30, RAM_size);
    m48t59_write(nvram, 0x34, boot_device[0] & 0xff);
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

    // OpenBIOS nvram variables
    // Variable partition
    start = 252;
    m48t59_write(nvram, start, 0x70);
    nvram_set_string(nvram, start + 4, "system", 12);

    end = start + 16;
    for (i = 0; i < nb_prom_envs; i++)
        end = nvram_set_var(nvram, end, prom_envs[i]);

    m48t59_write(nvram, end++ , 0);
    end = start + ((end - start + 15) & ~15);
    nvram_finish_partition(nvram, start, end);

    // free partition
    start = end;
    m48t59_write(nvram, start, 0x7f);
    nvram_set_string(nvram, start + 4, "free", 12);

    end = 0x1fd0;
    nvram_finish_partition(nvram, start, end);

    // Sun4m specific use
    start = i = 0x1fd8;
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
    for (i = start; i < start + 15; i++) {
        tmp ^= m48t59_read(nvram, i);
    }
    m48t59_write(nvram, start + 15, tmp);
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

void cpu_check_irqs(CPUState *env)
{
    if (env->pil_in && (env->interrupt_index == 0 ||
                        (env->interrupt_index & ~15) == TT_EXTINT)) {
        unsigned int i;

        for (i = 15; i > 0; i--) {
            if (env->pil_in & (1 << i)) {
                int old_interrupt = env->interrupt_index;

                env->interrupt_index = TT_EXTINT | i;
                if (old_interrupt != env->interrupt_index)
                    cpu_interrupt(env, CPU_INTERRUPT_HARD);
                break;
            }
        }
    } else if (!env->pil_in && (env->interrupt_index & ~15) == TT_EXTINT) {
        env->interrupt_index = 0;
        cpu_reset_interrupt(env, CPU_INTERRUPT_HARD);
    }
}

static void cpu_set_irq(void *opaque, int irq, int level)
{
    CPUState *env = opaque;

    if (level) {
        DPRINTF("Raise CPU IRQ %d\n", irq);
        env->halted = 0;
        env->pil_in |= 1 << irq;
        cpu_check_irqs(env);
    } else {
        DPRINTF("Lower CPU IRQ %d\n", irq);
        env->pil_in &= ~(1 << irq);
        cpu_check_irqs(env);
    }
}

static void dummy_cpu_set_irq(void *opaque, int irq, int level)
{
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
    env->halted = 0;
}

static void secondary_cpu_reset(void *opaque)
{
    CPUState *env = opaque;

    cpu_reset(env);
    env->halted = 1;
}

static void *sun4m_hw_init(const struct hwdef *hwdef, int RAM_size,
                           DisplayState *ds, const char *cpu_model)

{
    CPUState *env, *envs[MAX_CPUS];
    unsigned int i;
    void *iommu, *espdma, *ledma, *main_esp, *nvram;
    const sparc_def_t *def;
    qemu_irq *cpu_irqs[MAX_CPUS], *slavio_irq, *slavio_cpu_irq,
        *espdma_irq, *ledma_irq;
    qemu_irq *esp_reset, *le_reset;

    /* init CPUs */
    sparc_find_by_name(cpu_model, &def);
    if (def == NULL) {
        fprintf(stderr, "Unable to find Sparc CPU definition\n");
        exit(1);
    }

    for(i = 0; i < smp_cpus; i++) {
        env = cpu_init();
        cpu_sparc_register(env, def, i);
        envs[i] = env;
        if (i == 0) {
            qemu_register_reset(main_cpu_reset, env);
        } else {
            qemu_register_reset(secondary_cpu_reset, env);
            env->halted = 1;
        }
        register_savevm("cpu", i, 3, cpu_save, cpu_load, env);
        cpu_irqs[i] = qemu_allocate_irqs(cpu_set_irq, envs[i], MAX_PILS);
    }

    for (i = smp_cpus; i < MAX_CPUS; i++)
        cpu_irqs[i] = qemu_allocate_irqs(dummy_cpu_set_irq, NULL, MAX_PILS);

    /* allocate RAM */
    cpu_register_physical_memory(0, RAM_size, 0);

    iommu = iommu_init(hwdef->iommu_base);
    slavio_intctl = slavio_intctl_init(hwdef->intctl_base,
                                       hwdef->intctl_base + 0x10000ULL,
                                       &hwdef->intbit_to_level[0],
                                       &slavio_irq, &slavio_cpu_irq,
                                       cpu_irqs,
                                       hwdef->clock_irq);

    espdma = sparc32_dma_init(hwdef->dma_base, slavio_irq[hwdef->esp_irq],
                              iommu, &espdma_irq, &esp_reset);

    ledma = sparc32_dma_init(hwdef->dma_base + 16ULL,
                             slavio_irq[hwdef->le_irq], iommu, &ledma_irq,
                             &le_reset);

    if (graphic_depth != 8 && graphic_depth != 24) {
        fprintf(stderr, "qemu: Unsupported depth: %d\n", graphic_depth);
        exit (1);
    }
    tcx_init(ds, hwdef->tcx_base, phys_ram_base + RAM_size, RAM_size,
             hwdef->vram_size, graphic_width, graphic_height, graphic_depth);

    if (nd_table[0].model == NULL
        || strcmp(nd_table[0].model, "lance") == 0) {
        lance_init(&nd_table[0], hwdef->le_base, ledma, *ledma_irq, le_reset);
    } else if (strcmp(nd_table[0].model, "?") == 0) {
        fprintf(stderr, "qemu: Supported NICs: lance\n");
        exit (1);
    } else {
        fprintf(stderr, "qemu: Unsupported NIC: %s\n", nd_table[0].model);
        exit (1);
    }

    nvram = m48t59_init(slavio_irq[0], hwdef->nvram_base, 0,
                        hwdef->nvram_size, 8);

    slavio_timer_init_all(hwdef->counter_base, slavio_irq[hwdef->clock1_irq],
                          slavio_cpu_irq);

    slavio_serial_ms_kbd_init(hwdef->ms_kb_base, slavio_irq[hwdef->ms_kb_irq]);
    // Slavio TTYA (base+4, Linux ttyS0) is the first Qemu serial device
    // Slavio TTYB (base+0, Linux ttyS1) is the second Qemu serial device
    slavio_serial_init(hwdef->serial_base, slavio_irq[hwdef->ser_irq],
                       serial_hds[1], serial_hds[0]);

    sun4m_fdctrl_init(slavio_irq[hwdef->fd_irq], hwdef->fd_base, fd_table);

    main_esp = esp_init(bs_table, hwdef->esp_base, espdma, *espdma_irq,
                        esp_reset);

    for (i = 0; i < MAX_DISKS; i++) {
        if (bs_table[i]) {
            esp_scsi_attach(main_esp, bs_table[i], i);
        }
    }

    slavio_misc = slavio_misc_init(hwdef->slavio_base, hwdef->power_base,
                                   slavio_irq[hwdef->me_irq]);
    if (hwdef->cs_base != (target_phys_addr_t)-1)
        cs_init(hwdef->cs_base, hwdef->cs_irq, slavio_intctl);

    return nvram;
}

static void sun4m_load_kernel(long vram_size, int RAM_size,
                              const char *boot_device,
                              const char *kernel_filename,
                              const char *kernel_cmdline,
                              const char *initrd_filename,
                              int machine_id,
                              void *nvram)
{
    int ret, linux_boot;
    char buf[1024];
    unsigned int i;
    long prom_offset, initrd_size, kernel_size;

    linux_boot = (kernel_filename != NULL);

    prom_offset = RAM_size + vram_size;
    cpu_register_physical_memory(PROM_PADDR,
                                 (PROM_SIZE_MAX + TARGET_PAGE_SIZE - 1) & TARGET_PAGE_MASK,
                                 prom_offset | IO_MEM_ROM);

    if (bios_name == NULL)
        bios_name = PROM_FILENAME;
    snprintf(buf, sizeof(buf), "%s/%s", bios_dir, bios_name);
    ret = load_elf(buf, PROM_PADDR - PROM_VADDR, NULL, NULL, NULL);
    if (ret < 0 || ret > PROM_SIZE_MAX)
        ret = load_image(buf, phys_ram_base + prom_offset);
    if (ret < 0 || ret > PROM_SIZE_MAX) {
        fprintf(stderr, "qemu: could not load prom '%s'\n",
                buf);
        exit(1);
    }

    kernel_size = 0;
    if (linux_boot) {
        kernel_size = load_elf(kernel_filename, -0xf0000000ULL, NULL, NULL,
                               NULL);
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
               boot_device, RAM_size, kernel_size, graphic_width,
               graphic_height, graphic_depth, machine_id);
}

static const struct hwdef hwdefs[] = {
    /* SS-5 */
    {
        .iommu_base   = 0x10000000,
        .tcx_base     = 0x50000000,
        .cs_base      = 0x6c000000,
        .slavio_base  = 0x70000000,
        .ms_kb_base   = 0x71000000,
        .serial_base  = 0x71100000,
        .nvram_base   = 0x71200000,
        .fd_base      = 0x71400000,
        .counter_base = 0x71d00000,
        .intctl_base  = 0x71e00000,
        .dma_base     = 0x78400000,
        .esp_base     = 0x78800000,
        .le_base      = 0x78c00000,
        .power_base   = 0x7a000000,
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
            2, 3, 5, 7, 9, 11, 0, 14,   3, 5, 7, 9, 11, 13, 12, 12,
            6, 0, 4, 10, 8, 0, 11, 0,   0, 0, 0, 0, 15, 0, 15, 0,
        },
    },
    /* SS-10 */
    {
        .iommu_base   = 0xfe0000000ULL,
        .tcx_base     = 0xe20000000ULL,
        .cs_base      = -1,
        .slavio_base  = 0xff0000000ULL,
        .ms_kb_base   = 0xff1000000ULL,
        .serial_base  = 0xff1100000ULL,
        .nvram_base   = 0xff1200000ULL,
        .fd_base      = 0xff1700000ULL,
        .counter_base = 0xff1300000ULL,
        .intctl_base  = 0xff1400000ULL,
        .dma_base     = 0xef0400000ULL,
        .esp_base     = 0xef0800000ULL,
        .le_base      = 0xef0c00000ULL,
        .power_base   = 0xefa000000ULL,
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
            2, 3, 5, 7, 9, 11, 0, 14,   3, 5, 7, 9, 11, 13, 12, 12,
            6, 0, 4, 10, 8, 0, 11, 0,   0, 0, 0, 0, 15, 0, 15, 0,
        },
    },
};

static void sun4m_common_init(int RAM_size, const char *boot_device, DisplayState *ds,
                              const char *kernel_filename, const char *kernel_cmdline,
                              const char *initrd_filename, const char *cpu_model,
                              unsigned int machine, int max_ram)
{
    void *nvram;

    if ((unsigned int)RAM_size > (unsigned int)max_ram) {
        fprintf(stderr, "qemu: Too much memory for this machine: %d, maximum %d\n",
                (unsigned int)RAM_size / (1024 * 1024),
                (unsigned int)max_ram / (1024 * 1024));
        exit(1);
    }
    nvram = sun4m_hw_init(&hwdefs[machine], RAM_size, ds, cpu_model);

    sun4m_load_kernel(hwdefs[machine].vram_size, RAM_size, boot_device,
                      kernel_filename, kernel_cmdline, initrd_filename,
                      hwdefs[machine].machine_id, nvram);
}

/* SPARCstation 5 hardware initialisation */
static void ss5_init(int RAM_size, int vga_ram_size, const char *boot_device,
                       DisplayState *ds, const char **fd_filename, int snapshot,
                       const char *kernel_filename, const char *kernel_cmdline,
                       const char *initrd_filename, const char *cpu_model)
{
    if (cpu_model == NULL)
        cpu_model = "Fujitsu MB86904";
    sun4m_common_init(RAM_size, boot_device, ds, kernel_filename,
                      kernel_cmdline, initrd_filename, cpu_model,
                      0, 0x10000000);
}

/* SPARCstation 10 hardware initialisation */
static void ss10_init(int RAM_size, int vga_ram_size, const char *boot_device,
                            DisplayState *ds, const char **fd_filename, int snapshot,
                            const char *kernel_filename, const char *kernel_cmdline,
                            const char *initrd_filename, const char *cpu_model)
{
    if (cpu_model == NULL)
        cpu_model = "TI SuperSparc II";
    sun4m_common_init(RAM_size, boot_device, ds, kernel_filename,
                      kernel_cmdline, initrd_filename, cpu_model,
                      1, 0xffffffff); // XXX actually first 62GB ok
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
