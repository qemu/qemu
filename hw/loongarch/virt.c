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
#include "exec/target_page.h"
#include "hw/boards.h"
#include "hw/char/serial-mm.h"
#include "system/kvm.h"
#include "system/tcg.h"
#include "system/system.h"
#include "system/qtest.h"
#include "system/runstate.h"
#include "system/reset.h"
#include "system/rtc.h"
#include "hw/loongarch/virt.h"
#include "system/address-spaces.h"
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
#include "qapi/qapi-visit-common.h"
#include "hw/acpi/generic_event_device.h"
#include "hw/mem/nvdimm.h"
#include "hw/platform-bus.h"
#include "hw/display/ramfb.h"
#include "hw/uefi/var-service-api.h"
#include "hw/mem/pc-dimm.h"
#include "system/tpm.h"
#include "system/block-backend.h"
#include "hw/block/flash.h"
#include "hw/virtio/virtio-iommu.h"
#include "qemu/error-report.h"

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

    smbios_set_defaults("QEMU", product, mc->name);

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
    virt_acpi_setup(lvms);
    virt_fdt_setup(lvms);
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
    MachineClass *mc = MACHINE_GET_CLASS(lvms);
    uint32_t event = ACPI_GED_PWR_DOWN_EVT;

    if (ms->ram_slots) {
        event |= ACPI_GED_MEM_HOTPLUG_EVT;
    }

    if (mc->has_hotpluggable_cpus) {
        event |= ACPI_GED_CPU_HOTPLUG_EVT;
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

    if (mc->has_hotpluggable_cpus) {
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 3, VIRT_GED_CPUHP_ADDR);
    }

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
                                   LoongArchVirtMachineState *lvms)
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

    for (i = 0; i < PCI_NUM_PINS; i++) {
        sysbus_connect_irq(d, i,
                           qdev_get_gpio_in(pch_pic, 16 + i));
        gpex_set_irq_num(GPEX_HOST(gpex_dev), i, 16 + i);
    }

    /*
     * Create uart fdt node in reverse order so that they appear
     * in the finished device tree lowest address first
     */
    for (i = VIRT_UART_COUNT; i-- > 0;) {
        hwaddr base = VIRT_UART_BASE + i * VIRT_UART_SIZE;
        int irq = VIRT_UART_IRQ + i - VIRT_GSI_BASE;
        serial_mm_init(get_system_memory(), base, 0,
                       qdev_get_gpio_in(pch_pic, irq),
                       115200, serial_hd(i), DEVICE_LITTLE_ENDIAN);
    }

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

    /* acpi ged */
    lvms->acpi_ged = create_acpi_ged(pch_pic, lvms);
    /* platform bus */
    lvms->platform_bus_dev = create_platform_bus(pch_pic);
}

static void virt_cpu_irq_init(LoongArchVirtMachineState *lvms)
{
    int num;
    MachineState *ms = MACHINE(lvms);
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    const CPUArchIdList *possible_cpus;
    CPUState *cs;

    /* cpu nodes */
    possible_cpus = mc->possible_cpu_arch_ids(ms);
    for (num = 0; num < possible_cpus->len; num++) {
        cs = possible_cpus->cpus[num].cpu;
        if (cs == NULL) {
            continue;
        }

        hotplug_handler_plug(HOTPLUG_HANDLER(lvms->ipi), DEVICE(cs),
                             &error_abort);
        hotplug_handler_plug(HOTPLUG_HANDLER(lvms->extioi), DEVICE(cs),
                             &error_abort);
    }
}

static void virt_irq_init(LoongArchVirtMachineState *lvms)
{
    DeviceState *pch_pic, *pch_msi;
    DeviceState *ipi, *extioi;
    SysBusDevice *d;
    int i, start, num;

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
    ipi = qdev_new(TYPE_LOONGARCH_IPI);
    lvms->ipi = ipi;
    sysbus_realize_and_unref(SYS_BUS_DEVICE(ipi), &error_fatal);

    /* IPI iocsr memory region */
    memory_region_add_subregion(&lvms->system_iocsr, SMP_IPI_MAILBOX,
                   sysbus_mmio_get_region(SYS_BUS_DEVICE(ipi), 0));
    memory_region_add_subregion(&lvms->system_iocsr, MAIL_SEND_ADDR,
                   sysbus_mmio_get_region(SYS_BUS_DEVICE(ipi), 1));

    /* Create EXTIOI device */
    extioi = qdev_new(TYPE_LOONGARCH_EXTIOI);
    lvms->extioi = extioi;
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

    virt_cpu_irq_init(lvms);
    pch_pic = qdev_new(TYPE_LOONGARCH_PIC);
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

    virt_devices_init(pch_pic, lvms);
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
    }

    if (size) {
        memmap_add_entry(base, size, 1);
        base += size;
    }

    if (nodes < 2) {
        return;
    }

    /* add fw_cfg memory map of other nodes */
    if (numa_info[0].node_mem < gap && ram_size > gap) {
        /*
         * memory map for the maining nodes splited into two part
         * lowram:  [base, +(gap - numa_info[0].node_mem))
         * highram: [VIRT_HIGHMEM_BASE, +(ram_size - gap))
         */
        memmap_add_entry(base, gap - numa_info[0].node_mem, 1);
        size = ram_size - gap;
        base = VIRT_HIGHMEM_BASE;
    } else {
        size = ram_size - numa_info[0].node_mem;
    }

    if (size) {
        memmap_add_entry(base, size, 1);
    }
}

static void virt_init(MachineState *machine)
{
    const char *cpu_model = machine->cpu_type;
    MemoryRegion *address_space_mem = get_system_memory();
    LoongArchVirtMachineState *lvms = LOONGARCH_VIRT_MACHINE(machine);
    int i;
    hwaddr base, size, ram_size = machine->ram_size;
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    Object *cpuobj;

    if (!cpu_model) {
        cpu_model = LOONGARCH_CPU_TYPE_NAME("la464");
    }

    /* Create IOCSR space */
    memory_region_init_io(&lvms->system_iocsr, OBJECT(machine), NULL,
                          machine, "iocsr", UINT64_MAX);
    address_space_init(&lvms->as_iocsr, &lvms->system_iocsr, "IOCSR");
    memory_region_init_io(&lvms->iocsr_mem, OBJECT(machine),
                          &virt_iocsr_misc_ops,
                          machine, "iocsr_misc", 0x428);
    memory_region_add_subregion(&lvms->system_iocsr, 0, &lvms->iocsr_mem);

    /* Init CPUs */
    mc->possible_cpu_arch_ids(machine);
    for (i = 0; i < machine->smp.cpus; i++) {
        cpuobj = object_new(machine->cpu_type);
        if (cpuobj == NULL) {
            error_report("Fail to create object with type %s ",
                         machine->cpu_type);
            exit(EXIT_FAILURE);
        }
        qdev_realize_and_unref(DEVICE(cpuobj), NULL, &error_fatal);
    }
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

    /* Initialize the IO interrupt subsystem */
    virt_irq_init(lvms);
    lvms->machine_done.notify = virt_done;
    qemu_add_machine_init_done_notifier(&lvms->machine_done);
     /* connect powerdown request */
    lvms->powerdown_notifier.notify = virt_powerdown_req;
    qemu_register_powerdown_notifier(&lvms->powerdown_notifier);

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

static void virt_get_topo_from_index(MachineState *ms,
                                     LoongArchCPUTopo *topo, int index)
{
    topo->socket_id = index / (ms->smp.cores * ms->smp.threads);
    topo->core_id = index / ms->smp.threads % ms->smp.cores;
    topo->thread_id = index % ms->smp.threads;
}

static unsigned int topo_align_up(unsigned int count)
{
    g_assert(count >= 1);
    count -= 1;
    return BIT(count ? 32 - clz32(count) : 0);
}

/*
 * LoongArch Reference Manual Vol1, Chapter 7.4.12 CPU Identity
 *  For CPU architecture, bit0 .. bit8 is valid for CPU id, max cpuid is 512
 *  However for IPI/Eiointc interrupt controller, max supported cpu id for
 *  irq routingis 256
 *
 *  Here max cpu id is 256 for virt machine
 */
static int virt_get_arch_id_from_topo(MachineState *ms, LoongArchCPUTopo *topo)
{
    int arch_id, threads, cores, sockets;

    threads = topo_align_up(ms->smp.threads);
    cores = topo_align_up(ms->smp.cores);
    sockets = topo_align_up(ms->smp.sockets);
    if ((threads * cores * sockets) > 256) {
        error_report("Exceeding max cpuid 256 with sockets[%d] cores[%d]"
                     " threads[%d]", ms->smp.sockets, ms->smp.cores,
                     ms->smp.threads);
        exit(1);
    }

    arch_id = topo->thread_id + topo->core_id * threads;
    arch_id += topo->socket_id * threads * cores;
    return arch_id;
}

/* Find cpu slot in machine->possible_cpus by arch_id */
static CPUArchId *virt_find_cpu_slot(MachineState *ms, int arch_id)
{
    int n;
    for (n = 0; n < ms->possible_cpus->len; n++) {
        if (ms->possible_cpus->cpus[n].arch_id == arch_id) {
            return &ms->possible_cpus->cpus[n];
        }
    }

    return NULL;
}

/* Find cpu slot for cold-plut CPU object where cpu is NULL */
static CPUArchId *virt_find_empty_cpu_slot(MachineState *ms)
{
    int n;
    for (n = 0; n < ms->possible_cpus->len; n++) {
        if (ms->possible_cpus->cpus[n].cpu == NULL) {
            return &ms->possible_cpus->cpus[n];
        }
    }

    return NULL;
}

static void virt_cpu_pre_plug(HotplugHandler *hotplug_dev,
                              DeviceState *dev, Error **errp)
{
    LoongArchVirtMachineState *lvms = LOONGARCH_VIRT_MACHINE(hotplug_dev);
    MachineState *ms = MACHINE(OBJECT(hotplug_dev));
    LoongArchCPU *cpu = LOONGARCH_CPU(dev);
    CPUState *cs = CPU(dev);
    CPUArchId *cpu_slot;
    LoongArchCPUTopo topo;
    int arch_id;

    if (lvms->acpi_ged) {
        if ((cpu->thread_id < 0) || (cpu->thread_id >= ms->smp.threads)) {
            error_setg(errp,
                       "Invalid thread-id %u specified, must be in range 1:%u",
                       cpu->thread_id, ms->smp.threads - 1);
            return;
        }

        if ((cpu->core_id < 0) || (cpu->core_id >= ms->smp.cores)) {
            error_setg(errp,
                       "Invalid core-id %u specified, must be in range 1:%u",
                       cpu->core_id, ms->smp.cores - 1);
            return;
        }

        if ((cpu->socket_id < 0) || (cpu->socket_id >= ms->smp.sockets)) {
            error_setg(errp,
                       "Invalid socket-id %u specified, must be in range 1:%u",
                       cpu->socket_id, ms->smp.sockets - 1);
            return;
        }

        topo.socket_id = cpu->socket_id;
        topo.core_id = cpu->core_id;
        topo.thread_id = cpu->thread_id;
        arch_id =  virt_get_arch_id_from_topo(ms, &topo);
        cpu_slot = virt_find_cpu_slot(ms, arch_id);
        if (CPU(cpu_slot->cpu)) {
            error_setg(errp,
                       "cpu(id%d=%d:%d:%d) with arch-id %" PRIu64 " exists",
                       cs->cpu_index, cpu->socket_id, cpu->core_id,
                       cpu->thread_id, cpu_slot->arch_id);
            return;
        }
    } else {
        /* For cold-add cpu, find empty cpu slot */
        cpu_slot = virt_find_empty_cpu_slot(ms);
        topo.socket_id = cpu_slot->props.socket_id;
        topo.core_id = cpu_slot->props.core_id;
        topo.thread_id = cpu_slot->props.thread_id;
        object_property_set_int(OBJECT(dev), "socket-id", topo.socket_id, NULL);
        object_property_set_int(OBJECT(dev), "core-id", topo.core_id, NULL);
        object_property_set_int(OBJECT(dev), "thread-id", topo.thread_id, NULL);
    }

    cpu->env.address_space_iocsr = &lvms->as_iocsr;
    cpu->phy_id = cpu_slot->arch_id;
    cs->cpu_index = cpu_slot - ms->possible_cpus->cpus;
    numa_cpu_pre_plug(cpu_slot, dev, errp);
}

static void virt_cpu_unplug_request(HotplugHandler *hotplug_dev,
                                    DeviceState *dev, Error **errp)
{
    LoongArchVirtMachineState *lvms = LOONGARCH_VIRT_MACHINE(hotplug_dev);
    LoongArchCPU *cpu = LOONGARCH_CPU(dev);
    CPUState *cs = CPU(dev);

    if (cs->cpu_index == 0) {
        error_setg(errp, "hot-unplug of boot cpu(id%d=%d:%d:%d) not supported",
                   cs->cpu_index, cpu->socket_id,
                   cpu->core_id, cpu->thread_id);
        return;
    }

    hotplug_handler_unplug_request(HOTPLUG_HANDLER(lvms->acpi_ged), dev, errp);
}

static void virt_cpu_unplug(HotplugHandler *hotplug_dev,
                            DeviceState *dev, Error **errp)
{
    CPUArchId *cpu_slot;
    LoongArchCPU *cpu = LOONGARCH_CPU(dev);
    LoongArchVirtMachineState *lvms = LOONGARCH_VIRT_MACHINE(hotplug_dev);

    /* Notify ipi and extioi irqchip to remove interrupt routing to CPU */
    hotplug_handler_unplug(HOTPLUG_HANDLER(lvms->ipi), dev, &error_abort);
    hotplug_handler_unplug(HOTPLUG_HANDLER(lvms->extioi), dev, &error_abort);

    /* Notify acpi ged CPU removed */
    hotplug_handler_unplug(HOTPLUG_HANDLER(lvms->acpi_ged), dev, &error_abort);

    cpu_slot = virt_find_cpu_slot(MACHINE(lvms), cpu->phy_id);
    cpu_slot->cpu = NULL;
}

static void virt_cpu_plug(HotplugHandler *hotplug_dev,
                          DeviceState *dev, Error **errp)
{
    CPUArchId *cpu_slot;
    LoongArchCPU *cpu = LOONGARCH_CPU(dev);
    LoongArchVirtMachineState *lvms = LOONGARCH_VIRT_MACHINE(hotplug_dev);

    if (lvms->ipi) {
        hotplug_handler_plug(HOTPLUG_HANDLER(lvms->ipi), dev, &error_abort);
    }

    if (lvms->extioi) {
        hotplug_handler_plug(HOTPLUG_HANDLER(lvms->extioi), dev, &error_abort);
    }

    if (lvms->acpi_ged) {
        hotplug_handler_plug(HOTPLUG_HANDLER(lvms->acpi_ged), dev,
                             &error_abort);
    }

    cpu_slot = virt_find_cpu_slot(MACHINE(lvms), cpu->phy_id);
    cpu_slot->cpu = CPU(dev);
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
    pc_dimm_pre_plug(PC_DIMM(dev), MACHINE(hotplug_dev), errp);
}

static void virt_device_pre_plug(HotplugHandler *hotplug_dev,
                                            DeviceState *dev, Error **errp)
{
    if (memhp_type_supported(dev)) {
        virt_mem_pre_plug(hotplug_dev, dev, errp);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_LOONGARCH_CPU)) {
        virt_cpu_pre_plug(hotplug_dev, dev, errp);
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
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_LOONGARCH_CPU)) {
        virt_cpu_unplug_request(hotplug_dev, dev, errp);
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
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_LOONGARCH_CPU)) {
        virt_cpu_unplug(hotplug_dev, dev, errp);
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
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_LOONGARCH_CPU)) {
        virt_cpu_plug(hotplug_dev, dev, errp);
    }
}

static HotplugHandler *virt_get_hotplug_handler(MachineState *machine,
                                                DeviceState *dev)
{
    MachineClass *mc = MACHINE_GET_CLASS(machine);

    if (device_is_dynamic_sysbus(mc, dev) ||
        object_dynamic_cast(OBJECT(dev), TYPE_LOONGARCH_CPU) ||
        object_dynamic_cast(OBJECT(dev), TYPE_VIRTIO_IOMMU_PCI) ||
        memhp_type_supported(dev)) {
        return HOTPLUG_HANDLER(machine);
    }
    return NULL;
}

static const CPUArchIdList *virt_possible_cpu_arch_ids(MachineState *ms)
{
    int n, arch_id;
    unsigned int max_cpus = ms->smp.max_cpus;
    LoongArchCPUTopo topo;

    if (ms->possible_cpus) {
        assert(ms->possible_cpus->len == max_cpus);
        return ms->possible_cpus;
    }

    ms->possible_cpus = g_malloc0(sizeof(CPUArchIdList) +
                                  sizeof(CPUArchId) * max_cpus);
    ms->possible_cpus->len = max_cpus;
    for (n = 0; n < ms->possible_cpus->len; n++) {
        virt_get_topo_from_index(ms, &topo, n);
        arch_id = virt_get_arch_id_from_topo(ms, &topo);
        ms->possible_cpus->cpus[n].type = ms->cpu_type;
        ms->possible_cpus->cpus[n].arch_id = arch_id;
        ms->possible_cpus->cpus[n].vcpus_count = 1;
        ms->possible_cpus->cpus[n].props.has_socket_id = true;
        ms->possible_cpus->cpus[n].props.socket_id = topo.socket_id;
        ms->possible_cpus->cpus[n].props.has_core_id = true;
        ms->possible_cpus->cpus[n].props.core_id = topo.core_id;
        ms->possible_cpus->cpus[n].props.has_thread_id = true;
        ms->possible_cpus->cpus[n].props.thread_id = topo.thread_id;
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
    mc->desc = "QEMU LoongArch Virtual Machine";
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
    mc->has_hotpluggable_cpus = true;
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
    machine_class_allow_dynamic_sysbus_dev(mc, TYPE_UEFI_VARS_SYSBUS);
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
