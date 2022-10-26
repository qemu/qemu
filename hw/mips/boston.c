/*
 * MIPS Boston development board emulation.
 *
 * Copyright (c) 2016 Imagination Technologies
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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
#include "qemu/units.h"

#include "elf.h"
#include "hw/boards.h"
#include "hw/char/serial.h"
#include "hw/ide/pci.h"
#include "hw/ide/ahci.h"
#include "hw/loader.h"
#include "hw/loader-fit.h"
#include "hw/mips/bootloader.h"
#include "hw/mips/cps.h"
#include "hw/pci-host/xilinx-pcie.h"
#include "hw/qdev-clock.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/guest-random.h"
#include "qemu/log.h"
#include "chardev/char.h"
#include "sysemu/device_tree.h"
#include "sysemu/sysemu.h"
#include "sysemu/qtest.h"
#include "sysemu/runstate.h"
#include "sysemu/reset.h"

#include <libfdt.h>
#include "qom/object.h"

#define TYPE_BOSTON "mips-boston"
typedef struct BostonState BostonState;
DECLARE_INSTANCE_CHECKER(BostonState, BOSTON,
                         TYPE_BOSTON)

#define FDT_IRQ_TYPE_NONE       0
#define FDT_IRQ_TYPE_LEVEL_HIGH 4
#define FDT_GIC_SHARED          0
#define FDT_GIC_LOCAL           1
#define FDT_BOSTON_CLK_SYS      1
#define FDT_BOSTON_CLK_CPU      2
#define FDT_PCI_IRQ_MAP_PINS    4
#define FDT_PCI_IRQ_MAP_DESCS   6

struct BostonState {
    SysBusDevice parent_obj;

    MachineState *mach;
    MIPSCPSState cps;
    SerialMM *uart;
    Clock *cpuclk;

    CharBackend lcd_display;
    char lcd_content[8];
    bool lcd_inited;

    hwaddr kernel_entry;
    hwaddr fdt_base;
};

enum {
    BOSTON_LOWDDR,
    BOSTON_PCIE0,
    BOSTON_PCIE1,
    BOSTON_PCIE2,
    BOSTON_PCIE2_MMIO,
    BOSTON_CM,
    BOSTON_GIC,
    BOSTON_CDMM,
    BOSTON_CPC,
    BOSTON_PLATREG,
    BOSTON_UART,
    BOSTON_LCD,
    BOSTON_FLASH,
    BOSTON_PCIE1_MMIO,
    BOSTON_PCIE0_MMIO,
    BOSTON_HIGHDDR,
};

static const MemMapEntry boston_memmap[] = {
    [BOSTON_LOWDDR] =     {        0x0,    0x10000000 },
    [BOSTON_PCIE0] =      { 0x10000000,     0x2000000 },
    [BOSTON_PCIE1] =      { 0x12000000,     0x2000000 },
    [BOSTON_PCIE2] =      { 0x14000000,     0x2000000 },
    [BOSTON_PCIE2_MMIO] = { 0x16000000,      0x100000 },
    [BOSTON_CM] =         { 0x16100000,       0x20000 },
    [BOSTON_GIC] =        { 0x16120000,       0x20000 },
    [BOSTON_CDMM] =       { 0x16140000,        0x8000 },
    [BOSTON_CPC] =        { 0x16200000,        0x8000 },
    [BOSTON_PLATREG] =    { 0x17ffd000,        0x1000 },
    [BOSTON_UART] =       { 0x17ffe000,          0x20 },
    [BOSTON_LCD] =        { 0x17fff000,           0x8 },
    [BOSTON_FLASH] =      { 0x18000000,     0x8000000 },
    [BOSTON_PCIE1_MMIO] = { 0x20000000,    0x20000000 },
    [BOSTON_PCIE0_MMIO] = { 0x40000000,    0x40000000 },
    [BOSTON_HIGHDDR] =    { 0x80000000,           0x0 },
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
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static uint64_t boston_platreg_read(void *opaque, hwaddr addr,
                                    unsigned size)
{
    BostonState *s = opaque;
    uint32_t gic_freq, val;

    if (size != 4) {
        qemu_log_mask(LOG_UNIMP, "%uB platform register read\n", size);
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
        gic_freq = mips_gictimer_get_freq(s->cps.gic.gic_timer) / 1000000;
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
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void mips_boston_instance_init(Object *obj)
{
    BostonState *s = BOSTON(obj);

    s->cpuclk = qdev_init_clock_out(DEVICE(obj), "cpu-refclk");
    clock_set_hz(s->cpuclk, 1000000000); /* 1 GHz */
}

static const TypeInfo boston_device = {
    .name          = TYPE_BOSTON,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BostonState),
    .instance_init = mips_boston_instance_init,
};

static void boston_register_types(void)
{
    type_register_static(&boston_device);
}
type_init(boston_register_types)

static void gen_firmware(uint32_t *p, hwaddr kernel_entry, hwaddr fdt_addr)
{
    uint64_t regaddr;

    /* Move CM GCRs */
    regaddr = cpu_mips_phys_to_kseg1(NULL, GCR_BASE_ADDR + GCR_BASE_OFS),
    bl_gen_write_ulong(&p, regaddr,
                       boston_memmap[BOSTON_CM].base);

    /* Move & enable GIC GCRs */
    regaddr = cpu_mips_phys_to_kseg1(NULL, boston_memmap[BOSTON_CM].base
                                           + GCR_GIC_BASE_OFS),
    bl_gen_write_ulong(&p, regaddr,
                       boston_memmap[BOSTON_GIC].base | GCR_GIC_BASE_GICEN_MSK);

    /* Move & enable CPC GCRs */
    regaddr = cpu_mips_phys_to_kseg1(NULL, boston_memmap[BOSTON_CM].base
                                           + GCR_CPC_BASE_OFS),
    bl_gen_write_ulong(&p, regaddr,
                       boston_memmap[BOSTON_CPC].base | GCR_CPC_BASE_CPCEN_MSK);

    /*
     * Setup argument registers to follow the UHI boot protocol:
     *
     * a0/$4 = -2
     * a1/$5 = virtual address of FDT
     * a2/$6 = 0
     * a3/$7 = 0
     */
    bl_gen_jump_kernel(&p,
                       true, 0, true, (int32_t)-2,
                       true, fdt_addr, true, 0, true, 0,
                       kernel_entry);
}

static const void *boston_fdt_filter(void *opaque, const void *fdt_orig,
                                     const void *match_data, hwaddr *load_addr)
{
    BostonState *s = BOSTON(opaque);
    MachineState *machine = s->mach;
    const char *cmdline;
    int err;
    size_t ram_low_sz, ram_high_sz;
    size_t fdt_sz = fdt_totalsize(fdt_orig) * 2;
    g_autofree void *fdt = g_malloc0(fdt_sz);
    uint8_t rng_seed[32];

    err = fdt_open_into(fdt_orig, fdt, fdt_sz);
    if (err) {
        fprintf(stderr, "unable to open FDT\n");
        return NULL;
    }

    qemu_guest_getrandom_nofail(rng_seed, sizeof(rng_seed));
    qemu_fdt_setprop(fdt, "/chosen", "rng-seed", rng_seed, sizeof(rng_seed));

    cmdline = (machine->kernel_cmdline && machine->kernel_cmdline[0])
            ? machine->kernel_cmdline : " ";
    err = qemu_fdt_setprop_string(fdt, "/chosen", "bootargs", cmdline);
    if (err < 0) {
        fprintf(stderr, "couldn't set /chosen/bootargs\n");
        return NULL;
    }

    ram_low_sz = MIN(256 * MiB, machine->ram_size);
    ram_high_sz = machine->ram_size - ram_low_sz;
    qemu_fdt_setprop_sized_cells(fdt, "/memory@0", "reg",
                        1, boston_memmap[BOSTON_LOWDDR].base, 1, ram_low_sz,
                        1, boston_memmap[BOSTON_HIGHDDR].base + ram_low_sz,
                        1, ram_high_sz);

    fdt = g_realloc(fdt, fdt_totalsize(fdt));
    qemu_fdt_dumpdtb(fdt, fdt_sz);

    s->fdt_base = *load_addr;

    return g_steal_pointer(&fdt);
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


static void fdt_create_pcie(void *fdt, int gic_ph, int irq, hwaddr reg_base,
                            hwaddr reg_size, hwaddr mmio_base, hwaddr mmio_size)
{
    int i;
    char *name, *intc_name;
    uint32_t intc_ph;
    uint32_t interrupt_map[FDT_PCI_IRQ_MAP_PINS][FDT_PCI_IRQ_MAP_DESCS];

    intc_ph = qemu_fdt_alloc_phandle(fdt);
    name = g_strdup_printf("/soc/pci@%" HWADDR_PRIx, reg_base);
    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible",
                            "xlnx,axi-pcie-host-1.00.a");
    qemu_fdt_setprop_string(fdt, name, "device_type", "pci");
    qemu_fdt_setprop_cells(fdt, name, "reg", reg_base, reg_size);

    qemu_fdt_setprop_cell(fdt, name, "#address-cells", 3);
    qemu_fdt_setprop_cell(fdt, name, "#size-cells", 2);
    qemu_fdt_setprop_cell(fdt, name, "#interrupt-cells", 1);

    qemu_fdt_setprop_cell(fdt, name, "interrupt-parent", gic_ph);
    qemu_fdt_setprop_cells(fdt, name, "interrupts", FDT_GIC_SHARED, irq,
                            FDT_IRQ_TYPE_LEVEL_HIGH);

    qemu_fdt_setprop_cells(fdt, name, "ranges", 0x02000000, 0, mmio_base,
                            mmio_base, 0, mmio_size);
    qemu_fdt_setprop_cells(fdt, name, "bus-range", 0x00, 0xff);



    intc_name = g_strdup_printf("%s/interrupt-controller", name);
    qemu_fdt_add_subnode(fdt, intc_name);
    qemu_fdt_setprop(fdt, intc_name, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop_cell(fdt, intc_name, "#address-cells", 0);
    qemu_fdt_setprop_cell(fdt, intc_name, "#interrupt-cells", 1);
    qemu_fdt_setprop_cell(fdt, intc_name, "phandle", intc_ph);

    qemu_fdt_setprop_cells(fdt, name, "interrupt-map-mask", 0, 0, 0, 7);
    for (i = 0; i < FDT_PCI_IRQ_MAP_PINS; i++) {
        uint32_t *irqmap = interrupt_map[i];

        irqmap[0] = cpu_to_be32(0);
        irqmap[1] = cpu_to_be32(0);
        irqmap[2] = cpu_to_be32(0);
        irqmap[3] = cpu_to_be32(i + 1);
        irqmap[4] = cpu_to_be32(intc_ph);
        irqmap[5] = cpu_to_be32(i + 1);
    }
    qemu_fdt_setprop(fdt, name, "interrupt-map",
                     &interrupt_map, sizeof(interrupt_map));

    g_free(intc_name);
    g_free(name);
}

static const void *create_fdt(BostonState *s,
                              const MemMapEntry *memmap, int *dt_size)
{
    void *fdt;
    int cpu;
    MachineState *mc = s->mach;
    uint32_t platreg_ph, gic_ph, clk_ph;
    char *name, *gic_name, *platreg_name, *stdout_name;
    static const char * const syscon_compat[2] = {
        "img,boston-platform-regs", "syscon"
    };

    fdt = create_device_tree(dt_size);
    if (!fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }

    platreg_ph = qemu_fdt_alloc_phandle(fdt);
    gic_ph = qemu_fdt_alloc_phandle(fdt);
    clk_ph = qemu_fdt_alloc_phandle(fdt);

    qemu_fdt_setprop_string(fdt, "/", "model", "img,boston");
    qemu_fdt_setprop_string(fdt, "/", "compatible", "img,boston");
    qemu_fdt_setprop_cell(fdt, "/", "#size-cells", 0x1);
    qemu_fdt_setprop_cell(fdt, "/", "#address-cells", 0x1);


    qemu_fdt_add_subnode(fdt, "/cpus");
    qemu_fdt_setprop_cell(fdt, "/cpus", "#size-cells", 0x0);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#address-cells", 0x1);

    for (cpu = 0; cpu < mc->smp.cpus; cpu++) {
        name = g_strdup_printf("/cpus/cpu@%d", cpu);
        qemu_fdt_add_subnode(fdt, name);
        qemu_fdt_setprop_string(fdt, name, "compatible", "img,mips");
        qemu_fdt_setprop_string(fdt, name, "status", "okay");
        qemu_fdt_setprop_cell(fdt, name, "reg", cpu);
        qemu_fdt_setprop_string(fdt, name, "device_type", "cpu");
        qemu_fdt_setprop_cells(fdt, name, "clocks", clk_ph, FDT_BOSTON_CLK_CPU);
        g_free(name);
    }

    qemu_fdt_add_subnode(fdt, "/soc");
    qemu_fdt_setprop(fdt, "/soc", "ranges", NULL, 0);
    qemu_fdt_setprop_string(fdt, "/soc", "compatible", "simple-bus");
    qemu_fdt_setprop_cell(fdt, "/soc", "#size-cells", 0x1);
    qemu_fdt_setprop_cell(fdt, "/soc", "#address-cells", 0x1);

    fdt_create_pcie(fdt, gic_ph, 2,
                memmap[BOSTON_PCIE0].base, memmap[BOSTON_PCIE0].size,
                memmap[BOSTON_PCIE0_MMIO].base, memmap[BOSTON_PCIE0_MMIO].size);

    fdt_create_pcie(fdt, gic_ph, 1,
                memmap[BOSTON_PCIE1].base, memmap[BOSTON_PCIE1].size,
                memmap[BOSTON_PCIE1_MMIO].base, memmap[BOSTON_PCIE1_MMIO].size);

    fdt_create_pcie(fdt, gic_ph, 0,
                memmap[BOSTON_PCIE2].base, memmap[BOSTON_PCIE2].size,
                memmap[BOSTON_PCIE2_MMIO].base, memmap[BOSTON_PCIE2_MMIO].size);

    /* GIC with it's timer node */
    gic_name = g_strdup_printf("/soc/interrupt-controller@%" HWADDR_PRIx,
                                memmap[BOSTON_GIC].base);
    qemu_fdt_add_subnode(fdt, gic_name);
    qemu_fdt_setprop_string(fdt, gic_name, "compatible", "mti,gic");
    qemu_fdt_setprop_cells(fdt, gic_name, "reg", memmap[BOSTON_GIC].base,
                            memmap[BOSTON_GIC].size);
    qemu_fdt_setprop(fdt, gic_name, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop_cell(fdt, gic_name, "#interrupt-cells", 3);
    qemu_fdt_setprop_cell(fdt, gic_name, "phandle", gic_ph);

    name = g_strdup_printf("%s/timer", gic_name);
    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", "mti,gic-timer");
    qemu_fdt_setprop_cells(fdt, name, "interrupts", FDT_GIC_LOCAL, 1,
                            FDT_IRQ_TYPE_NONE);
    qemu_fdt_setprop_cells(fdt, name, "clocks", clk_ph, FDT_BOSTON_CLK_CPU);
    g_free(name);
    g_free(gic_name);

    /* CDMM node */
    name = g_strdup_printf("/soc/cdmm@%" HWADDR_PRIx, memmap[BOSTON_CDMM].base);
    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", "mti,mips-cdmm");
    qemu_fdt_setprop_cells(fdt, name, "reg", memmap[BOSTON_CDMM].base,
                            memmap[BOSTON_CDMM].size);
    g_free(name);

    /* CPC node */
    name = g_strdup_printf("/soc/cpc@%" HWADDR_PRIx, memmap[BOSTON_CPC].base);
    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", "mti,mips-cpc");
    qemu_fdt_setprop_cells(fdt, name, "reg", memmap[BOSTON_CPC].base,
                            memmap[BOSTON_CPC].size);
    g_free(name);

    /* platreg and it's clk node */
    platreg_name = g_strdup_printf("/soc/system-controller@%" HWADDR_PRIx,
                                   memmap[BOSTON_PLATREG].base);
    qemu_fdt_add_subnode(fdt, platreg_name);
    qemu_fdt_setprop_string_array(fdt, platreg_name, "compatible",
                                 (char **)&syscon_compat,
                                 ARRAY_SIZE(syscon_compat));
    qemu_fdt_setprop_cells(fdt, platreg_name, "reg",
                           memmap[BOSTON_PLATREG].base,
                           memmap[BOSTON_PLATREG].size);
    qemu_fdt_setprop_cell(fdt, platreg_name, "phandle", platreg_ph);

    name = g_strdup_printf("%s/clock", platreg_name);
    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", "img,boston-clock");
    qemu_fdt_setprop_cell(fdt, name, "#clock-cells", 1);
    qemu_fdt_setprop_cell(fdt, name, "phandle", clk_ph);
    g_free(name);
    g_free(platreg_name);

    /* reboot node */
    name = g_strdup_printf("/soc/reboot");
    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", "syscon-reboot");
    qemu_fdt_setprop_cell(fdt, name, "regmap", platreg_ph);
    qemu_fdt_setprop_cell(fdt, name, "offset", 0x10);
    qemu_fdt_setprop_cell(fdt, name, "mask", 0x10);
    g_free(name);

    /* uart node */
    name = g_strdup_printf("/soc/uart@%" HWADDR_PRIx, memmap[BOSTON_UART].base);
    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", "ns16550a");
    qemu_fdt_setprop_cells(fdt, name, "reg", memmap[BOSTON_UART].base,
                            memmap[BOSTON_UART].size);
    qemu_fdt_setprop_cell(fdt, name, "reg-shift", 0x2);
    qemu_fdt_setprop_cell(fdt, name, "interrupt-parent", gic_ph);
    qemu_fdt_setprop_cells(fdt, name, "interrupts", FDT_GIC_SHARED, 3,
                            FDT_IRQ_TYPE_LEVEL_HIGH);
    qemu_fdt_setprop_cells(fdt, name, "clocks", clk_ph, FDT_BOSTON_CLK_SYS);

    qemu_fdt_add_subnode(fdt, "/chosen");
    stdout_name = g_strdup_printf("%s:115200", name);
    qemu_fdt_setprop_string(fdt, "/chosen", "stdout-path", stdout_name);
    g_free(stdout_name);
    g_free(name);

    /* lcd node */
    name = g_strdup_printf("/soc/lcd@%" HWADDR_PRIx, memmap[BOSTON_LCD].base);
    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", "img,boston-lcd");
    qemu_fdt_setprop_cells(fdt, name, "reg", memmap[BOSTON_LCD].base,
                            memmap[BOSTON_LCD].size);
    g_free(name);

    name = g_strdup_printf("/memory@0");
    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "device_type", "memory");
    g_free(name);

    return fdt;
}

static void boston_mach_init(MachineState *machine)
{
    DeviceState *dev;
    BostonState *s;
    MemoryRegion *flash, *ddr_low_alias, *lcd, *platreg;
    MemoryRegion *sys_mem = get_system_memory();
    XilinxPCIEHost *pcie2;
    PCIDevice *ahci;
    DriveInfo *hd[6];
    Chardev *chr;
    int fw_size, fit_err;

    if ((machine->ram_size % GiB) ||
        (machine->ram_size > (2 * GiB))) {
        error_report("Memory size must be 1GB or 2GB");
        exit(1);
    }

    dev = qdev_new(TYPE_BOSTON);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    s = BOSTON(dev);
    s->mach = machine;

    if (!cpu_type_supports_cps_smp(machine->cpu_type)) {
        error_report("Boston requires CPUs which support CPS");
        exit(1);
    }

    object_initialize_child(OBJECT(machine), "cps", &s->cps, TYPE_MIPS_CPS);
    object_property_set_str(OBJECT(&s->cps), "cpu-type", machine->cpu_type,
                            &error_fatal);
    object_property_set_int(OBJECT(&s->cps), "num-vp", machine->smp.cpus,
                            &error_fatal);
    qdev_connect_clock_in(DEVICE(&s->cps), "clk-in",
                          qdev_get_clock_out(dev, "cpu-refclk"));
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

    xilinx_pcie_init(sys_mem, 0,
                     boston_memmap[BOSTON_PCIE0].base,
                     boston_memmap[BOSTON_PCIE0].size,
                     boston_memmap[BOSTON_PCIE0_MMIO].base,
                     boston_memmap[BOSTON_PCIE0_MMIO].size,
                     get_cps_irq(&s->cps, 2));

    xilinx_pcie_init(sys_mem, 1,
                     boston_memmap[BOSTON_PCIE1].base,
                     boston_memmap[BOSTON_PCIE1].size,
                     boston_memmap[BOSTON_PCIE1_MMIO].base,
                     boston_memmap[BOSTON_PCIE1_MMIO].size,
                     get_cps_irq(&s->cps, 1));

    pcie2 = xilinx_pcie_init(sys_mem, 2,
                             boston_memmap[BOSTON_PCIE2].base,
                             boston_memmap[BOSTON_PCIE2].size,
                             boston_memmap[BOSTON_PCIE2_MMIO].base,
                             boston_memmap[BOSTON_PCIE2_MMIO].size,
                             get_cps_irq(&s->cps, 0));

    platreg = g_new(MemoryRegion, 1);
    memory_region_init_io(platreg, NULL, &boston_platreg_ops, s,
                          "boston-platregs",
                          boston_memmap[BOSTON_PLATREG].size);
    memory_region_add_subregion_overlap(sys_mem,
                          boston_memmap[BOSTON_PLATREG].base, platreg, 0);

    s->uart = serial_mm_init(sys_mem, boston_memmap[BOSTON_UART].base, 2,
                             get_cps_irq(&s->cps, 3), 10000000,
                             serial_hd(0), DEVICE_NATIVE_ENDIAN);

    lcd = g_new(MemoryRegion, 1);
    memory_region_init_io(lcd, NULL, &boston_lcd_ops, s, "boston-lcd", 0x8);
    memory_region_add_subregion_overlap(sys_mem,
                                        boston_memmap[BOSTON_LCD].base, lcd, 0);

    chr = qemu_chr_new("lcd", "vc:320x240", NULL);
    qemu_chr_fe_init(&s->lcd_display, chr, NULL);
    qemu_chr_fe_set_handlers(&s->lcd_display, NULL, NULL,
                             boston_lcd_event, NULL, s, NULL, true);

    ahci = pci_create_simple_multifunction(&PCI_BRIDGE(&pcie2->root)->sec_bus,
                                           PCI_DEVFN(0, 0),
                                           true, TYPE_ICH9_AHCI);
    g_assert(ARRAY_SIZE(hd) == ahci_get_num_ports(ahci));
    ide_drive_get(hd, ahci_get_num_ports(ahci));
    ahci_ide_create_devs(ahci, hd);

    if (machine->firmware) {
        fw_size = load_image_targphys(machine->firmware,
                                      0x1fc00000, 4 * MiB);
        if (fw_size == -1) {
            error_report("unable to load firmware image '%s'",
                          machine->firmware);
            exit(1);
        }
    } else if (machine->kernel_filename) {
        uint64_t kernel_entry, kernel_high;
        ssize_t kernel_size;

        kernel_size = load_elf(machine->kernel_filename, NULL,
                           cpu_mips_kseg0_to_phys, NULL,
                           &kernel_entry, NULL, &kernel_high,
                           NULL, 0, EM_MIPS, 1, 0);

        if (kernel_size > 0) {
            int dt_size;
            g_autofree const void *dtb_file_data = NULL;
            g_autofree const void *dtb_load_data = NULL;
            hwaddr dtb_paddr = QEMU_ALIGN_UP(kernel_high, 64 * KiB);
            hwaddr dtb_vaddr = cpu_mips_phys_to_kseg0(NULL, dtb_paddr);

            s->kernel_entry = kernel_entry;
            if (machine->dtb) {
                dtb_file_data = load_device_tree(machine->dtb, &dt_size);
            } else {
                dtb_file_data = create_fdt(s, boston_memmap, &dt_size);
            }

            dtb_load_data = boston_fdt_filter(s, dtb_file_data,
                                              NULL, &dtb_vaddr);

            /* Calculate real fdt size after filter */
            dt_size = fdt_totalsize(dtb_load_data);
            rom_add_blob_fixed("dtb", dtb_load_data, dt_size, dtb_paddr);
            qemu_register_reset_nosnapshotload(qemu_fdt_randomize_seeds,
                                rom_ptr(dtb_paddr, dt_size));
        } else {
            /* Try to load file as FIT */
            fit_err = load_fit(&boston_fit_loader, machine->kernel_filename, s);
            if (fit_err) {
                error_report("unable to load kernel image");
                exit(1);
            }
        }

        gen_firmware(memory_region_get_ram_ptr(flash) + 0x7c00000,
                     s->kernel_entry, s->fdt_base);
    } else if (!qtest_enabled()) {
        error_report("Please provide either a -kernel or -bios argument");
        exit(1);
    }
}

static void boston_mach_class_init(MachineClass *mc)
{
    mc->desc = "MIPS Boston";
    mc->init = boston_mach_init;
    mc->block_default_type = IF_IDE;
    mc->default_ram_size = 1 * GiB;
    mc->default_ram_id = "boston.ddr";
    mc->max_cpus = 16;
    mc->default_cpu_type = MIPS_CPU_TYPE_NAME("I6400");
}

DEFINE_MACHINE("boston", boston_mach_class_init)
