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
#include "sysbus.h"
#include "qemu-timer.h"
#include "sun4m.h"
#include "nvram.h"
#include "sparc32_dma.h"
#include "fdc.h"
#include "sysemu.h"
#include "net.h"
#include "boards.h"
#include "firmware_abi.h"
#include "scsi.h"
#include "pc.h"
#include "isa.h"
#include "fw_cfg.h"
#include "escc.h"

//#define DEBUG_IRQ

/*
 * Sun4m architecture was used in the following machines:
 *
 * SPARCserver 6xxMP/xx
 * SPARCclassic (SPARCclassic Server)(SPARCstation LC) (4/15),
 * SPARCclassic X (4/10)
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
#define DPRINTF(fmt, ...)                                       \
    do { printf("CPUIRQ: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...)
#endif

#define KERNEL_LOAD_ADDR     0x00004000
#define CMDLINE_ADDR         0x007ff000
#define INITRD_LOAD_ADDR     0x00800000
#define PROM_SIZE_MAX        (1024 * 1024)
#define PROM_VADDR           0xffd00000
#define PROM_FILENAME        "openbios-sparc32"
#define CFG_ADDR             0xd00000510ULL
#define FW_CFG_SUN4M_DEPTH   (FW_CFG_ARCH_LOCAL + 0x00)

#define MAX_CPUS 16
#define MAX_PILS 16

#define ESCC_CLOCK 4915200

struct sun4m_hwdef {
    target_phys_addr_t iommu_base, slavio_base;
    target_phys_addr_t intctl_base, counter_base, nvram_base, ms_kb_base;
    target_phys_addr_t serial_base, fd_base;
    target_phys_addr_t idreg_base, dma_base, esp_base, le_base;
    target_phys_addr_t tcx_base, cs_base, apc_base, aux1_base, aux2_base;
    target_phys_addr_t ecc_base;
    uint32_t ecc_version;
    long vram_size, nvram_size;
    // IRQ numbers are not PIL ones, but master interrupt controller
    // register bit numbers
    int esp_irq, le_irq, clock_irq, clock1_irq;
    int ser_irq, ms_kb_irq, fd_irq, me_irq, cs_irq, ecc_irq;
    uint8_t nvram_machine_id;
    uint16_t machine_id;
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
    uint8_t nvram_machine_id;
    uint16_t machine_id;
    uint32_t iounit_version;
    uint64_t max_mem;
    const char * const default_cpu_model;
};

struct sun4c_hwdef {
    target_phys_addr_t iommu_base, slavio_base;
    target_phys_addr_t intctl_base, counter_base, nvram_base, ms_kb_base;
    target_phys_addr_t serial_base, fd_base;
    target_phys_addr_t idreg_base, dma_base, esp_base, le_base;
    target_phys_addr_t tcx_base, aux1_base;
    long vram_size, nvram_size;
    // IRQ numbers are not PIL ones, but master interrupt controller
    // register bit numbers
    int esp_irq, le_irq, clock_irq, clock1_irq;
    int ser_irq, ms_kb_irq, fd_irq, me_irq;
    uint8_t nvram_machine_id;
    uint16_t machine_id;
    uint32_t iommu_version;
    uint32_t intbit_to_level[32];
    uint64_t max_mem;
    const char * const default_cpu_model;
};

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
void DMA_init (int high_page_enable) {}
void DMA_register_channel (int nchan,
                           DMA_transfer_handler transfer_handler,
                           void *opaque)
{
}

static int fw_cfg_boot_set(void *opaque, const char *boot_device)
{
    fw_cfg_add_i16(opaque, FW_CFG_BOOT_DEVICE, boot_device[0]);
    return 0;
}

static void nvram_init(m48t59_t *nvram, uint8_t *macaddr, const char *cmdline,
                       const char *boot_devices, ram_addr_t RAM_size,
                       uint32_t kernel_size,
                       int width, int height, int depth,
                       int nvram_machine_id, const char *arch)
{
    unsigned int i;
    uint32_t start, end;
    uint8_t image[0x1ff0];
    struct OpenBIOS_nvpart_v1 *part_header;

    memset(image, '\0', sizeof(image));

    start = 0;

    // OpenBIOS nvram variables
    // Variable partition
    part_header = (struct OpenBIOS_nvpart_v1 *)&image[start];
    part_header->signature = OPENBIOS_PART_SYSTEM;
    pstrcpy(part_header->name, sizeof(part_header->name), "system");

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
    pstrcpy(part_header->name, sizeof(part_header->name), "free");

    end = 0x1fd0;
    OpenBIOS_finish_partition(part_header, end - start);

    Sun_init_header((struct Sun_nvram *)&image[0x1fd8], macaddr,
                    nvram_machine_id);

    for (i = 0; i < sizeof(image); i++)
        m48t59_write(nvram, i, image[i]);
}

static void *slavio_intctl;

void pic_info(Monitor *mon)
{
    if (slavio_intctl)
        slavio_pic_info(mon, slavio_intctl);
}

void irq_info(Monitor *mon)
{
    if (slavio_intctl)
        slavio_irq_info(mon, slavio_intctl);
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
                if (old_interrupt != env->interrupt_index) {
                    DPRINTF("Set CPU IRQ %d\n", i);
                    cpu_interrupt(env, CPU_INTERRUPT_HARD);
                }
                break;
            }
        }
    } else if (!env->pil_in && (env->interrupt_index & ~15) == TT_EXTINT) {
        DPRINTF("Reset CPU IRQ %d\n", env->interrupt_index & 15);
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

static void cpu_halt_signal(void *opaque, int irq, int level)
{
    if (level && cpu_single_env)
        cpu_interrupt(cpu_single_env, CPU_INTERRUPT_HALT);
}

static unsigned long sun4m_load_kernel(const char *kernel_filename,
                                       const char *initrd_filename,
                                       ram_addr_t RAM_size)
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
            kernel_size = load_aout(kernel_filename, KERNEL_LOAD_ADDR,
                                    RAM_size - KERNEL_LOAD_ADDR);
        if (kernel_size < 0)
            kernel_size = load_image_targphys(kernel_filename,
                                              KERNEL_LOAD_ADDR,
                                              RAM_size - KERNEL_LOAD_ADDR);
        if (kernel_size < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n",
                    kernel_filename);
            exit(1);
        }

        /* load initrd */
        initrd_size = 0;
        if (initrd_filename) {
            initrd_size = load_image_targphys(initrd_filename,
                                              INITRD_LOAD_ADDR,
                                              RAM_size - INITRD_LOAD_ADDR);
            if (initrd_size < 0) {
                fprintf(stderr, "qemu: could not load initial ram disk '%s'\n",
                        initrd_filename);
                exit(1);
            }
        }
        if (initrd_size > 0) {
            for (i = 0; i < 64 * TARGET_PAGE_SIZE; i += TARGET_PAGE_SIZE) {
                if (ldl_phys(KERNEL_LOAD_ADDR + i) == 0x48647253) { // HdrS
                    stl_phys(KERNEL_LOAD_ADDR + i + 16, INITRD_LOAD_ADDR);
                    stl_phys(KERNEL_LOAD_ADDR + i + 20, initrd_size);
                    break;
                }
            }
        }
    }
    return kernel_size;
}

static void lance_init(NICInfo *nd, target_phys_addr_t leaddr,
                       void *dma_opaque, qemu_irq irq, qemu_irq *reset)
{
    DeviceState *dev;
    SysBusDevice *s;

    qemu_check_nic_model(&nd_table[0], "lance");

    dev = qdev_create(NULL, "lance");
    qdev_set_netdev(dev, nd);
    qdev_set_prop_ptr(dev, "dma", dma_opaque);
    qdev_init(dev);
    s = sysbus_from_qdev(dev);
    sysbus_mmio_map(s, 0, leaddr);
    sysbus_connect_irq(s, 0, irq);
    *reset = qdev_get_irq_sink(dev, 0);
}

static void sun4m_hw_init(const struct sun4m_hwdef *hwdef, ram_addr_t RAM_size,
                          const char *boot_device,
                          const char *kernel_filename,
                          const char *kernel_cmdline,
                          const char *initrd_filename, const char *cpu_model)

{
    CPUState *env, *envs[MAX_CPUS];
    unsigned int i;
    void *iommu, *espdma, *ledma, *nvram;
    qemu_irq *cpu_irqs[MAX_CPUS], *slavio_irq, *slavio_cpu_irq,
        *espdma_irq, *ledma_irq;
    qemu_irq *esp_reset, *le_reset;
    qemu_irq *fdc_tc;
    qemu_irq *cpu_halt;
    ram_addr_t ram_offset, prom_offset, idreg_offset;
    unsigned long kernel_size;
    int ret;
    char buf[1024];
    BlockDriverState *fd[MAX_FD];
    int drive_index;
    void *fw_cfg;

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
        cpu_irqs[i] = qemu_allocate_irqs(cpu_set_irq, envs[i], MAX_PILS);
        env->prom_addr = hwdef->slavio_base;
    }

    for (i = smp_cpus; i < MAX_CPUS; i++)
        cpu_irqs[i] = qemu_allocate_irqs(dummy_cpu_set_irq, NULL, MAX_PILS);


    /* allocate RAM */
    if ((uint64_t)RAM_size > hwdef->max_mem) {
        fprintf(stderr,
                "qemu: Too much memory for this machine: %d, maximum %d\n",
                (unsigned int)(RAM_size / (1024 * 1024)),
                (unsigned int)(hwdef->max_mem / (1024 * 1024)));
        exit(1);
    }
    ram_offset = qemu_ram_alloc(RAM_size);
    cpu_register_physical_memory(0, RAM_size, ram_offset);

    /* load boot prom */
    prom_offset = qemu_ram_alloc(PROM_SIZE_MAX);
    cpu_register_physical_memory(hwdef->slavio_base,
                                 (PROM_SIZE_MAX + TARGET_PAGE_SIZE - 1) &
                                 TARGET_PAGE_MASK,
                                 prom_offset | IO_MEM_ROM);

    if (bios_name == NULL)
        bios_name = PROM_FILENAME;
    snprintf(buf, sizeof(buf), "%s/%s", bios_dir, bios_name);
    ret = load_elf(buf, hwdef->slavio_base - PROM_VADDR, NULL, NULL, NULL);
    if (ret < 0 || ret > PROM_SIZE_MAX)
        ret = load_image_targphys(buf, hwdef->slavio_base, PROM_SIZE_MAX);
    if (ret < 0 || ret > PROM_SIZE_MAX) {
        fprintf(stderr, "qemu: could not load prom '%s'\n",
                buf);
        exit(1);
    }

    /* set up devices */
    slavio_intctl = slavio_intctl_init(hwdef->intctl_base,
                                       hwdef->intctl_base + 0x10000ULL,
                                       &hwdef->intbit_to_level[0],
                                       &slavio_irq, &slavio_cpu_irq,
                                       cpu_irqs,
                                       hwdef->clock_irq);

    if (hwdef->idreg_base) {
        static const uint8_t idreg_data[] = { 0xfe, 0x81, 0x01, 0x03 };

        idreg_offset = qemu_ram_alloc(sizeof(idreg_data));
        cpu_register_physical_memory(hwdef->idreg_base, sizeof(idreg_data),
                                     idreg_offset | IO_MEM_ROM);
        cpu_physical_memory_write_rom(hwdef->idreg_base, idreg_data,
                                      sizeof(idreg_data));
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
    tcx_init(hwdef->tcx_base, hwdef->vram_size, graphic_width, graphic_height,
             graphic_depth);

    lance_init(&nd_table[0], hwdef->le_base, ledma, *ledma_irq, le_reset);

    nvram = m48t59_init(slavio_irq[0], hwdef->nvram_base, 0,
                        hwdef->nvram_size, 8);

    slavio_timer_init_all(hwdef->counter_base, slavio_irq[hwdef->clock1_irq],
                          slavio_cpu_irq, smp_cpus);

    slavio_serial_ms_kbd_init(hwdef->ms_kb_base, slavio_irq[hwdef->ms_kb_irq],
                              display_type == DT_NOGRAPHIC, ESCC_CLOCK, 1);
    // Slavio TTYA (base+4, Linux ttyS0) is the first Qemu serial device
    // Slavio TTYB (base+0, Linux ttyS1) is the second Qemu serial device
    escc_init(hwdef->serial_base, slavio_irq[hwdef->ser_irq], slavio_irq[hwdef->ser_irq],
              serial_hds[0], serial_hds[1], ESCC_CLOCK, 1);

    cpu_halt = qemu_allocate_irqs(cpu_halt_signal, NULL, 1);
    slavio_misc = slavio_misc_init(hwdef->slavio_base, hwdef->apc_base,
                                   hwdef->aux1_base, hwdef->aux2_base,
                                   slavio_irq[hwdef->me_irq], cpu_halt[0],
                                   &fdc_tc);

    if (hwdef->fd_base) {
        /* there is zero or one floppy drive */
        memset(fd, 0, sizeof(fd));
        drive_index = drive_get_index(IF_FLOPPY, 0, 0);
        if (drive_index != -1)
            fd[0] = drives_table[drive_index].bdrv;

        sun4m_fdctrl_init(slavio_irq[hwdef->fd_irq], hwdef->fd_base, fd,
                          fdc_tc);
    }

    if (drive_get_max_bus(IF_SCSI) > 0) {
        fprintf(stderr, "qemu: too many SCSI bus\n");
        exit(1);
    }

    esp_init(hwdef->esp_base, 2,
             espdma_memory_read, espdma_memory_write,
             espdma, *espdma_irq, esp_reset);

    if (hwdef->cs_base)
        cs_init(hwdef->cs_base, hwdef->cs_irq, slavio_intctl);

    kernel_size = sun4m_load_kernel(kernel_filename, initrd_filename,
                                    RAM_size);

    nvram_init(nvram, (uint8_t *)&nd_table[0].macaddr, kernel_cmdline,
               boot_device, RAM_size, kernel_size, graphic_width,
               graphic_height, graphic_depth, hwdef->nvram_machine_id,
               "Sun4m");

    if (hwdef->ecc_base)
        ecc_init(hwdef->ecc_base, slavio_irq[hwdef->ecc_irq],
                 hwdef->ecc_version);

    fw_cfg = fw_cfg_init(0, 0, CFG_ADDR, CFG_ADDR + 2);
    fw_cfg_add_i32(fw_cfg, FW_CFG_ID, 1);
    fw_cfg_add_i64(fw_cfg, FW_CFG_RAM_SIZE, (uint64_t)ram_size);
    fw_cfg_add_i16(fw_cfg, FW_CFG_MACHINE_ID, hwdef->machine_id);
    fw_cfg_add_i16(fw_cfg, FW_CFG_SUN4M_DEPTH, graphic_depth);
    fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_ADDR, KERNEL_LOAD_ADDR);
    fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_SIZE, kernel_size);
    if (kernel_cmdline) {
        fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_CMDLINE, CMDLINE_ADDR);
        pstrcpy_targphys(CMDLINE_ADDR, TARGET_PAGE_SIZE, kernel_cmdline);
    } else {
        fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_CMDLINE, 0);
    }
    fw_cfg_add_i32(fw_cfg, FW_CFG_INITRD_ADDR, INITRD_LOAD_ADDR);
    fw_cfg_add_i32(fw_cfg, FW_CFG_INITRD_SIZE, 0); // not used
    fw_cfg_add_i16(fw_cfg, FW_CFG_BOOT_DEVICE, boot_device[0]);
    qemu_register_boot_set(fw_cfg_boot_set, fw_cfg);
}

enum {
    ss2_id = 0,
    ss5_id = 32,
    vger_id,
    lx_id,
    ss4_id,
    scls_id,
    sbook_id,
    ss10_id = 64,
    ss20_id,
    ss600mp_id,
    ss1000_id = 96,
    ss2000_id,
};

static const struct sun4m_hwdef sun4m_hwdefs[] = {
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
        .apc_base     = 0x6a000000,
        .aux1_base    = 0x71900000,
        .aux2_base    = 0x71910000,
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
        .nvram_machine_id = 0x80,
        .machine_id = ss5_id,
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
        .apc_base     = 0xefa000000ULL, // XXX should not exist
        .aux1_base    = 0xff1800000ULL,
        .aux2_base    = 0xff1a01000ULL,
        .ecc_base     = 0xf00000000ULL,
        .ecc_version  = 0x10000000, // version 0, implementation 1
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
        .ecc_irq = 28,
        .nvram_machine_id = 0x72,
        .machine_id = ss10_id,
        .iommu_version = 0x03000000,
        .intbit_to_level = {
            2, 3, 5, 7, 9, 11, 0, 14,   3, 5, 7, 9, 11, 13, 12, 12,
            6, 0, 4, 10, 8, 0, 11, 0,   0, 0, 0, 0, 15, 0, 15, 0,
        },
        .max_mem = 0xf00000000ULL,
        .default_cpu_model = "TI SuperSparc II",
    },
    /* SS-600MP */
    {
        .iommu_base   = 0xfe0000000ULL,
        .tcx_base     = 0xe20000000ULL,
        .slavio_base  = 0xff0000000ULL,
        .ms_kb_base   = 0xff1000000ULL,
        .serial_base  = 0xff1100000ULL,
        .nvram_base   = 0xff1200000ULL,
        .counter_base = 0xff1300000ULL,
        .intctl_base  = 0xff1400000ULL,
        .dma_base     = 0xef0081000ULL,
        .esp_base     = 0xef0080000ULL,
        .le_base      = 0xef0060000ULL,
        .apc_base     = 0xefa000000ULL, // XXX should not exist
        .aux1_base    = 0xff1800000ULL,
        .aux2_base    = 0xff1a01000ULL, // XXX should not exist
        .ecc_base     = 0xf00000000ULL,
        .ecc_version  = 0x00000000, // version 0, implementation 0
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
        .ecc_irq = 28,
        .nvram_machine_id = 0x71,
        .machine_id = ss600mp_id,
        .iommu_version = 0x01000000,
        .intbit_to_level = {
            2, 3, 5, 7, 9, 11, 0, 14,   3, 5, 7, 9, 11, 13, 12, 12,
            6, 0, 4, 10, 8, 0, 11, 0,   0, 0, 0, 0, 15, 0, 15, 0,
        },
        .max_mem = 0xf00000000ULL,
        .default_cpu_model = "TI SuperSparc II",
    },
    /* SS-20 */
    {
        .iommu_base   = 0xfe0000000ULL,
        .tcx_base     = 0xe20000000ULL,
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
        .apc_base     = 0xefa000000ULL, // XXX should not exist
        .aux1_base    = 0xff1800000ULL,
        .aux2_base    = 0xff1a01000ULL,
        .ecc_base     = 0xf00000000ULL,
        .ecc_version  = 0x20000000, // version 0, implementation 2
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
        .ecc_irq = 28,
        .nvram_machine_id = 0x72,
        .machine_id = ss20_id,
        .iommu_version = 0x13000000,
        .intbit_to_level = {
            2, 3, 5, 7, 9, 11, 0, 14,   3, 5, 7, 9, 11, 13, 12, 12,
            6, 0, 4, 10, 8, 0, 11, 0,   0, 0, 0, 0, 15, 0, 15, 0,
        },
        .max_mem = 0xf00000000ULL,
        .default_cpu_model = "TI SuperSparc II",
    },
    /* Voyager */
    {
        .iommu_base   = 0x10000000,
        .tcx_base     = 0x50000000,
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
        .apc_base     = 0x71300000, // pmc
        .aux1_base    = 0x71900000,
        .aux2_base    = 0x71910000,
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
        .nvram_machine_id = 0x80,
        .machine_id = vger_id,
        .iommu_version = 0x05000000,
        .intbit_to_level = {
            2, 3, 5, 7, 9, 11, 0, 14,   3, 5, 7, 9, 11, 13, 12, 12,
            6, 0, 4, 10, 8, 0, 11, 0,   0, 0, 0, 0, 15, 0, 15, 0,
        },
        .max_mem = 0x10000000,
        .default_cpu_model = "Fujitsu MB86904",
    },
    /* LX */
    {
        .iommu_base   = 0x10000000,
        .tcx_base     = 0x50000000,
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
        .aux1_base    = 0x71900000,
        .aux2_base    = 0x71910000,
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
        .nvram_machine_id = 0x80,
        .machine_id = lx_id,
        .iommu_version = 0x04000000,
        .intbit_to_level = {
            2, 3, 5, 7, 9, 11, 0, 14,   3, 5, 7, 9, 11, 13, 12, 12,
            6, 0, 4, 10, 8, 0, 11, 0,   0, 0, 0, 0, 15, 0, 15, 0,
        },
        .max_mem = 0x10000000,
        .default_cpu_model = "TI MicroSparc I",
    },
    /* SS-4 */
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
        .apc_base     = 0x6a000000,
        .aux1_base    = 0x71900000,
        .aux2_base    = 0x71910000,
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
        .nvram_machine_id = 0x80,
        .machine_id = ss4_id,
        .iommu_version = 0x05000000,
        .intbit_to_level = {
            2, 3, 5, 7, 9, 11, 0, 14,   3, 5, 7, 9, 11, 13, 12, 12,
            6, 0, 4, 10, 8, 0, 11, 0,   0, 0, 0, 0, 15, 0, 15, 0,
        },
        .max_mem = 0x10000000,
        .default_cpu_model = "Fujitsu MB86904",
    },
    /* SPARCClassic */
    {
        .iommu_base   = 0x10000000,
        .tcx_base     = 0x50000000,
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
        .apc_base     = 0x6a000000,
        .aux1_base    = 0x71900000,
        .aux2_base    = 0x71910000,
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
        .nvram_machine_id = 0x80,
        .machine_id = scls_id,
        .iommu_version = 0x05000000,
        .intbit_to_level = {
            2, 3, 5, 7, 9, 11, 0, 14,   3, 5, 7, 9, 11, 13, 12, 12,
            6, 0, 4, 10, 8, 0, 11, 0,   0, 0, 0, 0, 15, 0, 15, 0,
        },
        .max_mem = 0x10000000,
        .default_cpu_model = "TI MicroSparc I",
    },
    /* SPARCbook */
    {
        .iommu_base   = 0x10000000,
        .tcx_base     = 0x50000000, // XXX
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
        .apc_base     = 0x6a000000,
        .aux1_base    = 0x71900000,
        .aux2_base    = 0x71910000,
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
        .nvram_machine_id = 0x80,
        .machine_id = sbook_id,
        .iommu_version = 0x05000000,
        .intbit_to_level = {
            2, 3, 5, 7, 9, 11, 0, 14,   3, 5, 7, 9, 11, 13, 12, 12,
            6, 0, 4, 10, 8, 0, 11, 0,   0, 0, 0, 0, 15, 0, 15, 0,
        },
        .max_mem = 0x10000000,
        .default_cpu_model = "TI MicroSparc I",
    },
};

/* SPARCstation 5 hardware initialisation */
static void ss5_init(ram_addr_t RAM_size,
                     const char *boot_device,
                     const char *kernel_filename, const char *kernel_cmdline,
                     const char *initrd_filename, const char *cpu_model)
{
    sun4m_hw_init(&sun4m_hwdefs[0], RAM_size, boot_device, kernel_filename,
                  kernel_cmdline, initrd_filename, cpu_model);
}

/* SPARCstation 10 hardware initialisation */
static void ss10_init(ram_addr_t RAM_size,
                      const char *boot_device,
                      const char *kernel_filename, const char *kernel_cmdline,
                      const char *initrd_filename, const char *cpu_model)
{
    sun4m_hw_init(&sun4m_hwdefs[1], RAM_size, boot_device, kernel_filename,
                  kernel_cmdline, initrd_filename, cpu_model);
}

/* SPARCserver 600MP hardware initialisation */
static void ss600mp_init(ram_addr_t RAM_size,
                         const char *boot_device,
                         const char *kernel_filename,
                         const char *kernel_cmdline,
                         const char *initrd_filename, const char *cpu_model)
{
    sun4m_hw_init(&sun4m_hwdefs[2], RAM_size, boot_device, kernel_filename,
                  kernel_cmdline, initrd_filename, cpu_model);
}

/* SPARCstation 20 hardware initialisation */
static void ss20_init(ram_addr_t RAM_size,
                      const char *boot_device,
                      const char *kernel_filename, const char *kernel_cmdline,
                      const char *initrd_filename, const char *cpu_model)
{
    sun4m_hw_init(&sun4m_hwdefs[3], RAM_size, boot_device, kernel_filename,
                  kernel_cmdline, initrd_filename, cpu_model);
}

/* SPARCstation Voyager hardware initialisation */
static void vger_init(ram_addr_t RAM_size,
                      const char *boot_device,
                      const char *kernel_filename, const char *kernel_cmdline,
                      const char *initrd_filename, const char *cpu_model)
{
    sun4m_hw_init(&sun4m_hwdefs[4], RAM_size, boot_device, kernel_filename,
                  kernel_cmdline, initrd_filename, cpu_model);
}

/* SPARCstation LX hardware initialisation */
static void ss_lx_init(ram_addr_t RAM_size,
                       const char *boot_device,
                       const char *kernel_filename, const char *kernel_cmdline,
                       const char *initrd_filename, const char *cpu_model)
{
    sun4m_hw_init(&sun4m_hwdefs[5], RAM_size, boot_device, kernel_filename,
                  kernel_cmdline, initrd_filename, cpu_model);
}

/* SPARCstation 4 hardware initialisation */
static void ss4_init(ram_addr_t RAM_size,
                     const char *boot_device,
                     const char *kernel_filename, const char *kernel_cmdline,
                     const char *initrd_filename, const char *cpu_model)
{
    sun4m_hw_init(&sun4m_hwdefs[6], RAM_size, boot_device, kernel_filename,
                  kernel_cmdline, initrd_filename, cpu_model);
}

/* SPARCClassic hardware initialisation */
static void scls_init(ram_addr_t RAM_size,
                      const char *boot_device,
                      const char *kernel_filename, const char *kernel_cmdline,
                      const char *initrd_filename, const char *cpu_model)
{
    sun4m_hw_init(&sun4m_hwdefs[7], RAM_size, boot_device, kernel_filename,
                  kernel_cmdline, initrd_filename, cpu_model);
}

/* SPARCbook hardware initialisation */
static void sbook_init(ram_addr_t RAM_size,
                       const char *boot_device,
                       const char *kernel_filename, const char *kernel_cmdline,
                       const char *initrd_filename, const char *cpu_model)
{
    sun4m_hw_init(&sun4m_hwdefs[8], RAM_size, boot_device, kernel_filename,
                  kernel_cmdline, initrd_filename, cpu_model);
}

static QEMUMachine ss5_machine = {
    .name = "SS-5",
    .desc = "Sun4m platform, SPARCstation 5",
    .init = ss5_init,
    .use_scsi = 1,
    .is_default = 1,
};

static QEMUMachine ss10_machine = {
    .name = "SS-10",
    .desc = "Sun4m platform, SPARCstation 10",
    .init = ss10_init,
    .use_scsi = 1,
    .max_cpus = 4,
};

static QEMUMachine ss600mp_machine = {
    .name = "SS-600MP",
    .desc = "Sun4m platform, SPARCserver 600MP",
    .init = ss600mp_init,
    .use_scsi = 1,
    .max_cpus = 4,
};

static QEMUMachine ss20_machine = {
    .name = "SS-20",
    .desc = "Sun4m platform, SPARCstation 20",
    .init = ss20_init,
    .use_scsi = 1,
    .max_cpus = 4,
};

static QEMUMachine voyager_machine = {
    .name = "Voyager",
    .desc = "Sun4m platform, SPARCstation Voyager",
    .init = vger_init,
    .use_scsi = 1,
};

static QEMUMachine ss_lx_machine = {
    .name = "LX",
    .desc = "Sun4m platform, SPARCstation LX",
    .init = ss_lx_init,
    .use_scsi = 1,
};

static QEMUMachine ss4_machine = {
    .name = "SS-4",
    .desc = "Sun4m platform, SPARCstation 4",
    .init = ss4_init,
    .use_scsi = 1,
};

static QEMUMachine scls_machine = {
    .name = "SPARCClassic",
    .desc = "Sun4m platform, SPARCClassic",
    .init = scls_init,
    .use_scsi = 1,
};

static QEMUMachine sbook_machine = {
    .name = "SPARCbook",
    .desc = "Sun4m platform, SPARCbook",
    .init = sbook_init,
    .use_scsi = 1,
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
        .nvram_machine_id = 0x80,
        .machine_id = ss1000_id,
        .iounit_version = 0x03000000,
        .max_mem = 0xf00000000ULL,
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
        .nvram_machine_id = 0x80,
        .machine_id = ss2000_id,
        .iounit_version = 0x03000000,
        .max_mem = 0xf00000000ULL,
        .default_cpu_model = "TI SuperSparc II",
    },
};

static void sun4d_hw_init(const struct sun4d_hwdef *hwdef, ram_addr_t RAM_size,
                          const char *boot_device,
                          const char *kernel_filename,
                          const char *kernel_cmdline,
                          const char *initrd_filename, const char *cpu_model)
{
    CPUState *env, *envs[MAX_CPUS];
    unsigned int i;
    void *iounits[MAX_IOUNITS], *espdma, *ledma, *nvram, *sbi;
    qemu_irq *cpu_irqs[MAX_CPUS], *sbi_irq, *sbi_cpu_irq,
        *espdma_irq, *ledma_irq;
    qemu_irq *esp_reset, *le_reset;
    ram_addr_t ram_offset, prom_offset;
    unsigned long kernel_size;
    int ret;
    char buf[1024];
    void *fw_cfg;

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
        cpu_irqs[i] = qemu_allocate_irqs(cpu_set_irq, envs[i], MAX_PILS);
        env->prom_addr = hwdef->slavio_base;
    }

    for (i = smp_cpus; i < MAX_CPUS; i++)
        cpu_irqs[i] = qemu_allocate_irqs(dummy_cpu_set_irq, NULL, MAX_PILS);

    /* allocate RAM */
    if ((uint64_t)RAM_size > hwdef->max_mem) {
        fprintf(stderr,
                "qemu: Too much memory for this machine: %d, maximum %d\n",
                (unsigned int)(RAM_size / (1024 * 1024)),
                (unsigned int)(hwdef->max_mem / (1024 * 1024)));
        exit(1);
    }
    ram_offset = qemu_ram_alloc(RAM_size);
    cpu_register_physical_memory(0, RAM_size, ram_offset);

    /* load boot prom */
    prom_offset = qemu_ram_alloc(PROM_SIZE_MAX);
    cpu_register_physical_memory(hwdef->slavio_base,
                                 (PROM_SIZE_MAX + TARGET_PAGE_SIZE - 1) &
                                 TARGET_PAGE_MASK,
                                 prom_offset | IO_MEM_ROM);

    if (bios_name == NULL)
        bios_name = PROM_FILENAME;
    snprintf(buf, sizeof(buf), "%s/%s", bios_dir, bios_name);
    ret = load_elf(buf, hwdef->slavio_base - PROM_VADDR, NULL, NULL, NULL);
    if (ret < 0 || ret > PROM_SIZE_MAX)
        ret = load_image_targphys(buf, hwdef->slavio_base, PROM_SIZE_MAX);
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
    tcx_init(hwdef->tcx_base, hwdef->vram_size, graphic_width, graphic_height,
             graphic_depth);

    lance_init(&nd_table[0], hwdef->le_base, ledma, *ledma_irq, le_reset);

    nvram = m48t59_init(sbi_irq[0], hwdef->nvram_base, 0,
                        hwdef->nvram_size, 8);

    slavio_timer_init_all(hwdef->counter_base, sbi_irq[hwdef->clock1_irq],
                          sbi_cpu_irq, smp_cpus);

    slavio_serial_ms_kbd_init(hwdef->ms_kb_base, sbi_irq[hwdef->ms_kb_irq],
                              display_type == DT_NOGRAPHIC, ESCC_CLOCK, 1);
    // Slavio TTYA (base+4, Linux ttyS0) is the first Qemu serial device
    // Slavio TTYB (base+0, Linux ttyS1) is the second Qemu serial device
    escc_init(hwdef->serial_base, sbi_irq[hwdef->ser_irq], sbi_irq[hwdef->ser_irq],
              serial_hds[0], serial_hds[1], ESCC_CLOCK, 1);

    if (drive_get_max_bus(IF_SCSI) > 0) {
        fprintf(stderr, "qemu: too many SCSI bus\n");
        exit(1);
    }

    esp_init(hwdef->esp_base, 2,
             espdma_memory_read, espdma_memory_write,
             espdma, *espdma_irq, esp_reset);

    kernel_size = sun4m_load_kernel(kernel_filename, initrd_filename,
                                    RAM_size);

    nvram_init(nvram, (uint8_t *)&nd_table[0].macaddr, kernel_cmdline,
               boot_device, RAM_size, kernel_size, graphic_width,
               graphic_height, graphic_depth, hwdef->nvram_machine_id,
               "Sun4d");

    fw_cfg = fw_cfg_init(0, 0, CFG_ADDR, CFG_ADDR + 2);
    fw_cfg_add_i32(fw_cfg, FW_CFG_ID, 1);
    fw_cfg_add_i64(fw_cfg, FW_CFG_RAM_SIZE, (uint64_t)ram_size);
    fw_cfg_add_i16(fw_cfg, FW_CFG_MACHINE_ID, hwdef->machine_id);
    fw_cfg_add_i16(fw_cfg, FW_CFG_SUN4M_DEPTH, graphic_depth);
    fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_ADDR, KERNEL_LOAD_ADDR);
    fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_SIZE, kernel_size);
    if (kernel_cmdline) {
        fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_CMDLINE, CMDLINE_ADDR);
        pstrcpy_targphys(CMDLINE_ADDR, TARGET_PAGE_SIZE, kernel_cmdline);
    } else {
        fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_CMDLINE, 0);
    }
    fw_cfg_add_i32(fw_cfg, FW_CFG_INITRD_ADDR, INITRD_LOAD_ADDR);
    fw_cfg_add_i32(fw_cfg, FW_CFG_INITRD_SIZE, 0); // not used
    fw_cfg_add_i16(fw_cfg, FW_CFG_BOOT_DEVICE, boot_device[0]);
    qemu_register_boot_set(fw_cfg_boot_set, fw_cfg);
}

/* SPARCserver 1000 hardware initialisation */
static void ss1000_init(ram_addr_t RAM_size,
                        const char *boot_device,
                        const char *kernel_filename, const char *kernel_cmdline,
                        const char *initrd_filename, const char *cpu_model)
{
    sun4d_hw_init(&sun4d_hwdefs[0], RAM_size, boot_device, kernel_filename,
                  kernel_cmdline, initrd_filename, cpu_model);
}

/* SPARCcenter 2000 hardware initialisation */
static void ss2000_init(ram_addr_t RAM_size,
                        const char *boot_device,
                        const char *kernel_filename, const char *kernel_cmdline,
                        const char *initrd_filename, const char *cpu_model)
{
    sun4d_hw_init(&sun4d_hwdefs[1], RAM_size, boot_device, kernel_filename,
                  kernel_cmdline, initrd_filename, cpu_model);
}

static QEMUMachine ss1000_machine = {
    .name = "SS-1000",
    .desc = "Sun4d platform, SPARCserver 1000",
    .init = ss1000_init,
    .use_scsi = 1,
    .max_cpus = 8,
};

static QEMUMachine ss2000_machine = {
    .name = "SS-2000",
    .desc = "Sun4d platform, SPARCcenter 2000",
    .init = ss2000_init,
    .use_scsi = 1,
    .max_cpus = 20,
};

static const struct sun4c_hwdef sun4c_hwdefs[] = {
    /* SS-2 */
    {
        .iommu_base   = 0xf8000000,
        .tcx_base     = 0xfe000000,
        .slavio_base  = 0xf6000000,
        .intctl_base  = 0xf5000000,
        .counter_base = 0xf3000000,
        .ms_kb_base   = 0xf0000000,
        .serial_base  = 0xf1000000,
        .nvram_base   = 0xf2000000,
        .fd_base      = 0xf7200000,
        .dma_base     = 0xf8400000,
        .esp_base     = 0xf8800000,
        .le_base      = 0xf8c00000,
        .aux1_base    = 0xf7400003,
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
        .nvram_machine_id = 0x55,
        .machine_id = ss2_id,
        .max_mem = 0x10000000,
        .default_cpu_model = "Cypress CY7C601",
    },
};

static void sun4c_hw_init(const struct sun4c_hwdef *hwdef, ram_addr_t RAM_size,
                          const char *boot_device,
                          const char *kernel_filename,
                          const char *kernel_cmdline,
                          const char *initrd_filename, const char *cpu_model)
{
    CPUState *env;
    void *iommu, *espdma, *ledma, *nvram;
    qemu_irq *cpu_irqs, *slavio_irq, *espdma_irq, *ledma_irq;
    qemu_irq *esp_reset, *le_reset;
    qemu_irq *fdc_tc;
    ram_addr_t ram_offset, prom_offset;
    unsigned long kernel_size;
    int ret;
    char buf[1024];
    BlockDriverState *fd[MAX_FD];
    int drive_index;
    void *fw_cfg;

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
    cpu_irqs = qemu_allocate_irqs(cpu_set_irq, env, MAX_PILS);
    env->prom_addr = hwdef->slavio_base;

    /* allocate RAM */
    if ((uint64_t)RAM_size > hwdef->max_mem) {
        fprintf(stderr,
                "qemu: Too much memory for this machine: %d, maximum %d\n",
                (unsigned int)(RAM_size / (1024 * 1024)),
                (unsigned int)(hwdef->max_mem / (1024 * 1024)));
        exit(1);
    }
    ram_offset = qemu_ram_alloc(RAM_size);
    cpu_register_physical_memory(0, RAM_size, ram_offset);

    /* load boot prom */
    prom_offset = qemu_ram_alloc(PROM_SIZE_MAX);
    cpu_register_physical_memory(hwdef->slavio_base,
                                 (PROM_SIZE_MAX + TARGET_PAGE_SIZE - 1) &
                                 TARGET_PAGE_MASK,
                                 prom_offset | IO_MEM_ROM);

    if (bios_name == NULL)
        bios_name = PROM_FILENAME;
    snprintf(buf, sizeof(buf), "%s/%s", bios_dir, bios_name);
    ret = load_elf(buf, hwdef->slavio_base - PROM_VADDR, NULL, NULL, NULL);
    if (ret < 0 || ret > PROM_SIZE_MAX)
        ret = load_image_targphys(buf, hwdef->slavio_base, PROM_SIZE_MAX);
    if (ret < 0 || ret > PROM_SIZE_MAX) {
        fprintf(stderr, "qemu: could not load prom '%s'\n",
                buf);
        exit(1);
    }

    /* set up devices */
    slavio_intctl = sun4c_intctl_init(hwdef->intctl_base,
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
    tcx_init(hwdef->tcx_base, hwdef->vram_size, graphic_width, graphic_height,
             graphic_depth);

    lance_init(&nd_table[0], hwdef->le_base, ledma, *ledma_irq, le_reset);

    nvram = m48t59_init(slavio_irq[0], hwdef->nvram_base, 0,
                        hwdef->nvram_size, 2);

    slavio_serial_ms_kbd_init(hwdef->ms_kb_base, slavio_irq[hwdef->ms_kb_irq],
                              display_type == DT_NOGRAPHIC, ESCC_CLOCK, 1);
    // Slavio TTYA (base+4, Linux ttyS0) is the first Qemu serial device
    // Slavio TTYB (base+0, Linux ttyS1) is the second Qemu serial device
    escc_init(hwdef->serial_base, slavio_irq[hwdef->ser_irq],
              slavio_irq[hwdef->ser_irq], serial_hds[0], serial_hds[1],
              ESCC_CLOCK, 1);

    slavio_misc = slavio_misc_init(0, 0, hwdef->aux1_base, 0,
                                   slavio_irq[hwdef->me_irq], NULL, &fdc_tc);

    if (hwdef->fd_base != (target_phys_addr_t)-1) {
        /* there is zero or one floppy drive */
        memset(fd, 0, sizeof(fd));
        drive_index = drive_get_index(IF_FLOPPY, 0, 0);
        if (drive_index != -1)
            fd[0] = drives_table[drive_index].bdrv;

        sun4m_fdctrl_init(slavio_irq[hwdef->fd_irq], hwdef->fd_base, fd,
                          fdc_tc);
    }

    if (drive_get_max_bus(IF_SCSI) > 0) {
        fprintf(stderr, "qemu: too many SCSI bus\n");
        exit(1);
    }

    esp_init(hwdef->esp_base, 2,
             espdma_memory_read, espdma_memory_write,
             espdma, *espdma_irq, esp_reset);

    kernel_size = sun4m_load_kernel(kernel_filename, initrd_filename,
                                    RAM_size);

    nvram_init(nvram, (uint8_t *)&nd_table[0].macaddr, kernel_cmdline,
               boot_device, RAM_size, kernel_size, graphic_width,
               graphic_height, graphic_depth, hwdef->nvram_machine_id,
               "Sun4c");

    fw_cfg = fw_cfg_init(0, 0, CFG_ADDR, CFG_ADDR + 2);
    fw_cfg_add_i32(fw_cfg, FW_CFG_ID, 1);
    fw_cfg_add_i64(fw_cfg, FW_CFG_RAM_SIZE, (uint64_t)ram_size);
    fw_cfg_add_i16(fw_cfg, FW_CFG_MACHINE_ID, hwdef->machine_id);
    fw_cfg_add_i16(fw_cfg, FW_CFG_SUN4M_DEPTH, graphic_depth);
    fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_ADDR, KERNEL_LOAD_ADDR);
    fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_SIZE, kernel_size);
    if (kernel_cmdline) {
        fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_CMDLINE, CMDLINE_ADDR);
        pstrcpy_targphys(CMDLINE_ADDR, TARGET_PAGE_SIZE, kernel_cmdline);
    } else {
        fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_CMDLINE, 0);
    }
    fw_cfg_add_i32(fw_cfg, FW_CFG_INITRD_ADDR, INITRD_LOAD_ADDR);
    fw_cfg_add_i32(fw_cfg, FW_CFG_INITRD_SIZE, 0); // not used
    fw_cfg_add_i16(fw_cfg, FW_CFG_BOOT_DEVICE, boot_device[0]);
    qemu_register_boot_set(fw_cfg_boot_set, fw_cfg);
}

/* SPARCstation 2 hardware initialisation */
static void ss2_init(ram_addr_t RAM_size,
                     const char *boot_device,
                     const char *kernel_filename, const char *kernel_cmdline,
                     const char *initrd_filename, const char *cpu_model)
{
    sun4c_hw_init(&sun4c_hwdefs[0], RAM_size, boot_device, kernel_filename,
                  kernel_cmdline, initrd_filename, cpu_model);
}

static QEMUMachine ss2_machine = {
    .name = "SS-2",
    .desc = "Sun4c platform, SPARCstation 2",
    .init = ss2_init,
    .use_scsi = 1,
};

static void ss2_machine_init(void)
{
    qemu_register_machine(&ss5_machine);
    qemu_register_machine(&ss10_machine);
    qemu_register_machine(&ss600mp_machine);
    qemu_register_machine(&ss20_machine);
    qemu_register_machine(&voyager_machine);
    qemu_register_machine(&ss_lx_machine);
    qemu_register_machine(&ss4_machine);
    qemu_register_machine(&scls_machine);
    qemu_register_machine(&sbook_machine);
    qemu_register_machine(&ss1000_machine);
    qemu_register_machine(&ss2000_machine);
    qemu_register_machine(&ss2_machine);
}

machine_init(ss2_machine_init);
