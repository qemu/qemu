/*
 * QEMU Sun4m & Sun4d & Sun4c System Emulator
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
#include "hw.h"
#include "qemu-timer.h"
#include "sun4m.h"
#include "nvram.h"
#include "sparc32_dma.h"
#include "fdc.h"
#include "sysemu.h"
#include "net.h"
#include "boards.h"
#include "firmware_abi.h"

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
 * Sun4d architecture was used in the following machines:
 *
 * SPARCcenter 2000
 * SPARCserver 1000
 *
 * Sun4c architecture was used in the following machines:
 * SPARCstation 1/1+, SPARCserver 1/1+
 * SPARCstation SLC
 * SPARCstation IPC
 * SPARCstation ELC
 * SPARCstation IPX
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
#define PROM_VADDR           0xffd00000
#define PROM_FILENAME        "openbios-sparc32"

#define MAX_CPUS 16
#define MAX_PILS 16

struct hwdef {
    target_phys_addr_t iommu_base, slavio_base;
    target_phys_addr_t intctl_base, counter_base, nvram_base, ms_kb_base;
    target_phys_addr_t serial_base, fd_base;
    target_phys_addr_t idreg_base, dma_base, esp_base, le_base;
    target_phys_addr_t tcx_base, cs_base, power_base;
    target_phys_addr_t ecc_base;
    uint32_t ecc_version;
    target_phys_addr_t sun4c_intctl_base, sun4c_counter_base;
    long vram_size, nvram_size;
    // IRQ numbers are not PIL ones, but master interrupt controller
    // register bit numbers
    int intctl_g_intr, esp_irq, le_irq, clock_irq, clock1_irq;
    int ser_irq, ms_kb_irq, fd_irq, me_irq, cs_irq;
    int machine_id; // For NVRAM
    uint32_t iommu_version;
    uint32_t intbit_to_level[32];
    uint64_t max_mem;
    const char * const default_cpu_model;
};

#define MAX_IOUNITS 5

struct sun4d_hwdef {
    target_phys_addr_t iounit_bases[MAX_IOUNITS], slavio_base;
    target_phys_addr_t counter_base, nvram_base, ms_kb_base;
    target_phys_addr_t serial_base;
    target_phys_addr_t espdma_base, esp_base;
    target_phys_addr_t ledma_base, le_base;
    target_phys_addr_t tcx_base;
    target_phys_addr_t sbi_base;
    unsigned long vram_size, nvram_size;
    // IRQ numbers are not PIL ones, but SBI register bit numbers
    int esp_irq, le_irq, clock_irq, clock1_irq;
    int ser_irq, ms_kb_irq, me_irq;
    int machine_id; // For NVRAM
    uint32_t iounit_version;
    uint64_t max_mem;
    const char * const default_cpu_model;
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

extern int nographic;

static void nvram_init(m48t59_t *nvram, uint8_t *macaddr, const char *cmdline,
                       const char *boot_devices, uint32_t RAM_size,
                       uint32_t kernel_size,
                       int width, int height, int depth,
                       int machine_id, const char *arch)
{
    unsigned int i;
    uint32_t start, end;
    uint8_t image[0x1ff0];
    ohwcfg_v3_t *header = (ohwcfg_v3_t *)&image;
    struct sparc_arch_cfg *sparc_header;
    struct OpenBIOS_nvpart_v1 *part_header;

    memset(image, '\0', sizeof(image));

    // Try to match PPC NVRAM
    strcpy(header->struct_ident, "QEMU_BIOS");
    header->struct_version = cpu_to_be32(3); /* structure v3 */

    header->nvram_size = cpu_to_be16(0x2000);
    header->nvram_arch_ptr = cpu_to_be16(sizeof(ohwcfg_v3_t));
    header->nvram_arch_size = cpu_to_be16(sizeof(struct sparc_arch_cfg));
    strcpy(header->arch, arch);
    header->nb_cpus = smp_cpus & 0xff;
    header->RAM0_base = 0;
    header->RAM0_size = cpu_to_be64((uint64_t)RAM_size);
    strcpy(header->boot_devices, boot_devices);
    header->nboot_devices = strlen(boot_devices) & 0xff;
    header->kernel_image = cpu_to_be64((uint64_t)KERNEL_LOAD_ADDR);
    header->kernel_size = cpu_to_be64((uint64_t)kernel_size);
    if (cmdline) {
        strcpy(phys_ram_base + CMDLINE_ADDR, cmdline);
        header->cmdline = cpu_to_be64((uint64_t)CMDLINE_ADDR);
        header->cmdline_size = cpu_to_be64((uint64_t)strlen(cmdline));
    }
    // XXX add initrd_image, initrd_size
    header->width = cpu_to_be16(width);
    header->height = cpu_to_be16(height);
    header->depth = cpu_to_be16(depth);
    if (nographic)
        header->graphic_flags = cpu_to_be16(OHW_GF_NOGRAPHICS);

    header->crc = cpu_to_be16(OHW_compute_crc(header, 0x00, 0xF8));

    // Architecture specific header
    start = sizeof(ohwcfg_v3_t);
    sparc_header = (struct sparc_arch_cfg *)&image[start];
    sparc_header->valid = 0;
    start += sizeof(struct sparc_arch_cfg);

    // OpenBIOS nvram variables
    // Variable partition
    part_header = (struct OpenBIOS_nvpart_v1 *)&image[start];
    part_header->signature = OPENBIOS_PART_SYSTEM;
    strcpy(part_header->name, "system");

    end = start + sizeof(struct OpenBIOS_nvpart_v1);
    for (i = 0; i < nb_prom_envs; i++)
        end = OpenBIOS_set_var(image, end, prom_envs[i]);

    // End marker
    image[end++] = '\0';

    end = start + ((end - start + 15) & ~15);
    OpenBIOS_finish_partition(part_header, end - start);

    // free partition
    start = end;
    part_header = (struct OpenBIOS_nvpart_v1 *)&image[start];
    part_header->signature = OPENBIOS_PART_FREE;
    strcpy(part_header->name, "free");

    end = 0x1fd0;
    OpenBIOS_finish_partition(part_header, end - start);

    Sun_init_header((struct Sun_nvram *)&image[0x1fd8], macaddr, machine_id);

    for (i = 0; i < sizeof(image); i++)
        m48t59_write(nvram, i, image[i]);
}

static void *slavio_intctl;

void pic_info()
{
    if (slavio_intctl)
        slavio_pic_info(slavio_intctl);
}

void irq_info()
{
    if (slavio_intctl)
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

static unsigned long sun4m_load_kernel(const char *kernel_filename,
                                       const char *kernel_cmdline,
                                       const char *initrd_filename)
{
    int linux_boot;
    unsigned int i;
    long initrd_size, kernel_size;

    linux_boot = (kernel_filename != NULL);

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
    return kernel_size;
}

static void sun4m_hw_init(const struct hwdef *hwdef, int RAM_size,
                          const char *boot_device,
                          DisplayState *ds, const char *kernel_filename,
                          const char *kernel_cmdline,
                          const char *initrd_filename, const char *cpu_model)

{
    CPUState *env, *envs[MAX_CPUS];
    unsigned int i;
    void *iommu, *espdma, *ledma, *main_esp, *nvram;
    qemu_irq *cpu_irqs[MAX_CPUS], *slavio_irq, *slavio_cpu_irq,
        *espdma_irq, *ledma_irq;
    qemu_irq *esp_reset, *le_reset;
    unsigned long prom_offset, kernel_size;
    int ret;
    char buf[1024];
    BlockDriverState *fd[MAX_FD];
    int index;

    /* init CPUs */
    if (!cpu_model)
        cpu_model = hwdef->default_cpu_model;

    for(i = 0; i < smp_cpus; i++) {
        env = cpu_init(cpu_model);
        if (!env) {
            fprintf(stderr, "qemu: Unable to find Sparc CPU definition\n");
            exit(1);
        }
        cpu_sparc_set_id(env, i);
        envs[i] = env;
        if (i == 0) {
            qemu_register_reset(main_cpu_reset, env);
        } else {
            qemu_register_reset(secondary_cpu_reset, env);
            env->halted = 1;
        }
        register_savevm("cpu", i, 3, cpu_save, cpu_load, env);
        cpu_irqs[i] = qemu_allocate_irqs(cpu_set_irq, envs[i], MAX_PILS);
        env->prom_addr = hwdef->slavio_base;
    }

    for (i = smp_cpus; i < MAX_CPUS; i++)
        cpu_irqs[i] = qemu_allocate_irqs(dummy_cpu_set_irq, NULL, MAX_PILS);


    /* allocate RAM */
    if ((uint64_t)RAM_size > hwdef->max_mem) {
        fprintf(stderr, "qemu: Too much memory for this machine: %d, maximum %d\n",
                (unsigned int)RAM_size / (1024 * 1024),
                (unsigned int)(hwdef->max_mem / (1024 * 1024)));
        exit(1);
    }
    cpu_register_physical_memory(0, RAM_size, 0);

    /* load boot prom */
    prom_offset = RAM_size + hwdef->vram_size;
    cpu_register_physical_memory(hwdef->slavio_base,
                                 (PROM_SIZE_MAX + TARGET_PAGE_SIZE - 1) &
                                 TARGET_PAGE_MASK,
                                 prom_offset | IO_MEM_ROM);

    if (bios_name == NULL)
        bios_name = PROM_FILENAME;
    snprintf(buf, sizeof(buf), "%s/%s", bios_dir, bios_name);
    ret = load_elf(buf, hwdef->slavio_base - PROM_VADDR, NULL, NULL, NULL);
    if (ret < 0 || ret > PROM_SIZE_MAX)
        ret = load_image(buf, phys_ram_base + prom_offset);
    if (ret < 0 || ret > PROM_SIZE_MAX) {
        fprintf(stderr, "qemu: could not load prom '%s'\n",
                buf);
        exit(1);
    }
    prom_offset += (ret + TARGET_PAGE_SIZE - 1) & TARGET_PAGE_MASK;

    /* set up devices */
    slavio_intctl = slavio_intctl_init(hwdef->intctl_base,
                                       hwdef->intctl_base + 0x10000ULL,
                                       &hwdef->intbit_to_level[0],
                                       &slavio_irq, &slavio_cpu_irq,
                                       cpu_irqs,
                                       hwdef->clock_irq);

    if (hwdef->idreg_base != (target_phys_addr_t)-1) {
        stl_raw(phys_ram_base + prom_offset, 0xfe810103);

        cpu_register_physical_memory(hwdef->idreg_base, sizeof(uint32_t),
                                     prom_offset | IO_MEM_ROM);
    }

    iommu = iommu_init(hwdef->iommu_base, hwdef->iommu_version,
                       slavio_irq[hwdef->me_irq]);

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
                          slavio_cpu_irq, smp_cpus);

    slavio_serial_ms_kbd_init(hwdef->ms_kb_base, slavio_irq[hwdef->ms_kb_irq],
                              nographic);
    // Slavio TTYA (base+4, Linux ttyS0) is the first Qemu serial device
    // Slavio TTYB (base+0, Linux ttyS1) is the second Qemu serial device
    slavio_serial_init(hwdef->serial_base, slavio_irq[hwdef->ser_irq],
                       serial_hds[1], serial_hds[0]);

    if (hwdef->fd_base != (target_phys_addr_t)-1) {
        /* there is zero or one floppy drive */
        fd[1] = fd[0] = NULL;
        index = drive_get_index(IF_FLOPPY, 0, 0);
        if (index != -1)
            fd[0] = drives_table[index].bdrv;

        sun4m_fdctrl_init(slavio_irq[hwdef->fd_irq], hwdef->fd_base, fd);
    }

    if (drive_get_max_bus(IF_SCSI) > 0) {
        fprintf(stderr, "qemu: too many SCSI bus\n");
        exit(1);
    }

    main_esp = esp_init(hwdef->esp_base, espdma, *espdma_irq,
                        esp_reset);

    for (i = 0; i < ESP_MAX_DEVS; i++) {
        index = drive_get_index(IF_SCSI, 0, i);
        if (index == -1)
            continue;
        esp_scsi_attach(main_esp, drives_table[index].bdrv, i);
    }

    slavio_misc = slavio_misc_init(hwdef->slavio_base, hwdef->power_base,
                                   slavio_irq[hwdef->me_irq]);
    if (hwdef->cs_base != (target_phys_addr_t)-1)
        cs_init(hwdef->cs_base, hwdef->cs_irq, slavio_intctl);

    kernel_size = sun4m_load_kernel(kernel_filename, kernel_cmdline,
                                    initrd_filename);

    nvram_init(nvram, (uint8_t *)&nd_table[0].macaddr, kernel_cmdline,
               boot_device, RAM_size, kernel_size, graphic_width,
               graphic_height, graphic_depth, hwdef->machine_id, "Sun4m");

    if (hwdef->ecc_base != (target_phys_addr_t)-1)
        ecc_init(hwdef->ecc_base, hwdef->ecc_version);
}

static void sun4c_hw_init(const struct hwdef *hwdef, int RAM_size,
                          const char *boot_device,
                          DisplayState *ds, const char *kernel_filename,
                          const char *kernel_cmdline,
                          const char *initrd_filename, const char *cpu_model)
{
    CPUState *env;
    unsigned int i;
    void *iommu, *espdma, *ledma, *main_esp, *nvram;
    qemu_irq *cpu_irqs, *slavio_irq, *espdma_irq, *ledma_irq;
    qemu_irq *esp_reset, *le_reset;
    unsigned long prom_offset, kernel_size;
    int ret;
    char buf[1024];
    BlockDriverState *fd[MAX_FD];
    int index;

    /* init CPU */
    if (!cpu_model)
        cpu_model = hwdef->default_cpu_model;

    env = cpu_init(cpu_model);
    if (!env) {
        fprintf(stderr, "qemu: Unable to find Sparc CPU definition\n");
        exit(1);
    }

    cpu_sparc_set_id(env, 0);

    qemu_register_reset(main_cpu_reset, env);
    register_savevm("cpu", 0, 3, cpu_save, cpu_load, env);
    cpu_irqs = qemu_allocate_irqs(cpu_set_irq, env, MAX_PILS);
    env->prom_addr = hwdef->slavio_base;

    /* allocate RAM */
    if ((uint64_t)RAM_size > hwdef->max_mem) {
        fprintf(stderr, "qemu: Too much memory for this machine: %d, maximum %d\n",
                (unsigned int)RAM_size / (1024 * 1024),
                (unsigned int)hwdef->max_mem / (1024 * 1024));
        exit(1);
    }
    cpu_register_physical_memory(0, RAM_size, 0);

    /* load boot prom */
    prom_offset = RAM_size + hwdef->vram_size;
    cpu_register_physical_memory(hwdef->slavio_base,
                                 (PROM_SIZE_MAX + TARGET_PAGE_SIZE - 1) &
                                 TARGET_PAGE_MASK,
                                 prom_offset | IO_MEM_ROM);

    if (bios_name == NULL)
        bios_name = PROM_FILENAME;
    snprintf(buf, sizeof(buf), "%s/%s", bios_dir, bios_name);
    ret = load_elf(buf, hwdef->slavio_base - PROM_VADDR, NULL, NULL, NULL);
    if (ret < 0 || ret > PROM_SIZE_MAX)
        ret = load_image(buf, phys_ram_base + prom_offset);
    if (ret < 0 || ret > PROM_SIZE_MAX) {
        fprintf(stderr, "qemu: could not load prom '%s'\n",
                buf);
        exit(1);
    }
    prom_offset += (ret + TARGET_PAGE_SIZE - 1) & TARGET_PAGE_MASK;

    /* set up devices */
    slavio_intctl = sun4c_intctl_init(hwdef->sun4c_intctl_base,
                                      &slavio_irq, cpu_irqs);

    iommu = iommu_init(hwdef->iommu_base, hwdef->iommu_version,
                       slavio_irq[hwdef->me_irq]);

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
                        hwdef->nvram_size, 2);

    slavio_serial_ms_kbd_init(hwdef->ms_kb_base, slavio_irq[hwdef->ms_kb_irq],
                              nographic);
    // Slavio TTYA (base+4, Linux ttyS0) is the first Qemu serial device
    // Slavio TTYB (base+0, Linux ttyS1) is the second Qemu serial device
    slavio_serial_init(hwdef->serial_base, slavio_irq[hwdef->ser_irq],
                       serial_hds[1], serial_hds[0]);

    if (hwdef->fd_base != (target_phys_addr_t)-1) {
        /* there is zero or one floppy drive */
        fd[1] = fd[0] = NULL;
        index = drive_get_index(IF_FLOPPY, 0, 0);
        if (index != -1)
            fd[0] = drives_table[index].bdrv;

        sun4m_fdctrl_init(slavio_irq[hwdef->fd_irq], hwdef->fd_base, fd);
    }

    if (drive_get_max_bus(IF_SCSI) > 0) {
        fprintf(stderr, "qemu: too many SCSI bus\n");
        exit(1);
    }

    main_esp = esp_init(hwdef->esp_base, espdma, *espdma_irq,
                        esp_reset);

    for (i = 0; i < ESP_MAX_DEVS; i++) {
        index = drive_get_index(IF_SCSI, 0, i);
        if (index == -1)
            continue;
        esp_scsi_attach(main_esp, drives_table[index].bdrv, i);
    }

    kernel_size = sun4m_load_kernel(kernel_filename, kernel_cmdline,
                                    initrd_filename);

    nvram_init(nvram, (uint8_t *)&nd_table[0].macaddr, kernel_cmdline,
               boot_device, RAM_size, kernel_size, graphic_width,
               graphic_height, graphic_depth, hwdef->machine_id, "Sun4c");
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
        .idreg_base   = 0x78000000,
        .dma_base     = 0x78400000,
        .esp_base     = 0x78800000,
        .le_base      = 0x78c00000,
        .power_base   = 0x7a000000,
        .ecc_base     = -1,
        .sun4c_intctl_base  = -1,
        .sun4c_counter_base = -1,
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
        .iommu_version = 0x05000000,
        .intbit_to_level = {
            2, 3, 5, 7, 9, 11, 0, 14,   3, 5, 7, 9, 11, 13, 12, 12,
            6, 0, 4, 10, 8, 0, 11, 0,   0, 0, 0, 0, 15, 0, 15, 0,
        },
        .max_mem = 0x10000000,
        .default_cpu_model = "Fujitsu MB86904",
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
        .idreg_base   = 0xef0000000ULL,
        .dma_base     = 0xef0400000ULL,
        .esp_base     = 0xef0800000ULL,
        .le_base      = 0xef0c00000ULL,
        .power_base   = 0xefa000000ULL,
        .ecc_base     = 0xf00000000ULL,
        .ecc_version  = 0x10000000, // version 0, implementation 1
        .sun4c_intctl_base  = -1,
        .sun4c_counter_base = -1,
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
        .iommu_version = 0x03000000,
        .intbit_to_level = {
            2, 3, 5, 7, 9, 11, 0, 14,   3, 5, 7, 9, 11, 13, 12, 12,
            6, 0, 4, 10, 8, 0, 11, 0,   0, 0, 0, 0, 15, 0, 15, 0,
        },
        .max_mem = 0xffffffff, // XXX actually first 62GB ok
        .default_cpu_model = "TI SuperSparc II",
    },
    /* SS-600MP */
    {
        .iommu_base   = 0xfe0000000ULL,
        .tcx_base     = 0xe20000000ULL,
        .cs_base      = -1,
        .slavio_base  = 0xff0000000ULL,
        .ms_kb_base   = 0xff1000000ULL,
        .serial_base  = 0xff1100000ULL,
        .nvram_base   = 0xff1200000ULL,
        .fd_base      = -1,
        .counter_base = 0xff1300000ULL,
        .intctl_base  = 0xff1400000ULL,
        .idreg_base   = -1,
        .dma_base     = 0xef0081000ULL,
        .esp_base     = 0xef0080000ULL,
        .le_base      = 0xef0060000ULL,
        .power_base   = 0xefa000000ULL,
        .ecc_base     = 0xf00000000ULL,
        .ecc_version  = 0x00000000, // version 0, implementation 0
        .sun4c_intctl_base  = -1,
        .sun4c_counter_base = -1,
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
        .machine_id = 0x71,
        .iommu_version = 0x01000000,
        .intbit_to_level = {
            2, 3, 5, 7, 9, 11, 0, 14,   3, 5, 7, 9, 11, 13, 12, 12,
            6, 0, 4, 10, 8, 0, 11, 0,   0, 0, 0, 0, 15, 0, 15, 0,
        },
        .max_mem = 0xffffffff, // XXX actually first 62GB ok
        .default_cpu_model = "TI SuperSparc II",
    },
    /* SS-20 */
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
        .idreg_base   = 0xef0000000ULL,
        .dma_base     = 0xef0400000ULL,
        .esp_base     = 0xef0800000ULL,
        .le_base      = 0xef0c00000ULL,
        .power_base   = 0xefa000000ULL,
        .ecc_base     = 0xf00000000ULL,
        .ecc_version  = 0x20000000, // version 0, implementation 2
        .sun4c_intctl_base  = -1,
        .sun4c_counter_base = -1,
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
        .iommu_version = 0x13000000,
        .intbit_to_level = {
            2, 3, 5, 7, 9, 11, 0, 14,   3, 5, 7, 9, 11, 13, 12, 12,
            6, 0, 4, 10, 8, 0, 11, 0,   0, 0, 0, 0, 15, 0, 15, 0,
        },
        .max_mem = 0xffffffff, // XXX actually first 62GB ok
        .default_cpu_model = "TI SuperSparc II",
    },
    /* SS-2 */
    {
        .iommu_base   = 0xf8000000,
        .tcx_base     = 0xfe000000,
        .cs_base      = -1,
        .slavio_base  = 0xf6000000,
        .ms_kb_base   = 0xf0000000,
        .serial_base  = 0xf1000000,
        .nvram_base   = 0xf2000000,
        .fd_base      = 0xf7200000,
        .counter_base = -1,
        .intctl_base  = -1,
        .dma_base     = 0xf8400000,
        .esp_base     = 0xf8800000,
        .le_base      = 0xf8c00000,
        .power_base   = -1,
        .sun4c_intctl_base  = 0xf5000000,
        .sun4c_counter_base = 0xf3000000,
        .vram_size    = 0x00100000,
        .nvram_size   = 0x800,
        .esp_irq = 2,
        .le_irq = 3,
        .clock_irq = 5,
        .clock1_irq = 7,
        .ms_kb_irq = 1,
        .ser_irq = 1,
        .fd_irq = 1,
        .me_irq = 1,
        .cs_irq = -1,
        .machine_id = 0x55,
        .max_mem = 0x10000000,
        .default_cpu_model = "Cypress CY7C601",
    },
};

/* SPARCstation 5 hardware initialisation */
static void ss5_init(int RAM_size, int vga_ram_size,
                     const char *boot_device, DisplayState *ds,
                     const char *kernel_filename, const char *kernel_cmdline,
                     const char *initrd_filename, const char *cpu_model)
{
    sun4m_hw_init(&hwdefs[0], RAM_size, boot_device, ds, kernel_filename,
                  kernel_cmdline, initrd_filename, cpu_model);
}

/* SPARCstation 10 hardware initialisation */
static void ss10_init(int RAM_size, int vga_ram_size,
                      const char *boot_device, DisplayState *ds,
                      const char *kernel_filename, const char *kernel_cmdline,
                      const char *initrd_filename, const char *cpu_model)
{
    sun4m_hw_init(&hwdefs[1], RAM_size, boot_device, ds, kernel_filename,
                  kernel_cmdline, initrd_filename, cpu_model);
}

/* SPARCserver 600MP hardware initialisation */
static void ss600mp_init(int RAM_size, int vga_ram_size,
                         const char *boot_device, DisplayState *ds,
                         const char *kernel_filename, const char *kernel_cmdline,
                         const char *initrd_filename, const char *cpu_model)
{
    sun4m_hw_init(&hwdefs[2], RAM_size, boot_device, ds, kernel_filename,
                  kernel_cmdline, initrd_filename, cpu_model);
}

/* SPARCstation 20 hardware initialisation */
static void ss20_init(int RAM_size, int vga_ram_size,
                      const char *boot_device, DisplayState *ds,
                      const char *kernel_filename, const char *kernel_cmdline,
                      const char *initrd_filename, const char *cpu_model)
{
    sun4m_hw_init(&hwdefs[3], RAM_size, boot_device, ds, kernel_filename,
                  kernel_cmdline, initrd_filename, cpu_model);
}

/* SPARCstation 2 hardware initialisation */
static void ss2_init(int RAM_size, int vga_ram_size,
                     const char *boot_device, DisplayState *ds,
                     const char *kernel_filename, const char *kernel_cmdline,
                     const char *initrd_filename, const char *cpu_model)
{
    sun4c_hw_init(&hwdefs[4], RAM_size, boot_device, ds, kernel_filename,
                  kernel_cmdline, initrd_filename, cpu_model);
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

QEMUMachine ss600mp_machine = {
    "SS-600MP",
    "Sun4m platform, SPARCserver 600MP",
    ss600mp_init,
};

QEMUMachine ss20_machine = {
    "SS-20",
    "Sun4m platform, SPARCstation 20",
    ss20_init,
};

QEMUMachine ss2_machine = {
    "SS-2",
    "Sun4c platform, SPARCstation 2",
    ss2_init,
};

static const struct sun4d_hwdef sun4d_hwdefs[] = {
    /* SS-1000 */
    {
        .iounit_bases   = {
            0xfe0200000ULL,
            0xfe1200000ULL,
            0xfe2200000ULL,
            0xfe3200000ULL,
            -1,
        },
        .tcx_base     = 0x820000000ULL,
        .slavio_base  = 0xf00000000ULL,
        .ms_kb_base   = 0xf00240000ULL,
        .serial_base  = 0xf00200000ULL,
        .nvram_base   = 0xf00280000ULL,
        .counter_base = 0xf00300000ULL,
        .espdma_base  = 0x800081000ULL,
        .esp_base     = 0x800080000ULL,
        .ledma_base   = 0x800040000ULL,
        .le_base      = 0x800060000ULL,
        .sbi_base     = 0xf02800000ULL,
        .vram_size    = 0x00100000,
        .nvram_size   = 0x2000,
        .esp_irq = 3,
        .le_irq = 4,
        .clock_irq = 14,
        .clock1_irq = 10,
        .ms_kb_irq = 12,
        .ser_irq = 12,
        .machine_id = 0x80,
        .iounit_version = 0x03000000,
        .max_mem = 0xffffffff, // XXX actually first 62GB ok
        .default_cpu_model = "TI SuperSparc II",
    },
    /* SS-2000 */
    {
        .iounit_bases   = {
            0xfe0200000ULL,
            0xfe1200000ULL,
            0xfe2200000ULL,
            0xfe3200000ULL,
            0xfe4200000ULL,
        },
        .tcx_base     = 0x820000000ULL,
        .slavio_base  = 0xf00000000ULL,
        .ms_kb_base   = 0xf00240000ULL,
        .serial_base  = 0xf00200000ULL,
        .nvram_base   = 0xf00280000ULL,
        .counter_base = 0xf00300000ULL,
        .espdma_base  = 0x800081000ULL,
        .esp_base     = 0x800080000ULL,
        .ledma_base   = 0x800040000ULL,
        .le_base      = 0x800060000ULL,
        .sbi_base     = 0xf02800000ULL,
        .vram_size    = 0x00100000,
        .nvram_size   = 0x2000,
        .esp_irq = 3,
        .le_irq = 4,
        .clock_irq = 14,
        .clock1_irq = 10,
        .ms_kb_irq = 12,
        .ser_irq = 12,
        .machine_id = 0x80,
        .iounit_version = 0x03000000,
        .max_mem = 0xffffffff, // XXX actually first 62GB ok
        .default_cpu_model = "TI SuperSparc II",
    },
};

static void sun4d_hw_init(const struct sun4d_hwdef *hwdef, int RAM_size,
                          const char *boot_device,
                          DisplayState *ds, const char *kernel_filename,
                          const char *kernel_cmdline,
                          const char *initrd_filename, const char *cpu_model)
{
    CPUState *env, *envs[MAX_CPUS];
    unsigned int i;
    void *iounits[MAX_IOUNITS], *espdma, *ledma, *main_esp, *nvram, *sbi;
    qemu_irq *cpu_irqs[MAX_CPUS], *sbi_irq, *sbi_cpu_irq,
        *espdma_irq, *ledma_irq;
    qemu_irq *esp_reset, *le_reset;
    unsigned long prom_offset, kernel_size;
    int ret;
    char buf[1024];
    int index;

    /* init CPUs */
    if (!cpu_model)
        cpu_model = hwdef->default_cpu_model;

    for (i = 0; i < smp_cpus; i++) {
        env = cpu_init(cpu_model);
        if (!env) {
            fprintf(stderr, "qemu: Unable to find Sparc CPU definition\n");
            exit(1);
        }
        cpu_sparc_set_id(env, i);
        envs[i] = env;
        if (i == 0) {
            qemu_register_reset(main_cpu_reset, env);
        } else {
            qemu_register_reset(secondary_cpu_reset, env);
            env->halted = 1;
        }
        register_savevm("cpu", i, 3, cpu_save, cpu_load, env);
        cpu_irqs[i] = qemu_allocate_irqs(cpu_set_irq, envs[i], MAX_PILS);
        env->prom_addr = hwdef->slavio_base;
    }

    for (i = smp_cpus; i < MAX_CPUS; i++)
        cpu_irqs[i] = qemu_allocate_irqs(dummy_cpu_set_irq, NULL, MAX_PILS);

    /* allocate RAM */
    if ((uint64_t)RAM_size > hwdef->max_mem) {
        fprintf(stderr, "qemu: Too much memory for this machine: %d, maximum %d\n",
                (unsigned int)RAM_size / (1024 * 1024),
                (unsigned int)(hwdef->max_mem / (1024 * 1024)));
        exit(1);
    }
    cpu_register_physical_memory(0, RAM_size, 0);

    /* load boot prom */
    prom_offset = RAM_size + hwdef->vram_size;
    cpu_register_physical_memory(hwdef->slavio_base,
                                 (PROM_SIZE_MAX + TARGET_PAGE_SIZE - 1) &
                                 TARGET_PAGE_MASK,
                                 prom_offset | IO_MEM_ROM);

    if (bios_name == NULL)
        bios_name = PROM_FILENAME;
    snprintf(buf, sizeof(buf), "%s/%s", bios_dir, bios_name);
    ret = load_elf(buf, hwdef->slavio_base - PROM_VADDR, NULL, NULL, NULL);
    if (ret < 0 || ret > PROM_SIZE_MAX)
        ret = load_image(buf, phys_ram_base + prom_offset);
    if (ret < 0 || ret > PROM_SIZE_MAX) {
        fprintf(stderr, "qemu: could not load prom '%s'\n",
                buf);
        exit(1);
    }

    /* set up devices */
    sbi = sbi_init(hwdef->sbi_base, &sbi_irq, &sbi_cpu_irq, cpu_irqs);

    for (i = 0; i < MAX_IOUNITS; i++)
        if (hwdef->iounit_bases[i] != (target_phys_addr_t)-1)
            iounits[i] = iommu_init(hwdef->iounit_bases[i],
                                    hwdef->iounit_version,
                                    sbi_irq[hwdef->me_irq]);

    espdma = sparc32_dma_init(hwdef->espdma_base, sbi_irq[hwdef->esp_irq],
                              iounits[0], &espdma_irq, &esp_reset);

    ledma = sparc32_dma_init(hwdef->ledma_base, sbi_irq[hwdef->le_irq],
                             iounits[0], &ledma_irq, &le_reset);

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

    nvram = m48t59_init(sbi_irq[0], hwdef->nvram_base, 0,
                        hwdef->nvram_size, 8);

    slavio_timer_init_all(hwdef->counter_base, sbi_irq[hwdef->clock1_irq],
                          sbi_cpu_irq, smp_cpus);

    slavio_serial_ms_kbd_init(hwdef->ms_kb_base, sbi_irq[hwdef->ms_kb_irq],
                              nographic);
    // Slavio TTYA (base+4, Linux ttyS0) is the first Qemu serial device
    // Slavio TTYB (base+0, Linux ttyS1) is the second Qemu serial device
    slavio_serial_init(hwdef->serial_base, sbi_irq[hwdef->ser_irq],
                       serial_hds[1], serial_hds[0]);

    if (drive_get_max_bus(IF_SCSI) > 0) {
        fprintf(stderr, "qemu: too many SCSI bus\n");
        exit(1);
    }

    main_esp = esp_init(hwdef->esp_base, espdma, *espdma_irq,
                        esp_reset);

    for (i = 0; i < ESP_MAX_DEVS; i++) {
        index = drive_get_index(IF_SCSI, 0, i);
        if (index == -1)
            continue;
        esp_scsi_attach(main_esp, drives_table[index].bdrv, i);
    }

    kernel_size = sun4m_load_kernel(kernel_filename, kernel_cmdline,
                                    initrd_filename);

    nvram_init(nvram, (uint8_t *)&nd_table[0].macaddr, kernel_cmdline,
               boot_device, RAM_size, kernel_size, graphic_width,
               graphic_height, graphic_depth, hwdef->machine_id, "Sun4d");
}

/* SPARCserver 1000 hardware initialisation */
static void ss1000_init(int RAM_size, int vga_ram_size,
                        const char *boot_device, DisplayState *ds,
                        const char *kernel_filename, const char *kernel_cmdline,
                        const char *initrd_filename, const char *cpu_model)
{
    sun4d_hw_init(&sun4d_hwdefs[0], RAM_size, boot_device, ds, kernel_filename,
                  kernel_cmdline, initrd_filename, cpu_model);
}

/* SPARCcenter 2000 hardware initialisation */
static void ss2000_init(int RAM_size, int vga_ram_size,
                        const char *boot_device, DisplayState *ds,
                        const char *kernel_filename, const char *kernel_cmdline,
                        const char *initrd_filename, const char *cpu_model)
{
    sun4d_hw_init(&sun4d_hwdefs[1], RAM_size, boot_device, ds, kernel_filename,
                  kernel_cmdline, initrd_filename, cpu_model);
}

QEMUMachine ss1000_machine = {
    "SS-1000",
    "Sun4d platform, SPARCserver 1000",
    ss1000_init,
};

QEMUMachine ss2000_machine = {
    "SS-2000",
    "Sun4d platform, SPARCcenter 2000",
    ss2000_init,
};
