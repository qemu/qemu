/*
 * Board-construction helpers shared by the Hexagon machines that describe
 * themselves to the guest with a generated device tree.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "elf.h"
#include "hw/char/pl011.h"
#include "hw/core/boards.h"
#include "hw/core/clock.h"
#include "hw/core/loader.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "hw/hexagon/hex-board.h"
#include "hw/hexagon/hex-subsys.h"
#include "qemu/error-report.h"
#include "qemu/guest-random.h"
#include "system/address-spaces.h"
#include "system/device_tree.h"
#include "system/reset.h"
#include "system/system.h"
#include "target/hexagon/cpu.h"

#define APB_CLK_HZ 24000000

void hex_board_create_fdt(MachineState *ms, const char *model,
                          const char *compatible, int *fdt_size)
{
    void *fdt = create_device_tree(fdt_size);
    uint8_t rng_seed[32];

    if (!fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }

    ms->fdt = fdt;

    qemu_fdt_setprop_cell(fdt, "/", "#address-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/", "#size-cells", 0x1);
    qemu_fdt_setprop_string(fdt, "/", "model", model);
    qemu_fdt_setprop_string(fdt, "/", "compatible", compatible);

    qemu_fdt_add_subnode(fdt, "/soc");
    qemu_fdt_setprop_cell(fdt, "/soc", "#address-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/soc", "#size-cells", 0x1);
    qemu_fdt_setprop(fdt, "/soc", "ranges", NULL, 0);

    qemu_fdt_add_subnode(fdt, "/chosen");
    qemu_guest_getrandom_nofail(rng_seed, sizeof(rng_seed));
    qemu_fdt_setprop(fdt, "/chosen", "rng-seed", rng_seed, sizeof(rng_seed));
}

static void fdt_set_irq(MachineState *ms, const char *nodename, int irq)
{
    qemu_fdt_setprop_cells(ms->fdt, nodename, "interrupts",
                           HEX_BOARD_L2VIC_SPI_BASE + irq, 0);
}

int32_t hex_board_fdt_add_l2vic(MachineState *ms,
                                const struct hexagon_machine_config *cfg)
{
    int32_t l2vic_phandle = qemu_fdt_alloc_phandle(ms->fdt);
    g_autofree char *nodename = g_strdup_printf("/soc/interrupt-controller@%x",
                                                cfg->l2vic_base);
    static const char compat[] = "qcom,h2-pic\0hvm-pic";

    qemu_fdt_setprop_cell(ms->fdt, "/soc", "interrupt-parent", l2vic_phandle);

    qemu_fdt_add_subnode(ms->fdt, nodename);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "#address-cells", 0x0);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "#interrupt-cells", 2);
    qemu_fdt_setprop(ms->fdt, nodename, "compatible", compat, sizeof(compat));
    qemu_fdt_setprop_cells(ms->fdt, nodename, "reg", 0, cfg->l2vic_base,
                           cfg->l2vic_size);
    qemu_fdt_setprop(ms->fdt, nodename, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "phandle", l2vic_phandle);

    return l2vic_phandle;
}

void hex_board_fdt_add_hvx(MachineState *ms,
                           const struct hexagon_machine_config *cfg)
{
    const union hexagon_config_table *t = &cfg->cfgtable;

    if (t->vtcm_size_kb > 0) {
        qemu_fdt_add_subnode(ms->fdt, "/soc/vtcm");
        qemu_fdt_setprop_string(ms->fdt, "/soc/vtcm", "compatible",
                                "qcom,hexagon_vtcm");
        qemu_fdt_setprop_cells(ms->fdt, "/soc/vtcm", "reg", 0,
                               t->vtcm_base << 16, t->vtcm_size_kb * 1024);
    }

    if (t->ext_contexts > 0) {
        qemu_fdt_add_subnode(ms->fdt, "/soc/hvx");
        qemu_fdt_setprop_string(ms->fdt, "/soc/hvx", "compatible",
                                "qcom,hexagon-hvx");
        qemu_fdt_setprop_cells(ms->fdt, "/soc/hvx", "qcom,hvx-max-ctxts",
                               t->ext_contexts);
        qemu_fdt_setprop_cells(ms->fdt, "/soc/hvx", "qcom,hvx-vlength",
                               t->hvx_vec_log_length);
    }
}

void hex_board_fdt_add_cpus(MachineState *ms)
{
    qemu_fdt_add_subnode(ms->fdt, "/cpus");
    qemu_fdt_setprop_cell(ms->fdt, "/cpus", "#address-cells", 0x1);
    qemu_fdt_setprop_cell(ms->fdt, "/cpus", "#size-cells", 0x0);

    for (int num = ms->smp.cpus - 1; num >= 0; num--) {
        g_autofree char *nodename = g_strdup_printf("/cpus/cpu@%d", num);

        qemu_fdt_add_subnode(ms->fdt, nodename);
        qemu_fdt_setprop_string(ms->fdt, nodename, "device_type", "cpu");
        qemu_fdt_setprop_cell(ms->fdt, nodename, "reg", num);
        qemu_fdt_setprop_cell(ms->fdt, nodename, "phandle",
                              qemu_fdt_alloc_phandle(ms->fdt));
    }
}

void hex_board_create_tcm(const struct hexagon_machine_config *cfg,
                          MemoryRegion *tcm)
{
    if (cfg->l2tcm_size) {
        memory_region_init_ram(tcm, NULL, "tcm.ram", cfg->l2tcm_size,
                               &error_fatal);
        memory_region_add_subregion(get_system_memory(),
                                    (hwaddr)cfg->cfgtable.l2tcm_base << 16,
                                    tcm);
    }
}

void hex_board_create_uart(MachineState *ms, hwaddr base, hwaddr size,
                           int irq, int32_t l2vic_phandle)
{
    static const char compat[] = "arm,pl011\0arm,primecell";
    static const char clocknames[] = "uartclk\0apb_pclk";
    HexagonCommonMachineState *hms = HEXAGON_COMMON_MACHINE(ms);
    int32_t clk_phandle = qemu_fdt_alloc_phandle(ms->fdt);
    g_autofree char *nodename = g_strdup_printf("/pl011@%" PRIx64, base);
    Clock *apb_clk = clock_new(OBJECT(ms), "apb-pclk");
    DeviceState *dev = qdev_new(TYPE_PL011);
    SysBusDevice *s = SYS_BUS_DEVICE(dev);

    clock_set_hz(apb_clk, APB_CLK_HZ);

    qdev_prop_set_chr(dev, "chardev", serial_hd(0));
    qdev_connect_clock_in(dev, "clk", apb_clk);
    sysbus_realize_and_unref(s, &error_fatal);
    sysbus_mmio_map(s, 0, base);
    sysbus_connect_irq(s, 0, qdev_get_gpio_in(hms->l2vic, irq));

    qemu_fdt_add_subnode(ms->fdt, "/apb-pclk");
    qemu_fdt_setprop_string(ms->fdt, "/apb-pclk", "compatible", "fixed-clock");
    qemu_fdt_setprop_cell(ms->fdt, "/apb-pclk", "#clock-cells", 0x0);
    qemu_fdt_setprop_cell(ms->fdt, "/apb-pclk", "clock-frequency", APB_CLK_HZ);
    qemu_fdt_setprop_string(ms->fdt, "/apb-pclk", "clock-output-names",
                            "clk24mhz");
    qemu_fdt_setprop_cell(ms->fdt, "/apb-pclk", "phandle", clk_phandle);

    qemu_fdt_add_subnode(ms->fdt, nodename);
    /* Can't use setprop_string because of the embedded NUL */
    qemu_fdt_setprop(ms->fdt, nodename, "compatible", compat, sizeof(compat));
    qemu_fdt_setprop_cells(ms->fdt, nodename, "reg", 0, base, size);
    fdt_set_irq(ms, nodename, irq);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "interrupt-parent",
                          l2vic_phandle);
    qemu_fdt_setprop_cells(ms->fdt, nodename, "clocks", clk_phandle,
                           clk_phandle);
    qemu_fdt_setprop(ms->fdt, nodename, "clock-names", clocknames,
                     sizeof(clocknames));

    qemu_fdt_setprop_string(ms->fdt, "/chosen", "stdout-path", nodename);
    qemu_fdt_add_subnode(ms->fdt, "/aliases");
    qemu_fdt_setprop_string(ms->fdt, "/aliases", "serial0", nodename);
}

void hex_board_create_virtio(MachineState *ms, hwaddr base, hwaddr size,
                             int irq_base, int count,
                             int32_t l2vic_phandle)
{
    HexagonCommonMachineState *hms = HEXAGON_COMMON_MACHINE(ms);

    for (int i = 0; i < count; i++) {
        int irq = irq_base + i;
        hwaddr slot = base + i * size;
        g_autofree char *nodename = g_strdup_printf("/soc/virtio_mmio@%" PRIx64,
                                                    slot);
        DeviceState *dev = qdev_new("virtio-mmio");
        SysBusDevice *s = SYS_BUS_DEVICE(dev);

        object_property_add_child(OBJECT(ms), "virtio-mmio[*]", OBJECT(dev));
        sysbus_realize_and_unref(s, &error_fatal);
        sysbus_mmio_map(s, 0, slot);
        sysbus_connect_irq(s, 0, qdev_get_gpio_in(hms->l2vic, irq));

        qemu_fdt_add_subnode(ms->fdt, nodename);
        qemu_fdt_setprop_string(ms->fdt, nodename, "compatible",
                                "virtio,mmio");
        qemu_fdt_setprop_cells(ms->fdt, nodename, "reg", 0, slot, size);
        fdt_set_irq(ms, nodename, irq);
        qemu_fdt_setprop_cell(ms->fdt, nodename, "interrupt-parent",
                              l2vic_phandle);
    }
}

uint64_t hex_board_load_elf(const char *filename,
                            uint64_t (*translate)(void *opaque, uint64_t addr),
                            void *opaque, uint64_t *high)
{
    uint64_t entry = 0;

    if (load_elf_ram_sym(filename, NULL, translate, opaque, &entry, NULL,
                         high, NULL, 0, EM_HEXAGON, 0, 0,
                         &address_space_memory, false, NULL) <= 0) {
        error_report("error loading '%s'", filename);
        exit(1);
    }
    return entry;
}

void hex_board_load_fdt(MachineState *ms, int fdt_size, hwaddr addr)
{
    rom_add_blob_fixed_as("fdt", ms->fdt, fdt_size, addr,
                          &address_space_memory);
    qemu_register_reset_nosnapshotload(
        qemu_fdt_randomize_seeds,
        rom_ptr_for_as(&address_space_memory, addr, fdt_size));
}

static void hex_board_cpu_reset(void *opaque)
{
    cpu_reset(CPU(opaque));
}

void hex_board_create_cpus(MachineState *ms, bool have_start, uint64_t start)
{
    HexagonCommonMachineState *hms = HEXAGON_COMMON_MACHINE(ms);
    g_autofree HexagonCPU **cpus = g_new(HexagonCPU *, ms->smp.cpus);

    for (int i = 0; i < ms->smp.cpus; i++) {
        HexagonCPU *cpu = HEXAGON_CPU(object_new(ms->cpu_type));

        qemu_register_reset(hex_board_cpu_reset, cpu);
        if (i == 0 && have_start) {
            qdev_prop_set_uint32(DEVICE(cpu), "exec-start-addr", start);
        }
        qdev_prop_set_uint32(DEVICE(cpu), "htid", i);
        qdev_prop_set_bit(DEVICE(cpu), "start-powered-off", i != 0);
        hex_subsys_add_cpu(hms, DEVICE(cpu));
        cpus[i] = cpu;
    }

    hex_subsys_realize_cluster(hms);
    for (int i = 0; i < ms->smp.cpus; i++) {
        hex_subsys_realize_cpu(hms, DEVICE(cpus[i]), i == 0);
    }
}
