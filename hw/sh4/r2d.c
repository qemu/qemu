/*
 * Renesas SH7751R R2D-PLUS emulation
 *
 * Copyright (c) 2007 Magnus Damm
 * Copyright (c) 2008 Paul Mundt
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

#include "hw/sysbus.h"
#include "hw/hw.h"
#include "hw/sh4/sh.h"
#include "hw/devices.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "hw/pci/pci.h"
#include "net/net.h"
#include "sh7750_regs.h"
#include "hw/ide.h"
#include "hw/loader.h"
#include "hw/usb.h"
#include "hw/block/flash.h"
#include "sysemu/blockdev.h"
#include "exec/address-spaces.h"

#define FLASH_BASE 0x00000000
#define FLASH_SIZE 0x02000000

#define SDRAM_BASE 0x0c000000 /* Physical location of SDRAM: Area 3 */
#define SDRAM_SIZE 0x04000000

#define SM501_VRAM_SIZE 0x800000

#define BOOT_PARAMS_OFFSET 0x0010000
/* CONFIG_BOOT_LINK_OFFSET of Linux kernel */
#define LINUX_LOAD_OFFSET  0x0800000
#define INITRD_LOAD_OFFSET 0x1800000

#define PA_IRLMSK	0x00
#define PA_POWOFF	0x30
#define PA_VERREG	0x32
#define PA_OUTPORT	0x36

typedef struct {
    uint16_t bcr;
    uint16_t irlmsk;
    uint16_t irlmon;
    uint16_t cfctl;
    uint16_t cfpow;
    uint16_t dispctl;
    uint16_t sdmpow;
    uint16_t rtcce;
    uint16_t pcicd;
    uint16_t voyagerrts;
    uint16_t cfrst;
    uint16_t admrts;
    uint16_t extrst;
    uint16_t cfcdintclr;
    uint16_t keyctlclr;
    uint16_t pad0;
    uint16_t pad1;
    uint16_t verreg;
    uint16_t inport;
    uint16_t outport;
    uint16_t bverreg;

/* output pin */
    qemu_irq irl;
    MemoryRegion iomem;
} r2d_fpga_t;

enum r2d_fpga_irq {
    PCI_INTD, CF_IDE, CF_CD, PCI_INTC, SM501, KEY, RTC_A, RTC_T,
    SDCARD, PCI_INTA, PCI_INTB, EXT, TP,
    NR_IRQS
};

static const struct { short irl; uint16_t msk; } irqtab[NR_IRQS] = {
    [CF_IDE]	= {  1, 1<<9 },
    [CF_CD]	= {  2, 1<<8 },
    [PCI_INTA]	= {  9, 1<<14 },
    [PCI_INTB]	= { 10, 1<<13 },
    [PCI_INTC]	= {  3, 1<<12 },
    [PCI_INTD]	= {  0, 1<<11 },
    [SM501]	= {  4, 1<<10 },
    [KEY]	= {  5, 1<<6 },
    [RTC_A]	= {  6, 1<<5 },
    [RTC_T]	= {  7, 1<<4 },
    [SDCARD]	= {  8, 1<<7 },
    [EXT]	= { 11, 1<<0 },
    [TP]	= { 12, 1<<15 },
};

static void update_irl(r2d_fpga_t *fpga)
{
    int i, irl = 15;
    for (i = 0; i < NR_IRQS; i++)
        if (fpga->irlmon & fpga->irlmsk & irqtab[i].msk)
            if (irqtab[i].irl < irl)
                irl = irqtab[i].irl;
    qemu_set_irq(fpga->irl, irl ^ 15);
}

static void r2d_fpga_irq_set(void *opaque, int n, int level)
{
    r2d_fpga_t *fpga = opaque;
    if (level)
        fpga->irlmon |= irqtab[n].msk;
    else
        fpga->irlmon &= ~irqtab[n].msk;
    update_irl(fpga);
}

static uint32_t r2d_fpga_read(void *opaque, hwaddr addr)
{
    r2d_fpga_t *s = opaque;

    switch (addr) {
    case PA_IRLMSK:
        return s->irlmsk;
    case PA_OUTPORT:
	return s->outport;
    case PA_POWOFF:
	return 0x00;
    case PA_VERREG:
	return 0x10;
    }

    return 0;
}

static void
r2d_fpga_write(void *opaque, hwaddr addr, uint32_t value)
{
    r2d_fpga_t *s = opaque;

    switch (addr) {
    case PA_IRLMSK:
        s->irlmsk = value;
        update_irl(s);
	break;
    case PA_OUTPORT:
	s->outport = value;
	break;
    case PA_POWOFF:
        if (value & 1) {
            qemu_system_shutdown_request();
        }
        break;
    case PA_VERREG:
	/* Discard writes */
	break;
    }
}

static const MemoryRegionOps r2d_fpga_ops = {
    .old_mmio = {
        .read = { r2d_fpga_read, r2d_fpga_read, NULL, },
        .write = { r2d_fpga_write, r2d_fpga_write, NULL, },
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static qemu_irq *r2d_fpga_init(MemoryRegion *sysmem,
                               hwaddr base, qemu_irq irl)
{
    r2d_fpga_t *s;

    s = g_malloc0(sizeof(r2d_fpga_t));

    s->irl = irl;

    memory_region_init_io(&s->iomem, &r2d_fpga_ops, s, "r2d-fpga", 0x40);
    memory_region_add_subregion(sysmem, base, &s->iomem);
    return qemu_allocate_irqs(r2d_fpga_irq_set, s, NR_IRQS);
}

typedef struct ResetData {
    SuperHCPU *cpu;
    uint32_t vector;
} ResetData;

static void main_cpu_reset(void *opaque)
{
    ResetData *s = (ResetData *)opaque;
    CPUSH4State *env = &s->cpu->env;

    cpu_reset(CPU(s->cpu));
    env->pc = s->vector;
}

static struct QEMU_PACKED
{
    int mount_root_rdonly;
    int ramdisk_flags;
    int orig_root_dev;
    int loader_type;
    int initrd_start;
    int initrd_size;

    char pad[232];

    char kernel_cmdline[256];
} boot_params;

static void r2d_init(QEMUMachineInitArgs *args)
{
    const char *cpu_model = args->cpu_model;
    const char *kernel_filename = args->kernel_filename;
    const char *kernel_cmdline = args->kernel_cmdline;
    const char *initrd_filename = args->initrd_filename;
    SuperHCPU *cpu;
    CPUSH4State *env;
    ResetData *reset_info;
    struct SH7750State *s;
    MemoryRegion *sdram = g_new(MemoryRegion, 1);
    qemu_irq *irq;
    DriveInfo *dinfo;
    int i;
    DeviceState *dev;
    SysBusDevice *busdev;
    MemoryRegion *address_space_mem = get_system_memory();

    if (cpu_model == NULL) {
        cpu_model = "SH7751R";
    }

    cpu = cpu_sh4_init(cpu_model);
    if (cpu == NULL) {
        fprintf(stderr, "Unable to find CPU definition\n");
        exit(1);
    }
    env = &cpu->env;

    reset_info = g_malloc0(sizeof(ResetData));
    reset_info->cpu = cpu;
    reset_info->vector = env->pc;
    qemu_register_reset(main_cpu_reset, reset_info);

    /* Allocate memory space */
    memory_region_init_ram(sdram, "r2d.sdram", SDRAM_SIZE);
    vmstate_register_ram_global(sdram);
    memory_region_add_subregion(address_space_mem, SDRAM_BASE, sdram);
    /* Register peripherals */
    s = sh7750_init(cpu, address_space_mem);
    irq = r2d_fpga_init(address_space_mem, 0x04000000, sh7750_irl(s));

    dev = qdev_create(NULL, "sh_pci");
    busdev = SYS_BUS_DEVICE(dev);
    qdev_init_nofail(dev);
    sysbus_mmio_map(busdev, 0, P4ADDR(0x1e200000));
    sysbus_mmio_map(busdev, 1, A7ADDR(0x1e200000));
    sysbus_connect_irq(busdev, 0, irq[PCI_INTA]);
    sysbus_connect_irq(busdev, 1, irq[PCI_INTB]);
    sysbus_connect_irq(busdev, 2, irq[PCI_INTC]);
    sysbus_connect_irq(busdev, 3, irq[PCI_INTD]);

    sm501_init(address_space_mem, 0x10000000, SM501_VRAM_SIZE,
               irq[SM501], serial_hds[2]);

    /* onboard CF (True IDE mode, Master only). */
    dinfo = drive_get(IF_IDE, 0, 0);
    dev = qdev_create(NULL, "mmio-ide");
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_connect_irq(busdev, 0, irq[CF_IDE]);
    qdev_prop_set_uint32(dev, "shift", 1);
    qdev_init_nofail(dev);
    sysbus_mmio_map(busdev, 0, 0x14001000);
    sysbus_mmio_map(busdev, 1, 0x1400080c);
    mmio_ide_init_drives(dev, dinfo, NULL);

    /* onboard flash memory */
    dinfo = drive_get(IF_PFLASH, 0, 0);
    pflash_cfi02_register(0x0, NULL, "r2d.flash", FLASH_SIZE,
                          dinfo ? dinfo->bdrv : NULL, (16 * 1024),
                          FLASH_SIZE >> 16,
                          1, 4, 0x0000, 0x0000, 0x0000, 0x0000,
                          0x555, 0x2aa, 0);

    /* NIC: rtl8139 on-board, and 2 slots. */
    for (i = 0; i < nb_nics; i++)
        pci_nic_init_nofail(&nd_table[i], "rtl8139", i==0 ? "2" : NULL);

    /* USB keyboard */
    usbdevice_create("keyboard");

    /* Todo: register on board registers */
    memset(&boot_params, 0, sizeof(boot_params));

    if (kernel_filename) {
        int kernel_size;

        kernel_size = load_image_targphys(kernel_filename,
                                          SDRAM_BASE + LINUX_LOAD_OFFSET,
                                          INITRD_LOAD_OFFSET - LINUX_LOAD_OFFSET);
        if (kernel_size < 0) {
          fprintf(stderr, "qemu: could not load kernel '%s'\n", kernel_filename);
          exit(1);
        }

        /* initialization which should be done by firmware */
        stl_phys(SH7750_BCR1, 1<<3); /* cs3 SDRAM */
        stw_phys(SH7750_BCR2, 3<<(3*2)); /* cs3 32bit */
        reset_info->vector = (SDRAM_BASE + LINUX_LOAD_OFFSET) | 0xa0000000; /* Start from P2 area */
    }

    if (initrd_filename) {
        int initrd_size;

        initrd_size = load_image_targphys(initrd_filename,
                                          SDRAM_BASE + INITRD_LOAD_OFFSET,
                                          SDRAM_SIZE - INITRD_LOAD_OFFSET);

        if (initrd_size < 0) {
          fprintf(stderr, "qemu: could not load initrd '%s'\n", initrd_filename);
          exit(1);
        }

        /* initialization which should be done by firmware */
        boot_params.loader_type = 1;
        boot_params.initrd_start = INITRD_LOAD_OFFSET;
        boot_params.initrd_size = initrd_size;
    }

    if (kernel_cmdline) {
        /* I see no evidence that this .kernel_cmdline buffer requires
           NUL-termination, so using strncpy should be ok. */
        strncpy(boot_params.kernel_cmdline, kernel_cmdline,
                sizeof(boot_params.kernel_cmdline));
    }

    rom_add_blob_fixed("boot_params", &boot_params, sizeof(boot_params),
                       SDRAM_BASE + BOOT_PARAMS_OFFSET);
}

static QEMUMachine r2d_machine = {
    .name = "r2d",
    .desc = "r2d-plus board",
    .init = r2d_init,
    DEFAULT_MACHINE_OPTIONS,
};

static void r2d_machine_init(void)
{
    qemu_register_machine(&r2d_machine);
}

machine_init(r2d_machine_init);
