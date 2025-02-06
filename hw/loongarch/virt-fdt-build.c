/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2025 Loongson Technology Corporation Limited
 */
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/guest-random.h"
#include <libfdt.h>
#include "hw/acpi/generic_event_device.h"
#include "hw/core/sysbus-fdt.h"
#include "hw/intc/loongarch_extioi.h"
#include "hw/loader.h"
#include "hw/loongarch/virt.h"
#include "hw/pci-host/gpex.h"
#include "hw/pci-host/ls7a.h"
#include "system/device_tree.h"
#include "system/reset.h"
#include "target/loongarch/cpu.h"

static void create_fdt(LoongArchVirtMachineState *lvms)
{
    MachineState *ms = MACHINE(lvms);
    uint8_t rng_seed[32];

    ms->fdt = create_device_tree(&lvms->fdt_size);
    if (!ms->fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }

    /* Header */
    qemu_fdt_setprop_string(ms->fdt, "/", "compatible",
                            "linux,dummy-loongson3");
    qemu_fdt_setprop_cell(ms->fdt, "/", "#address-cells", 0x2);
    qemu_fdt_setprop_cell(ms->fdt, "/", "#size-cells", 0x2);
    qemu_fdt_add_subnode(ms->fdt, "/chosen");

    /* Pass seed to RNG */
    qemu_guest_getrandom_nofail(rng_seed, sizeof(rng_seed));
    qemu_fdt_setprop(ms->fdt, "/chosen", "rng-seed", rng_seed, sizeof(rng_seed));
}

static void fdt_add_cpu_nodes(const LoongArchVirtMachineState *lvms)
{
    int num;
    MachineState *ms = MACHINE(lvms);
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    const CPUArchIdList *possible_cpus;
    LoongArchCPU *cpu;
    CPUState *cs;
    char *nodename, *map_path;

    qemu_fdt_add_subnode(ms->fdt, "/cpus");
    qemu_fdt_setprop_cell(ms->fdt, "/cpus", "#address-cells", 0x1);
    qemu_fdt_setprop_cell(ms->fdt, "/cpus", "#size-cells", 0x0);

    /* cpu nodes */
    possible_cpus = mc->possible_cpu_arch_ids(ms);
    for (num = 0; num < possible_cpus->len; num++) {
        cs = possible_cpus->cpus[num].cpu;
        if (cs == NULL) {
            continue;
        }

        nodename = g_strdup_printf("/cpus/cpu@%d", num);
        cpu = LOONGARCH_CPU(cs);

        qemu_fdt_add_subnode(ms->fdt, nodename);
        qemu_fdt_setprop_string(ms->fdt, nodename, "device_type", "cpu");
        qemu_fdt_setprop_string(ms->fdt, nodename, "compatible",
                                cpu->dtb_compatible);
        if (possible_cpus->cpus[num].props.has_node_id) {
            qemu_fdt_setprop_cell(ms->fdt, nodename, "numa-node-id",
                possible_cpus->cpus[num].props.node_id);
        }
        qemu_fdt_setprop_cell(ms->fdt, nodename, "reg", num);
        qemu_fdt_setprop_cell(ms->fdt, nodename, "phandle",
                              qemu_fdt_alloc_phandle(ms->fdt));
        g_free(nodename);
    }

    /*cpu map */
    qemu_fdt_add_subnode(ms->fdt, "/cpus/cpu-map");
    for (num = 0; num < possible_cpus->len; num++) {
        cs = possible_cpus->cpus[num].cpu;
        if (cs == NULL) {
            continue;
        }

        nodename = g_strdup_printf("/cpus/cpu@%d", num);
        if (ms->smp.threads > 1) {
            map_path = g_strdup_printf(
                "/cpus/cpu-map/socket%d/core%d/thread%d",
                num / (ms->smp.cores * ms->smp.threads),
                (num / ms->smp.threads) % ms->smp.cores,
                num % ms->smp.threads);
        } else {
            map_path = g_strdup_printf(
                "/cpus/cpu-map/socket%d/core%d",
                num / ms->smp.cores,
                num % ms->smp.cores);
        }
        qemu_fdt_add_path(ms->fdt, map_path);
        qemu_fdt_setprop_phandle(ms->fdt, map_path, "cpu", nodename);

        g_free(map_path);
        g_free(nodename);
    }
}

static void fdt_add_memory_node(MachineState *ms,
                                uint64_t base, uint64_t size, int node_id)
{
    char *nodename = g_strdup_printf("/memory@%" PRIx64, base);

    qemu_fdt_add_subnode(ms->fdt, nodename);
    qemu_fdt_setprop_cells(ms->fdt, nodename, "reg", base >> 32, base,
                           size >> 32, size);
    qemu_fdt_setprop_string(ms->fdt, nodename, "device_type", "memory");

    if (ms->numa_state && ms->numa_state->num_nodes) {
        qemu_fdt_setprop_cell(ms->fdt, nodename, "numa-node-id", node_id);
    }

    g_free(nodename);
}

static void fdt_add_memory_nodes(MachineState *ms)
{
    hwaddr base, size, ram_size, gap;
    int i, nb_numa_nodes, nodes;
    NodeInfo *numa_info;

    ram_size = ms->ram_size;
    base = VIRT_LOWMEM_BASE;
    gap = VIRT_LOWMEM_SIZE;
    nodes = nb_numa_nodes = ms->numa_state->num_nodes;
    numa_info = ms->numa_state->nodes;
    if (!nodes) {
        nodes = 1;
    }

    for (i = 0; i < nodes; i++) {
        if (nb_numa_nodes) {
            size = numa_info[i].node_mem;
        } else {
            size = ram_size;
        }

        /*
         * memory for the node splited into two part
         *   lowram:  [base, +gap)
         *   highram: [VIRT_HIGHMEM_BASE, +(len - gap))
         */
        if (size >= gap) {
            fdt_add_memory_node(ms, base, gap, i);
            size -= gap;
            base = VIRT_HIGHMEM_BASE;
            gap = ram_size - VIRT_LOWMEM_SIZE;
        }

        if (size) {
            fdt_add_memory_node(ms, base, size, i);
            base += size;
            gap -= size;
        }
    }
}

static void fdt_add_fw_cfg_node(const LoongArchVirtMachineState *lvms)
{
    char *nodename;
    hwaddr base = VIRT_FWCFG_BASE;
    const MachineState *ms = MACHINE(lvms);

    nodename = g_strdup_printf("/fw_cfg@%" PRIx64, base);
    qemu_fdt_add_subnode(ms->fdt, nodename);
    qemu_fdt_setprop_string(ms->fdt, nodename,
                            "compatible", "qemu,fw-cfg-mmio");
    qemu_fdt_setprop_sized_cells(ms->fdt, nodename, "reg",
                                 2, base, 2, 0x18);
    qemu_fdt_setprop(ms->fdt, nodename, "dma-coherent", NULL, 0);
    g_free(nodename);
}

static void fdt_add_flash_node(LoongArchVirtMachineState *lvms)
{
    MachineState *ms = MACHINE(lvms);
    char *nodename;
    MemoryRegion *flash_mem;

    hwaddr flash0_base;
    hwaddr flash0_size;

    hwaddr flash1_base;
    hwaddr flash1_size;

    flash_mem = pflash_cfi01_get_memory(lvms->flash[0]);
    flash0_base = flash_mem->addr;
    flash0_size = memory_region_size(flash_mem);

    flash_mem = pflash_cfi01_get_memory(lvms->flash[1]);
    flash1_base = flash_mem->addr;
    flash1_size = memory_region_size(flash_mem);

    nodename = g_strdup_printf("/flash@%" PRIx64, flash0_base);
    qemu_fdt_add_subnode(ms->fdt, nodename);
    qemu_fdt_setprop_string(ms->fdt, nodename, "compatible", "cfi-flash");
    qemu_fdt_setprop_sized_cells(ms->fdt, nodename, "reg",
                                 2, flash0_base, 2, flash0_size,
                                 2, flash1_base, 2, flash1_size);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "bank-width", 4);
    g_free(nodename);
}

static void fdt_add_cpuic_node(LoongArchVirtMachineState *lvms,
                               uint32_t *cpuintc_phandle)
{
    MachineState *ms = MACHINE(lvms);
    char *nodename;

    *cpuintc_phandle = qemu_fdt_alloc_phandle(ms->fdt);
    nodename = g_strdup_printf("/cpuic");
    qemu_fdt_add_subnode(ms->fdt, nodename);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "phandle", *cpuintc_phandle);
    qemu_fdt_setprop_string(ms->fdt, nodename, "compatible",
                            "loongson,cpu-interrupt-controller");
    qemu_fdt_setprop(ms->fdt, nodename, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "#interrupt-cells", 1);
    g_free(nodename);
}

static void fdt_add_eiointc_node(LoongArchVirtMachineState *lvms,
                                  uint32_t *cpuintc_phandle,
                                  uint32_t *eiointc_phandle)
{
    MachineState *ms = MACHINE(lvms);
    char *nodename;
    hwaddr extioi_base = APIC_BASE;
    hwaddr extioi_size = EXTIOI_SIZE;

    *eiointc_phandle = qemu_fdt_alloc_phandle(ms->fdt);
    nodename = g_strdup_printf("/eiointc@%" PRIx64, extioi_base);
    qemu_fdt_add_subnode(ms->fdt, nodename);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "phandle", *eiointc_phandle);
    qemu_fdt_setprop_string(ms->fdt, nodename, "compatible",
                            "loongson,ls2k2000-eiointc");
    qemu_fdt_setprop(ms->fdt, nodename, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "#interrupt-cells", 1);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "interrupt-parent",
                          *cpuintc_phandle);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "interrupts", 3);
    qemu_fdt_setprop_cells(ms->fdt, nodename, "reg", 0x0,
                           extioi_base, 0x0, extioi_size);
    g_free(nodename);
}

static void fdt_add_pch_pic_node(LoongArchVirtMachineState *lvms,
                                 uint32_t *eiointc_phandle,
                                 uint32_t *pch_pic_phandle)
{
    MachineState *ms = MACHINE(lvms);
    char *nodename;
    hwaddr pch_pic_base = VIRT_PCH_REG_BASE;
    hwaddr pch_pic_size = VIRT_PCH_REG_SIZE;

    *pch_pic_phandle = qemu_fdt_alloc_phandle(ms->fdt);
    nodename = g_strdup_printf("/platic@%" PRIx64, pch_pic_base);
    qemu_fdt_add_subnode(ms->fdt, nodename);
    qemu_fdt_setprop_cell(ms->fdt,  nodename, "phandle", *pch_pic_phandle);
    qemu_fdt_setprop_string(ms->fdt, nodename, "compatible",
                            "loongson,pch-pic-1.0");
    qemu_fdt_setprop_cells(ms->fdt, nodename, "reg", 0,
                           pch_pic_base, 0, pch_pic_size);
    qemu_fdt_setprop(ms->fdt, nodename, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "#interrupt-cells", 2);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "interrupt-parent",
                          *eiointc_phandle);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "loongson,pic-base-vec", 0);
    g_free(nodename);
}

static void fdt_add_pch_msi_node(LoongArchVirtMachineState *lvms,
                                 uint32_t *eiointc_phandle,
                                 uint32_t *pch_msi_phandle)
{
    MachineState *ms = MACHINE(lvms);
    char *nodename;
    hwaddr pch_msi_base = VIRT_PCH_MSI_ADDR_LOW;
    hwaddr pch_msi_size = VIRT_PCH_MSI_SIZE;

    *pch_msi_phandle = qemu_fdt_alloc_phandle(ms->fdt);
    nodename = g_strdup_printf("/msi@%" PRIx64, pch_msi_base);
    qemu_fdt_add_subnode(ms->fdt, nodename);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "phandle", *pch_msi_phandle);
    qemu_fdt_setprop_string(ms->fdt, nodename, "compatible",
                            "loongson,pch-msi-1.0");
    qemu_fdt_setprop_cells(ms->fdt, nodename, "reg",
                           0, pch_msi_base,
                           0, pch_msi_size);
    qemu_fdt_setprop(ms->fdt, nodename, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "interrupt-parent",
                          *eiointc_phandle);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "loongson,msi-base-vec",
                          VIRT_PCH_PIC_IRQ_NUM);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "loongson,msi-num-vecs",
                          EXTIOI_IRQS - VIRT_PCH_PIC_IRQ_NUM);
    g_free(nodename);
}

static void fdt_add_pcie_irq_map_node(const LoongArchVirtMachineState *lvms,
                                      char *nodename,
                                      uint32_t *pch_pic_phandle)
{
    int pin, dev;
    uint32_t irq_map_stride = 0;
    uint32_t full_irq_map[PCI_NUM_PINS * PCI_NUM_PINS * 10] = {};
    uint32_t *irq_map = full_irq_map;
    const MachineState *ms = MACHINE(lvms);

    /*
     * This code creates a standard swizzle of interrupts such that
     * each device's first interrupt is based on it's PCI_SLOT number.
     * (See pci_swizzle_map_irq_fn())
     *
     * We only need one entry per interrupt in the table (not one per
     * possible slot) seeing the interrupt-map-mask will allow the table
     * to wrap to any number of devices.
     */

    for (dev = 0; dev < PCI_NUM_PINS; dev++) {
        int devfn = dev * 0x8;

        for (pin = 0; pin < PCI_NUM_PINS; pin++) {
            int irq_nr = 16 + ((pin + PCI_SLOT(devfn)) % PCI_NUM_PINS);
            int i = 0;

            /* Fill PCI address cells */
            irq_map[i] = cpu_to_be32(devfn << 8);
            i += 3;

            /* Fill PCI Interrupt cells */
            irq_map[i] = cpu_to_be32(pin + 1);
            i += 1;

            /* Fill interrupt controller phandle and cells */
            irq_map[i++] = cpu_to_be32(*pch_pic_phandle);
            irq_map[i++] = cpu_to_be32(irq_nr);

            if (!irq_map_stride) {
                irq_map_stride = i;
            }
            irq_map += irq_map_stride;
        }
    }


    qemu_fdt_setprop(ms->fdt, nodename, "interrupt-map", full_irq_map,
                     PCI_NUM_PINS * PCI_NUM_PINS *
                     irq_map_stride * sizeof(uint32_t));
    qemu_fdt_setprop_cells(ms->fdt, nodename, "interrupt-map-mask",
                     0x1800, 0, 0, 0x7);
}

static void fdt_add_pcie_node(const LoongArchVirtMachineState *lvms,
                              uint32_t *pch_pic_phandle,
                              uint32_t *pch_msi_phandle)
{
    char *nodename;
    hwaddr base_mmio = VIRT_PCI_MEM_BASE;
    hwaddr size_mmio = VIRT_PCI_MEM_SIZE;
    hwaddr base_pio = VIRT_PCI_IO_BASE;
    hwaddr size_pio = VIRT_PCI_IO_SIZE;
    hwaddr base_pcie = VIRT_PCI_CFG_BASE;
    hwaddr size_pcie = VIRT_PCI_CFG_SIZE;
    hwaddr base = base_pcie;
    const MachineState *ms = MACHINE(lvms);

    nodename = g_strdup_printf("/pcie@%" PRIx64, base);
    qemu_fdt_add_subnode(ms->fdt, nodename);
    qemu_fdt_setprop_string(ms->fdt, nodename,
                            "compatible", "pci-host-ecam-generic");
    qemu_fdt_setprop_string(ms->fdt, nodename, "device_type", "pci");
    qemu_fdt_setprop_cell(ms->fdt, nodename, "#address-cells", 3);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "#size-cells", 2);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "linux,pci-domain", 0);
    qemu_fdt_setprop_cells(ms->fdt, nodename, "bus-range", 0,
                           PCIE_MMCFG_BUS(VIRT_PCI_CFG_SIZE - 1));
    qemu_fdt_setprop(ms->fdt, nodename, "dma-coherent", NULL, 0);
    qemu_fdt_setprop_sized_cells(ms->fdt, nodename, "reg",
                                 2, base_pcie, 2, size_pcie);
    qemu_fdt_setprop_sized_cells(ms->fdt, nodename, "ranges",
                                 1, FDT_PCI_RANGE_IOPORT, 2, VIRT_PCI_IO_OFFSET,
                                 2, base_pio, 2, size_pio,
                                 1, FDT_PCI_RANGE_MMIO, 2, base_mmio,
                                 2, base_mmio, 2, size_mmio);
    qemu_fdt_setprop_cells(ms->fdt, nodename, "msi-map",
                           0, *pch_msi_phandle, 0, 0x10000);
    fdt_add_pcie_irq_map_node(lvms, nodename, pch_pic_phandle);
    g_free(nodename);
}

static void fdt_add_uart_node(LoongArchVirtMachineState *lvms,
                              uint32_t *pch_pic_phandle, hwaddr base,
                              int irq, bool chosen)
{
    char *nodename;
    hwaddr size = VIRT_UART_SIZE;
    MachineState *ms = MACHINE(lvms);

    nodename = g_strdup_printf("/serial@%" PRIx64, base);
    qemu_fdt_add_subnode(ms->fdt, nodename);
    qemu_fdt_setprop_string(ms->fdt, nodename, "compatible", "ns16550a");
    qemu_fdt_setprop_cells(ms->fdt, nodename, "reg", 0x0, base, 0x0, size);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "clock-frequency", 100000000);
    if (chosen) {
        qemu_fdt_setprop_string(ms->fdt, "/chosen", "stdout-path", nodename);
    }
    qemu_fdt_setprop_cells(ms->fdt, nodename, "interrupts", irq, 0x4);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "interrupt-parent",
                          *pch_pic_phandle);
    g_free(nodename);
}

static void fdt_add_rtc_node(LoongArchVirtMachineState *lvms,
                             uint32_t *pch_pic_phandle)
{
    char *nodename;
    hwaddr base = VIRT_RTC_REG_BASE;
    hwaddr size = VIRT_RTC_LEN;
    MachineState *ms = MACHINE(lvms);

    nodename = g_strdup_printf("/rtc@%" PRIx64, base);
    qemu_fdt_add_subnode(ms->fdt, nodename);
    qemu_fdt_setprop_string(ms->fdt, nodename, "compatible",
                            "loongson,ls7a-rtc");
    qemu_fdt_setprop_sized_cells(ms->fdt, nodename, "reg", 2, base, 2, size);
    qemu_fdt_setprop_cells(ms->fdt, nodename, "interrupts",
                           VIRT_RTC_IRQ - VIRT_GSI_BASE , 0x4);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "interrupt-parent",
                          *pch_pic_phandle);
    g_free(nodename);
}

static void fdt_add_ged_reset(LoongArchVirtMachineState *lvms)
{
    char *name;
    uint32_t ged_handle;
    MachineState *ms = MACHINE(lvms);
    hwaddr base = VIRT_GED_REG_ADDR;
    hwaddr size = ACPI_GED_REG_COUNT;

    ged_handle = qemu_fdt_alloc_phandle(ms->fdt);
    name = g_strdup_printf("/ged@%" PRIx64, base);
    qemu_fdt_add_subnode(ms->fdt, name);
    qemu_fdt_setprop_string(ms->fdt, name, "compatible", "syscon");
    qemu_fdt_setprop_cells(ms->fdt, name, "reg", 0x0, base, 0x0, size);
    /* 8 bit registers */
    qemu_fdt_setprop_cell(ms->fdt, name, "reg-shift", 0);
    qemu_fdt_setprop_cell(ms->fdt, name, "reg-io-width", 1);
    qemu_fdt_setprop_cell(ms->fdt, name, "phandle", ged_handle);
    ged_handle = qemu_fdt_get_phandle(ms->fdt, name);
    g_free(name);

    name = g_strdup_printf("/reboot");
    qemu_fdt_add_subnode(ms->fdt, name);
    qemu_fdt_setprop_string(ms->fdt, name, "compatible", "syscon-reboot");
    qemu_fdt_setprop_cell(ms->fdt, name, "regmap", ged_handle);
    qemu_fdt_setprop_cell(ms->fdt, name, "offset", ACPI_GED_REG_RESET);
    qemu_fdt_setprop_cell(ms->fdt, name, "value", ACPI_GED_RESET_VALUE);
    g_free(name);

    name = g_strdup_printf("/poweroff");
    qemu_fdt_add_subnode(ms->fdt, name);
    qemu_fdt_setprop_string(ms->fdt, name, "compatible", "syscon-poweroff");
    qemu_fdt_setprop_cell(ms->fdt, name, "regmap", ged_handle);
    qemu_fdt_setprop_cell(ms->fdt, name, "offset", ACPI_GED_REG_SLEEP_CTL);
    qemu_fdt_setprop_cell(ms->fdt, name, "value", ACPI_GED_SLP_EN |
                          (ACPI_GED_SLP_TYP_S5 << ACPI_GED_SLP_TYP_POS));
    g_free(name);
}

void virt_fdt_setup(LoongArchVirtMachineState *lvms)
{
    MachineState *machine = MACHINE(lvms);
    uint32_t cpuintc_phandle, eiointc_phandle, pch_pic_phandle, pch_msi_phandle;
    int i;

    create_fdt(lvms);
    fdt_add_cpu_nodes(lvms);
    fdt_add_memory_nodes(machine);
    fdt_add_fw_cfg_node(lvms);
    fdt_add_flash_node(lvms);

    /* Add cpu interrupt-controller */
    fdt_add_cpuic_node(lvms, &cpuintc_phandle);
    /* Add Extend I/O Interrupt Controller node */
    fdt_add_eiointc_node(lvms, &cpuintc_phandle, &eiointc_phandle);
    /* Add PCH PIC node */
    fdt_add_pch_pic_node(lvms, &eiointc_phandle, &pch_pic_phandle);
    /* Add PCH MSI node */
    fdt_add_pch_msi_node(lvms, &eiointc_phandle, &pch_msi_phandle);
    /* Add pcie node */
    fdt_add_pcie_node(lvms, &pch_pic_phandle, &pch_msi_phandle);

    /*
     * Create uart fdt node in reverse order so that they appear
     * in the finished device tree lowest address first
     */
    for (i = VIRT_UART_COUNT; i-- > 0;) {
        hwaddr base = VIRT_UART_BASE + i * VIRT_UART_SIZE;
        int irq = VIRT_UART_IRQ + i - VIRT_GSI_BASE;
        fdt_add_uart_node(lvms, &pch_pic_phandle, base, irq, i == 0);
    }

    fdt_add_rtc_node(lvms, &pch_pic_phandle);
    fdt_add_ged_reset(lvms);
    platform_bus_add_all_fdt_nodes(machine->fdt, "/platic",
                                   VIRT_PLATFORM_BUS_BASEADDRESS,
                                   VIRT_PLATFORM_BUS_SIZE,
                                   VIRT_PLATFORM_BUS_IRQ);

    /*
     * Since lowmem region starts from 0 and Linux kernel legacy start address
     * at 2 MiB, FDT base address is located at 1 MiB to avoid NULL pointer
     * access. FDT size limit with 1 MiB.
     * Put the FDT into the memory map as a ROM image: this will ensure
     * the FDT is copied again upon reset, even if addr points into RAM.
     */
    rom_add_blob_fixed_as("fdt", machine->fdt, lvms->fdt_size, FDT_BASE,
                          &address_space_memory);
    qemu_register_reset_nosnapshotload(qemu_fdt_randomize_seeds,
            rom_ptr_for_as(&address_space_memory, FDT_BASE, lvms->fdt_size));
}
