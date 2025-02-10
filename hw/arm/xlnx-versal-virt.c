/*
 * Xilinx Versal Virtual board.
 *
 * Copyright (c) 2018 Xilinx Inc.
 * Written by Edgar E. Iglesias
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "system/device_tree.h"
#include "hw/block/flash.h"
#include "hw/boards.h"
#include "hw/sysbus.h"
#include "hw/arm/fdt.h"
#include "hw/qdev-properties.h"
#include "hw/arm/xlnx-versal.h"
#include "hw/arm/boot.h"
#include "target/arm/multiprocessing.h"
#include "qom/object.h"

#define TYPE_XLNX_VERSAL_VIRT_MACHINE MACHINE_TYPE_NAME("xlnx-versal-virt")
OBJECT_DECLARE_SIMPLE_TYPE(VersalVirt, XLNX_VERSAL_VIRT_MACHINE)

#define XLNX_VERSAL_NUM_OSPI_FLASH 4

struct VersalVirt {
    MachineState parent_obj;

    Versal soc;

    void *fdt;
    int fdt_size;
    struct {
        uint32_t gic;
        uint32_t ethernet_phy[2];
        uint32_t clk_125Mhz;
        uint32_t clk_25Mhz;
        uint32_t usb;
        uint32_t dwc;
        uint32_t canfd[2];
    } phandle;
    struct arm_boot_info binfo;

    CanBusState *canbus[XLNX_VERSAL_NR_CANFD];
    struct {
        bool secure;
    } cfg;
    char *ospi_model;
};

static void fdt_create(VersalVirt *s)
{
    MachineClass *mc = MACHINE_GET_CLASS(s);
    int i;

    s->fdt = create_device_tree(&s->fdt_size);
    if (!s->fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }

    /* Allocate all phandles.  */
    s->phandle.gic = qemu_fdt_alloc_phandle(s->fdt);
    for (i = 0; i < ARRAY_SIZE(s->phandle.ethernet_phy); i++) {
        s->phandle.ethernet_phy[i] = qemu_fdt_alloc_phandle(s->fdt);
    }
    s->phandle.clk_25Mhz = qemu_fdt_alloc_phandle(s->fdt);
    s->phandle.clk_125Mhz = qemu_fdt_alloc_phandle(s->fdt);

    s->phandle.usb = qemu_fdt_alloc_phandle(s->fdt);
    s->phandle.dwc = qemu_fdt_alloc_phandle(s->fdt);
    /* Create /chosen node for load_dtb.  */
    qemu_fdt_add_subnode(s->fdt, "/chosen");

    /* Header */
    qemu_fdt_setprop_cell(s->fdt, "/", "interrupt-parent", s->phandle.gic);
    qemu_fdt_setprop_cell(s->fdt, "/", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(s->fdt, "/", "#address-cells", 0x2);
    qemu_fdt_setprop_string(s->fdt, "/", "model", mc->desc);
    qemu_fdt_setprop_string(s->fdt, "/", "compatible", "xlnx-versal-virt");
}

static void fdt_add_clk_node(VersalVirt *s, const char *name,
                             unsigned int freq_hz, uint32_t phandle)
{
    qemu_fdt_add_subnode(s->fdt, name);
    qemu_fdt_setprop_cell(s->fdt, name, "phandle", phandle);
    qemu_fdt_setprop_cell(s->fdt, name, "clock-frequency", freq_hz);
    qemu_fdt_setprop_cell(s->fdt, name, "#clock-cells", 0x0);
    qemu_fdt_setprop_string(s->fdt, name, "compatible", "fixed-clock");
    qemu_fdt_setprop(s->fdt, name, "u-boot,dm-pre-reloc", NULL, 0);
}

static void fdt_add_cpu_nodes(VersalVirt *s, uint32_t psci_conduit)
{
    int i;

    qemu_fdt_add_subnode(s->fdt, "/cpus");
    qemu_fdt_setprop_cell(s->fdt, "/cpus", "#size-cells", 0x0);
    qemu_fdt_setprop_cell(s->fdt, "/cpus", "#address-cells", 1);

    for (i = XLNX_VERSAL_NR_ACPUS - 1; i >= 0; i--) {
        char *name = g_strdup_printf("/cpus/cpu@%d", i);
        ARMCPU *armcpu = ARM_CPU(qemu_get_cpu(i));

        qemu_fdt_add_subnode(s->fdt, name);
        qemu_fdt_setprop_cell(s->fdt, name, "reg",
                              arm_cpu_mp_affinity(armcpu));
        if (psci_conduit != QEMU_PSCI_CONDUIT_DISABLED) {
            qemu_fdt_setprop_string(s->fdt, name, "enable-method", "psci");
        }
        qemu_fdt_setprop_string(s->fdt, name, "device_type", "cpu");
        qemu_fdt_setprop_string(s->fdt, name, "compatible",
                                armcpu->dtb_compatible);
        g_free(name);
    }
}

static void fdt_add_gic_nodes(VersalVirt *s)
{
    char *nodename;

    nodename = g_strdup_printf("/gic@%x", MM_GIC_APU_DIST_MAIN);
    qemu_fdt_add_subnode(s->fdt, nodename);
    qemu_fdt_setprop_cell(s->fdt, nodename, "phandle", s->phandle.gic);
    qemu_fdt_setprop_cells(s->fdt, nodename, "interrupts",
                           GIC_FDT_IRQ_TYPE_PPI, VERSAL_GIC_MAINT_IRQ,
                           GIC_FDT_IRQ_FLAGS_LEVEL_HI);
    qemu_fdt_setprop(s->fdt, nodename, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop_sized_cells(s->fdt, nodename, "reg",
                                 2, MM_GIC_APU_DIST_MAIN,
                                 2, MM_GIC_APU_DIST_MAIN_SIZE,
                                 2, MM_GIC_APU_REDIST_0,
                                 2, MM_GIC_APU_REDIST_0_SIZE);
    qemu_fdt_setprop_cell(s->fdt, nodename, "#interrupt-cells", 3);
    qemu_fdt_setprop_string(s->fdt, nodename, "compatible", "arm,gic-v3");
    g_free(nodename);
}

static void fdt_add_timer_nodes(VersalVirt *s)
{
    const char compat[] = "arm,armv8-timer";
    uint32_t irqflags = GIC_FDT_IRQ_FLAGS_LEVEL_HI;

    qemu_fdt_add_subnode(s->fdt, "/timer");
    qemu_fdt_setprop_cells(s->fdt, "/timer", "interrupts",
            GIC_FDT_IRQ_TYPE_PPI, VERSAL_TIMER_S_EL1_IRQ, irqflags,
            GIC_FDT_IRQ_TYPE_PPI, VERSAL_TIMER_NS_EL1_IRQ, irqflags,
            GIC_FDT_IRQ_TYPE_PPI, VERSAL_TIMER_VIRT_IRQ, irqflags,
            GIC_FDT_IRQ_TYPE_PPI, VERSAL_TIMER_NS_EL2_IRQ, irqflags);
    qemu_fdt_setprop(s->fdt, "/timer", "compatible",
                     compat, sizeof(compat));
}

static void fdt_add_usb_xhci_nodes(VersalVirt *s)
{
    const char clocknames[] = "bus_clk\0ref_clk";
    const char irq_name[] = "dwc_usb3";
    const char compatVersalDWC3[] = "xlnx,versal-dwc3";
    const char compatDWC3[] = "snps,dwc3";
    char *name = g_strdup_printf("/usb@%" PRIx32, MM_USB2_CTRL_REGS);

    qemu_fdt_add_subnode(s->fdt, name);
    qemu_fdt_setprop(s->fdt, name, "compatible",
                         compatVersalDWC3, sizeof(compatVersalDWC3));
    qemu_fdt_setprop_sized_cells(s->fdt, name, "reg",
                                 2, MM_USB2_CTRL_REGS,
                                 2, MM_USB2_CTRL_REGS_SIZE);
    qemu_fdt_setprop(s->fdt, name, "clock-names",
                         clocknames, sizeof(clocknames));
    qemu_fdt_setprop_cells(s->fdt, name, "clocks",
                               s->phandle.clk_25Mhz, s->phandle.clk_125Mhz);
    qemu_fdt_setprop(s->fdt, name, "ranges", NULL, 0);
    qemu_fdt_setprop_cell(s->fdt, name, "#address-cells", 2);
    qemu_fdt_setprop_cell(s->fdt, name, "#size-cells", 2);
    qemu_fdt_setprop_cell(s->fdt, name, "phandle", s->phandle.usb);
    g_free(name);

    name = g_strdup_printf("/usb@%" PRIx32 "/dwc3@%" PRIx32,
                           MM_USB2_CTRL_REGS, MM_USB_0);
    qemu_fdt_add_subnode(s->fdt, name);
    qemu_fdt_setprop(s->fdt, name, "compatible",
                     compatDWC3, sizeof(compatDWC3));
    qemu_fdt_setprop_sized_cells(s->fdt, name, "reg",
                                 2, MM_USB_0, 2, MM_USB_0_SIZE);
    qemu_fdt_setprop(s->fdt, name, "interrupt-names",
                     irq_name, sizeof(irq_name));
    qemu_fdt_setprop_cells(s->fdt, name, "interrupts",
                               GIC_FDT_IRQ_TYPE_SPI, VERSAL_USB0_IRQ_0,
                               GIC_FDT_IRQ_FLAGS_LEVEL_HI);
    qemu_fdt_setprop_cell(s->fdt, name,
                          "snps,quirk-frame-length-adjustment", 0x20);
    qemu_fdt_setprop_cells(s->fdt, name, "#stream-id-cells", 1);
    qemu_fdt_setprop_string(s->fdt, name, "dr_mode", "host");
    qemu_fdt_setprop_string(s->fdt, name, "phy-names", "usb3-phy");
    qemu_fdt_setprop(s->fdt, name, "snps,dis_u2_susphy_quirk", NULL, 0);
    qemu_fdt_setprop(s->fdt, name, "snps,dis_u3_susphy_quirk", NULL, 0);
    qemu_fdt_setprop(s->fdt, name, "snps,refclk_fladj", NULL, 0);
    qemu_fdt_setprop(s->fdt, name, "snps,mask_phy_reset", NULL, 0);
    qemu_fdt_setprop_cell(s->fdt, name, "phandle", s->phandle.dwc);
    qemu_fdt_setprop_string(s->fdt, name, "maximum-speed", "high-speed");
    g_free(name);
}

static void fdt_add_uart_nodes(VersalVirt *s)
{
    uint64_t addrs[] = { MM_UART1, MM_UART0 };
    unsigned int irqs[] = { VERSAL_UART1_IRQ_0, VERSAL_UART0_IRQ_0 };
    const char compat[] = "arm,pl011\0arm,sbsa-uart";
    const char clocknames[] = "uartclk\0apb_pclk";
    int i;

    for (i = 0; i < ARRAY_SIZE(addrs); i++) {
        char *name = g_strdup_printf("/uart@%" PRIx64, addrs[i]);
        qemu_fdt_add_subnode(s->fdt, name);
        qemu_fdt_setprop_cell(s->fdt, name, "current-speed", 115200);
        qemu_fdt_setprop_cells(s->fdt, name, "clocks",
                               s->phandle.clk_125Mhz, s->phandle.clk_125Mhz);
        qemu_fdt_setprop(s->fdt, name, "clock-names",
                         clocknames, sizeof(clocknames));

        qemu_fdt_setprop_cells(s->fdt, name, "interrupts",
                               GIC_FDT_IRQ_TYPE_SPI, irqs[i],
                               GIC_FDT_IRQ_FLAGS_LEVEL_HI);
        qemu_fdt_setprop_sized_cells(s->fdt, name, "reg",
                                     2, addrs[i], 2, 0x1000);
        qemu_fdt_setprop(s->fdt, name, "compatible",
                         compat, sizeof(compat));
        qemu_fdt_setprop(s->fdt, name, "u-boot,dm-pre-reloc", NULL, 0);

        if (addrs[i] == MM_UART0) {
            /* Select UART0.  */
            qemu_fdt_setprop_string(s->fdt, "/chosen", "stdout-path", name);
        }
        g_free(name);
    }
}

static void fdt_add_canfd_nodes(VersalVirt *s)
{
    uint64_t addrs[] = { MM_CANFD1, MM_CANFD0 };
    uint32_t size[] = { MM_CANFD1_SIZE, MM_CANFD0_SIZE };
    unsigned int irqs[] = { VERSAL_CANFD1_IRQ_0, VERSAL_CANFD0_IRQ_0 };
    const char clocknames[] = "can_clk\0s_axi_aclk";
    int i;

    /* Create and connect CANFD0 and CANFD1 nodes to canbus0. */
    for (i = 0; i < ARRAY_SIZE(addrs); i++) {
        char *name = g_strdup_printf("/canfd@%" PRIx64, addrs[i]);
        qemu_fdt_add_subnode(s->fdt, name);

        qemu_fdt_setprop_cell(s->fdt, name, "rx-fifo-depth", 0x40);
        qemu_fdt_setprop_cell(s->fdt, name, "tx-mailbox-count", 0x20);

        qemu_fdt_setprop_cells(s->fdt, name, "clocks",
                               s->phandle.clk_25Mhz, s->phandle.clk_25Mhz);
        qemu_fdt_setprop(s->fdt, name, "clock-names",
                         clocknames, sizeof(clocknames));
        qemu_fdt_setprop_cells(s->fdt, name, "interrupts",
                               GIC_FDT_IRQ_TYPE_SPI, irqs[i],
                               GIC_FDT_IRQ_FLAGS_LEVEL_HI);
        qemu_fdt_setprop_sized_cells(s->fdt, name, "reg",
                                     2, addrs[i], 2, size[i]);
        qemu_fdt_setprop_string(s->fdt, name, "compatible",
                                "xlnx,canfd-2.0");

        g_free(name);
    }
}

static void fdt_add_fixed_link_nodes(VersalVirt *s, char *gemname,
                                     uint32_t phandle)
{
    char *name = g_strdup_printf("%s/fixed-link", gemname);

    qemu_fdt_add_subnode(s->fdt, name);
    qemu_fdt_setprop_cell(s->fdt, name, "phandle", phandle);
    qemu_fdt_setprop(s->fdt, name, "full-duplex", NULL, 0);
    qemu_fdt_setprop_cell(s->fdt, name, "speed", 1000);
    g_free(name);
}

static void fdt_add_gem_nodes(VersalVirt *s)
{
    uint64_t addrs[] = { MM_GEM1, MM_GEM0 };
    unsigned int irqs[] = { VERSAL_GEM1_IRQ_0, VERSAL_GEM0_IRQ_0 };
    const char clocknames[] = "pclk\0hclk\0tx_clk\0rx_clk";
    const char compat_gem[] = "cdns,zynqmp-gem\0cdns,gem";
    int i;

    for (i = 0; i < ARRAY_SIZE(addrs); i++) {
        char *name = g_strdup_printf("/ethernet@%" PRIx64, addrs[i]);
        qemu_fdt_add_subnode(s->fdt, name);

        fdt_add_fixed_link_nodes(s, name, s->phandle.ethernet_phy[i]);
        qemu_fdt_setprop_string(s->fdt, name, "phy-mode", "rgmii-id");
        qemu_fdt_setprop_cell(s->fdt, name, "phy-handle",
                              s->phandle.ethernet_phy[i]);
        qemu_fdt_setprop_cells(s->fdt, name, "clocks",
                               s->phandle.clk_25Mhz, s->phandle.clk_25Mhz,
                               s->phandle.clk_125Mhz, s->phandle.clk_125Mhz);
        qemu_fdt_setprop(s->fdt, name, "clock-names",
                         clocknames, sizeof(clocknames));
        qemu_fdt_setprop_cells(s->fdt, name, "interrupts",
                               GIC_FDT_IRQ_TYPE_SPI, irqs[i],
                               GIC_FDT_IRQ_FLAGS_LEVEL_HI,
                               GIC_FDT_IRQ_TYPE_SPI, irqs[i],
                               GIC_FDT_IRQ_FLAGS_LEVEL_HI);
        qemu_fdt_setprop_sized_cells(s->fdt, name, "reg",
                                     2, addrs[i], 2, 0x1000);
        qemu_fdt_setprop(s->fdt, name, "compatible",
                         compat_gem, sizeof(compat_gem));
        qemu_fdt_setprop_cell(s->fdt, name, "#address-cells", 1);
        qemu_fdt_setprop_cell(s->fdt, name, "#size-cells", 0);
        g_free(name);
    }
}

static void fdt_add_zdma_nodes(VersalVirt *s)
{
    const char clocknames[] = "clk_main\0clk_apb";
    const char compat[] = "xlnx,zynqmp-dma-1.0";
    int i;

    for (i = XLNX_VERSAL_NR_ADMAS - 1; i >= 0; i--) {
        uint64_t addr = MM_ADMA_CH0 + MM_ADMA_CH0_SIZE * i;
        char *name = g_strdup_printf("/dma@%" PRIx64, addr);

        qemu_fdt_add_subnode(s->fdt, name);

        qemu_fdt_setprop_cell(s->fdt, name, "xlnx,bus-width", 64);
        qemu_fdt_setprop_cells(s->fdt, name, "clocks",
                               s->phandle.clk_25Mhz, s->phandle.clk_25Mhz);
        qemu_fdt_setprop(s->fdt, name, "clock-names",
                         clocknames, sizeof(clocknames));
        qemu_fdt_setprop_cells(s->fdt, name, "interrupts",
                               GIC_FDT_IRQ_TYPE_SPI, VERSAL_ADMA_IRQ_0 + i,
                               GIC_FDT_IRQ_FLAGS_LEVEL_HI);
        qemu_fdt_setprop_sized_cells(s->fdt, name, "reg",
                                     2, addr, 2, 0x1000);
        qemu_fdt_setprop(s->fdt, name, "compatible", compat, sizeof(compat));
        g_free(name);
    }
}

static void fdt_add_sd_nodes(VersalVirt *s)
{
    const char clocknames[] = "clk_xin\0clk_ahb";
    const char compat[] = "arasan,sdhci-8.9a";
    int i;

    for (i = ARRAY_SIZE(s->soc.pmc.iou.sd) - 1; i >= 0; i--) {
        uint64_t addr = MM_PMC_SD0 + MM_PMC_SD0_SIZE * i;
        char *name = g_strdup_printf("/sdhci@%" PRIx64, addr);

        qemu_fdt_add_subnode(s->fdt, name);

        qemu_fdt_setprop_cells(s->fdt, name, "clocks",
                               s->phandle.clk_25Mhz, s->phandle.clk_25Mhz);
        qemu_fdt_setprop(s->fdt, name, "clock-names",
                         clocknames, sizeof(clocknames));
        qemu_fdt_setprop_cells(s->fdt, name, "interrupts",
                               GIC_FDT_IRQ_TYPE_SPI, VERSAL_SD0_IRQ_0 + i * 2,
                               GIC_FDT_IRQ_FLAGS_LEVEL_HI);
        qemu_fdt_setprop_sized_cells(s->fdt, name, "reg",
                                     2, addr, 2, MM_PMC_SD0_SIZE);
        qemu_fdt_setprop(s->fdt, name, "compatible", compat, sizeof(compat));
        g_free(name);
    }
}

static void fdt_add_rtc_node(VersalVirt *s)
{
    const char compat[] = "xlnx,zynqmp-rtc";
    const char interrupt_names[] = "alarm\0sec";
    char *name = g_strdup_printf("/rtc@%x", MM_PMC_RTC);

    qemu_fdt_add_subnode(s->fdt, name);

    qemu_fdt_setprop_cells(s->fdt, name, "interrupts",
                           GIC_FDT_IRQ_TYPE_SPI, VERSAL_RTC_ALARM_IRQ,
                           GIC_FDT_IRQ_FLAGS_LEVEL_HI,
                           GIC_FDT_IRQ_TYPE_SPI, VERSAL_RTC_SECONDS_IRQ,
                           GIC_FDT_IRQ_FLAGS_LEVEL_HI);
    qemu_fdt_setprop(s->fdt, name, "interrupt-names",
                     interrupt_names, sizeof(interrupt_names));
    qemu_fdt_setprop_sized_cells(s->fdt, name, "reg",
                                 2, MM_PMC_RTC, 2, MM_PMC_RTC_SIZE);
    qemu_fdt_setprop(s->fdt, name, "compatible", compat, sizeof(compat));
    g_free(name);
}

static void fdt_add_bbram_node(VersalVirt *s)
{
    const char compat[] = TYPE_XLNX_BBRAM;
    const char interrupt_names[] = "bbram-error";
    char *name = g_strdup_printf("/bbram@%x", MM_PMC_BBRAM_CTRL);

    qemu_fdt_add_subnode(s->fdt, name);

    qemu_fdt_setprop_cells(s->fdt, name, "interrupts",
                           GIC_FDT_IRQ_TYPE_SPI, VERSAL_PMC_APB_IRQ,
                           GIC_FDT_IRQ_FLAGS_LEVEL_HI);
    qemu_fdt_setprop(s->fdt, name, "interrupt-names",
                     interrupt_names, sizeof(interrupt_names));
    qemu_fdt_setprop_sized_cells(s->fdt, name, "reg",
                                 2, MM_PMC_BBRAM_CTRL,
                                 2, MM_PMC_BBRAM_CTRL_SIZE);
    qemu_fdt_setprop(s->fdt, name, "compatible", compat, sizeof(compat));
    g_free(name);
}

static void fdt_add_efuse_ctrl_node(VersalVirt *s)
{
    const char compat[] = TYPE_XLNX_VERSAL_EFUSE_CTRL;
    const char interrupt_names[] = "pmc_efuse";
    char *name = g_strdup_printf("/pmc_efuse@%x", MM_PMC_EFUSE_CTRL);

    qemu_fdt_add_subnode(s->fdt, name);

    qemu_fdt_setprop_cells(s->fdt, name, "interrupts",
                           GIC_FDT_IRQ_TYPE_SPI, VERSAL_EFUSE_IRQ,
                           GIC_FDT_IRQ_FLAGS_LEVEL_HI);
    qemu_fdt_setprop(s->fdt, name, "interrupt-names",
                     interrupt_names, sizeof(interrupt_names));
    qemu_fdt_setprop_sized_cells(s->fdt, name, "reg",
                                 2, MM_PMC_EFUSE_CTRL,
                                 2, MM_PMC_EFUSE_CTRL_SIZE);
    qemu_fdt_setprop(s->fdt, name, "compatible", compat, sizeof(compat));
    g_free(name);
}

static void fdt_add_efuse_cache_node(VersalVirt *s)
{
    const char compat[] = TYPE_XLNX_VERSAL_EFUSE_CACHE;
    char *name = g_strdup_printf("/xlnx_pmc_efuse_cache@%x",
                                 MM_PMC_EFUSE_CACHE);

    qemu_fdt_add_subnode(s->fdt, name);

    qemu_fdt_setprop_sized_cells(s->fdt, name, "reg",
                                 2, MM_PMC_EFUSE_CACHE,
                                 2, MM_PMC_EFUSE_CACHE_SIZE);
    qemu_fdt_setprop(s->fdt, name, "compatible", compat, sizeof(compat));
    g_free(name);
}

static void fdt_nop_memory_nodes(void *fdt, Error **errp)
{
    Error *err = NULL;
    char **node_path;
    int n = 0;

    node_path = qemu_fdt_node_unit_path(fdt, "memory", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    while (node_path[n]) {
        if (g_str_has_prefix(node_path[n], "/memory")) {
            qemu_fdt_nop_node(fdt, node_path[n]);
        }
        n++;
    }
    g_strfreev(node_path);
}

static void fdt_add_memory_nodes(VersalVirt *s, void *fdt, uint64_t ram_size)
{
    /* Describes the various split DDR access regions.  */
    static const struct {
        uint64_t base;
        uint64_t size;
    } addr_ranges[] = {
        { MM_TOP_DDR, MM_TOP_DDR_SIZE },
        { MM_TOP_DDR_2, MM_TOP_DDR_2_SIZE },
        { MM_TOP_DDR_3, MM_TOP_DDR_3_SIZE },
        { MM_TOP_DDR_4, MM_TOP_DDR_4_SIZE }
    };
    uint64_t mem_reg_prop[8] = {0};
    uint64_t size = ram_size;
    Error *err = NULL;
    char *name;
    int i;

    fdt_nop_memory_nodes(fdt, &err);
    if (err) {
        error_report_err(err);
        return;
    }

    name = g_strdup_printf("/memory@%x", MM_TOP_DDR);
    for (i = 0; i < ARRAY_SIZE(addr_ranges) && size; i++) {
        uint64_t mapsize;

        mapsize = size < addr_ranges[i].size ? size : addr_ranges[i].size;

        mem_reg_prop[i * 2] = addr_ranges[i].base;
        mem_reg_prop[i * 2 + 1] = mapsize;
        size -= mapsize;
    }
    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "device_type", "memory");

    switch (i) {
    case 1:
        qemu_fdt_setprop_sized_cells(fdt, name, "reg",
                                     2, mem_reg_prop[0],
                                     2, mem_reg_prop[1]);
        break;
    case 2:
        qemu_fdt_setprop_sized_cells(fdt, name, "reg",
                                     2, mem_reg_prop[0],
                                     2, mem_reg_prop[1],
                                     2, mem_reg_prop[2],
                                     2, mem_reg_prop[3]);
        break;
    case 3:
        qemu_fdt_setprop_sized_cells(fdt, name, "reg",
                                     2, mem_reg_prop[0],
                                     2, mem_reg_prop[1],
                                     2, mem_reg_prop[2],
                                     2, mem_reg_prop[3],
                                     2, mem_reg_prop[4],
                                     2, mem_reg_prop[5]);
        break;
    case 4:
        qemu_fdt_setprop_sized_cells(fdt, name, "reg",
                                     2, mem_reg_prop[0],
                                     2, mem_reg_prop[1],
                                     2, mem_reg_prop[2],
                                     2, mem_reg_prop[3],
                                     2, mem_reg_prop[4],
                                     2, mem_reg_prop[5],
                                     2, mem_reg_prop[6],
                                     2, mem_reg_prop[7]);
        break;
    default:
        g_assert_not_reached();
    }
    g_free(name);
}

static void versal_virt_modify_dtb(const struct arm_boot_info *binfo,
                                    void *fdt)
{
    VersalVirt *s = container_of(binfo, VersalVirt, binfo);

    fdt_add_memory_nodes(s, fdt, binfo->ram_size);
}

static void *versal_virt_get_dtb(const struct arm_boot_info *binfo,
                                  int *fdt_size)
{
    const VersalVirt *board = container_of(binfo, VersalVirt, binfo);

    *fdt_size = board->fdt_size;
    return board->fdt;
}

#define NUM_VIRTIO_TRANSPORT 8
static void create_virtio_regions(VersalVirt *s)
{
    int virtio_mmio_size = 0x200;
    int i;

    for (i = 0; i < NUM_VIRTIO_TRANSPORT; i++) {
        char *name = g_strdup_printf("virtio%d", i);
        hwaddr base = MM_TOP_RSVD + i * virtio_mmio_size;
        int irq = VERSAL_RSVD_IRQ_FIRST + i;
        MemoryRegion *mr;
        DeviceState *dev;
        qemu_irq pic_irq;

        pic_irq = qdev_get_gpio_in(DEVICE(&s->soc.fpd.apu.gic), irq);
        dev = qdev_new("virtio-mmio");
        object_property_add_child(OBJECT(&s->soc), name, OBJECT(dev));
        sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, pic_irq);
        mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
        memory_region_add_subregion(&s->soc.mr_ps, base, mr);
        g_free(name);
    }

    for (i = 0; i < NUM_VIRTIO_TRANSPORT; i++) {
        hwaddr base = MM_TOP_RSVD + i * virtio_mmio_size;
        int irq = VERSAL_RSVD_IRQ_FIRST + i;
        char *name = g_strdup_printf("/virtio_mmio@%" PRIx64, base);

        qemu_fdt_add_subnode(s->fdt, name);
        qemu_fdt_setprop(s->fdt, name, "dma-coherent", NULL, 0);
        qemu_fdt_setprop_cells(s->fdt, name, "interrupts",
                               GIC_FDT_IRQ_TYPE_SPI, irq,
                               GIC_FDT_IRQ_FLAGS_EDGE_LO_HI);
        qemu_fdt_setprop_sized_cells(s->fdt, name, "reg",
                                     2, base, 2, virtio_mmio_size);
        qemu_fdt_setprop_string(s->fdt, name, "compatible", "virtio,mmio");
        g_free(name);
    }
}

static void bbram_attach_drive(XlnxBBRam *dev)
{
    DriveInfo *dinfo;
    BlockBackend *blk;

    dinfo = drive_get_by_index(IF_PFLASH, 0);
    blk = dinfo ? blk_by_legacy_dinfo(dinfo) : NULL;
    if (blk) {
        qdev_prop_set_drive(DEVICE(dev), "drive", blk);
    }
}

static void efuse_attach_drive(XlnxEFuse *dev)
{
    DriveInfo *dinfo;
    BlockBackend *blk;

    dinfo = drive_get_by_index(IF_PFLASH, 1);
    blk = dinfo ? blk_by_legacy_dinfo(dinfo) : NULL;
    if (blk) {
        qdev_prop_set_drive(DEVICE(dev), "drive", blk);
    }
}

static void sd_plugin_card(SDHCIState *sd, DriveInfo *di)
{
    BlockBackend *blk = di ? blk_by_legacy_dinfo(di) : NULL;
    DeviceState *card;

    card = qdev_new(TYPE_SD_CARD);
    object_property_add_child(OBJECT(sd), "card[*]", OBJECT(card));
    qdev_prop_set_drive_err(card, "drive", blk, &error_fatal);
    qdev_realize_and_unref(card, qdev_get_child_bus(DEVICE(sd), "sd-bus"),
                           &error_fatal);
}

static char *versal_get_ospi_model(Object *obj, Error **errp)
{
    VersalVirt *s = XLNX_VERSAL_VIRT_MACHINE(obj);

    return g_strdup(s->ospi_model);
}

static void versal_set_ospi_model(Object *obj, const char *value, Error **errp)
{
    VersalVirt *s = XLNX_VERSAL_VIRT_MACHINE(obj);

    g_free(s->ospi_model);
    s->ospi_model = g_strdup(value);
}


static void versal_virt_init(MachineState *machine)
{
    VersalVirt *s = XLNX_VERSAL_VIRT_MACHINE(machine);
    int psci_conduit = QEMU_PSCI_CONDUIT_DISABLED;
    int i;

    /*
     * If the user provides an Operating System to be loaded, we expect them
     * to use the -kernel command line option.
     *
     * Users can load firmware or boot-loaders with the -device loader options.
     *
     * When loading an OS, we generate a dtb and let arm_load_kernel() select
     * where it gets loaded. This dtb will be passed to the kernel in x0.
     *
     * If there's no -kernel option, we generate a DTB and place it at 0x1000
     * for the bootloaders or firmware to pick up.
     *
     * If users want to provide their own DTB, they can use the -dtb option.
     * These dtb's will have their memory nodes modified to match QEMU's
     * selected ram_size option before they get passed to the kernel or fw.
     *
     * When loading an OS, we turn on QEMU's PSCI implementation with SMC
     * as the PSCI conduit. When there's no -kernel, we assume the user
     * provides EL3 firmware to handle PSCI.
     *
     * Even if the user provides a kernel filename, arm_load_kernel()
     * may suppress PSCI if it's going to boot that guest code at EL3.
     */
    if (machine->kernel_filename) {
        psci_conduit = QEMU_PSCI_CONDUIT_SMC;
    }

    object_initialize_child(OBJECT(machine), "xlnx-versal", &s->soc,
                            TYPE_XLNX_VERSAL);
    object_property_set_link(OBJECT(&s->soc), "ddr", OBJECT(machine->ram),
                             &error_abort);
    object_property_set_link(OBJECT(&s->soc), "canbus0", OBJECT(s->canbus[0]),
                             &error_abort);
    object_property_set_link(OBJECT(&s->soc), "canbus1", OBJECT(s->canbus[1]),
                             &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(&s->soc), &error_fatal);

    fdt_create(s);
    create_virtio_regions(s);
    fdt_add_gem_nodes(s);
    fdt_add_uart_nodes(s);
    fdt_add_canfd_nodes(s);
    fdt_add_gic_nodes(s);
    fdt_add_timer_nodes(s);
    fdt_add_zdma_nodes(s);
    fdt_add_usb_xhci_nodes(s);
    fdt_add_sd_nodes(s);
    fdt_add_rtc_node(s);
    fdt_add_bbram_node(s);
    fdt_add_efuse_ctrl_node(s);
    fdt_add_efuse_cache_node(s);
    fdt_add_cpu_nodes(s, psci_conduit);
    fdt_add_clk_node(s, "/clk125", 125000000, s->phandle.clk_125Mhz);
    fdt_add_clk_node(s, "/clk25", 25000000, s->phandle.clk_25Mhz);

    /* Make the APU cpu address space visible to virtio and other
     * modules unaware of multiple address-spaces.  */
    memory_region_add_subregion_overlap(get_system_memory(),
                                        0, &s->soc.fpd.apu.mr, 0);

    /* Attach bbram backend, if given */
    bbram_attach_drive(&s->soc.pmc.bbram);

    /* Attach efuse backend, if given */
    efuse_attach_drive(&s->soc.pmc.efuse);

    /* Plugin SD cards.  */
    for (i = 0; i < ARRAY_SIZE(s->soc.pmc.iou.sd); i++) {
        sd_plugin_card(&s->soc.pmc.iou.sd[i],
                       drive_get(IF_SD, 0, i));
    }

    s->binfo.ram_size = machine->ram_size;
    s->binfo.loader_start = 0x0;
    s->binfo.get_dtb = versal_virt_get_dtb;
    s->binfo.modify_dtb = versal_virt_modify_dtb;
    s->binfo.psci_conduit = psci_conduit;
    if (!machine->kernel_filename) {
        /* Some boot-loaders (e.g u-boot) don't like blobs at address 0 (NULL).
         * Offset things by 4K.  */
        s->binfo.loader_start = 0x1000;
        s->binfo.dtb_limit = 0x1000000;
    }
    arm_load_kernel(&s->soc.fpd.apu.cpu[0], machine, &s->binfo);

    for (i = 0; i < XLNX_VERSAL_NUM_OSPI_FLASH; i++) {
        BusState *spi_bus;
        DeviceState *flash_dev;
        ObjectClass *flash_klass;
        qemu_irq cs_line;
        DriveInfo *dinfo = drive_get(IF_MTD, 0, i);

        spi_bus = qdev_get_child_bus(DEVICE(&s->soc.pmc.iou.ospi), "spi0");

        if (s->ospi_model) {
            flash_klass = object_class_by_name(s->ospi_model);
            if (!flash_klass ||
                object_class_is_abstract(flash_klass) ||
                !object_class_dynamic_cast(flash_klass, TYPE_M25P80)) {
                error_report("'%s' is either abstract or"
                       " not a subtype of m25p80", s->ospi_model);
                exit(1);
            }
        }

        flash_dev = qdev_new(s->ospi_model ? s->ospi_model : "mt35xu01g");

        if (dinfo) {
            qdev_prop_set_drive_err(flash_dev, "drive",
                                    blk_by_legacy_dinfo(dinfo), &error_fatal);
        }
        qdev_prop_set_uint8(flash_dev, "cs", i);
        qdev_realize_and_unref(flash_dev, spi_bus, &error_fatal);

        cs_line = qdev_get_gpio_in_named(flash_dev, SSI_GPIO_CS, 0);

        sysbus_connect_irq(SYS_BUS_DEVICE(&s->soc.pmc.iou.ospi),
                           i + 1, cs_line);
    }
}

static void versal_virt_machine_instance_init(Object *obj)
{
    VersalVirt *s = XLNX_VERSAL_VIRT_MACHINE(obj);

    /*
     * User can set canbus0 and canbus1 properties to can-bus object and connect
     * to socketcan(optional) interface via command line.
     */
    object_property_add_link(obj, "canbus0", TYPE_CAN_BUS,
                             (Object **)&s->canbus[0],
                             object_property_allow_set_link,
                             0);
    object_property_add_link(obj, "canbus1", TYPE_CAN_BUS,
                             (Object **)&s->canbus[1],
                             object_property_allow_set_link,
                             0);
}

static void versal_virt_machine_finalize(Object *obj)
{
    VersalVirt *s = XLNX_VERSAL_VIRT_MACHINE(obj);

    g_free(s->ospi_model);
}

static void versal_virt_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Xilinx Versal Virtual development board";
    mc->init = versal_virt_init;
    mc->min_cpus = XLNX_VERSAL_NR_ACPUS + XLNX_VERSAL_NR_RCPUS;
    mc->max_cpus = XLNX_VERSAL_NR_ACPUS + XLNX_VERSAL_NR_RCPUS;
    mc->default_cpus = XLNX_VERSAL_NR_ACPUS + XLNX_VERSAL_NR_RCPUS;
    mc->no_cdrom = true;
    mc->default_ram_id = "ddr";
    object_class_property_add_str(oc, "ospi-flash", versal_get_ospi_model,
                                   versal_set_ospi_model);
    object_class_property_set_description(oc, "ospi-flash",
                                          "Change the OSPI Flash model");
}

static const TypeInfo versal_virt_machine_init_typeinfo = {
    .name       = TYPE_XLNX_VERSAL_VIRT_MACHINE,
    .parent     = TYPE_MACHINE,
    .class_init = versal_virt_machine_class_init,
    .instance_init = versal_virt_machine_instance_init,
    .instance_size = sizeof(VersalVirt),
    .instance_finalize = versal_virt_machine_finalize,
};

static void versal_virt_machine_init_register_types(void)
{
    type_register_static(&versal_virt_machine_init_typeinfo);
}

type_init(versal_virt_machine_init_register_types)

