/*
 * MIPS Boston development board emulation.
 *
 * Copyright (c) 2016 Imagination Technologies
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"

#include "exec/address-spaces.h"
#include "hw/boards.h"
#include "hw/char/serial.h"
#include "hw/hw.h"
#include "hw/ide/pci.h"
#include "hw/ide/ahci.h"
#include "hw/loader.h"
#include "hw/loader-fit.h"
#include "hw/mips/cps.h"
#include "hw/mips/cpudevs.h"
#include "hw/pci-host/xilinx-pcie.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "sysemu/char.h"
#include "sysemu/device_tree.h"
#include "sysemu/sysemu.h"
#include "sysemu/qtest.h"

#include <libfdt.h>

#define TYPE_MIPS_BOSTON "mips-boston"
#define BOSTON(obj) OBJECT_CHECK(BostonState, (obj), TYPE_MIPS_BOSTON)

typedef struct {
    SysBusDevice parent_obj;

    MachineState *mach;
    MIPSCPSState *cps;
    SerialState *uart;

    CharBackend lcd_display;
    char lcd_content[8];
    bool lcd_inited;

    hwaddr kernel_entry;
    hwaddr fdt_base;
} BostonState;

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

static void boston_lcd_event(void *opaque, int event)
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
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static uint64_t boston_platreg_read(void *opaque, hwaddr addr,
                                    unsigned size)
{
    BostonState *s = opaque;
    uint32_t gic_freq, val;

    if (size != 4) {
        qemu_log_mask(LOG_UNIMP, "%uB platform register read", size);
        return 0;
    }

    switch (addr & 0xffff) {
    case PLAT_FPGA_BUILD:
    case PLAT_CORE_CL:
    case PLAT_WRAPPER_CL:
        return 0;
    case PLAT_DDR3_STATUS:
        return PLAT_DDR3_STATUS_LOCKED | PLAT_DDR3_STATUS_CALIBRATED;
    case PLAT_MMCM_DIV:
        gic_freq = mips_gictimer_get_freq(s->cps->gic.gic_timer) / 1000000;
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
        val = s->mach->ram_size / G_BYTE;
        assert(!(val & ~PLAT_DDR_CFG_SIZE));
        val |= PLAT_DDR_CFG_MHZ;
        return val;
    default:
        qemu_log_mask(LOG_UNIMP, "Read platform register 0x%" HWADDR_PRIx,
                      addr & 0xffff);
        return 0;
    }
}

static void boston_platreg_write(void *opaque, hwaddr addr,
                                 uint64_t val, unsigned size)
{
    if (size != 4) {
        qemu_log_mask(LOG_UNIMP, "%uB platform register write", size);
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
            qemu_system_reset_request();
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "Write platform register 0x%" HWADDR_PRIx
                      " = 0x%" PRIx64, addr & 0xffff, val);
        break;
    }
}

static const MemoryRegionOps boston_platreg_ops = {
    .read = boston_platreg_read,
    .write = boston_platreg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void boston_flash_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned size)
{
}

static const MemoryRegionOps boston_flash_ops = {
    .write = boston_flash_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const TypeInfo boston_device = {
    .name          = TYPE_MIPS_BOSTON,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BostonState),
};

static void boston_register_types(void)
{
    type_register_static(&boston_device);
}
type_init(boston_register_types)

static void gen_firmware(uint32_t *p, hwaddr kernel_entry, hwaddr fdt_addr,
                         bool is_64b)
{
    const uint32_t cm_base = 0x16100000;
    const uint32_t gic_base = 0x16120000;
    const uint32_t cpc_base = 0x16200000;

    /* Move CM GCRs */
    if (is_64b) {
        stl_p(p++, 0x40287803);                 /* dmfc0 $8, CMGCRBase */
        stl_p(p++, 0x00084138);                 /* dsll $8, $8, 4 */
    } else {
        stl_p(p++, 0x40087803);                 /* mfc0 $8, CMGCRBase */
        stl_p(p++, 0x00084100);                 /* sll  $8, $8, 4 */
    }
    stl_p(p++, 0x3c09a000);                     /* lui  $9, 0xa000 */
    stl_p(p++, 0x01094025);                     /* or   $8, $9 */
    stl_p(p++, 0x3c0a0000 | (cm_base >> 16));   /* lui  $10, cm_base >> 16 */
    if (is_64b) {
        stl_p(p++, 0xfd0a0008);                 /* sd   $10, 0x8($8) */
    } else {
        stl_p(p++, 0xad0a0008);                 /* sw   $10, 0x8($8) */
    }
    stl_p(p++, 0x012a4025);                     /* or   $8, $10 */

    /* Move & enable GIC GCRs */
    stl_p(p++, 0x3c090000 | (gic_base >> 16));  /* lui  $9, gic_base >> 16 */
    stl_p(p++, 0x35290001);                     /* ori  $9, 0x1 */
    if (is_64b) {
        stl_p(p++, 0xfd090080);                 /* sd   $9, 0x80($8) */
    } else {
        stl_p(p++, 0xad090080);                 /* sw   $9, 0x80($8) */
    }

    /* Move & enable CPC GCRs */
    stl_p(p++, 0x3c090000 | (cpc_base >> 16));  /* lui  $9, cpc_base >> 16 */
    stl_p(p++, 0x35290001);                     /* ori  $9, 0x1 */
    if (is_64b) {
        stl_p(p++, 0xfd090088);                 /* sd   $9, 0x88($8) */
    } else {
        stl_p(p++, 0xad090088);                 /* sw   $9, 0x88($8) */
    }

    /*
     * Setup argument registers to follow the UHI boot protocol:
     *
     * a0/$4 = -2
     * a1/$5 = virtual address of FDT
     * a2/$6 = 0
     * a3/$7 = 0
     */
    stl_p(p++, 0x2404fffe);                     /* li   $4, -2 */
                                                /* lui  $5, hi(fdt_addr) */
    stl_p(p++, 0x3c050000 | ((fdt_addr >> 16) & 0xffff));
    if (fdt_addr & 0xffff) {                    /* ori  $5, lo(fdt_addr) */
        stl_p(p++, 0x34a50000 | (fdt_addr & 0xffff));
    }
    stl_p(p++, 0x34060000);                     /* li   $6, 0 */
    stl_p(p++, 0x34070000);                     /* li   $7, 0 */

    /* Load kernel entry address & jump to it */
                                                /* lui  $25, hi(kernel_entry) */
    stl_p(p++, 0x3c190000 | ((kernel_entry >> 16) & 0xffff));
                                                /* ori  $25, lo(kernel_entry) */
    stl_p(p++, 0x37390000 | (kernel_entry & 0xffff));
    stl_p(p++, 0x03200009);                     /* jr   $25 */
}

static const void *boston_fdt_filter(void *opaque, const void *fdt_orig,
                                     const void *match_data, hwaddr *load_addr)
{
    BostonState *s = BOSTON(opaque);
    MachineState *machine = s->mach;
    const char *cmdline;
    int err;
    void *fdt;
    size_t fdt_sz, ram_low_sz, ram_high_sz;

    fdt_sz = fdt_totalsize(fdt_orig) * 2;
    fdt = g_malloc0(fdt_sz);

    err = fdt_open_into(fdt_orig, fdt, fdt_sz);
    if (err) {
        fprintf(stderr, "unable to open FDT\n");
        return NULL;
    }

    cmdline = (machine->kernel_cmdline && machine->kernel_cmdline[0])
            ? machine->kernel_cmdline : " ";
    err = qemu_fdt_setprop_string(fdt, "/chosen", "bootargs", cmdline);
    if (err < 0) {
        fprintf(stderr, "couldn't set /chosen/bootargs\n");
        return NULL;
    }

    ram_low_sz = MIN(256 * M_BYTE, machine->ram_size);
    ram_high_sz = machine->ram_size - ram_low_sz;
    qemu_fdt_setprop_sized_cells(fdt, "/memory@0", "reg",
                                 1, 0x00000000, 1, ram_low_sz,
                                 1, 0x90000000, 1, ram_high_sz);

    fdt = g_realloc(fdt, fdt_totalsize(fdt));
    qemu_fdt_dumpdtb(fdt, fdt_sz);

    s->fdt_base = *load_addr;

    return fdt;
}

static const void *boston_kernel_filter(void *opaque, const void *kernel,
                                        hwaddr *load_addr, hwaddr *entry_addr)
{
    BostonState *s = BOSTON(opaque);

    s->kernel_entry = *entry_addr;

    return kernel;
}

static const struct fit_loader_match boston_matches[] = {
    { "img,boston" },
    { NULL },
};

static const struct fit_loader boston_fit_loader = {
    .matches = boston_matches,
    .addr_to_phys = cpu_mips_kseg0_to_phys,
    .fdt_filter = boston_fdt_filter,
    .kernel_filter = boston_kernel_filter,
};

static inline XilinxPCIEHost *
xilinx_pcie_init(MemoryRegion *sys_mem, uint32_t bus_nr,
                 hwaddr cfg_base, uint64_t cfg_size,
                 hwaddr mmio_base, uint64_t mmio_size,
                 qemu_irq irq, bool link_up)
{
    DeviceState *dev;
    MemoryRegion *cfg, *mmio;

    dev = qdev_create(NULL, TYPE_XILINX_PCIE_HOST);

    qdev_prop_set_uint32(dev, "bus_nr", bus_nr);
    qdev_prop_set_uint64(dev, "cfg_base", cfg_base);
    qdev_prop_set_uint64(dev, "cfg_size", cfg_size);
    qdev_prop_set_uint64(dev, "mmio_base", mmio_base);
    qdev_prop_set_uint64(dev, "mmio_size", mmio_size);
    qdev_prop_set_bit(dev, "link_up", link_up);

    qdev_init_nofail(dev);

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
    Error *err = NULL;
    const char *cpu_model;
    MemoryRegion *flash, *ddr, *ddr_low_alias, *lcd, *platreg;
    MemoryRegion *sys_mem = get_system_memory();
    XilinxPCIEHost *pcie2;
    PCIDevice *ahci;
    DriveInfo *hd[6];
    Chardev *chr;
    int fw_size, fit_err;
    bool is_64b;

    if ((machine->ram_size % G_BYTE) ||
        (machine->ram_size > (2 * G_BYTE))) {
        error_report("Memory size must be 1GB or 2GB");
        exit(1);
    }

    cpu_model = machine->cpu_model ?: "I6400";

    dev = qdev_create(NULL, TYPE_MIPS_BOSTON);
    qdev_init_nofail(dev);

    s = BOSTON(dev);
    s->mach = machine;
    s->cps = g_new0(MIPSCPSState, 1);

    if (!cpu_supports_cps_smp(cpu_model)) {
        error_report("Boston requires CPUs which support CPS");
        exit(1);
    }

    is_64b = cpu_supports_isa(cpu_model, ISA_MIPS64);

    object_initialize(s->cps, sizeof(MIPSCPSState), TYPE_MIPS_CPS);
    qdev_set_parent_bus(DEVICE(s->cps), sysbus_get_default());

    object_property_set_str(OBJECT(s->cps), cpu_model, "cpu-model", &err);
    object_property_set_int(OBJECT(s->cps), smp_cpus, "num-vp", &err);
    object_property_set_bool(OBJECT(s->cps), true, "realized", &err);

    if (err != NULL) {
        error_report("%s", error_get_pretty(err));
        exit(1);
    }

    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(s->cps), 0, 0, 1);

    flash =  g_new(MemoryRegion, 1);
    memory_region_init_rom_device(flash, NULL, &boston_flash_ops, s,
                                  "boston.flash", 128 * M_BYTE, &err);
    memory_region_add_subregion_overlap(sys_mem, 0x18000000, flash, 0);

    ddr = g_new(MemoryRegion, 1);
    memory_region_allocate_system_memory(ddr, NULL, "boston.ddr",
                                         machine->ram_size);
    memory_region_add_subregion_overlap(sys_mem, 0x80000000, ddr, 0);

    ddr_low_alias = g_new(MemoryRegion, 1);
    memory_region_init_alias(ddr_low_alias, NULL, "boston_low.ddr",
                             ddr, 0, MIN(machine->ram_size, (256 * M_BYTE)));
    memory_region_add_subregion_overlap(sys_mem, 0, ddr_low_alias, 0);

    xilinx_pcie_init(sys_mem, 0,
                     0x10000000, 32 * M_BYTE,
                     0x40000000, 1 * G_BYTE,
                     get_cps_irq(s->cps, 2), false);

    xilinx_pcie_init(sys_mem, 1,
                     0x12000000, 32 * M_BYTE,
                     0x20000000, 512 * M_BYTE,
                     get_cps_irq(s->cps, 1), false);

    pcie2 = xilinx_pcie_init(sys_mem, 2,
                             0x14000000, 32 * M_BYTE,
                             0x16000000, 1 * M_BYTE,
                             get_cps_irq(s->cps, 0), true);

    platreg = g_new(MemoryRegion, 1);
    memory_region_init_io(platreg, NULL, &boston_platreg_ops, s,
                          "boston-platregs", 0x1000);
    memory_region_add_subregion_overlap(sys_mem, 0x17ffd000, platreg, 0);

    if (!serial_hds[0]) {
        serial_hds[0] = qemu_chr_new("serial0", "null");
    }

    s->uart = serial_mm_init(sys_mem, 0x17ffe000, 2,
                             get_cps_irq(s->cps, 3), 10000000,
                             serial_hds[0], DEVICE_NATIVE_ENDIAN);

    lcd = g_new(MemoryRegion, 1);
    memory_region_init_io(lcd, NULL, &boston_lcd_ops, s, "boston-lcd", 0x8);
    memory_region_add_subregion_overlap(sys_mem, 0x17fff000, lcd, 0);

    chr = qemu_chr_new("lcd", "vc:320x240");
    qemu_chr_fe_init(&s->lcd_display, chr, NULL);
    qemu_chr_fe_set_handlers(&s->lcd_display, NULL, NULL,
                             boston_lcd_event, s, NULL, true);

    ahci = pci_create_simple_multifunction(&PCI_BRIDGE(&pcie2->root)->sec_bus,
                                           PCI_DEVFN(0, 0),
                                           true, TYPE_ICH9_AHCI);
    g_assert(ARRAY_SIZE(hd) == ICH_AHCI(ahci)->ahci.ports);
    ide_drive_get(hd, ICH_AHCI(ahci)->ahci.ports);
    ahci_ide_create_devs(ahci, hd);

    if (machine->firmware) {
        fw_size = load_image_targphys(machine->firmware,
                                      0x1fc00000, 4 * M_BYTE);
        if (fw_size == -1) {
            error_printf("unable to load firmware image '%s'\n",
                          machine->firmware);
            exit(1);
        }
    } else if (machine->kernel_filename) {
        fit_err = load_fit(&boston_fit_loader, machine->kernel_filename, s);
        if (fit_err) {
            error_printf("unable to load FIT image\n");
            exit(1);
        }

        gen_firmware(memory_region_get_ram_ptr(flash) + 0x7c00000,
                     s->kernel_entry, s->fdt_base, is_64b);
    } else if (!qtest_enabled()) {
        error_printf("Please provide either a -kernel or -bios argument\n");
        exit(1);
    }
}

static void boston_mach_class_init(MachineClass *mc)
{
    mc->desc = "MIPS Boston";
    mc->init = boston_mach_init;
    mc->block_default_type = IF_IDE;
    mc->default_ram_size = 1 * G_BYTE;
    mc->max_cpus = 16;
}

DEFINE_MACHINE("boston", boston_mach_class_init)
