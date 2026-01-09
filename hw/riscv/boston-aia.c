/*
 * MIPS Boston-aia development board emulation.
 *
 * Copyright (c) 2016 Imagination Technologies
 *
 * Copyright (c) 2025 MIPS
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "qemu/osdep.h"
#include "qemu/units.h"

#include "hw/core/boards.h"
#include "hw/char/serial-mm.h"
#include "hw/ide/pci.h"
#include "hw/ide/ahci-pci.h"
#include "hw/core/loader.h"
#include "hw/riscv/cps.h"
#include "hw/pci-host/xilinx-pcie.h"
#include "hw/core/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "chardev/char.h"
#include "system/address-spaces.h"
#include "system/device_tree.h"
#include "system/system.h"
#include "system/qtest.h"
#include "system/runstate.h"

#include <libfdt.h>
#include "qom/object.h"

#define TYPE_MIPS_BOSTON_AIA "mips-boston-aia"
typedef struct BostonState BostonState;
DECLARE_INSTANCE_CHECKER(BostonState, BOSTON,
                         TYPE_MIPS_BOSTON_AIA)

enum {
    BOSTON_PCIE2,
    BOSTON_PCIE2_MMIO,
    BOSTON_PLATREG,
    BOSTON_UART,
    BOSTON_LCD,
    BOSTON_FLASH,
    BOSTON_HIGHDDR,
};

static const MemMapEntry boston_memmap[] = {
    [BOSTON_PCIE2] =      { 0x14000000,     0x2000000 },
    [BOSTON_PCIE2_MMIO] = { 0x16000000,      0x100000 },
    [BOSTON_PLATREG] =    { 0x17ffd000,        0x1000 },
    [BOSTON_UART] =       { 0x17ffe000,          0x20 },
    [BOSTON_LCD] =        { 0x17fff000,           0x8 },
    [BOSTON_FLASH] =      { 0x18000000,     0x8000000 },
    [BOSTON_HIGHDDR] =    { 0x80000000,           0x0 },
};

/* Interrupt numbers for APLIC. */
#define UART_INT 4
#define PCIE2_INT 7

struct BostonState {
    SysBusDevice parent_obj;

    MachineState *mach;
    RISCVCPSState cps;
    SerialMM *uart;

    CharFrontend lcd_display;
    char lcd_content[8];
    bool lcd_inited;
};

enum boston_plat_reg {
    PLAT_FPGA_BUILD     = 0x00,
    PLAT_CORE_CL        = 0x04,
    PLAT_WRAPPER_CL     = 0x08,
    PLAT_SYSCLK_STATUS  = 0x0c,
    PLAT_SOFTRST_CTL    = 0x10,
#define PLAT_SOFTRST_CTL_SYSRESET       (1 << 4)
    PLAT_DDR3_STATUS    = 0x14,
#define PLAT_DDR3_STATUS_LOCKED         (1 << 0)
#define PLAT_DDR3_STATUS_CALIBRATED     (1 << 2)
#define PLAT_DDR3_INTERFACE_RESET       (1 << 3)
    PLAT_PCIE_STATUS    = 0x18,
#define PLAT_PCIE_STATUS_PCIE0_LOCKED   (1 << 0)
#define PLAT_PCIE_STATUS_PCIE1_LOCKED   (1 << 8)
#define PLAT_PCIE_STATUS_PCIE2_LOCKED   (1 << 16)
    PLAT_FLASH_CTL      = 0x1c,
    PLAT_SPARE0         = 0x20,
    PLAT_SPARE1         = 0x24,
    PLAT_SPARE2         = 0x28,
    PLAT_SPARE3         = 0x2c,
    PLAT_MMCM_DIV       = 0x30,
#define PLAT_MMCM_DIV_CLK0DIV_SHIFT     0
#define PLAT_MMCM_DIV_INPUT_SHIFT       8
#define PLAT_MMCM_DIV_MUL_SHIFT         16
#define PLAT_MMCM_DIV_CLK1DIV_SHIFT     24
    PLAT_BUILD_CFG      = 0x34,
#define PLAT_BUILD_CFG_IOCU_EN          (1 << 0)
#define PLAT_BUILD_CFG_PCIE0_EN         (1 << 1)
#define PLAT_BUILD_CFG_PCIE1_EN         (1 << 2)
#define PLAT_BUILD_CFG_PCIE2_EN         (1 << 3)
    PLAT_DDR_CFG        = 0x38,
#define PLAT_DDR_CFG_SIZE               (0xf << 0)
#define PLAT_DDR_CFG_MHZ                (0xfff << 4)
    PLAT_NOC_PCIE0_ADDR = 0x3c,
    PLAT_NOC_PCIE1_ADDR = 0x40,
    PLAT_NOC_PCIE2_ADDR = 0x44,
    PLAT_SYS_CTL        = 0x48,
};

static void boston_lcd_event(void *opaque, QEMUChrEvent event)
{
    BostonState *s = opaque;
    if (event == CHR_EVENT_OPENED && !s->lcd_inited) {
        qemu_chr_fe_printf(&s->lcd_display, "        ");
        s->lcd_inited = true;
    }
}

static uint64_t boston_lcd_read(void *opaque, hwaddr addr,
                                unsigned size)
{
    BostonState *s = opaque;
    uint64_t val = 0;

    switch (size) {
    case 8:
        val |= (uint64_t)s->lcd_content[(addr + 7) & 0x7] << 56;
        val |= (uint64_t)s->lcd_content[(addr + 6) & 0x7] << 48;
        val |= (uint64_t)s->lcd_content[(addr + 5) & 0x7] << 40;
        val |= (uint64_t)s->lcd_content[(addr + 4) & 0x7] << 32;
        /* fall through */
    case 4:
        val |= (uint64_t)s->lcd_content[(addr + 3) & 0x7] << 24;
        val |= (uint64_t)s->lcd_content[(addr + 2) & 0x7] << 16;
        /* fall through */
    case 2:
        val |= (uint64_t)s->lcd_content[(addr + 1) & 0x7] << 8;
        /* fall through */
    case 1:
        val |= (uint64_t)s->lcd_content[(addr + 0) & 0x7];
        break;
    }

    return val;
}

static void boston_lcd_write(void *opaque, hwaddr addr,
                             uint64_t val, unsigned size)
{
    BostonState *s = opaque;

    switch (size) {
    case 8:
        s->lcd_content[(addr + 7) & 0x7] = val >> 56;
        s->lcd_content[(addr + 6) & 0x7] = val >> 48;
        s->lcd_content[(addr + 5) & 0x7] = val >> 40;
        s->lcd_content[(addr + 4) & 0x7] = val >> 32;
        /* fall through */
    case 4:
        s->lcd_content[(addr + 3) & 0x7] = val >> 24;
        s->lcd_content[(addr + 2) & 0x7] = val >> 16;
        /* fall through */
    case 2:
        s->lcd_content[(addr + 1) & 0x7] = val >> 8;
        /* fall through */
    case 1:
        s->lcd_content[(addr + 0) & 0x7] = val;
        break;
    }

    qemu_chr_fe_printf(&s->lcd_display,
                       "\r%-8.8s", s->lcd_content);
}

static const MemoryRegionOps boston_lcd_ops = {
    .read = boston_lcd_read,
    .write = boston_lcd_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t boston_platreg_read(void *opaque, hwaddr addr,
                                    unsigned size)
{
    BostonState *s = opaque;
    uint32_t gic_freq, val;

    switch (addr & 0xffff) {
    case PLAT_FPGA_BUILD:
    case PLAT_CORE_CL:
    case PLAT_WRAPPER_CL:
        return 0;
    case PLAT_DDR3_STATUS:
        return PLAT_DDR3_STATUS_LOCKED | PLAT_DDR3_STATUS_CALIBRATED
               | PLAT_DDR3_INTERFACE_RESET;
    case PLAT_MMCM_DIV:
        gic_freq = 25000000 / 1000000;
        val = gic_freq << PLAT_MMCM_DIV_INPUT_SHIFT;
        val |= 1 << PLAT_MMCM_DIV_MUL_SHIFT;
        val |= 1 << PLAT_MMCM_DIV_CLK0DIV_SHIFT;
        val |= 1 << PLAT_MMCM_DIV_CLK1DIV_SHIFT;
        return val;
    case PLAT_BUILD_CFG:
        val = PLAT_BUILD_CFG_PCIE0_EN;
        val |= PLAT_BUILD_CFG_PCIE1_EN;
        val |= PLAT_BUILD_CFG_PCIE2_EN;
        return val;
    case PLAT_DDR_CFG:
        val = s->mach->ram_size / GiB;
        assert(!(val & ~PLAT_DDR_CFG_SIZE));
        val |= PLAT_DDR_CFG_MHZ;
        return val;
    default:
        qemu_log_mask(LOG_UNIMP, "Read platform register 0x%" HWADDR_PRIx "\n",
                      addr & 0xffff);
        return 0;
    }
}

static void boston_platreg_write(void *opaque, hwaddr addr,
                                 uint64_t val, unsigned size)
{
    if (size != 4) {
        qemu_log_mask(LOG_UNIMP, "%uB platform register write\n", size);
        return;
    }

    switch (addr & 0xffff) {
    case PLAT_FPGA_BUILD:
    case PLAT_CORE_CL:
    case PLAT_WRAPPER_CL:
    case PLAT_DDR3_STATUS:
    case PLAT_PCIE_STATUS:
    case PLAT_MMCM_DIV:
    case PLAT_BUILD_CFG:
    case PLAT_DDR_CFG:
        /* read only */
        break;
    case PLAT_SOFTRST_CTL:
        if (val & PLAT_SOFTRST_CTL_SYSRESET) {
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "Write platform register 0x%" HWADDR_PRIx
                      " = 0x%" PRIx64 "\n", addr & 0xffff, val);
        break;
    }
}

static const MemoryRegionOps boston_platreg_ops = {
    .read = boston_platreg_read,
    .write = boston_platreg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const TypeInfo boston_device = {
    .name          = TYPE_MIPS_BOSTON_AIA,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BostonState),
};

static void boston_register_types(void)
{
    type_register_static(&boston_device);
}
type_init(boston_register_types)

#define NUM_INSNS 6
static void gen_firmware(uint32_t *p)
{
    int i;
    uint32_t reset_vec[NUM_INSNS] = {
           /* CM relocate */
           0x1fb802b7,     /* li   t0,0x1fb80000   */
           0x16100337,     /* li   t1,0x16100000   */
           0x0062b423,     /* sd   t1,8(t0)        */
           /* Jump to 0x80000000 */
           0x00100293,     /* li   t0,1            */
           0x01f29293,     /* slli t0,t0,1f        */
           0x00028067      /* jr   t0              */
    };

    for (i = 0; i < NUM_INSNS; i++) {
        *p++ = reset_vec[i];
    }
}

static inline XilinxPCIEHost *
xilinx_pcie_init(MemoryRegion *sys_mem, uint32_t bus_nr,
                 hwaddr cfg_base, uint64_t cfg_size,
                 hwaddr mmio_base, uint64_t mmio_size,
                 qemu_irq irq)
{
    DeviceState *dev;
    MemoryRegion *cfg, *mmio;

    dev = qdev_new(TYPE_XILINX_PCIE_HOST);

    qdev_prop_set_uint32(dev, "bus_nr", bus_nr);
    qdev_prop_set_uint64(dev, "cfg_base", cfg_base);
    qdev_prop_set_uint64(dev, "cfg_size", cfg_size);
    qdev_prop_set_uint64(dev, "mmio_base", mmio_base);
    qdev_prop_set_uint64(dev, "mmio_size", mmio_size);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    cfg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    memory_region_add_subregion_overlap(sys_mem, cfg_base, cfg, 0);

    mmio = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 1);
    memory_region_add_subregion_overlap(sys_mem, 0, mmio, 0);

    qdev_connect_gpio_out_named(dev, "interrupt_out", 0, irq);

    return XILINX_PCIE_HOST(dev);
}

static void boston_mach_init(MachineState *machine)
{
    DeviceState *dev;
    BostonState *s;
    MemoryRegion *flash, *ddr_low_alias, *lcd, *platreg;
    MemoryRegion *sys_mem = get_system_memory();
    XilinxPCIEHost *pcie2;
    PCIDevice *pdev;
    AHCIPCIState *ich9;
    DriveInfo *hd[6];
    Chardev *chr;
    int fw_size;

    if ((machine->ram_size % GiB) ||
        (machine->ram_size > (4 * GiB))) {
        error_report("Memory size must be 1GB, 2GB, 3GB, or 4GB");
        exit(1);
    }

    if (machine->smp.cpus / machine->smp.cores / machine->smp.threads > 1) {
        error_report(
            "Invalid -smp x,cores=y,threads=z. The max number of clusters "
            "supported is 1");
        exit(1);
    }

    dev = qdev_new(TYPE_MIPS_BOSTON_AIA);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    s = BOSTON(dev);
    s->mach = machine;

    object_initialize_child(OBJECT(machine), "cps", &s->cps, TYPE_RISCV_CPS);
    object_property_set_str(OBJECT(&s->cps), "cpu-type", machine->cpu_type,
                            &error_fatal);
    object_property_set_uint(OBJECT(&s->cps), "num-vp", machine->smp.cpus,
                             &error_fatal);
    object_property_set_uint(OBJECT(&s->cps), "num-hart", machine->smp.threads,
                             &error_fatal);
    object_property_set_uint(OBJECT(&s->cps), "num-core", machine->smp.cores,
                             &error_fatal);
    object_property_set_uint(OBJECT(&s->cps), "gcr-base", GCR_BASE_ADDR,
                             &error_fatal);
    sysbus_realize(SYS_BUS_DEVICE(&s->cps), &error_fatal);

    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(&s->cps), 0, 0, 1);

    flash =  g_new(MemoryRegion, 1);
    memory_region_init_rom(flash, NULL, "boston.flash",
                           boston_memmap[BOSTON_FLASH].size, &error_fatal);
    memory_region_add_subregion_overlap(sys_mem,
                                        boston_memmap[BOSTON_FLASH].base,
                                        flash, 0);

    memory_region_add_subregion_overlap(sys_mem,
                                        boston_memmap[BOSTON_HIGHDDR].base,
                                        machine->ram, 0);

    ddr_low_alias = g_new(MemoryRegion, 1);
    memory_region_init_alias(ddr_low_alias, NULL, "boston_low.ddr",
                             machine->ram, 0,
                             MIN(machine->ram_size, (256 * MiB)));
    memory_region_add_subregion_overlap(sys_mem, 0, ddr_low_alias, 0);

    pcie2 = xilinx_pcie_init(sys_mem, 2,
                             boston_memmap[BOSTON_PCIE2].base,
                             boston_memmap[BOSTON_PCIE2].size,
                             boston_memmap[BOSTON_PCIE2_MMIO].base,
                             boston_memmap[BOSTON_PCIE2_MMIO].size,
                             qdev_get_gpio_in(s->cps.aplic, PCIE2_INT));

    platreg = g_new(MemoryRegion, 1);
    memory_region_init_io(platreg, NULL, &boston_platreg_ops, s,
                          "boston-platregs",
                          boston_memmap[BOSTON_PLATREG].size);
    memory_region_add_subregion_overlap(sys_mem,
                          boston_memmap[BOSTON_PLATREG].base, platreg, 0);

    s->uart = serial_mm_init(sys_mem, boston_memmap[BOSTON_UART].base, 2,
                             qdev_get_gpio_in(s->cps.aplic, UART_INT), 10000000,
                             serial_hd(0), DEVICE_LITTLE_ENDIAN);

    lcd = g_new(MemoryRegion, 1);
    memory_region_init_io(lcd, NULL, &boston_lcd_ops, s, "boston-lcd", 0x8);
    memory_region_add_subregion_overlap(sys_mem,
                                        boston_memmap[BOSTON_LCD].base, lcd, 0);

    chr = qemu_chr_new("lcd", "vc:320x240", NULL);
    qemu_chr_fe_init(&s->lcd_display, chr, NULL);
    qemu_chr_fe_set_handlers(&s->lcd_display, NULL, NULL,
                             boston_lcd_event, NULL, s, NULL, true);

    pdev = pci_create_simple_multifunction(&PCI_BRIDGE(&pcie2->root)->sec_bus,
                                           PCI_DEVFN(0, 0), TYPE_ICH9_AHCI);
    ich9 = ICH9_AHCI(pdev);
    g_assert(ARRAY_SIZE(hd) == ich9->ahci.ports);
    ide_drive_get(hd, ich9->ahci.ports);
    ahci_ide_create_devs(&ich9->ahci, hd);

    /* Create e1000e using slot 0 func 1 */
    pci_init_nic_in_slot(&PCI_BRIDGE(&pcie2->root)->sec_bus, "e1000e", NULL,
                         "00.1");
    pci_init_nic_devices(&PCI_BRIDGE(&pcie2->root)->sec_bus, "e1000e");

    if (machine->firmware) {
        fw_size = load_image_targphys(machine->firmware,
                                      0x1fc00000, 4 * MiB, NULL);
        if (fw_size == -1) {
            error_report("unable to load firmware image '%s'",
                          machine->firmware);
            exit(1);
        }
        if (machine->kernel_filename) {
                fw_size = load_image_targphys(machine->kernel_filename,
                                              0x80000000, 64 * MiB, NULL);
                if (fw_size == -1) {
                    error_report("unable to load kernel image '%s'",
                                  machine->kernel_filename);
                    exit(1);
                }
        }
    } else if (machine->kernel_filename) {
        fw_size = load_image_targphys(machine->kernel_filename,
                                      0x80000000, 64 * MiB, NULL);
        if (fw_size == -1) {
            error_report("unable to load kernel image '%s'",
                          machine->kernel_filename);
            exit(1);
        }

        gen_firmware(memory_region_get_ram_ptr(flash) + 0x7c00000);
    } else if (!qtest_enabled()) {
        error_report("Please provide either a -kernel or -bios argument");
        exit(1);
    }
}

static void boston_mach_class_init(MachineClass *mc)
{
    mc->desc = "MIPS Boston-aia";
    mc->init = boston_mach_init;
    mc->block_default_type = IF_IDE;
    mc->default_ram_size = 2 * GiB;
    mc->default_ram_id = "boston.ddr";
    mc->max_cpus = MAX_HARTS;
    mc->default_cpu_type = TYPE_RISCV_CPU_MIPS_P8700;
}

DEFINE_MACHINE("boston-aia", boston_mach_class_init)
