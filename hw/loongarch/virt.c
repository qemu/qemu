/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU loongson 3a5000 develop board emulation
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */
#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/datadir.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/char/serial.h"
#include "sysemu/kvm.h"
#include "sysemu/tcg.h"
#include "sysemu/sysemu.h"
#include "sysemu/qtest.h"
#include "sysemu/runstate.h"
#include "sysemu/reset.h"
#include "sysemu/rtc.h"
#include "hw/loongarch/virt.h"
#include "exec/address-spaces.h"
#include "hw/irq.h"
#include "net/net.h"
#include "hw/loader.h"
#include "elf.h"
#include "hw/intc/loongson_ipi.h"
#include "hw/intc/loongarch_extioi.h"
#include "hw/intc/loongarch_pch_pic.h"
#include "hw/intc/loongarch_pch_msi.h"
#include "hw/pci-host/ls7a.h"
#include "hw/pci-host/gpex.h"
#include "hw/misc/unimp.h"
#include "hw/loongarch/fw_cfg.h"
#include "target/loongarch/cpu.h"
#include "hw/firmware/smbios.h"
#include "hw/acpi/aml-build.h"
#include "qapi/qapi-visit-common.h"
#include "hw/acpi/generic_event_device.h"
#include "hw/mem/nvdimm.h"
#include "sysemu/device_tree.h"
#include <libfdt.h>
#include "hw/core/sysbus-fdt.h"
#include "hw/platform-bus.h"
#include "hw/display/ramfb.h"
#include "hw/mem/pc-dimm.h"
#include "sysemu/tpm.h"
#include "sysemu/block-backend.h"
#include "hw/block/flash.h"
#include "hw/virtio/virtio-iommu.h"
#include "qemu/error-report.h"

static bool virt_is_veiointc_enabled(LoongArchVirtMachineState *lvms)
{
    if (lvms->veiointc == ON_OFF_AUTO_OFF) {
        return false;
    }
    return true;
}

static void virt_get_veiointc(Object *obj, Visitor *v, const char *name,
                              void *opaque, Error **errp)
{
    LoongArchVirtMachineState *lvms = LOONGARCH_VIRT_MACHINE(obj);
    OnOffAuto veiointc = lvms->veiointc;

    visit_type_OnOffAuto(v, name, &veiointc, errp);
}

static void virt_set_veiointc(Object *obj, Visitor *v, const char *name,
                              void *opaque, Error **errp)
{
    LoongArchVirtMachineState *lvms = LOONGARCH_VIRT_MACHINE(obj);

    visit_type_OnOffAuto(v, name, &lvms->veiointc, errp);
}

static PFlashCFI01 *virt_flash_create1(LoongArchVirtMachineState *lvms,
                                       const char *name,
                                       const char *alias_prop_name)
{
    DeviceState *dev = qdev_new(TYPE_PFLASH_CFI01);

    qdev_prop_set_uint64(dev, "sector-length", VIRT_FLASH_SECTOR_SIZE);
    qdev_prop_set_uint8(dev, "width", 4);
    qdev_prop_set_uint8(dev, "device-width", 2);
    qdev_prop_set_bit(dev, "big-endian", false);
    qdev_prop_set_uint16(dev, "id0", 0x89);
    qdev_prop_set_uint16(dev, "id1", 0x18);
    qdev_prop_set_uint16(dev, "id2", 0x00);
    qdev_prop_set_uint16(dev, "id3", 0x00);
    qdev_prop_set_string(dev, "name", name);
    object_property_add_child(OBJECT(lvms), name, OBJECT(dev));
    object_property_add_alias(OBJECT(lvms), alias_prop_name,
                              OBJECT(dev), "drive");
    return PFLASH_CFI01(dev);
}

static void virt_flash_create(LoongArchVirtMachineState *lvms)
{
    lvms->flash[0] = virt_flash_create1(lvms, "virt.flash0", "pflash0");
    lvms->flash[1] = virt_flash_create1(lvms, "virt.flash1", "pflash1");
}

static void virt_flash_map1(PFlashCFI01 *flash,
                            hwaddr base, hwaddr size,
                            MemoryRegion *sysmem)
{
    DeviceState *dev = DEVICE(flash);
    BlockBackend *blk;
    hwaddr real_size = size;

    blk = pflash_cfi01_get_blk(flash);
    if (blk) {
        real_size = blk_getlength(blk);
        assert(real_size && real_size <= size);
    }

    assert(QEMU_IS_ALIGNED(real_size, VIRT_FLASH_SECTOR_SIZE));
    assert(real_size / VIRT_FLASH_SECTOR_SIZE <= UINT32_MAX);

    qdev_prop_set_uint32(dev, "num-blocks", real_size / VIRT_FLASH_SECTOR_SIZE);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    memory_region_add_subregion(sysmem, base,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0));
}

static void virt_flash_map(LoongArchVirtMachineState *lvms,
                           MemoryRegion *sysmem)
{
    PFlashCFI01 *flash0 = lvms->flash[0];
    PFlashCFI01 *flash1 = lvms->flash[1];

    virt_flash_map1(flash0, VIRT_FLASH0_BASE, VIRT_FLASH0_SIZE, sysmem);
    virt_flash_map1(flash1, VIRT_FLASH1_BASE, VIRT_FLASH1_SIZE, sysmem);
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

static void fdt_add_uart_node(LoongArchVirtMachineState *lvms,
                              uint32_t *pch_pic_phandle)
{
    char *nodename;
    hwaddr base = VIRT_UART_BASE;
    hwaddr size = VIRT_UART_SIZE;
    MachineState *ms = MACHINE(lvms);

    nodename = g_strdup_printf("/serial@%" PRIx64, base);
    qemu_fdt_add_subnode(ms->fdt, nodename);
    qemu_fdt_setprop_string(ms->fdt, nodename, "compatible", "ns16550a");
    qemu_fdt_setprop_cells(ms->fdt, nodename, "reg", 0x0, base, 0x0, size);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "clock-frequency", 100000000);
    qemu_fdt_setprop_string(ms->fdt, "/chosen", "stdout-path", nodename);
    qemu_fdt_setprop_cells(ms->fdt, nodename, "interrupts",
                           VIRT_UART_IRQ - VIRT_GSI_BASE, 0x4);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "interrupt-parent",
                          *pch_pic_phandle);
    g_free(nodename);
}

static void create_fdt(LoongArchVirtMachineState *lvms)
{
    MachineState *ms = MACHINE(lvms);

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
}

static void fdt_add_cpu_nodes(const LoongArchVirtMachineState *lvms)
{
    int num;
    const MachineState *ms = MACHINE(lvms);
    int smp_cpus = ms->smp.cpus;

    qemu_fdt_add_subnode(ms->fdt, "/cpus");
    qemu_fdt_setprop_cell(ms->fdt, "/cpus", "#address-cells", 0x1);
    qemu_fdt_setprop_cell(ms->fdt, "/cpus", "#size-cells", 0x0);

    /* cpu nodes */
    for (num = smp_cpus - 1; num >= 0; num--) {
        char *nodename = g_strdup_printf("/cpus/cpu@%d", num);
        LoongArchCPU *cpu = LOONGARCH_CPU(qemu_get_cpu(num));
        CPUState *cs = CPU(cpu);

        qemu_fdt_add_subnode(ms->fdt, nodename);
        qemu_fdt_setprop_string(ms->fdt, nodename, "device_type", "cpu");
        qemu_fdt_setprop_string(ms->fdt, nodename, "compatible",
                                cpu->dtb_compatible);
        if (ms->possible_cpus->cpus[cs->cpu_index].props.has_node_id) {
            qemu_fdt_setprop_cell(ms->fdt, nodename, "numa-node-id",
                ms->possible_cpus->cpus[cs->cpu_index].props.node_id);
        }
        qemu_fdt_setprop_cell(ms->fdt, nodename, "reg", num);
        qemu_fdt_setprop_cell(ms->fdt, nodename, "phandle",
                              qemu_fdt_alloc_phandle(ms->fdt));
        g_free(nodename);
    }

    /*cpu map */
    qemu_fdt_add_subnode(ms->fdt, "/cpus/cpu-map");

    for (num = smp_cpus - 1; num >= 0; num--) {
        char *cpu_path = g_strdup_printf("/cpus/cpu@%d", num);
        char *map_path;

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
        qemu_fdt_setprop_phandle(ms->fdt, map_path, "cpu", cpu_path);

        g_free(map_path);
        g_free(cpu_path);
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

static void fdt_add_pcie_irq_map_node(const LoongArchVirtMachineState *lvms,
                                      char *nodename,
                                      uint32_t *pch_pic_phandle)
{
    int pin, dev;
    uint32_t irq_map_stride = 0;
    uint32_t full_irq_map[GPEX_NUM_IRQS *GPEX_NUM_IRQS * 10] = {};
    uint32_t *irq_map = full_irq_map;
    const MachineState *ms = MACHINE(lvms);

    /* This code creates a standard swizzle of interrupts such that
     * each device's first interrupt is based on it's PCI_SLOT number.
     * (See pci_swizzle_map_irq_fn())
     *
     * We only need one entry per interrupt in the table (not one per
     * possible slot) seeing the interrupt-map-mask will allow the table
     * to wrap to any number of devices.
     */

    for (dev = 0; dev < GPEX_NUM_IRQS; dev++) {
        int devfn = dev * 0x8;

        for (pin = 0; pin  < GPEX_NUM_IRQS; pin++) {
            int irq_nr = 16 + ((pin + PCI_SLOT(devfn)) % GPEX_NUM_IRQS);
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
                     GPEX_NUM_IRQS * GPEX_NUM_IRQS *
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

static void virt_build_smbios(LoongArchVirtMachineState *lvms)
{
    MachineState *ms = MACHINE(lvms);
    MachineClass *mc = MACHINE_GET_CLASS(lvms);
    uint8_t *smbios_tables, *smbios_anchor;
    size_t smbios_tables_len, smbios_anchor_len;
    const char *product = "QEMU Virtual Machine";

    if (!lvms->fw_cfg) {
        return;
    }

    smbios_set_defaults("QEMU", product, mc->name, true);

    smbios_get_tables(ms, SMBIOS_ENTRY_POINT_TYPE_64,
                      NULL, 0,
                      &smbios_tables, &smbios_tables_len,
                      &smbios_anchor, &smbios_anchor_len, &error_fatal);

    if (smbios_anchor) {
        fw_cfg_add_file(lvms->fw_cfg, "etc/smbios/smbios-tables",
                        smbios_tables, smbios_tables_len);
        fw_cfg_add_file(lvms->fw_cfg, "etc/smbios/smbios-anchor",
                        smbios_anchor, smbios_anchor_len);
    }
}

static void virt_done(Notifier *notifier, void *data)
{
    LoongArchVirtMachineState *lvms = container_of(notifier,
                                      LoongArchVirtMachineState, machine_done);
    virt_build_smbios(lvms);
    loongarch_acpi_setup(lvms);
}

static void virt_powerdown_req(Notifier *notifier, void *opaque)
{
    LoongArchVirtMachineState *s;

    s = container_of(notifier, LoongArchVirtMachineState, powerdown_notifier);
    acpi_send_event(s->acpi_ged, ACPI_POWER_DOWN_STATUS);
}

static void memmap_add_entry(uint64_t address, uint64_t length, uint32_t type)
{
    /* Ensure there are no duplicate entries. */
    for (unsigned i = 0; i < memmap_entries; i++) {
        assert(memmap_table[i].address != address);
    }

    memmap_table = g_renew(struct memmap_entry, memmap_table,
                           memmap_entries + 1);
    memmap_table[memmap_entries].address = cpu_to_le64(address);
    memmap_table[memmap_entries].length = cpu_to_le64(length);
    memmap_table[memmap_entries].type = cpu_to_le32(type);
    memmap_table[memmap_entries].reserved = 0;
    memmap_entries++;
}

static DeviceState *create_acpi_ged(DeviceState *pch_pic,
                                    LoongArchVirtMachineState *lvms)
{
    DeviceState *dev;
    MachineState *ms = MACHINE(lvms);
    uint32_t event = ACPI_GED_PWR_DOWN_EVT;

    if (ms->ram_slots) {
        event |= ACPI_GED_MEM_HOTPLUG_EVT;
    }
    dev = qdev_new(TYPE_ACPI_GED);
    qdev_prop_set_uint32(dev, "ged-event", event);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    /* ged event */
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, VIRT_GED_EVT_ADDR);
    /* memory hotplug */
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 1, VIRT_GED_MEM_ADDR);
    /* ged regs used for reset and power down */
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 2, VIRT_GED_REG_ADDR);

    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
                       qdev_get_gpio_in(pch_pic, VIRT_SCI_IRQ - VIRT_GSI_BASE));
    return dev;
}

static DeviceState *create_platform_bus(DeviceState *pch_pic)
{
    DeviceState *dev;
    SysBusDevice *sysbus;
    int i, irq;
    MemoryRegion *sysmem = get_system_memory();

    dev = qdev_new(TYPE_PLATFORM_BUS_DEVICE);
    dev->id = g_strdup(TYPE_PLATFORM_BUS_DEVICE);
    qdev_prop_set_uint32(dev, "num_irqs", VIRT_PLATFORM_BUS_NUM_IRQS);
    qdev_prop_set_uint32(dev, "mmio_size", VIRT_PLATFORM_BUS_SIZE);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    sysbus = SYS_BUS_DEVICE(dev);
    for (i = 0; i < VIRT_PLATFORM_BUS_NUM_IRQS; i++) {
        irq = VIRT_PLATFORM_BUS_IRQ - VIRT_GSI_BASE + i;
        sysbus_connect_irq(sysbus, i, qdev_get_gpio_in(pch_pic, irq));
    }

    memory_region_add_subregion(sysmem,
                                VIRT_PLATFORM_BUS_BASEADDRESS,
                                sysbus_mmio_get_region(sysbus, 0));
    return dev;
}

static void virt_devices_init(DeviceState *pch_pic,
                                   LoongArchVirtMachineState *lvms,
                                   uint32_t *pch_pic_phandle,
                                   uint32_t *pch_msi_phandle)
{
    MachineClass *mc = MACHINE_GET_CLASS(lvms);
    DeviceState *gpex_dev;
    SysBusDevice *d;
    PCIBus *pci_bus;
    MemoryRegion *ecam_alias, *ecam_reg, *pio_alias, *pio_reg;
    MemoryRegion *mmio_alias, *mmio_reg;
    int i;

    gpex_dev = qdev_new(TYPE_GPEX_HOST);
    d = SYS_BUS_DEVICE(gpex_dev);
    sysbus_realize_and_unref(d, &error_fatal);
    pci_bus = PCI_HOST_BRIDGE(gpex_dev)->bus;
    lvms->pci_bus = pci_bus;

    /* Map only part size_ecam bytes of ECAM space */
    ecam_alias = g_new0(MemoryRegion, 1);
    ecam_reg = sysbus_mmio_get_region(d, 0);
    memory_region_init_alias(ecam_alias, OBJECT(gpex_dev), "pcie-ecam",
                             ecam_reg, 0, VIRT_PCI_CFG_SIZE);
    memory_region_add_subregion(get_system_memory(), VIRT_PCI_CFG_BASE,
                                ecam_alias);

    /* Map PCI mem space */
    mmio_alias = g_new0(MemoryRegion, 1);
    mmio_reg = sysbus_mmio_get_region(d, 1);
    memory_region_init_alias(mmio_alias, OBJECT(gpex_dev), "pcie-mmio",
                             mmio_reg, VIRT_PCI_MEM_BASE, VIRT_PCI_MEM_SIZE);
    memory_region_add_subregion(get_system_memory(), VIRT_PCI_MEM_BASE,
                                mmio_alias);

    /* Map PCI IO port space. */
    pio_alias = g_new0(MemoryRegion, 1);
    pio_reg = sysbus_mmio_get_region(d, 2);
    memory_region_init_alias(pio_alias, OBJECT(gpex_dev), "pcie-io", pio_reg,
                             VIRT_PCI_IO_OFFSET, VIRT_PCI_IO_SIZE);
    memory_region_add_subregion(get_system_memory(), VIRT_PCI_IO_BASE,
                                pio_alias);

    for (i = 0; i < GPEX_NUM_IRQS; i++) {
        sysbus_connect_irq(d, i,
                           qdev_get_gpio_in(pch_pic, 16 + i));
        gpex_set_irq_num(GPEX_HOST(gpex_dev), i, 16 + i);
    }

    /* Add pcie node */
    fdt_add_pcie_node(lvms, pch_pic_phandle, pch_msi_phandle);

    serial_mm_init(get_system_memory(), VIRT_UART_BASE, 0,
                   qdev_get_gpio_in(pch_pic,
                                    VIRT_UART_IRQ - VIRT_GSI_BASE),
                   115200, serial_hd(0), DEVICE_LITTLE_ENDIAN);
    fdt_add_uart_node(lvms, pch_pic_phandle);

    /* Network init */
    pci_init_nic_devices(pci_bus, mc->default_nic);

    /*
     * There are some invalid guest memory access.
     * Create some unimplemented devices to emulate this.
     */
    create_unimplemented_device("pci-dma-cfg", 0x1001041c, 0x4);
    sysbus_create_simple("ls7a_rtc", VIRT_RTC_REG_BASE,
                         qdev_get_gpio_in(pch_pic,
                         VIRT_RTC_IRQ - VIRT_GSI_BASE));
    fdt_add_rtc_node(lvms, pch_pic_phandle);

    /* acpi ged */
    lvms->acpi_ged = create_acpi_ged(pch_pic, lvms);
    /* platform bus */
    lvms->platform_bus_dev = create_platform_bus(pch_pic);
}

static void virt_irq_init(LoongArchVirtMachineState *lvms)
{
    MachineState *ms = MACHINE(lvms);
    DeviceState *pch_pic, *pch_msi, *cpudev;
    DeviceState *ipi, *extioi;
    SysBusDevice *d;
    LoongArchCPU *lacpu;
    CPULoongArchState *env;
    CPUState *cpu_state;
    int cpu, pin, i, start, num;
    uint32_t cpuintc_phandle, eiointc_phandle, pch_pic_phandle, pch_msi_phandle;

    /*
     * Extended IRQ model.
     *                                 |
     * +-----------+     +-------------|--------+     +-----------+
     * | IPI/Timer | --> | CPUINTC(0-3)|(4-255) | <-- | IPI/Timer |
     * +-----------+     +-------------|--------+     +-----------+
     *                         ^       |
     *                         |
     *                    +---------+
     *                    | EIOINTC |
     *                    +---------+
     *                     ^       ^
     *                     |       |
     *              +---------+ +---------+
     *              | PCH-PIC | | PCH-MSI |
     *              +---------+ +---------+
     *                ^      ^          ^
     *                |      |          |
     *         +--------+ +---------+ +---------+
     *         | UARTs  | | Devices | | Devices |
     *         +--------+ +---------+ +---------+
     *
     * Virt extended IRQ model.
     *
     *   +-----+    +---------------+     +-------+
     *   | IPI |--> | CPUINTC(0-255)| <-- | Timer |
     *   +-----+    +---------------+     +-------+
     *                     ^
     *                     |
     *               +-----------+
     *               | V-EIOINTC |
     *               +-----------+
     *                ^         ^
     *                |         |
     *         +---------+ +---------+
     *         | PCH-PIC | | PCH-MSI |
     *         +---------+ +---------+
     *           ^      ^          ^
     *           |      |          |
     *    +--------+ +---------+ +---------+
     *    | UARTs  | | Devices | | Devices |
     *    +--------+ +---------+ +---------+
     */

    /* Create IPI device */
    ipi = qdev_new(TYPE_LOONGSON_IPI);
    qdev_prop_set_uint32(ipi, "num-cpu", ms->smp.cpus);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(ipi), &error_fatal);

    /* IPI iocsr memory region */
    memory_region_add_subregion(&lvms->system_iocsr, SMP_IPI_MAILBOX,
                   sysbus_mmio_get_region(SYS_BUS_DEVICE(ipi), 0));
    memory_region_add_subregion(&lvms->system_iocsr, MAIL_SEND_ADDR,
                   sysbus_mmio_get_region(SYS_BUS_DEVICE(ipi), 1));

    /* Add cpu interrupt-controller */
    fdt_add_cpuic_node(lvms, &cpuintc_phandle);

    for (cpu = 0; cpu < ms->smp.cpus; cpu++) {
        cpu_state = qemu_get_cpu(cpu);
        cpudev = DEVICE(cpu_state);
        lacpu = LOONGARCH_CPU(cpu_state);
        env = &(lacpu->env);
        env->address_space_iocsr = &lvms->as_iocsr;

        /* connect ipi irq to cpu irq */
        qdev_connect_gpio_out(ipi, cpu, qdev_get_gpio_in(cpudev, IRQ_IPI));
        env->ipistate = ipi;
    }

    /* Create EXTIOI device */
    extioi = qdev_new(TYPE_LOONGARCH_EXTIOI);
    qdev_prop_set_uint32(extioi, "num-cpu", ms->smp.cpus);
    if (virt_is_veiointc_enabled(lvms)) {
        qdev_prop_set_bit(extioi, "has-virtualization-extension", true);
    }
    sysbus_realize_and_unref(SYS_BUS_DEVICE(extioi), &error_fatal);
    memory_region_add_subregion(&lvms->system_iocsr, APIC_BASE,
                    sysbus_mmio_get_region(SYS_BUS_DEVICE(extioi), 0));
    if (virt_is_veiointc_enabled(lvms)) {
        memory_region_add_subregion(&lvms->system_iocsr, EXTIOI_VIRT_BASE,
                    sysbus_mmio_get_region(SYS_BUS_DEVICE(extioi), 1));
    }

    /*
     * connect ext irq to the cpu irq
     * cpu_pin[9:2] <= intc_pin[7:0]
     */
    for (cpu = 0; cpu < ms->smp.cpus; cpu++) {
        cpudev = DEVICE(qemu_get_cpu(cpu));
        for (pin = 0; pin < LS3A_INTC_IP; pin++) {
            qdev_connect_gpio_out(extioi, (cpu * 8 + pin),
                                  qdev_get_gpio_in(cpudev, pin + 2));
        }
    }

    /* Add Extend I/O Interrupt Controller node */
    fdt_add_eiointc_node(lvms, &cpuintc_phandle, &eiointc_phandle);

    pch_pic = qdev_new(TYPE_LOONGARCH_PCH_PIC);
    num = VIRT_PCH_PIC_IRQ_NUM;
    qdev_prop_set_uint32(pch_pic, "pch_pic_irq_num", num);
    d = SYS_BUS_DEVICE(pch_pic);
    sysbus_realize_and_unref(d, &error_fatal);
    memory_region_add_subregion(get_system_memory(), VIRT_IOAPIC_REG_BASE,
                            sysbus_mmio_get_region(d, 0));
    memory_region_add_subregion(get_system_memory(),
                            VIRT_IOAPIC_REG_BASE + PCH_PIC_ROUTE_ENTRY_OFFSET,
                            sysbus_mmio_get_region(d, 1));
    memory_region_add_subregion(get_system_memory(),
                            VIRT_IOAPIC_REG_BASE + PCH_PIC_INT_STATUS_LO,
                            sysbus_mmio_get_region(d, 2));

    /* Connect pch_pic irqs to extioi */
    for (i = 0; i < num; i++) {
        qdev_connect_gpio_out(DEVICE(d), i, qdev_get_gpio_in(extioi, i));
    }

    /* Add PCH PIC node */
    fdt_add_pch_pic_node(lvms, &eiointc_phandle, &pch_pic_phandle);

    pch_msi = qdev_new(TYPE_LOONGARCH_PCH_MSI);
    start   =  num;
    num = EXTIOI_IRQS - start;
    qdev_prop_set_uint32(pch_msi, "msi_irq_base", start);
    qdev_prop_set_uint32(pch_msi, "msi_irq_num", num);
    d = SYS_BUS_DEVICE(pch_msi);
    sysbus_realize_and_unref(d, &error_fatal);
    sysbus_mmio_map(d, 0, VIRT_PCH_MSI_ADDR_LOW);
    for (i = 0; i < num; i++) {
        /* Connect pch_msi irqs to extioi */
        qdev_connect_gpio_out(DEVICE(d), i,
                              qdev_get_gpio_in(extioi, i + start));
    }

    /* Add PCH MSI node */
    fdt_add_pch_msi_node(lvms, &eiointc_phandle, &pch_msi_phandle);

    virt_devices_init(pch_pic, lvms, &pch_pic_phandle, &pch_msi_phandle);
}

static void virt_firmware_init(LoongArchVirtMachineState *lvms)
{
    char *filename = MACHINE(lvms)->firmware;
    char *bios_name = NULL;
    int bios_size, i;
    BlockBackend *pflash_blk0;
    MemoryRegion *mr;

    lvms->bios_loaded = false;

    /* Map legacy -drive if=pflash to machine properties */
    for (i = 0; i < ARRAY_SIZE(lvms->flash); i++) {
        pflash_cfi01_legacy_drive(lvms->flash[i],
                                  drive_get(IF_PFLASH, 0, i));
    }

    virt_flash_map(lvms, get_system_memory());

    pflash_blk0 = pflash_cfi01_get_blk(lvms->flash[0]);

    if (pflash_blk0) {
        if (filename) {
            error_report("cannot use both '-bios' and '-drive if=pflash'"
                         "options at once");
            exit(1);
        }
        lvms->bios_loaded = true;
        return;
    }

    if (filename) {
        bios_name = qemu_find_file(QEMU_FILE_TYPE_BIOS, filename);
        if (!bios_name) {
            error_report("Could not find ROM image '%s'", filename);
            exit(1);
        }

        mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(lvms->flash[0]), 0);
        bios_size = load_image_mr(bios_name, mr);
        if (bios_size < 0) {
            error_report("Could not load ROM image '%s'", bios_name);
            exit(1);
        }
        g_free(bios_name);
        lvms->bios_loaded = true;
    }
}

static MemTxResult virt_iocsr_misc_write(void *opaque, hwaddr addr,
                                         uint64_t val, unsigned size,
                                         MemTxAttrs attrs)
{
    LoongArchVirtMachineState *lvms = LOONGARCH_VIRT_MACHINE(opaque);
    uint64_t features;

    switch (addr) {
    case MISC_FUNC_REG:
        if (!virt_is_veiointc_enabled(lvms)) {
            return MEMTX_OK;
        }

        features = address_space_ldl(&lvms->as_iocsr,
                                     EXTIOI_VIRT_BASE + EXTIOI_VIRT_CONFIG,
                                     attrs, NULL);
        if (val & BIT_ULL(IOCSRM_EXTIOI_EN)) {
            features |= BIT(EXTIOI_ENABLE);
        }
        if (val & BIT_ULL(IOCSRM_EXTIOI_INT_ENCODE)) {
            features |= BIT(EXTIOI_ENABLE_INT_ENCODE);
        }

        address_space_stl(&lvms->as_iocsr,
                          EXTIOI_VIRT_BASE + EXTIOI_VIRT_CONFIG,
                          features, attrs, NULL);
        break;
    default:
        g_assert_not_reached();
    }

    return MEMTX_OK;
}

static MemTxResult virt_iocsr_misc_read(void *opaque, hwaddr addr,
                                        uint64_t *data,
                                        unsigned size, MemTxAttrs attrs)
{
    LoongArchVirtMachineState *lvms = LOONGARCH_VIRT_MACHINE(opaque);
    uint64_t ret = 0;
    int features;

    switch (addr) {
    case VERSION_REG:
        ret = 0x11ULL;
        break;
    case FEATURE_REG:
        ret = BIT(IOCSRF_MSI) | BIT(IOCSRF_EXTIOI) | BIT(IOCSRF_CSRIPI);
        if (kvm_enabled()) {
            ret |= BIT(IOCSRF_VM);
        }
        break;
    case VENDOR_REG:
        ret = 0x6e6f73676e6f6f4cULL; /* "Loongson" */
        break;
    case CPUNAME_REG:
        ret = 0x303030354133ULL;     /* "3A5000" */
        break;
    case MISC_FUNC_REG:
        if (!virt_is_veiointc_enabled(lvms)) {
            ret |= BIT_ULL(IOCSRM_EXTIOI_EN);
            break;
        }

        features = address_space_ldl(&lvms->as_iocsr,
                                     EXTIOI_VIRT_BASE + EXTIOI_VIRT_CONFIG,
                                     attrs, NULL);
        if (features & BIT(EXTIOI_ENABLE)) {
            ret |= BIT_ULL(IOCSRM_EXTIOI_EN);
        }
        if (features & BIT(EXTIOI_ENABLE_INT_ENCODE)) {
            ret |= BIT_ULL(IOCSRM_EXTIOI_INT_ENCODE);
        }
        break;
    default:
        g_assert_not_reached();
    }

    *data = ret;
    return MEMTX_OK;
}

static const MemoryRegionOps virt_iocsr_misc_ops = {
    .read_with_attrs  = virt_iocsr_misc_read,
    .write_with_attrs = virt_iocsr_misc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
};

static void fw_cfg_add_memory(MachineState *ms)
{
    hwaddr base, size, ram_size, gap;
    int nb_numa_nodes, nodes;
    NodeInfo *numa_info;

    ram_size = ms->ram_size;
    base = VIRT_LOWMEM_BASE;
    gap = VIRT_LOWMEM_SIZE;
    nodes = nb_numa_nodes = ms->numa_state->num_nodes;
    numa_info = ms->numa_state->nodes;
    if (!nodes) {
        nodes = 1;
    }

    /* add fw_cfg memory map of node0 */
    if (nb_numa_nodes) {
        size = numa_info[0].node_mem;
    } else {
        size = ram_size;
    }

    if (size >= gap) {
        memmap_add_entry(base, gap, 1);
        size -= gap;
        base = VIRT_HIGHMEM_BASE;
        gap = ram_size - VIRT_LOWMEM_SIZE;
    }

    if (size) {
        memmap_add_entry(base, size, 1);
        base += size;
    }

    if (nodes < 2) {
        return;
    }

    /* add fw_cfg memory map of other nodes */
    size = ram_size - numa_info[0].node_mem;
    gap  = VIRT_LOWMEM_BASE + VIRT_LOWMEM_SIZE;
    if (base < gap && (base + size) > gap) {
        /*
         * memory map for the maining nodes splited into two part
         *   lowram:  [base, +(gap - base))
         *   highram: [VIRT_HIGHMEM_BASE, +(size - (gap - base)))
         */
        memmap_add_entry(base, gap - base, 1);
        size -= gap - base;
        base = VIRT_HIGHMEM_BASE;
    }

   if (size)
        memmap_add_entry(base, size, 1);
}

static void virt_init(MachineState *machine)
{
    LoongArchCPU *lacpu;
    const char *cpu_model = machine->cpu_type;
    MemoryRegion *address_space_mem = get_system_memory();
    LoongArchVirtMachineState *lvms = LOONGARCH_VIRT_MACHINE(machine);
    int i;
    hwaddr base, size, ram_size = machine->ram_size;
    const CPUArchIdList *possible_cpus;
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    CPUState *cpu;

    if (!cpu_model) {
        cpu_model = LOONGARCH_CPU_TYPE_NAME("la464");
    }

    create_fdt(lvms);

    /* Create IOCSR space */
    memory_region_init_io(&lvms->system_iocsr, OBJECT(machine), NULL,
                          machine, "iocsr", UINT64_MAX);
    address_space_init(&lvms->as_iocsr, &lvms->system_iocsr, "IOCSR");
    memory_region_init_io(&lvms->iocsr_mem, OBJECT(machine),
                          &virt_iocsr_misc_ops,
                          machine, "iocsr_misc", 0x428);
    memory_region_add_subregion(&lvms->system_iocsr, 0, &lvms->iocsr_mem);

    /* Init CPUs */
    possible_cpus = mc->possible_cpu_arch_ids(machine);
    for (i = 0; i < possible_cpus->len; i++) {
        cpu = cpu_create(machine->cpu_type);
        cpu->cpu_index = i;
        machine->possible_cpus->cpus[i].cpu = cpu;
        lacpu = LOONGARCH_CPU(cpu);
        lacpu->phy_id = machine->possible_cpus->cpus[i].arch_id;
    }
    fdt_add_cpu_nodes(lvms);
    fdt_add_memory_nodes(machine);
    fw_cfg_add_memory(machine);

    /* Node0 memory */
    size = ram_size;
    base = VIRT_LOWMEM_BASE;
    if (size > VIRT_LOWMEM_SIZE) {
        size = VIRT_LOWMEM_SIZE;
    }

    memory_region_init_alias(&lvms->lowmem, NULL, "loongarch.lowram",
                              machine->ram, base, size);
    memory_region_add_subregion(address_space_mem, base, &lvms->lowmem);
    base += size;
    if (ram_size - size) {
        base = VIRT_HIGHMEM_BASE;
        memory_region_init_alias(&lvms->highmem, NULL, "loongarch.highram",
                machine->ram, VIRT_LOWMEM_BASE + size, ram_size - size);
        memory_region_add_subregion(address_space_mem, base, &lvms->highmem);
        base += ram_size - size;
    }

    /* initialize device memory address space */
    if (machine->ram_size < machine->maxram_size) {
        ram_addr_t device_mem_size = machine->maxram_size - machine->ram_size;

        if (machine->ram_slots > ACPI_MAX_RAM_SLOTS) {
            error_report("unsupported amount of memory slots: %"PRIu64,
                         machine->ram_slots);
            exit(EXIT_FAILURE);
        }

        if (QEMU_ALIGN_UP(machine->maxram_size,
                          TARGET_PAGE_SIZE) != machine->maxram_size) {
            error_report("maximum memory size must by aligned to multiple of "
                         "%d bytes", TARGET_PAGE_SIZE);
            exit(EXIT_FAILURE);
        }
        machine_memory_devices_init(machine, base, device_mem_size);
    }

    /* load the BIOS image. */
    virt_firmware_init(lvms);

    /* fw_cfg init */
    lvms->fw_cfg = virt_fw_cfg_init(ram_size, machine);
    rom_set_fw(lvms->fw_cfg);
    if (lvms->fw_cfg != NULL) {
        fw_cfg_add_file(lvms->fw_cfg, "etc/memmap",
                        memmap_table,
                        sizeof(struct memmap_entry) * (memmap_entries));
    }
    fdt_add_fw_cfg_node(lvms);
    fdt_add_flash_node(lvms);

    /* Initialize the IO interrupt subsystem */
    virt_irq_init(lvms);
    platform_bus_add_all_fdt_nodes(machine->fdt, "/platic",
                                   VIRT_PLATFORM_BUS_BASEADDRESS,
                                   VIRT_PLATFORM_BUS_SIZE,
                                   VIRT_PLATFORM_BUS_IRQ);
    lvms->machine_done.notify = virt_done;
    qemu_add_machine_init_done_notifier(&lvms->machine_done);
     /* connect powerdown request */
    lvms->powerdown_notifier.notify = virt_powerdown_req;
    qemu_register_powerdown_notifier(&lvms->powerdown_notifier);

    /*
     * Since lowmem region starts from 0 and Linux kernel legacy start address
     * at 2 MiB, FDT base address is located at 1 MiB to avoid NULL pointer
     * access. FDT size limit with 1 MiB.
     * Put the FDT into the memory map as a ROM image: this will ensure
     * the FDT is copied again upon reset, even if addr points into RAM.
     */
    qemu_fdt_dumpdtb(machine->fdt, lvms->fdt_size);
    rom_add_blob_fixed_as("fdt", machine->fdt, lvms->fdt_size, FDT_BASE,
                          &address_space_memory);
    qemu_register_reset_nosnapshotload(qemu_fdt_randomize_seeds,
            rom_ptr_for_as(&address_space_memory, FDT_BASE, lvms->fdt_size));

    lvms->bootinfo.ram_size = ram_size;
    loongarch_load_kernel(machine, &lvms->bootinfo);
}

static void virt_get_acpi(Object *obj, Visitor *v, const char *name,
                          void *opaque, Error **errp)
{
    LoongArchVirtMachineState *lvms = LOONGARCH_VIRT_MACHINE(obj);
    OnOffAuto acpi = lvms->acpi;

    visit_type_OnOffAuto(v, name, &acpi, errp);
}

static void virt_set_acpi(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    LoongArchVirtMachineState *lvms = LOONGARCH_VIRT_MACHINE(obj);

    visit_type_OnOffAuto(v, name, &lvms->acpi, errp);
}

static void virt_initfn(Object *obj)
{
    LoongArchVirtMachineState *lvms = LOONGARCH_VIRT_MACHINE(obj);

    if (tcg_enabled()) {
        lvms->veiointc = ON_OFF_AUTO_OFF;
    }
    lvms->acpi = ON_OFF_AUTO_AUTO;
    lvms->oem_id = g_strndup(ACPI_BUILD_APPNAME6, 6);
    lvms->oem_table_id = g_strndup(ACPI_BUILD_APPNAME8, 8);
    virt_flash_create(lvms);
}

static bool memhp_type_supported(DeviceState *dev)
{
    /* we only support pc dimm now */
    return object_dynamic_cast(OBJECT(dev), TYPE_PC_DIMM) &&
           !object_dynamic_cast(OBJECT(dev), TYPE_NVDIMM);
}

static void virt_mem_pre_plug(HotplugHandler *hotplug_dev, DeviceState *dev,
                                 Error **errp)
{
    pc_dimm_pre_plug(PC_DIMM(dev), MACHINE(hotplug_dev), NULL, errp);
}

static void virt_device_pre_plug(HotplugHandler *hotplug_dev,
                                            DeviceState *dev, Error **errp)
{
    if (memhp_type_supported(dev)) {
        virt_mem_pre_plug(hotplug_dev, dev, errp);
    }
}

static void virt_mem_unplug_request(HotplugHandler *hotplug_dev,
                                     DeviceState *dev, Error **errp)
{
    LoongArchVirtMachineState *lvms = LOONGARCH_VIRT_MACHINE(hotplug_dev);

    /* the acpi ged is always exist */
    hotplug_handler_unplug_request(HOTPLUG_HANDLER(lvms->acpi_ged), dev,
                                   errp);
}

static void virt_device_unplug_request(HotplugHandler *hotplug_dev,
                                          DeviceState *dev, Error **errp)
{
    if (memhp_type_supported(dev)) {
        virt_mem_unplug_request(hotplug_dev, dev, errp);
    }
}

static void virt_mem_unplug(HotplugHandler *hotplug_dev,
                             DeviceState *dev, Error **errp)
{
    LoongArchVirtMachineState *lvms = LOONGARCH_VIRT_MACHINE(hotplug_dev);

    hotplug_handler_unplug(HOTPLUG_HANDLER(lvms->acpi_ged), dev, errp);
    pc_dimm_unplug(PC_DIMM(dev), MACHINE(lvms));
    qdev_unrealize(dev);
}

static void virt_device_unplug(HotplugHandler *hotplug_dev,
                                          DeviceState *dev, Error **errp)
{
    if (memhp_type_supported(dev)) {
        virt_mem_unplug(hotplug_dev, dev, errp);
    }
}

static void virt_mem_plug(HotplugHandler *hotplug_dev,
                             DeviceState *dev, Error **errp)
{
    LoongArchVirtMachineState *lvms = LOONGARCH_VIRT_MACHINE(hotplug_dev);

    pc_dimm_plug(PC_DIMM(dev), MACHINE(lvms));
    hotplug_handler_plug(HOTPLUG_HANDLER(lvms->acpi_ged),
                         dev, &error_abort);
}

static void virt_device_plug_cb(HotplugHandler *hotplug_dev,
                                        DeviceState *dev, Error **errp)
{
    LoongArchVirtMachineState *lvms = LOONGARCH_VIRT_MACHINE(hotplug_dev);
    MachineClass *mc = MACHINE_GET_CLASS(lvms);
    PlatformBusDevice *pbus;

    if (device_is_dynamic_sysbus(mc, dev)) {
        if (lvms->platform_bus_dev) {
            pbus = PLATFORM_BUS_DEVICE(lvms->platform_bus_dev);
            platform_bus_link_device(pbus, SYS_BUS_DEVICE(dev));
        }
    } else if (memhp_type_supported(dev)) {
        virt_mem_plug(hotplug_dev, dev, errp);
    }
}

static HotplugHandler *virt_get_hotplug_handler(MachineState *machine,
                                                DeviceState *dev)
{
    MachineClass *mc = MACHINE_GET_CLASS(machine);

    if (device_is_dynamic_sysbus(mc, dev) ||
        object_dynamic_cast(OBJECT(dev), TYPE_VIRTIO_IOMMU_PCI) ||
        memhp_type_supported(dev)) {
        return HOTPLUG_HANDLER(machine);
    }
    return NULL;
}

static const CPUArchIdList *virt_possible_cpu_arch_ids(MachineState *ms)
{
    int n;
    unsigned int max_cpus = ms->smp.max_cpus;

    if (ms->possible_cpus) {
        assert(ms->possible_cpus->len == max_cpus);
        return ms->possible_cpus;
    }

    ms->possible_cpus = g_malloc0(sizeof(CPUArchIdList) +
                                  sizeof(CPUArchId) * max_cpus);
    ms->possible_cpus->len = max_cpus;
    for (n = 0; n < ms->possible_cpus->len; n++) {
        ms->possible_cpus->cpus[n].type = ms->cpu_type;
        ms->possible_cpus->cpus[n].arch_id = n;

        ms->possible_cpus->cpus[n].props.has_socket_id = true;
        ms->possible_cpus->cpus[n].props.socket_id  =
                                   n / (ms->smp.cores * ms->smp.threads);
        ms->possible_cpus->cpus[n].props.has_core_id = true;
        ms->possible_cpus->cpus[n].props.core_id =
                                   n / ms->smp.threads % ms->smp.cores;
        ms->possible_cpus->cpus[n].props.has_thread_id = true;
        ms->possible_cpus->cpus[n].props.thread_id = n % ms->smp.threads;
    }
    return ms->possible_cpus;
}

static CpuInstanceProperties virt_cpu_index_to_props(MachineState *ms,
                                                     unsigned cpu_index)
{
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    const CPUArchIdList *possible_cpus = mc->possible_cpu_arch_ids(ms);

    assert(cpu_index < possible_cpus->len);
    return possible_cpus->cpus[cpu_index].props;
}

static int64_t virt_get_default_cpu_node_id(const MachineState *ms, int idx)
{
    int64_t socket_id;

    if (ms->numa_state->num_nodes) {
        socket_id = ms->possible_cpus->cpus[idx].props.socket_id;
        return socket_id % ms->numa_state->num_nodes;
    } else {
        return 0;
    }
}

static void virt_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(oc);

    mc->init = virt_init;
    mc->default_cpu_type = LOONGARCH_CPU_TYPE_NAME("la464");
    mc->default_ram_id = "loongarch.ram";
    mc->max_cpus = LOONGARCH_MAX_CPUS;
    mc->is_default = 1;
    mc->default_kernel_irqchip_split = false;
    mc->block_default_type = IF_VIRTIO;
    mc->default_boot_order = "c";
    mc->no_cdrom = 1;
    mc->possible_cpu_arch_ids = virt_possible_cpu_arch_ids;
    mc->cpu_index_to_instance_props = virt_cpu_index_to_props;
    mc->get_default_cpu_node_id = virt_get_default_cpu_node_id;
    mc->numa_mem_supported = true;
    mc->auto_enable_numa_with_memhp = true;
    mc->auto_enable_numa_with_memdev = true;
    mc->get_hotplug_handler = virt_get_hotplug_handler;
    mc->default_nic = "virtio-net-pci";
    hc->plug = virt_device_plug_cb;
    hc->pre_plug = virt_device_pre_plug;
    hc->unplug_request = virt_device_unplug_request;
    hc->unplug = virt_device_unplug;

    object_class_property_add(oc, "acpi", "OnOffAuto",
        virt_get_acpi, virt_set_acpi,
        NULL, NULL);
    object_class_property_set_description(oc, "acpi",
        "Enable ACPI");
    object_class_property_add(oc, "v-eiointc", "OnOffAuto",
        virt_get_veiointc, virt_set_veiointc,
        NULL, NULL);
    object_class_property_set_description(oc, "v-eiointc",
                            "Enable Virt Extend I/O Interrupt Controller.");
    machine_class_allow_dynamic_sysbus_dev(mc, TYPE_RAMFB_DEVICE);
#ifdef CONFIG_TPM
    machine_class_allow_dynamic_sysbus_dev(mc, TYPE_TPM_TIS_SYSBUS);
#endif
}

static const TypeInfo virt_machine_types[] = {
    {
        .name           = TYPE_LOONGARCH_VIRT_MACHINE,
        .parent         = TYPE_MACHINE,
        .instance_size  = sizeof(LoongArchVirtMachineState),
        .class_init     = virt_class_init,
        .instance_init  = virt_initfn,
        .interfaces = (InterfaceInfo[]) {
         { TYPE_HOTPLUG_HANDLER },
         { }
        },
    }
};

DEFINE_TYPES(virt_machine_types)
