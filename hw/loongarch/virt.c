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
#include "hw/intc/loongarch_ipi.h"
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
#include "qemu/error-report.h"


static void virt_flash_create(LoongArchMachineState *lams)
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
    qdev_prop_set_string(dev, "name", "virt.flash");
    object_property_add_child(OBJECT(lams), "virt.flash", OBJECT(dev));
    object_property_add_alias(OBJECT(lams), "pflash",
                              OBJECT(dev), "drive");

    lams->flash = PFLASH_CFI01(dev);
}

static void virt_flash_map(LoongArchMachineState *lams,
                           MemoryRegion *sysmem)
{
    PFlashCFI01 *flash = lams->flash;
    DeviceState *dev = DEVICE(flash);
    hwaddr base = VIRT_FLASH_BASE;
    hwaddr size = VIRT_FLASH_SIZE;

    assert(QEMU_IS_ALIGNED(size, VIRT_FLASH_SECTOR_SIZE));
    assert(size / VIRT_FLASH_SECTOR_SIZE <= UINT32_MAX);

    qdev_prop_set_uint32(dev, "num-blocks", size / VIRT_FLASH_SECTOR_SIZE);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    memory_region_add_subregion(sysmem, base,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0));

}

static void fdt_add_flash_node(LoongArchMachineState *lams)
{
    MachineState *ms = MACHINE(lams);
    char *nodename;

    hwaddr flash_base = VIRT_FLASH_BASE;
    hwaddr flash_size = VIRT_FLASH_SIZE;

    nodename = g_strdup_printf("/flash@%" PRIx64, flash_base);
    qemu_fdt_add_subnode(ms->fdt, nodename);
    qemu_fdt_setprop_string(ms->fdt, nodename, "compatible", "cfi-flash");
    qemu_fdt_setprop_sized_cells(ms->fdt, nodename, "reg",
                                 2, flash_base, 2, flash_size);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "bank-width", 4);
    g_free(nodename);
}

static void fdt_add_rtc_node(LoongArchMachineState *lams)
{
    char *nodename;
    hwaddr base = VIRT_RTC_REG_BASE;
    hwaddr size = VIRT_RTC_LEN;
    MachineState *ms = MACHINE(lams);

    nodename = g_strdup_printf("/rtc@%" PRIx64, base);
    qemu_fdt_add_subnode(ms->fdt, nodename);
    qemu_fdt_setprop_string(ms->fdt, nodename, "compatible", "loongson,ls7a-rtc");
    qemu_fdt_setprop_sized_cells(ms->fdt, nodename, "reg", 2, base, 2, size);
    g_free(nodename);
}

static void fdt_add_uart_node(LoongArchMachineState *lams)
{
    char *nodename;
    hwaddr base = VIRT_UART_BASE;
    hwaddr size = VIRT_UART_SIZE;
    MachineState *ms = MACHINE(lams);

    nodename = g_strdup_printf("/serial@%" PRIx64, base);
    qemu_fdt_add_subnode(ms->fdt, nodename);
    qemu_fdt_setprop_string(ms->fdt, nodename, "compatible", "ns16550a");
    qemu_fdt_setprop_cells(ms->fdt, nodename, "reg", 0x0, base, 0x0, size);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "clock-frequency", 100000000);
    qemu_fdt_setprop_string(ms->fdt, "/chosen", "stdout-path", nodename);
    g_free(nodename);
}

static void create_fdt(LoongArchMachineState *lams)
{
    MachineState *ms = MACHINE(lams);

    ms->fdt = create_device_tree(&lams->fdt_size);
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

static void fdt_add_cpu_nodes(const LoongArchMachineState *lams)
{
    int num;
    const MachineState *ms = MACHINE(lams);
    int smp_cpus = ms->smp.cpus;

    qemu_fdt_add_subnode(ms->fdt, "/cpus");
    qemu_fdt_setprop_cell(ms->fdt, "/cpus", "#address-cells", 0x1);
    qemu_fdt_setprop_cell(ms->fdt, "/cpus", "#size-cells", 0x0);

    /* cpu nodes */
    for (num = smp_cpus - 1; num >= 0; num--) {
        char *nodename = g_strdup_printf("/cpus/cpu@%d", num);
        LoongArchCPU *cpu = LOONGARCH_CPU(qemu_get_cpu(num));

        qemu_fdt_add_subnode(ms->fdt, nodename);
        qemu_fdt_setprop_string(ms->fdt, nodename, "device_type", "cpu");
        qemu_fdt_setprop_string(ms->fdt, nodename, "compatible",
                                cpu->dtb_compatible);
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

static void fdt_add_fw_cfg_node(const LoongArchMachineState *lams)
{
    char *nodename;
    hwaddr base = VIRT_FWCFG_BASE;
    const MachineState *ms = MACHINE(lams);

    nodename = g_strdup_printf("/fw_cfg@%" PRIx64, base);
    qemu_fdt_add_subnode(ms->fdt, nodename);
    qemu_fdt_setprop_string(ms->fdt, nodename,
                            "compatible", "qemu,fw-cfg-mmio");
    qemu_fdt_setprop_sized_cells(ms->fdt, nodename, "reg",
                                 2, base, 2, 0x18);
    qemu_fdt_setprop(ms->fdt, nodename, "dma-coherent", NULL, 0);
    g_free(nodename);
}

static void fdt_add_pcie_node(const LoongArchMachineState *lams)
{
    char *nodename;
    hwaddr base_mmio = VIRT_PCI_MEM_BASE;
    hwaddr size_mmio = VIRT_PCI_MEM_SIZE;
    hwaddr base_pio = VIRT_PCI_IO_BASE;
    hwaddr size_pio = VIRT_PCI_IO_SIZE;
    hwaddr base_pcie = VIRT_PCI_CFG_BASE;
    hwaddr size_pcie = VIRT_PCI_CFG_SIZE;
    hwaddr base = base_pcie;

    const MachineState *ms = MACHINE(lams);

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
    g_free(nodename);
}

static void fdt_add_irqchip_node(LoongArchMachineState *lams)
{
    MachineState *ms = MACHINE(lams);
    char *nodename;
    uint32_t irqchip_phandle;

    irqchip_phandle = qemu_fdt_alloc_phandle(ms->fdt);
    qemu_fdt_setprop_cell(ms->fdt, "/", "interrupt-parent", irqchip_phandle);

    nodename = g_strdup_printf("/intc@%lx", VIRT_IOAPIC_REG_BASE);
    qemu_fdt_add_subnode(ms->fdt, nodename);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "#interrupt-cells", 3);
    qemu_fdt_setprop(ms->fdt, nodename, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "#address-cells", 0x2);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "#size-cells", 0x2);
    qemu_fdt_setprop(ms->fdt, nodename, "ranges", NULL, 0);

    qemu_fdt_setprop_string(ms->fdt, nodename, "compatible",
                            "loongarch,ls7a");

    qemu_fdt_setprop_sized_cells(ms->fdt, nodename, "reg",
                                 2, VIRT_IOAPIC_REG_BASE,
                                 2, PCH_PIC_ROUTE_ENTRY_OFFSET);

    qemu_fdt_setprop_cell(ms->fdt, nodename, "phandle", irqchip_phandle);
    g_free(nodename);
}

#define PM_BASE 0x10080000
#define PM_SIZE 0x100
#define PM_CTRL 0x10

static void virt_build_smbios(LoongArchMachineState *lams)
{
    MachineState *ms = MACHINE(lams);
    MachineClass *mc = MACHINE_GET_CLASS(lams);
    uint8_t *smbios_tables, *smbios_anchor;
    size_t smbios_tables_len, smbios_anchor_len;
    const char *product = "QEMU Virtual Machine";

    if (!lams->fw_cfg) {
        return;
    }

    smbios_set_defaults("QEMU", product, mc->name, false,
                        true, SMBIOS_ENTRY_POINT_TYPE_64);

    smbios_get_tables(ms, NULL, 0, &smbios_tables, &smbios_tables_len,
                      &smbios_anchor, &smbios_anchor_len, &error_fatal);

    if (smbios_anchor) {
        fw_cfg_add_file(lams->fw_cfg, "etc/smbios/smbios-tables",
                        smbios_tables, smbios_tables_len);
        fw_cfg_add_file(lams->fw_cfg, "etc/smbios/smbios-anchor",
                        smbios_anchor, smbios_anchor_len);
    }
}

static void virt_machine_done(Notifier *notifier, void *data)
{
    LoongArchMachineState *lams = container_of(notifier,
                                        LoongArchMachineState, machine_done);
    virt_build_smbios(lams);
    loongarch_acpi_setup(lams);
}

static void virt_powerdown_req(Notifier *notifier, void *opaque)
{
    LoongArchMachineState *s = container_of(notifier,
                                   LoongArchMachineState, powerdown_notifier);

    acpi_send_event(s->acpi_ged, ACPI_POWER_DOWN_STATUS);
}

struct memmap_entry {
    uint64_t address;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
};

static struct memmap_entry *memmap_table;
static unsigned memmap_entries;

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

/*
 * This is a placeholder for missing ACPI,
 * and will eventually be replaced.
 */
static uint64_t loongarch_virt_pm_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void loongarch_virt_pm_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned size)
{
    if (addr != PM_CTRL) {
        return;
    }

    switch (val) {
    case 0x00:
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        return;
    case 0xff:
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
        return;
    default:
        return;
    }
}

static const MemoryRegionOps loongarch_virt_pm_ops = {
    .read  = loongarch_virt_pm_read,
    .write = loongarch_virt_pm_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1
    }
};

static struct _loaderparams {
    uint64_t ram_size;
    const char *kernel_filename;
    const char *kernel_cmdline;
    const char *initrd_filename;
} loaderparams;

static uint64_t cpu_loongarch_virt_to_phys(void *opaque, uint64_t addr)
{
    return addr & MAKE_64BIT_MASK(0, TARGET_PHYS_ADDR_SPACE_BITS);
}

static int64_t load_kernel_info(void)
{
    uint64_t kernel_entry, kernel_low, kernel_high;
    ssize_t kernel_size;

    kernel_size = load_elf(loaderparams.kernel_filename, NULL,
                           cpu_loongarch_virt_to_phys, NULL,
                           &kernel_entry, &kernel_low,
                           &kernel_high, NULL, 0,
                           EM_LOONGARCH, 1, 0);

    if (kernel_size < 0) {
        error_report("could not load kernel '%s': %s",
                     loaderparams.kernel_filename,
                     load_elf_strerror(kernel_size));
        exit(1);
    }
    return kernel_entry;
}

static DeviceState *create_acpi_ged(DeviceState *pch_pic, LoongArchMachineState *lams)
{
    DeviceState *dev;
    MachineState *ms = MACHINE(lams);
    uint32_t event = ACPI_GED_PWR_DOWN_EVT;

    if (ms->ram_slots) {
        event |= ACPI_GED_MEM_HOTPLUG_EVT;
    }
    dev = qdev_new(TYPE_ACPI_GED);
    qdev_prop_set_uint32(dev, "ged-event", event);

    /* ged event */
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, VIRT_GED_EVT_ADDR);
    /* memory hotplug */
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 1, VIRT_GED_MEM_ADDR);
    /* ged regs used for reset and power down */
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 2, VIRT_GED_REG_ADDR);

    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
                       qdev_get_gpio_in(pch_pic, VIRT_SCI_IRQ - VIRT_GSI_BASE));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
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

static void loongarch_devices_init(DeviceState *pch_pic, LoongArchMachineState *lams)
{
    DeviceState *gpex_dev;
    SysBusDevice *d;
    PCIBus *pci_bus;
    MemoryRegion *ecam_alias, *ecam_reg, *pio_alias, *pio_reg;
    MemoryRegion *mmio_alias, *mmio_reg, *pm_mem;
    int i;

    gpex_dev = qdev_new(TYPE_GPEX_HOST);
    d = SYS_BUS_DEVICE(gpex_dev);
    sysbus_realize_and_unref(d, &error_fatal);
    pci_bus = PCI_HOST_BRIDGE(gpex_dev)->bus;
    lams->pci_bus = pci_bus;

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

    serial_mm_init(get_system_memory(), VIRT_UART_BASE, 0,
                   qdev_get_gpio_in(pch_pic,
                                    VIRT_UART_IRQ - VIRT_GSI_BASE),
                   115200, serial_hd(0), DEVICE_LITTLE_ENDIAN);
    fdt_add_uart_node(lams);

    /* Network init */
    for (i = 0; i < nb_nics; i++) {
        NICInfo *nd = &nd_table[i];

        if (!nd->model) {
            nd->model = g_strdup("virtio");
        }

        pci_nic_init_nofail(nd, pci_bus, nd->model, NULL);
    }

    /*
     * There are some invalid guest memory access.
     * Create some unimplemented devices to emulate this.
     */
    create_unimplemented_device("pci-dma-cfg", 0x1001041c, 0x4);
    sysbus_create_simple("ls7a_rtc", VIRT_RTC_REG_BASE,
                         qdev_get_gpio_in(pch_pic,
                         VIRT_RTC_IRQ - VIRT_GSI_BASE));
    fdt_add_rtc_node(lams);

    pm_mem = g_new(MemoryRegion, 1);
    memory_region_init_io(pm_mem, NULL, &loongarch_virt_pm_ops,
                          NULL, "loongarch_virt_pm", PM_SIZE);
    memory_region_add_subregion(get_system_memory(), PM_BASE, pm_mem);
    /* acpi ged */
    lams->acpi_ged = create_acpi_ged(pch_pic, lams);
    /* platform bus */
    lams->platform_bus_dev = create_platform_bus(pch_pic);
}

static void loongarch_irq_init(LoongArchMachineState *lams)
{
    MachineState *ms = MACHINE(lams);
    DeviceState *pch_pic, *pch_msi, *cpudev;
    DeviceState *ipi, *extioi;
    SysBusDevice *d;
    LoongArchCPU *lacpu;
    CPULoongArchState *env;
    CPUState *cpu_state;
    int cpu, pin, i, start, num;

    ipi = qdev_new(TYPE_LOONGARCH_IPI);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(ipi), &error_fatal);

    extioi = qdev_new(TYPE_LOONGARCH_EXTIOI);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(extioi), &error_fatal);

    /*
     * The connection of interrupts:
     *   +-----+    +---------+     +-------+
     *   | IPI |--> | CPUINTC | <-- | Timer |
     *   +-----+    +---------+     +-------+
     *                  ^
     *                  |
     *            +---------+
     *            | EIOINTC |
     *            +---------+
     *             ^       ^
     *             |       |
     *      +---------+ +---------+
     *      | PCH-PIC | | PCH-MSI |
     *      +---------+ +---------+
     *        ^      ^          ^
     *        |      |          |
     * +--------+ +---------+ +---------+
     * | UARTs  | | Devices | | Devices |
     * +--------+ +---------+ +---------+
     */
    for (cpu = 0; cpu < ms->smp.cpus; cpu++) {
        cpu_state = qemu_get_cpu(cpu);
        cpudev = DEVICE(cpu_state);
        lacpu = LOONGARCH_CPU(cpu_state);
        env = &(lacpu->env);

        /* connect ipi irq to cpu irq */
        qdev_connect_gpio_out(ipi, cpu, qdev_get_gpio_in(cpudev, IRQ_IPI));
        /* IPI iocsr memory region */
        memory_region_add_subregion(&env->system_iocsr, SMP_IPI_MAILBOX,
                                    sysbus_mmio_get_region(SYS_BUS_DEVICE(ipi),
                                    cpu * 2));
        memory_region_add_subregion(&env->system_iocsr, MAIL_SEND_ADDR,
                                    sysbus_mmio_get_region(SYS_BUS_DEVICE(ipi),
                                    cpu * 2 + 1));
        /* extioi iocsr memory region */
        memory_region_add_subregion(&env->system_iocsr, APIC_BASE,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(extioi),
                                cpu));
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
    for (int i = 0; i < num; i++) {
        qdev_connect_gpio_out(DEVICE(d), i, qdev_get_gpio_in(extioi, i));
    }

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

    loongarch_devices_init(pch_pic, lams);
}

static void loongarch_firmware_init(LoongArchMachineState *lams)
{
    char *filename = MACHINE(lams)->firmware;
    char *bios_name = NULL;
    int bios_size;

    lams->bios_loaded = false;

    virt_flash_map(lams, get_system_memory());

    if (filename) {
        bios_name = qemu_find_file(QEMU_FILE_TYPE_BIOS, filename);
        if (!bios_name) {
            error_report("Could not find ROM image '%s'", filename);
            exit(1);
        }

        bios_size = load_image_targphys(bios_name, VIRT_BIOS_BASE, VIRT_BIOS_SIZE);
        if (bios_size < 0) {
            error_report("Could not load ROM image '%s'", bios_name);
            exit(1);
        }

        g_free(bios_name);

        memory_region_init_ram(&lams->bios, NULL, "loongarch.bios",
                               VIRT_BIOS_SIZE, &error_fatal);
        memory_region_set_readonly(&lams->bios, true);
        memory_region_add_subregion(get_system_memory(), VIRT_BIOS_BASE, &lams->bios);
        lams->bios_loaded = true;
    }

}

static void reset_load_elf(void *opaque)
{
    LoongArchCPU *cpu = opaque;
    CPULoongArchState *env = &cpu->env;

    cpu_reset(CPU(cpu));
    if (env->load_elf) {
        cpu_set_pc(CPU(cpu), env->elf_address);
    }
}

static void fw_cfg_add_kernel_info(FWCfgState *fw_cfg)
{
    /*
     * Expose the kernel, the command line, and the initrd in fw_cfg.
     * We don't process them here at all, it's all left to the
     * firmware.
     */
    load_image_to_fw_cfg(fw_cfg,
                         FW_CFG_KERNEL_SIZE, FW_CFG_KERNEL_DATA,
                         loaderparams.kernel_filename,
                         false);

    if (loaderparams.initrd_filename) {
        load_image_to_fw_cfg(fw_cfg,
                             FW_CFG_INITRD_SIZE, FW_CFG_INITRD_DATA,
                             loaderparams.initrd_filename, false);
    }

    if (loaderparams.kernel_cmdline) {
        fw_cfg_add_i32(fw_cfg, FW_CFG_CMDLINE_SIZE,
                       strlen(loaderparams.kernel_cmdline) + 1);
        fw_cfg_add_string(fw_cfg, FW_CFG_CMDLINE_DATA,
                          loaderparams.kernel_cmdline);
    }
}

static void loongarch_firmware_boot(LoongArchMachineState *lams)
{
    fw_cfg_add_kernel_info(lams->fw_cfg);
}

static void loongarch_direct_kernel_boot(LoongArchMachineState *lams)
{
    MachineState *machine = MACHINE(lams);
    int64_t kernel_addr = 0;
    LoongArchCPU *lacpu;
    int i;

    kernel_addr = load_kernel_info();
    if (!machine->firmware) {
        for (i = 0; i < machine->smp.cpus; i++) {
            lacpu = LOONGARCH_CPU(qemu_get_cpu(i));
            lacpu->env.load_elf = true;
            lacpu->env.elf_address = kernel_addr;
        }
    }
}

static void loongarch_init(MachineState *machine)
{
    LoongArchCPU *lacpu;
    const char *cpu_model = machine->cpu_type;
    ram_addr_t offset = 0;
    ram_addr_t ram_size = machine->ram_size;
    uint64_t highram_size = 0;
    MemoryRegion *address_space_mem = get_system_memory();
    LoongArchMachineState *lams = LOONGARCH_MACHINE(machine);
    int i;
    hwaddr fdt_base;

    if (!cpu_model) {
        cpu_model = LOONGARCH_CPU_TYPE_NAME("la464");
    }

    if (!strstr(cpu_model, "la464")) {
        error_report("LoongArch/TCG needs cpu type la464");
        exit(1);
    }

    if (ram_size < 1 * GiB) {
        error_report("ram_size must be greater than 1G.");
        exit(1);
    }
    create_fdt(lams);
    /* Init CPUs */
    for (i = 0; i < machine->smp.cpus; i++) {
        cpu_create(machine->cpu_type);
    }
    fdt_add_cpu_nodes(lams);
    /* Add memory region */
    memory_region_init_alias(&lams->lowmem, NULL, "loongarch.lowram",
                             machine->ram, 0, 256 * MiB);
    memory_region_add_subregion(address_space_mem, offset, &lams->lowmem);
    offset += 256 * MiB;
    memmap_add_entry(0, 256 * MiB, 1);
    highram_size = ram_size - 256 * MiB;
    memory_region_init_alias(&lams->highmem, NULL, "loongarch.highmem",
                             machine->ram, offset, highram_size);
    memory_region_add_subregion(address_space_mem, 0x90000000, &lams->highmem);
    memmap_add_entry(0x90000000, highram_size, 1);

    /* initialize device memory address space */
    if (machine->ram_size < machine->maxram_size) {
        machine->device_memory = g_malloc0(sizeof(*machine->device_memory));
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
        /* device memory base is the top of high memory address. */
        machine->device_memory->base = 0x90000000 + highram_size;
        machine->device_memory->base =
            ROUND_UP(machine->device_memory->base, 1 * GiB);

        memory_region_init(&machine->device_memory->mr, OBJECT(lams),
                           "device-memory", device_mem_size);
        memory_region_add_subregion(address_space_mem, machine->device_memory->base,
                                    &machine->device_memory->mr);
    }

    /* Add isa io region */
    memory_region_init_alias(&lams->isa_io, NULL, "isa-io",
                             get_system_io(), 0, VIRT_ISA_IO_SIZE);
    memory_region_add_subregion(address_space_mem, VIRT_ISA_IO_BASE,
                                &lams->isa_io);
    /* load the BIOS image. */
    loongarch_firmware_init(lams);

    /* fw_cfg init */
    lams->fw_cfg = loongarch_fw_cfg_init(ram_size, machine);
    rom_set_fw(lams->fw_cfg);
    if (lams->fw_cfg != NULL) {
        fw_cfg_add_file(lams->fw_cfg, "etc/memmap",
                        memmap_table,
                        sizeof(struct memmap_entry) * (memmap_entries));
    }
    fdt_add_fw_cfg_node(lams);
    loaderparams.ram_size = ram_size;
    loaderparams.kernel_filename = machine->kernel_filename;
    loaderparams.kernel_cmdline = machine->kernel_cmdline;
    loaderparams.initrd_filename = machine->initrd_filename;
    /* load the kernel. */
    if (loaderparams.kernel_filename) {
        if (lams->bios_loaded) {
            loongarch_firmware_boot(lams);
        } else {
            loongarch_direct_kernel_boot(lams);
        }
    }
    fdt_add_flash_node(lams);
    /* register reset function */
    for (i = 0; i < machine->smp.cpus; i++) {
        lacpu = LOONGARCH_CPU(qemu_get_cpu(i));
        qemu_register_reset(reset_load_elf, lacpu);
    }
    /* Initialize the IO interrupt subsystem */
    loongarch_irq_init(lams);
    fdt_add_irqchip_node(lams);
    platform_bus_add_all_fdt_nodes(machine->fdt, "/intc",
                                   VIRT_PLATFORM_BUS_BASEADDRESS,
                                   VIRT_PLATFORM_BUS_SIZE,
                                   VIRT_PLATFORM_BUS_IRQ);
    lams->machine_done.notify = virt_machine_done;
    qemu_add_machine_init_done_notifier(&lams->machine_done);
     /* connect powerdown request */
    lams->powerdown_notifier.notify = virt_powerdown_req;
    qemu_register_powerdown_notifier(&lams->powerdown_notifier);

    fdt_add_pcie_node(lams);
    /*
     * Since lowmem region starts from 0 and Linux kernel legacy start address
     * at 2 MiB, FDT base address is located at 1 MiB to avoid NULL pointer
     * access. FDT size limit with 1 MiB.
     * Put the FDT into the memory map as a ROM image: this will ensure
     * the FDT is copied again upon reset, even if addr points into RAM.
     */
    fdt_base = 1 * MiB;
    qemu_fdt_dumpdtb(machine->fdt, lams->fdt_size);
    rom_add_blob_fixed("fdt", machine->fdt, lams->fdt_size, fdt_base);
}

bool loongarch_is_acpi_enabled(LoongArchMachineState *lams)
{
    if (lams->acpi == ON_OFF_AUTO_OFF) {
        return false;
    }
    return true;
}

static void loongarch_get_acpi(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    LoongArchMachineState *lams = LOONGARCH_MACHINE(obj);
    OnOffAuto acpi = lams->acpi;

    visit_type_OnOffAuto(v, name, &acpi, errp);
}

static void loongarch_set_acpi(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    LoongArchMachineState *lams = LOONGARCH_MACHINE(obj);

    visit_type_OnOffAuto(v, name, &lams->acpi, errp);
}

static void loongarch_machine_initfn(Object *obj)
{
    LoongArchMachineState *lams = LOONGARCH_MACHINE(obj);

    lams->acpi = ON_OFF_AUTO_AUTO;
    lams->oem_id = g_strndup(ACPI_BUILD_APPNAME6, 6);
    lams->oem_table_id = g_strndup(ACPI_BUILD_APPNAME8, 8);
    virt_flash_create(lams);
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

static void virt_machine_device_pre_plug(HotplugHandler *hotplug_dev,
                                            DeviceState *dev, Error **errp)
{
    if (memhp_type_supported(dev)) {
        virt_mem_pre_plug(hotplug_dev, dev, errp);
    }
}

static void virt_mem_unplug_request(HotplugHandler *hotplug_dev,
                                     DeviceState *dev, Error **errp)
{
    LoongArchMachineState *lams = LOONGARCH_MACHINE(hotplug_dev);

    /* the acpi ged is always exist */
    hotplug_handler_unplug_request(HOTPLUG_HANDLER(lams->acpi_ged), dev,
                                   errp);
}

static void virt_machine_device_unplug_request(HotplugHandler *hotplug_dev,
                                          DeviceState *dev, Error **errp)
{
    if (memhp_type_supported(dev)) {
        virt_mem_unplug_request(hotplug_dev, dev, errp);
    }
}

static void virt_mem_unplug(HotplugHandler *hotplug_dev,
                             DeviceState *dev, Error **errp)
{
    LoongArchMachineState *lams = LOONGARCH_MACHINE(hotplug_dev);

    hotplug_handler_unplug(HOTPLUG_HANDLER(lams->acpi_ged), dev, errp);
    pc_dimm_unplug(PC_DIMM(dev), MACHINE(lams));
    qdev_unrealize(dev);
}

static void virt_machine_device_unplug(HotplugHandler *hotplug_dev,
                                          DeviceState *dev, Error **errp)
{
    if (memhp_type_supported(dev)) {
        virt_mem_unplug(hotplug_dev, dev, errp);
    }
}

static void virt_mem_plug(HotplugHandler *hotplug_dev,
                             DeviceState *dev, Error **errp)
{
    LoongArchMachineState *lams = LOONGARCH_MACHINE(hotplug_dev);

    pc_dimm_plug(PC_DIMM(dev), MACHINE(lams));
    hotplug_handler_plug(HOTPLUG_HANDLER(lams->acpi_ged),
                         dev, &error_abort);
}

static void loongarch_machine_device_plug_cb(HotplugHandler *hotplug_dev,
                                        DeviceState *dev, Error **errp)
{
    LoongArchMachineState *lams = LOONGARCH_MACHINE(hotplug_dev);
    MachineClass *mc = MACHINE_GET_CLASS(lams);

    if (device_is_dynamic_sysbus(mc, dev)) {
        if (lams->platform_bus_dev) {
            platform_bus_link_device(PLATFORM_BUS_DEVICE(lams->platform_bus_dev),
                                     SYS_BUS_DEVICE(dev));
        }
    } else if (memhp_type_supported(dev)) {
        virt_mem_plug(hotplug_dev, dev, errp);
    }
}

static HotplugHandler *virt_machine_get_hotplug_handler(MachineState *machine,
                                                        DeviceState *dev)
{
    MachineClass *mc = MACHINE_GET_CLASS(machine);

    if (device_is_dynamic_sysbus(mc, dev) ||
        memhp_type_supported(dev)) {
        return HOTPLUG_HANDLER(machine);
    }
    return NULL;
}

static void loongarch_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(oc);

    mc->desc = "Loongson-3A5000 LS7A1000 machine";
    mc->init = loongarch_init;
    mc->default_ram_size = 1 * GiB;
    mc->default_cpu_type = LOONGARCH_CPU_TYPE_NAME("la464");
    mc->default_ram_id = "loongarch.ram";
    mc->max_cpus = LOONGARCH_MAX_VCPUS;
    mc->is_default = 1;
    mc->default_kernel_irqchip_split = false;
    mc->block_default_type = IF_VIRTIO;
    mc->default_boot_order = "c";
    mc->no_cdrom = 1;
    mc->get_hotplug_handler = virt_machine_get_hotplug_handler;
    hc->plug = loongarch_machine_device_plug_cb;
    hc->pre_plug = virt_machine_device_pre_plug;
    hc->unplug_request = virt_machine_device_unplug_request;
    hc->unplug = virt_machine_device_unplug;

    object_class_property_add(oc, "acpi", "OnOffAuto",
        loongarch_get_acpi, loongarch_set_acpi,
        NULL, NULL);
    object_class_property_set_description(oc, "acpi",
        "Enable ACPI");
    machine_class_allow_dynamic_sysbus_dev(mc, TYPE_RAMFB_DEVICE);
#ifdef CONFIG_TPM
    machine_class_allow_dynamic_sysbus_dev(mc, TYPE_TPM_TIS_SYSBUS);
#endif
}

static const TypeInfo loongarch_machine_types[] = {
    {
        .name           = TYPE_LOONGARCH_MACHINE,
        .parent         = TYPE_MACHINE,
        .instance_size  = sizeof(LoongArchMachineState),
        .class_init     = loongarch_class_init,
        .instance_init = loongarch_machine_initfn,
        .interfaces = (InterfaceInfo[]) {
         { TYPE_HOTPLUG_HANDLER },
         { }
        },
    }
};

DEFINE_TYPES(loongarch_machine_types)
