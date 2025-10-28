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

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "cpu.h"
#include "hw/sysbus.h"
#include "hw/sh4/sh.h"
#include "system/reset.h"
#include "system/runstate.h"
#include "system/system.h"
#include "hw/boards.h"
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"
#include "net/net.h"
#include "sh7750_regs.h"
#include "hw/ide/mmio.h"
#include "hw/irq.h"
#include "hw/loader.h"
#include "hw/usb.h"
#include "hw/block/flash.h"
#include "exec/tswap.h"

#define FLASH_BASE 0x00000000
#define FLASH_SIZE (16 * MiB)

#define SDRAM_BASE 0x0c000000 /* Physical location of SDRAM: Area 3 */
#define SDRAM_SIZE 0x04000000

#define SM501_VRAM_SIZE 0x800000

#define BOOT_PARAMS_OFFSET 0x0010000
/* CONFIG_BOOT_LINK_OFFSET of Linux kernel */
#define LINUX_LOAD_OFFSET  0x0800000
#define INITRD_LOAD_OFFSET 0x1800000

#define PA_IRLMSK 0x00
#define PA_POWOFF 0x30
#define PA_VERREG 0x32
#define PA_OUTPORT 0x36

enum r2d_fpga_irq {
    PCI_INTD, CF_IDE, CF_CD, PCI_INTC, SM501, KEY, RTC_A, RTC_T,
    SDCARD, PCI_INTA, PCI_INTB, EXT, TP,
    NR_IRQS
};

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
    IRQState irq[NR_IRQS];
    MemoryRegion iomem;
} r2d_fpga_t;

static const struct { short irl; uint16_t msk; } irqtab[NR_IRQS] = {
    [CF_IDE] =   {  1, 1 << 9 },
    [CF_CD] =    {  2, 1 << 8 },
    [PCI_INTA] = {  9, 1 << 14 },
    [PCI_INTB] = { 10, 1 << 13 },
    [PCI_INTC] = {  3, 1 << 12 },
    [PCI_INTD] = {  0, 1 << 11 },
    [SM501] =    {  4, 1 << 10 },
    [KEY] =      {  5, 1 << 6 },
    [RTC_A] =    {  6, 1 << 5 },
    [RTC_T] =    {  7, 1 << 4 },
    [SDCARD] =   {  8, 1 << 7 },
    [EXT] =      { 11, 1 << 0 },
    [TP] =       { 12, 1 << 15 },
};

static void update_irl(r2d_fpga_t *fpga)
{
    int i, irl = 15;
    for (i = 0; i < NR_IRQS; i++) {
        if ((fpga->irlmon & fpga->irlmsk & irqtab[i].msk) &&
            irqtab[i].irl < irl) {
            irl = irqtab[i].irl;
        }
    }
    qemu_set_irq(fpga->irl, irl ^ 15);
}

static void r2d_fpga_irq_set(void *opaque, int n, int level)
{
    r2d_fpga_t *fpga = opaque;
    if (level) {
        fpga->irlmon |= irqtab[n].msk;
    } else {
        fpga->irlmon &= ~irqtab[n].msk;
    }
    update_irl(fpga);
}

static uint64_t r2d_fpga_read(void *opaque, hwaddr addr, unsigned int size)
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
r2d_fpga_write(void *opaque, hwaddr addr, uint64_t value, unsigned int size)
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
            qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
        }
        break;
    case PA_VERREG:
        /* Discard writes */
        break;
    }
}

static const MemoryRegionOps r2d_fpga_ops = {
    .read = r2d_fpga_read,
    .write = r2d_fpga_write,
    .impl.min_access_size = 2,
    .impl.max_access_size = 2,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static r2d_fpga_t *r2d_fpga_init(MemoryRegion *sysmem,
                                 hwaddr base, qemu_irq irl)
{
    r2d_fpga_t *s;

    s = g_new0(r2d_fpga_t, 1);

    s->irl = irl;

    memory_region_init_io(&s->iomem, NULL, &r2d_fpga_ops, s, "r2d-fpga", 0x40);
    memory_region_add_subregion(sysmem, base, &s->iomem);

    qemu_init_irqs(s->irq, NR_IRQS, r2d_fpga_irq_set, s);

    return s;
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

    char kernel_cmdline[256] QEMU_NONSTRING;
} boot_params;

static void r2d_init(MachineState *machine)
{
    const char *kernel_filename = machine->kernel_filename;
    const char *kernel_cmdline = machine->kernel_cmdline;
    const char *initrd_filename = machine->initrd_filename;
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    SuperHCPU *cpu;
    CPUSH4State *env;
    ResetData *reset_info;
    struct SH7750State *s;
    MemoryRegion *sdram = g_new(MemoryRegion, 1);
    DriveInfo *dinfo;
    DeviceState *dev;
    SysBusDevice *busdev;
    MemoryRegion *address_space_mem = get_system_memory();
    PCIBus *pci_bus;
    USBBus *usb_bus;
    r2d_fpga_t *fpga;

    cpu = SUPERH_CPU(cpu_create(machine->cpu_type));
    env = &cpu->env;

    reset_info = g_new0(ResetData, 1);
    reset_info->cpu = cpu;
    reset_info->vector = env->pc;
    qemu_register_reset(main_cpu_reset, reset_info);

    /* Allocate memory space */
    memory_region_init_ram(sdram, NULL, "r2d.sdram", SDRAM_SIZE, &error_fatal);
    memory_region_add_subregion(address_space_mem, SDRAM_BASE, sdram);
    /* Register peripherals */
    s = sh7750_init(cpu, address_space_mem);
    fpga = r2d_fpga_init(address_space_mem, 0x04000000, sh7750_irl(s));

    dev = qdev_new("sh_pci");
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(busdev, &error_fatal);
    pci_bus = PCI_BUS(qdev_get_child_bus(dev, "pci"));
    sysbus_mmio_map(busdev, 0, P4ADDR(0x1e200000));
    sysbus_mmio_map(busdev, 1, A7ADDR(0x1e200000));
    sysbus_connect_irq(busdev, 0, &fpga->irq[PCI_INTA]);
    sysbus_connect_irq(busdev, 1, &fpga->irq[PCI_INTB]);
    sysbus_connect_irq(busdev, 2, &fpga->irq[PCI_INTC]);
    sysbus_connect_irq(busdev, 3, &fpga->irq[PCI_INTD]);

    dev = qdev_new("sysbus-sm501");
    busdev = SYS_BUS_DEVICE(dev);
    qdev_prop_set_uint32(dev, "vram-size", SM501_VRAM_SIZE);
    qdev_prop_set_uint64(dev, "dma-offset", 0x10000000);
    qdev_prop_set_chr(dev, "chardev", serial_hd(2));
    sysbus_realize_and_unref(busdev, &error_fatal);
    sysbus_mmio_map(busdev, 0, 0x10000000);
    sysbus_mmio_map(busdev, 1, 0x13e00000);
    sysbus_connect_irq(busdev, 0, &fpga->irq[SM501]);

    /* onboard CF (True IDE mode, Master only). */
    dinfo = drive_get(IF_IDE, 0, 0);
    dev = qdev_new("mmio-ide");
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_connect_irq(busdev, 0, &fpga->irq[CF_IDE]);
    qdev_prop_set_uint32(dev, "shift", 1);
    sysbus_realize_and_unref(busdev, &error_fatal);
    sysbus_mmio_map(busdev, 0, 0x14001000);
    sysbus_mmio_map(busdev, 1, 0x1400080c);
    mmio_ide_init_drives(dev, dinfo, NULL);

    /*
     * Onboard flash memory
     * According to the old board user document in Japanese (under
     * NDA) what is referred to as FROM (Area0) is connected via a
     * 32-bit bus and CS0 to CN8. The docs mention a Cypress
     * S29PL127J60TFI130 chipsset.  Per the 'S29PL-J 002-00615
     * Rev. *E' datasheet, it is a 128Mbit NOR parallel flash
     * addressable in words of 16bit.
     */
    dinfo = drive_get(IF_PFLASH, 0, 0);
    pflash_cfi02_register(0x0, "r2d.flash", FLASH_SIZE,
                          dinfo ? blk_by_legacy_dinfo(dinfo) : NULL,
                          64 * KiB, 1, 2, 0x0001, 0x227e, 0x2220, 0x2200,
                          0x555, 0x2aa, 0);

    /* NIC: rtl8139 on-board, and 2 slots. */
    pci_init_nic_in_slot(pci_bus, mc->default_nic, NULL, "2");
    pci_init_nic_devices(pci_bus, mc->default_nic);

    /* USB keyboard */
    usb_bus = USB_BUS(object_resolve_type_unambiguous(TYPE_USB_BUS,
                                                      &error_abort));
    usb_create_simple(usb_bus, "usb-kbd");

    /* Todo: register on board registers */
    memset(&boot_params, 0, sizeof(boot_params));

    if (kernel_filename) {
        int kernel_size;

        kernel_size = load_image_targphys(kernel_filename,
                                        SDRAM_BASE + LINUX_LOAD_OFFSET,
                                        INITRD_LOAD_OFFSET - LINUX_LOAD_OFFSET,
                                        NULL);
        if (kernel_size < 0) {
            error_report("qemu: could not load kernel '%s'", kernel_filename);
            exit(1);
        }

        /* initialization which should be done by firmware */
        address_space_stl(&address_space_memory, SH7750_BCR1, 1 << 3,
                          MEMTXATTRS_UNSPECIFIED, NULL); /* cs3 SDRAM */
        address_space_stw(&address_space_memory, SH7750_BCR2, 3 << (3 * 2),
                          MEMTXATTRS_UNSPECIFIED, NULL); /* cs3 32bit */
        /* Start from P2 area */
        reset_info->vector = (SDRAM_BASE + LINUX_LOAD_OFFSET) | 0xa0000000;
    }

    if (initrd_filename) {
        int initrd_size;

        initrd_size = load_image_targphys(initrd_filename,
                                          SDRAM_BASE + INITRD_LOAD_OFFSET,
                                          SDRAM_SIZE - INITRD_LOAD_OFFSET,
                                          NULL);

        if (initrd_size < 0) {
            error_report("qemu: could not load initrd '%s'", initrd_filename);
            exit(1);
        }

        /* initialization which should be done by firmware */
        boot_params.loader_type = tswap32(1);
        boot_params.initrd_start = tswap32(INITRD_LOAD_OFFSET);
        boot_params.initrd_size = tswap32(initrd_size);
    }

    if (kernel_cmdline) {
        /*
         * I see no evidence that this .kernel_cmdline buffer requires
         * NUL-termination, so using strncpy should be ok.
         */
        strncpy(boot_params.kernel_cmdline, kernel_cmdline,
                sizeof(boot_params.kernel_cmdline));
    }

    rom_add_blob_fixed("boot_params", &boot_params, sizeof(boot_params),
                       SDRAM_BASE + BOOT_PARAMS_OFFSET);
}

static void r2d_machine_init(MachineClass *mc)
{
    mc->desc = "r2d-plus board";
    mc->init = r2d_init;
    mc->block_default_type = IF_IDE;
    mc->default_cpu_type = TYPE_SH7751R_CPU;
    mc->default_nic = "rtl8139";
}

DEFINE_MACHINE("r2d", r2d_machine_init)
