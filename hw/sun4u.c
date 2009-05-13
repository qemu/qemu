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

//#define DEBUG_IRQ

#ifdef DEBUG_IRQ
#define DPRINTF(fmt, args...)                           \
    do { printf("CPUIRQ: " fmt , ##args); } while (0)
#else
#define DPRINTF(fmt, args...)
#endif

#define KERNEL_LOAD_ADDR     0x00404000
#define CMDLINE_ADDR         0x003ff000
#define INITRD_LOAD_ADDR     0x00300000
#define PROM_SIZE_MAX        (4 * 1024 * 1024)
#define PROM_VADDR           0x000ffd00000ULL
#define APB_SPECIAL_BASE     0x1fe00000000ULL
#define APB_MEM_BASE         0x1ff00000000ULL
#define VGA_BASE             (APB_MEM_BASE + 0x400000ULL)
#define PROM_FILENAME        "openbios-sparc64"
#define NVRAM_SIZE           0x2000
#define MAX_IDE_BUS          2
#define BIOS_CFG_IOPORT      0x510

#define MAX_PILS 16

#define TICK_INT_DIS         0x8000000000000000ULL
#define TICK_MAX             0x7fffffffffffffffULL

struct hwdef {
    const char * const default_cpu_model;
    uint16_t machine_id;
    uint64_t prom_addr;
    uint64_t console_serial_base;
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

    Sun_init_header((struct Sun_nvram *)&image[0x1fd8], macaddr, 0x80);

    for (i = 0; i < sizeof(image); i++)
        m48t59_write(nvram, i, image[i]);

    return 0;
}

void pic_info(Monitor *mon)
{
}

void irq_info(Monitor *mon)
{
}

void cpu_check_irqs(CPUState *env)
{
    uint32_t pil = env->pil_in | (env->softint & ~SOFTINT_TIMER) |
        ((env->softint & SOFTINT_TIMER) << 14);

    if (pil && (env->interrupt_index == 0 ||
                (env->interrupt_index & ~15) == TT_EXTINT)) {
        unsigned int i;

        for (i = 15; i > 0; i--) {
            if (pil & (1 << i)) {
                int old_interrupt = env->interrupt_index;

                env->interrupt_index = TT_EXTINT | i;
                if (old_interrupt != env->interrupt_index) {
                    DPRINTF("Set CPU IRQ %d\n", i);
                    cpu_interrupt(env, CPU_INTERRUPT_HARD);
                }
                break;
            }
        }
    } else if (!pil && (env->interrupt_index & ~15) == TT_EXTINT) {
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

void qemu_system_powerdown(void)
{
}

typedef struct ResetData {
    CPUState *env;
    uint64_t reset_addr;
} ResetData;

static void main_cpu_reset(void *opaque)
{
    ResetData *s = (ResetData *)opaque;
    CPUState *env = s->env;

    cpu_reset(env);
    env->tick_cmpr = TICK_INT_DIS | 0;
    ptimer_set_limit(env->tick, TICK_MAX, 1);
    ptimer_run(env->tick, 1);
    env->stick_cmpr = TICK_INT_DIS | 0;
    ptimer_set_limit(env->stick, TICK_MAX, 1);
    ptimer_run(env->stick, 1);
    env->hstick_cmpr = TICK_INT_DIS | 0;
    ptimer_set_limit(env->hstick, TICK_MAX, 1);
    ptimer_run(env->hstick, 1);
    env->gregs[1] = 0; // Memory start
    env->gregs[2] = ram_size; // Memory size
    env->gregs[3] = 0; // Machine description XXX
    env->pc = s->reset_addr;
    env->npc = env->pc + 4;
}

static void tick_irq(void *opaque)
{
    CPUState *env = opaque;

    if (!(env->tick_cmpr & TICK_INT_DIS)) {
        env->softint |= SOFTINT_TIMER;
        cpu_interrupt(env, CPU_INTERRUPT_TIMER);
    }
}

static void stick_irq(void *opaque)
{
    CPUState *env = opaque;

    if (!(env->stick_cmpr & TICK_INT_DIS)) {
        env->softint |= SOFTINT_STIMER;
        cpu_interrupt(env, CPU_INTERRUPT_TIMER);
    }
}

static void hstick_irq(void *opaque)
{
    CPUState *env = opaque;

    if (!(env->hstick_cmpr & TICK_INT_DIS)) {
        cpu_interrupt(env, CPU_INTERRUPT_TIMER);
    }
}

void cpu_tick_set_count(void *opaque, uint64_t count)
{
    ptimer_set_count(opaque, -count);
}

uint64_t cpu_tick_get_count(void *opaque)
{
    return -ptimer_get_count(opaque);
}

void cpu_tick_set_limit(void *opaque, uint64_t limit)
{
    ptimer_set_limit(opaque, -limit, 0);
}

static const int ide_iobase[2] = { 0x1f0, 0x170 };
static const int ide_iobase2[2] = { 0x3f6, 0x376 };
static const int ide_irq[2] = { 14, 15 };

static const int serial_io[MAX_SERIAL_PORTS] = { 0x3f8, 0x2f8, 0x3e8, 0x2e8 };
static const int serial_irq[MAX_SERIAL_PORTS] = { 4, 3, 4, 3 };

static const int parallel_io[MAX_PARALLEL_PORTS] = { 0x378, 0x278, 0x3bc };
static const int parallel_irq[MAX_PARALLEL_PORTS] = { 7, 7, 7 };

static fdctrl_t *floppy_controller;

static void ebus_mmio_mapfunc(PCIDevice *pci_dev, int region_num,
                              uint32_t addr, uint32_t size, int type)
{
    DPRINTF("Mapping region %d registers at %08x\n", region_num, addr);
    switch (region_num) {
    case 0:
        isa_mmio_init(addr, 0x1000000);
        break;
    case 1:
        isa_mmio_init(addr, 0x800000);
        break;
    }
}

/* EBUS (Eight bit bus) bridge */
static void
pci_ebus_init(PCIBus *bus, int devfn)
{
    PCIDevice *s;

    s = pci_register_device(bus, "EBUS", sizeof(*s), devfn, NULL, NULL);
    pci_config_set_vendor_id(s->config, PCI_VENDOR_ID_SUN);
    pci_config_set_device_id(s->config, PCI_DEVICE_ID_SUN_EBUS);
    s->config[0x04] = 0x06; // command = bus master, pci mem
    s->config[0x05] = 0x00;
    s->config[0x06] = 0xa0; // status = fast back-to-back, 66MHz, no error
    s->config[0x07] = 0x03; // status = medium devsel
    s->config[0x08] = 0x01; // revision
    s->config[0x09] = 0x00; // programming i/f
    pci_config_set_class(s->config, PCI_CLASS_BRIDGE_OTHER);
    s->config[0x0D] = 0x0a; // latency_timer
    s->config[PCI_HEADER_TYPE] = PCI_HEADER_TYPE_NORMAL; // header_type

    pci_register_io_region(s, 0, 0x1000000, PCI_ADDRESS_SPACE_MEM,
                           ebus_mmio_mapfunc);
    pci_register_io_region(s, 1, 0x800000,  PCI_ADDRESS_SPACE_MEM,
                           ebus_mmio_mapfunc);
}

static void sun4uv_init(ram_addr_t RAM_size,
                        const char *boot_devices,
                        const char *kernel_filename, const char *kernel_cmdline,
                        const char *initrd_filename, const char *cpu_model,
                        const struct hwdef *hwdef)
{
    CPUState *env;
    char buf[1024];
    m48t59_t *nvram;
    int ret, linux_boot;
    unsigned int i;
    ram_addr_t ram_offset, prom_offset;
    long initrd_size, kernel_size;
    PCIBus *pci_bus, *pci_bus2, *pci_bus3;
    QEMUBH *bh;
    qemu_irq *irq;
    int drive_index;
    BlockDriverState *hd[MAX_IDE_BUS * MAX_IDE_DEVS];
    BlockDriverState *fd[MAX_FD];
    void *fw_cfg;
    ResetData *reset_info;

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

    reset_info = qemu_mallocz(sizeof(ResetData));
    reset_info->env = env;
    reset_info->reset_addr = hwdef->prom_addr + 0x40ULL;
    qemu_register_reset(main_cpu_reset, reset_info);
    main_cpu_reset(reset_info);
    // Override warm reset address with cold start address
    env->pc = hwdef->prom_addr + 0x20ULL;
    env->npc = env->pc + 4;

    /* allocate RAM */
    ram_offset = qemu_ram_alloc(RAM_size);
    cpu_register_physical_memory(0, RAM_size, ram_offset);

    prom_offset = qemu_ram_alloc(PROM_SIZE_MAX);
    cpu_register_physical_memory(hwdef->prom_addr,
                                 (PROM_SIZE_MAX + TARGET_PAGE_SIZE) &
                                 TARGET_PAGE_MASK,
                                 prom_offset | IO_MEM_ROM);

    if (bios_name == NULL)
        bios_name = PROM_FILENAME;
    snprintf(buf, sizeof(buf), "%s/%s", bios_dir, bios_name);
    ret = load_elf(buf, hwdef->prom_addr - PROM_VADDR, NULL, NULL, NULL);
    if (ret < 0) {
        ret = load_image_targphys(buf, hwdef->prom_addr,
                                  (PROM_SIZE_MAX + TARGET_PAGE_SIZE) &
                                  TARGET_PAGE_MASK);
        if (ret < 0) {
            fprintf(stderr, "qemu: could not load prom '%s'\n",
                    buf);
            exit(1);
        }
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
    pci_bus = pci_apb_init(APB_SPECIAL_BASE, APB_MEM_BASE, NULL, &pci_bus2,
                           &pci_bus3);
    isa_mem_base = VGA_BASE;
    pci_vga_init(pci_bus, 0, 0);

    // XXX Should be pci_bus3
    pci_ebus_init(pci_bus, -1);

    i = 0;
    if (hwdef->console_serial_base) {
        serial_mm_init(hwdef->console_serial_base, 0, NULL, 115200,
                       serial_hds[i], 1);
        i++;
    }
    for(; i < MAX_SERIAL_PORTS; i++) {
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

    for(i = 0; i < nb_nics; i++)
        pci_nic_init(pci_bus, &nd_table[i], -1, "ne2k_pci");

    irq = qemu_allocate_irqs(cpu_set_irq, env, MAX_PILS);
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

    pci_cmd646_ide_init(pci_bus, hd, 1);

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
    fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_ADDR, KERNEL_LOAD_ADDR);
    fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_SIZE, kernel_size);
    if (kernel_cmdline) {
        fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_CMDLINE, CMDLINE_ADDR);
        pstrcpy_targphys(CMDLINE_ADDR, TARGET_PAGE_SIZE, kernel_cmdline);
    } else {
        fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_CMDLINE, 0);
    }
    fw_cfg_add_i32(fw_cfg, FW_CFG_INITRD_ADDR, INITRD_LOAD_ADDR);
    fw_cfg_add_i32(fw_cfg, FW_CFG_INITRD_SIZE, initrd_size);
    fw_cfg_add_i16(fw_cfg, FW_CFG_BOOT_DEVICE, boot_devices[0]);
    qemu_register_boot_set(fw_cfg_boot_set, fw_cfg);
}

enum {
    sun4u_id = 0,
    sun4v_id = 64,
    niagara_id,
};

static const struct hwdef hwdefs[] = {
    /* Sun4u generic PC-like machine */
    {
        .default_cpu_model = "TI UltraSparc II",
        .machine_id = sun4u_id,
        .prom_addr = 0x1fff0000000ULL,
        .console_serial_base = 0,
    },
    /* Sun4v generic PC-like machine */
    {
        .default_cpu_model = "Sun UltraSparc T1",
        .machine_id = sun4v_id,
        .prom_addr = 0x1fff0000000ULL,
        .console_serial_base = 0,
    },
    /* Sun4v generic Niagara machine */
    {
        .default_cpu_model = "Sun UltraSparc T1",
        .machine_id = niagara_id,
        .prom_addr = 0xfff0000000ULL,
        .console_serial_base = 0xfff0c2c000ULL,
    },
};

/* Sun4u hardware initialisation */
static void sun4u_init(ram_addr_t RAM_size,
                       const char *boot_devices,
                       const char *kernel_filename, const char *kernel_cmdline,
                       const char *initrd_filename, const char *cpu_model)
{
    sun4uv_init(RAM_size, boot_devices, kernel_filename,
                kernel_cmdline, initrd_filename, cpu_model, &hwdefs[0]);
}

/* Sun4v hardware initialisation */
static void sun4v_init(ram_addr_t RAM_size,
                       const char *boot_devices,
                       const char *kernel_filename, const char *kernel_cmdline,
                       const char *initrd_filename, const char *cpu_model)
{
    sun4uv_init(RAM_size, boot_devices, kernel_filename,
                kernel_cmdline, initrd_filename, cpu_model, &hwdefs[1]);
}

/* Niagara hardware initialisation */
static void niagara_init(ram_addr_t RAM_size,
                         const char *boot_devices,
                         const char *kernel_filename, const char *kernel_cmdline,
                         const char *initrd_filename, const char *cpu_model)
{
    sun4uv_init(RAM_size, boot_devices, kernel_filename,
                kernel_cmdline, initrd_filename, cpu_model, &hwdefs[2]);
}

QEMUMachine sun4u_machine = {
    .name = "sun4u",
    .desc = "Sun4u platform",
    .init = sun4u_init,
    .max_cpus = 1, // XXX for now
};

QEMUMachine sun4v_machine = {
    .name = "sun4v",
    .desc = "Sun4v platform",
    .init = sun4v_init,
    .max_cpus = 1, // XXX for now
};

QEMUMachine niagara_machine = {
    .name = "Niagara",
    .desc = "Sun4v platform, Niagara",
    .init = niagara_init,
    .max_cpus = 1, // XXX for now
};
