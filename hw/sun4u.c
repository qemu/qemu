/*
 * QEMU Sun4u/Sun4v System Emulator
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
#include "hw.h"
#include "pci.h"
#include "pc.h"
#include "nvram.h"
#include "fdc.h"
#include "net.h"
#include "qemu-timer.h"
#include "sysemu.h"
#include "boards.h"
#include "firmware_abi.h"
#include "fw_cfg.h"

#define KERNEL_LOAD_ADDR     0x00404000
#define CMDLINE_ADDR         0x003ff000
#define INITRD_LOAD_ADDR     0x00300000
#define PROM_SIZE_MAX        (4 * 1024 * 1024)
#define PROM_ADDR            0x1fff0000000ULL
#define PROM_VADDR           0x000ffd00000ULL
#define APB_SPECIAL_BASE     0x1fe00000000ULL
#define APB_MEM_BASE         0x1ff00000000ULL
#define VGA_BASE             (APB_MEM_BASE + 0x400000ULL)
#define PROM_FILENAME        "openbios-sparc64"
#define NVRAM_SIZE           0x2000
#define MAX_IDE_BUS          2
#define BIOS_CFG_IOPORT      0x510

struct hwdef {
    const char * const default_cpu_model;
    uint16_t machine_id;
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
void DMA_run (void) {}
void DMA_init (int high_page_enable) {}
void DMA_register_channel (int nchan,
                           DMA_transfer_handler transfer_handler,
                           void *opaque)
{
}

static int nvram_boot_set(void *opaque, const char *boot_device)
{
    unsigned int i;
    uint8_t image[sizeof(ohwcfg_v3_t)];
    ohwcfg_v3_t *header = (ohwcfg_v3_t *)&image;
    m48t59_t *nvram = (m48t59_t *)opaque;

    for (i = 0; i < sizeof(image); i++)
        image[i] = m48t59_read(nvram, i) & 0xff;

    pstrcpy((char *)header->boot_devices, sizeof(header->boot_devices),
            boot_device);
    header->nboot_devices = strlen(boot_device) & 0xff;
    header->crc = cpu_to_be16(OHW_compute_crc(header, 0x00, 0xF8));

    for (i = 0; i < sizeof(image); i++)
        m48t59_write(nvram, i, image[i]);

    return 0;
}

extern int nographic;

static int sun4u_NVRAM_set_params (m48t59_t *nvram, uint16_t NVRAM_size,
                                   const char *arch,
                                   ram_addr_t RAM_size,
                                   const char *boot_devices,
                                   uint32_t kernel_image, uint32_t kernel_size,
                                   const char *cmdline,
                                   uint32_t initrd_image, uint32_t initrd_size,
                                   uint32_t NVRAM_image,
                                   int width, int height, int depth,
                                   const uint8_t *macaddr)
{
    unsigned int i;
    uint32_t start, end;
    uint8_t image[0x1ff0];
    ohwcfg_v3_t *header = (ohwcfg_v3_t *)&image;
    struct sparc_arch_cfg *sparc_header;
    struct OpenBIOS_nvpart_v1 *part_header;

    memset(image, '\0', sizeof(image));

    // Try to match PPC NVRAM
    pstrcpy((char *)header->struct_ident, sizeof(header->struct_ident),
            "QEMU_BIOS");
    header->struct_version = cpu_to_be32(3); /* structure v3 */

    header->nvram_size = cpu_to_be16(NVRAM_size);
    header->nvram_arch_ptr = cpu_to_be16(sizeof(ohwcfg_v3_t));
    header->nvram_arch_size = cpu_to_be16(sizeof(struct sparc_arch_cfg));
    pstrcpy((char *)header->arch, sizeof(header->arch), arch);
    header->nb_cpus = smp_cpus & 0xff;
    header->RAM0_base = 0;
    header->RAM0_size = cpu_to_be64((uint64_t)RAM_size);
    pstrcpy((char *)header->boot_devices, sizeof(header->boot_devices),
            boot_devices);
    header->nboot_devices = strlen(boot_devices) & 0xff;
    header->kernel_image = cpu_to_be64((uint64_t)kernel_image);
    header->kernel_size = cpu_to_be64((uint64_t)kernel_size);
    if (cmdline) {
        pstrcpy_targphys(CMDLINE_ADDR, TARGET_PAGE_SIZE, cmdline);
        header->cmdline = cpu_to_be64((uint64_t)CMDLINE_ADDR);
        header->cmdline_size = cpu_to_be64((uint64_t)strlen(cmdline));
    }
    header->initrd_image = cpu_to_be64((uint64_t)initrd_image);
    header->initrd_size = cpu_to_be64((uint64_t)initrd_size);
    header->NVRAM_image = cpu_to_be64((uint64_t)NVRAM_image);

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

    Sun_init_header((struct Sun_nvram *)&image[0x1fd8], macaddr, 0x80);

    for (i = 0; i < sizeof(image); i++)
        m48t59_write(nvram, i, image[i]);

    qemu_register_boot_set(nvram_boot_set, nvram);

    return 0;
}

void pic_info(void)
{
}

void irq_info(void)
{
}

void qemu_system_powerdown(void)
{
}

static void main_cpu_reset(void *opaque)
{
    CPUState *env = opaque;

    cpu_reset(env);
    ptimer_set_limit(env->tick, 0x7fffffffffffffffULL, 1);
    ptimer_run(env->tick, 0);
    ptimer_set_limit(env->stick, 0x7fffffffffffffffULL, 1);
    ptimer_run(env->stick, 0);
    ptimer_set_limit(env->hstick, 0x7fffffffffffffffULL, 1);
    ptimer_run(env->hstick, 0);
}

static void tick_irq(void *opaque)
{
    CPUState *env = opaque;

    cpu_interrupt(env, CPU_INTERRUPT_TIMER);
}

static void stick_irq(void *opaque)
{
    CPUState *env = opaque;

    cpu_interrupt(env, CPU_INTERRUPT_TIMER);
}

static void hstick_irq(void *opaque)
{
    CPUState *env = opaque;

    cpu_interrupt(env, CPU_INTERRUPT_TIMER);
}

static void dummy_cpu_set_irq(void *opaque, int irq, int level)
{
}

static const int ide_iobase[2] = { 0x1f0, 0x170 };
static const int ide_iobase2[2] = { 0x3f6, 0x376 };
static const int ide_irq[2] = { 14, 15 };

static const int serial_io[MAX_SERIAL_PORTS] = { 0x3f8, 0x2f8, 0x3e8, 0x2e8 };
static const int serial_irq[MAX_SERIAL_PORTS] = { 4, 3, 4, 3 };

static const int parallel_io[MAX_PARALLEL_PORTS] = { 0x378, 0x278, 0x3bc };
static const int parallel_irq[MAX_PARALLEL_PORTS] = { 7, 7, 7 };

static fdctrl_t *floppy_controller;

static void sun4uv_init(ram_addr_t RAM_size, int vga_ram_size,
                        const char *boot_devices, DisplayState *ds,
                        const char *kernel_filename, const char *kernel_cmdline,
                        const char *initrd_filename, const char *cpu_model,
                        const struct hwdef *hwdef)
{
    CPUState *env;
    char buf[1024];
    m48t59_t *nvram;
    int ret, linux_boot;
    unsigned int i;
    long prom_offset, initrd_size, kernel_size;
    PCIBus *pci_bus;
    QEMUBH *bh;
    qemu_irq *irq;
    int drive_index;
    BlockDriverState *hd[MAX_IDE_BUS * MAX_IDE_DEVS];
    BlockDriverState *fd[MAX_FD];
    void *fw_cfg;

    linux_boot = (kernel_filename != NULL);

    /* init CPUs */
    if (!cpu_model)
        cpu_model = hwdef->default_cpu_model;

    env = cpu_init(cpu_model);
    if (!env) {
        fprintf(stderr, "Unable to find Sparc CPU definition\n");
        exit(1);
    }
    bh = qemu_bh_new(tick_irq, env);
    env->tick = ptimer_init(bh);
    ptimer_set_period(env->tick, 1ULL);

    bh = qemu_bh_new(stick_irq, env);
    env->stick = ptimer_init(bh);
    ptimer_set_period(env->stick, 1ULL);

    bh = qemu_bh_new(hstick_irq, env);
    env->hstick = ptimer_init(bh);
    ptimer_set_period(env->hstick, 1ULL);
    qemu_register_reset(main_cpu_reset, env);
    main_cpu_reset(env);

    /* allocate RAM */
    cpu_register_physical_memory(0, RAM_size, 0);

    prom_offset = RAM_size + vga_ram_size;
    cpu_register_physical_memory(PROM_ADDR,
                                 (PROM_SIZE_MAX + TARGET_PAGE_SIZE) &
                                 TARGET_PAGE_MASK,
                                 prom_offset | IO_MEM_ROM);

    if (bios_name == NULL)
        bios_name = PROM_FILENAME;
    snprintf(buf, sizeof(buf), "%s/%s", bios_dir, bios_name);
    ret = load_elf(buf, PROM_ADDR - PROM_VADDR, NULL, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "qemu: could not load prom '%s'\n",
                buf);
        exit(1);
    }

    kernel_size = 0;
    initrd_size = 0;
    if (linux_boot) {
        /* XXX: put correct offset */
        kernel_size = load_elf(kernel_filename, 0, NULL, NULL, NULL);
        if (kernel_size < 0)
            kernel_size = load_aout(kernel_filename, KERNEL_LOAD_ADDR,
                                    ram_size - KERNEL_LOAD_ADDR);
        if (kernel_size < 0)
            kernel_size = load_image_targphys(kernel_filename,
                                              KERNEL_LOAD_ADDR,
                                              ram_size - KERNEL_LOAD_ADDR);
        if (kernel_size < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n",
                    kernel_filename);
            exit(1);
        }

        /* load initrd */
        if (initrd_filename) {
            initrd_size = load_image_targphys(initrd_filename,
                                              INITRD_LOAD_ADDR,
                                              ram_size - INITRD_LOAD_ADDR);
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
    pci_bus = pci_apb_init(APB_SPECIAL_BASE, APB_MEM_BASE, NULL);
    isa_mem_base = VGA_BASE;
    pci_cirrus_vga_init(pci_bus, ds, phys_ram_base + RAM_size, RAM_size,
                        vga_ram_size);

    for(i = 0; i < MAX_SERIAL_PORTS; i++) {
        if (serial_hds[i]) {
            serial_init(serial_io[i], NULL/*serial_irq[i]*/, 115200,
                        serial_hds[i]);
        }
    }

    for(i = 0; i < MAX_PARALLEL_PORTS; i++) {
        if (parallel_hds[i]) {
            parallel_init(parallel_io[i], NULL/*parallel_irq[i]*/,
                          parallel_hds[i]);
        }
    }

    for(i = 0; i < nb_nics; i++) {
        if (!nd_table[i].model)
            nd_table[i].model = "ne2k_pci";
        pci_nic_init(pci_bus, &nd_table[i], -1);
    }

    irq = qemu_allocate_irqs(dummy_cpu_set_irq, NULL, 32);
    if (drive_get_max_bus(IF_IDE) >= MAX_IDE_BUS) {
        fprintf(stderr, "qemu: too many IDE bus\n");
        exit(1);
    }
    for(i = 0; i < MAX_IDE_BUS * MAX_IDE_DEVS; i++) {
        drive_index = drive_get_index(IF_IDE, i / MAX_IDE_DEVS,
                                      i % MAX_IDE_DEVS);
       if (drive_index != -1)
           hd[i] = drives_table[drive_index].bdrv;
       else
           hd[i] = NULL;
    }

    // XXX pci_cmd646_ide_init(pci_bus, hd, 1);
    pci_piix3_ide_init(pci_bus, hd, -1, irq);
    /* FIXME: wire up interrupts.  */
    i8042_init(NULL/*1*/, NULL/*12*/, 0x60);
    for(i = 0; i < MAX_FD; i++) {
        drive_index = drive_get_index(IF_FLOPPY, 0, i);
       if (drive_index != -1)
           fd[i] = drives_table[drive_index].bdrv;
       else
           fd[i] = NULL;
    }
    floppy_controller = fdctrl_init(NULL/*6*/, 2, 0, 0x3f0, fd);
    nvram = m48t59_init(NULL/*8*/, 0, 0x0074, NVRAM_SIZE, 59);
    sun4u_NVRAM_set_params(nvram, NVRAM_SIZE, "Sun4u", RAM_size, boot_devices,
                           KERNEL_LOAD_ADDR, kernel_size,
                           kernel_cmdline,
                           INITRD_LOAD_ADDR, initrd_size,
                           /* XXX: need an option to load a NVRAM image */
                           0,
                           graphic_width, graphic_height, graphic_depth,
                           (uint8_t *)&nd_table[0].macaddr);

    fw_cfg = fw_cfg_init(BIOS_CFG_IOPORT, BIOS_CFG_IOPORT + 1, 0, 0);
    fw_cfg_add_i32(fw_cfg, FW_CFG_ID, 1);
    fw_cfg_add_i64(fw_cfg, FW_CFG_RAM_SIZE, (uint64_t)ram_size);
    fw_cfg_add_i16(fw_cfg, FW_CFG_MACHINE_ID, hwdef->machine_id);
}

enum {
    sun4u_id = 0,
    sun4v_id = 64,
};

static const struct hwdef hwdefs[] = {
    /* Sun4u generic PC-like machine */
    {
        .default_cpu_model = "TI UltraSparc II",
        .machine_id = sun4u_id,
    },
    /* Sun4v generic PC-like machine */
    {
        .default_cpu_model = "Sun UltraSparc T1",
        .machine_id = sun4v_id,
    },
};

/* Sun4u hardware initialisation */
static void sun4u_init(ram_addr_t RAM_size, int vga_ram_size,
                       const char *boot_devices, DisplayState *ds,
                       const char *kernel_filename, const char *kernel_cmdline,
                       const char *initrd_filename, const char *cpu_model)
{
    sun4uv_init(RAM_size, vga_ram_size, boot_devices, ds, kernel_filename,
                kernel_cmdline, initrd_filename, cpu_model, &hwdefs[0]);
}

/* Sun4v hardware initialisation */
static void sun4v_init(ram_addr_t RAM_size, int vga_ram_size,
                       const char *boot_devices, DisplayState *ds,
                       const char *kernel_filename, const char *kernel_cmdline,
                       const char *initrd_filename, const char *cpu_model)
{
    sun4uv_init(RAM_size, vga_ram_size, boot_devices, ds, kernel_filename,
                kernel_cmdline, initrd_filename, cpu_model, &hwdefs[1]);
}

QEMUMachine sun4u_machine = {
    .name = "sun4u",
    .desc = "Sun4u platform",
    .init = sun4u_init,
    .ram_require = PROM_SIZE_MAX + VGA_RAM_SIZE,
    .nodisk_ok = 1,
};

QEMUMachine sun4v_machine = {
    .name = "sun4v",
    .desc = "Sun4v platform",
    .init = sun4v_init,
    .ram_require = PROM_SIZE_MAX + VGA_RAM_SIZE,
    .nodisk_ok = 1,
};
