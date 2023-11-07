/* Support for generating ACPI tables and passing them to Guests
 *
 * ARM virt ACPI generation
 *
 * Copyright (C) 2008-2010  Kevin O'Connor <kevin@koconnor.net>
 * Copyright (C) 2006 Fabrice Bellard
 * Copyright (C) 2013 Red Hat Inc
 *
 * Author: Michael S. Tsirkin <mst@redhat.com>
 *
 * Copyright (c) 2015 HUAWEI TECHNOLOGIES CO.,LTD.
 *
 * Author: Shannon Zhao <zhaoshenglong@huawei.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/bitmap.h"
#include "qemu/error-report.h"
#include "trace.h"
#include "hw/core/cpu.h"
#include "target/arm/cpu.h"
#include "hw/acpi/acpi-defs.h"
#include "hw/acpi/acpi.h"
#include "hw/nvram/fw_cfg.h"
#include "hw/acpi/bios-linker-loader.h"
#include "hw/acpi/aml-build.h"
#include "hw/acpi/utils.h"
#include "hw/acpi/pci.h"
#include "hw/acpi/memory_hotplug.h"
#include "hw/acpi/generic_event_device.h"
#include "hw/acpi/tpm.h"
#include "hw/acpi/hmat.h"
#include "hw/pci/pcie_host.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci-host/gpex.h"
#include "hw/arm/virt.h"
#include "hw/intc/arm_gicv3_its_common.h"
#include "hw/mem/nvdimm.h"
#include "hw/platform-bus.h"
#include "sysemu/numa.h"
#include "sysemu/reset.h"
#include "sysemu/tpm.h"
#include "migration/vmstate.h"
#include "hw/acpi/ghes.h"
#include "hw/acpi/viot.h"

#define ARM_SPI_BASE 32

#define ACPI_BUILD_TABLE_SIZE             0x20000

static void acpi_dsdt_add_cpus(Aml *scope, VirtMachineState *vms)
{
    MachineState *ms = MACHINE(vms);
    uint16_t i;

    for (i = 0; i < ms->smp.cpus; i++) {
        Aml *dev = aml_device("C%.03X", i);
        aml_append(dev, aml_name_decl("_HID", aml_string("ACPI0007")));
        aml_append(dev, aml_name_decl("_UID", aml_int(i)));
        aml_append(scope, dev);
    }
}

static void acpi_dsdt_add_uart(Aml *scope, const MemMapEntry *uart_memmap,
                                           uint32_t uart_irq)
{
    Aml *dev = aml_device("COM0");
    aml_append(dev, aml_name_decl("_HID", aml_string("ARMH0011")));
    aml_append(dev, aml_name_decl("_UID", aml_int(0)));

    Aml *crs = aml_resource_template();
    aml_append(crs, aml_memory32_fixed(uart_memmap->base,
                                       uart_memmap->size, AML_READ_WRITE));
    aml_append(crs,
               aml_interrupt(AML_CONSUMER, AML_LEVEL, AML_ACTIVE_HIGH,
                             AML_EXCLUSIVE, &uart_irq, 1));
    aml_append(dev, aml_name_decl("_CRS", crs));

    aml_append(scope, dev);
}

static void acpi_dsdt_add_fw_cfg(Aml *scope, const MemMapEntry *fw_cfg_memmap)
{
    Aml *dev = aml_device("FWCF");
    aml_append(dev, aml_name_decl("_HID", aml_string("QEMU0002")));
    /* device present, functioning, decoding, not shown in UI */
    aml_append(dev, aml_name_decl("_STA", aml_int(0xB)));
    aml_append(dev, aml_name_decl("_CCA", aml_int(1)));

    Aml *crs = aml_resource_template();
    aml_append(crs, aml_memory32_fixed(fw_cfg_memmap->base,
                                       fw_cfg_memmap->size, AML_READ_WRITE));
    aml_append(dev, aml_name_decl("_CRS", crs));
    aml_append(scope, dev);
}

static void acpi_dsdt_add_flash(Aml *scope, const MemMapEntry *flash_memmap)
{
    Aml *dev, *crs;
    hwaddr base = flash_memmap->base;
    hwaddr size = flash_memmap->size / 2;

    dev = aml_device("FLS0");
    aml_append(dev, aml_name_decl("_HID", aml_string("LNRO0015")));
    aml_append(dev, aml_name_decl("_UID", aml_int(0)));

    crs = aml_resource_template();
    aml_append(crs, aml_memory32_fixed(base, size, AML_READ_WRITE));
    aml_append(dev, aml_name_decl("_CRS", crs));
    aml_append(scope, dev);

    dev = aml_device("FLS1");
    aml_append(dev, aml_name_decl("_HID", aml_string("LNRO0015")));
    aml_append(dev, aml_name_decl("_UID", aml_int(1)));
    crs = aml_resource_template();
    aml_append(crs, aml_memory32_fixed(base + size, size, AML_READ_WRITE));
    aml_append(dev, aml_name_decl("_CRS", crs));
    aml_append(scope, dev);
}

static void acpi_dsdt_add_virtio(Aml *scope,
                                 const MemMapEntry *virtio_mmio_memmap,
                                 uint32_t mmio_irq, int num)
{
    hwaddr base = virtio_mmio_memmap->base;
    hwaddr size = virtio_mmio_memmap->size;
    int i;

    for (i = 0; i < num; i++) {
        uint32_t irq = mmio_irq + i;
        Aml *dev = aml_device("VR%02u", i);
        aml_append(dev, aml_name_decl("_HID", aml_string("LNRO0005")));
        aml_append(dev, aml_name_decl("_UID", aml_int(i)));
        aml_append(dev, aml_name_decl("_CCA", aml_int(1)));

        Aml *crs = aml_resource_template();
        aml_append(crs, aml_memory32_fixed(base, size, AML_READ_WRITE));
        aml_append(crs,
                   aml_interrupt(AML_CONSUMER, AML_LEVEL, AML_ACTIVE_HIGH,
                                 AML_EXCLUSIVE, &irq, 1));
        aml_append(dev, aml_name_decl("_CRS", crs));
        aml_append(scope, dev);
        base += size;
    }
}

static void acpi_dsdt_add_pci(Aml *scope, const MemMapEntry *memmap,
                              uint32_t irq, VirtMachineState *vms)
{
    int ecam_id = VIRT_ECAM_ID(vms->highmem_ecam);
    struct GPEXConfig cfg = {
        .mmio32 = memmap[VIRT_PCIE_MMIO],
        .pio    = memmap[VIRT_PCIE_PIO],
        .ecam   = memmap[ecam_id],
        .irq    = irq,
        .bus    = vms->bus,
    };

    if (vms->highmem_mmio) {
        cfg.mmio64 = memmap[VIRT_HIGH_PCIE_MMIO];
    }

    acpi_dsdt_add_gpex(scope, &cfg);
}

static void acpi_dsdt_add_gpio(Aml *scope, const MemMapEntry *gpio_memmap,
                                           uint32_t gpio_irq)
{
    Aml *dev = aml_device("GPO0");
    aml_append(dev, aml_name_decl("_HID", aml_string("ARMH0061")));
    aml_append(dev, aml_name_decl("_UID", aml_int(0)));

    Aml *crs = aml_resource_template();
    aml_append(crs, aml_memory32_fixed(gpio_memmap->base, gpio_memmap->size,
                                       AML_READ_WRITE));
    aml_append(crs, aml_interrupt(AML_CONSUMER, AML_LEVEL, AML_ACTIVE_HIGH,
                                  AML_EXCLUSIVE, &gpio_irq, 1));
    aml_append(dev, aml_name_decl("_CRS", crs));

    Aml *aei = aml_resource_template();
    /* Pin 3 for power button */
    const uint32_t pin_list[1] = {3};
    aml_append(aei, aml_gpio_int(AML_CONSUMER, AML_EDGE, AML_ACTIVE_HIGH,
                                 AML_EXCLUSIVE, AML_PULL_UP, 0, pin_list, 1,
                                 "GPO0", NULL, 0));
    aml_append(dev, aml_name_decl("_AEI", aei));

    /* _E03 is handle for power button */
    Aml *method = aml_method("_E03", 0, AML_NOTSERIALIZED);
    aml_append(method, aml_notify(aml_name(ACPI_POWER_BUTTON_DEVICE),
                                  aml_int(0x80)));
    aml_append(dev, method);
    aml_append(scope, dev);
}

#ifdef CONFIG_TPM
static void acpi_dsdt_add_tpm(Aml *scope, VirtMachineState *vms)
{
    PlatformBusDevice *pbus = PLATFORM_BUS_DEVICE(vms->platform_bus_dev);
    hwaddr pbus_base = vms->memmap[VIRT_PLATFORM_BUS].base;
    SysBusDevice *sbdev = SYS_BUS_DEVICE(tpm_find());
    MemoryRegion *sbdev_mr;
    hwaddr tpm_base;

    if (!sbdev) {
        return;
    }

    tpm_base = platform_bus_get_mmio_addr(pbus, sbdev, 0);
    assert(tpm_base != -1);

    tpm_base += pbus_base;

    sbdev_mr = sysbus_mmio_get_region(sbdev, 0);

    Aml *dev = aml_device("TPM0");
    aml_append(dev, aml_name_decl("_HID", aml_string("MSFT0101")));
    aml_append(dev, aml_name_decl("_STR", aml_string("TPM 2.0 Device")));
    aml_append(dev, aml_name_decl("_UID", aml_int(0)));

    Aml *crs = aml_resource_template();
    aml_append(crs,
               aml_memory32_fixed(tpm_base,
                                  (uint32_t)memory_region_size(sbdev_mr),
                                  AML_READ_WRITE));
    aml_append(dev, aml_name_decl("_CRS", crs));
    aml_append(scope, dev);
}
#endif

#define ID_MAPPING_ENTRY_SIZE 20
#define SMMU_V3_ENTRY_SIZE 68
#define ROOT_COMPLEX_ENTRY_SIZE 36
#define IORT_NODE_OFFSET 48

static void build_iort_id_mapping(GArray *table_data, uint32_t input_base,
                                  uint32_t id_count, uint32_t out_ref)
{
    /* Table 4 ID mapping format */
    build_append_int_noprefix(table_data, input_base, 4); /* Input base */
    build_append_int_noprefix(table_data, id_count, 4); /* Number of IDs */
    build_append_int_noprefix(table_data, input_base, 4); /* Output base */
    build_append_int_noprefix(table_data, out_ref, 4); /* Output Reference */
    /* Flags */
    build_append_int_noprefix(table_data, 0 /* Single mapping (disabled) */, 4);
}

struct AcpiIortIdMapping {
    uint32_t input_base;
    uint32_t id_count;
};
typedef struct AcpiIortIdMapping AcpiIortIdMapping;

/* Build the iort ID mapping to SMMUv3 for a given PCI host bridge */
static int
iort_host_bridges(Object *obj, void *opaque)
{
    GArray *idmap_blob = opaque;

    if (object_dynamic_cast(obj, TYPE_PCI_HOST_BRIDGE)) {
        PCIBus *bus = PCI_HOST_BRIDGE(obj)->bus;

        if (bus && !pci_bus_bypass_iommu(bus)) {
            int min_bus, max_bus;

            pci_bus_range(bus, &min_bus, &max_bus);

            AcpiIortIdMapping idmap = {
                .input_base = min_bus << 8,
                .id_count = (max_bus - min_bus + 1) << 8,
            };
            g_array_append_val(idmap_blob, idmap);
        }
    }

    return 0;
}

static int iort_idmap_compare(gconstpointer a, gconstpointer b)
{
    AcpiIortIdMapping *idmap_a = (AcpiIortIdMapping *)a;
    AcpiIortIdMapping *idmap_b = (AcpiIortIdMapping *)b;

    return idmap_a->input_base - idmap_b->input_base;
}

/*
 * Input Output Remapping Table (IORT)
 * Conforms to "IO Remapping Table System Software on ARM Platforms",
 * Document number: ARM DEN 0049E.b, Feb 2021
 */
static void
build_iort(GArray *table_data, BIOSLinker *linker, VirtMachineState *vms)
{
    int i, nb_nodes, rc_mapping_count;
    const uint32_t iort_node_offset = IORT_NODE_OFFSET;
    size_t node_size, smmu_offset = 0;
    AcpiIortIdMapping *idmap;
    uint32_t id = 0;
    GArray *smmu_idmaps = g_array_new(false, true, sizeof(AcpiIortIdMapping));
    GArray *its_idmaps = g_array_new(false, true, sizeof(AcpiIortIdMapping));

    AcpiTable table = { .sig = "IORT", .rev = 3, .oem_id = vms->oem_id,
                        .oem_table_id = vms->oem_table_id };
    /* Table 2 The IORT */
    acpi_table_begin(&table, table_data);

    if (vms->iommu == VIRT_IOMMU_SMMUV3) {
        AcpiIortIdMapping next_range = {0};

        object_child_foreach_recursive(object_get_root(),
                                       iort_host_bridges, smmu_idmaps);

        /* Sort the smmu idmap by input_base */
        g_array_sort(smmu_idmaps, iort_idmap_compare);

        /*
         * Split the whole RIDs by mapping from RC to SMMU,
         * build the ID mapping from RC to ITS directly.
         */
        for (i = 0; i < smmu_idmaps->len; i++) {
            idmap = &g_array_index(smmu_idmaps, AcpiIortIdMapping, i);

            if (next_range.input_base < idmap->input_base) {
                next_range.id_count = idmap->input_base - next_range.input_base;
                g_array_append_val(its_idmaps, next_range);
            }

            next_range.input_base = idmap->input_base + idmap->id_count;
        }

        /* Append the last RC -> ITS ID mapping */
        if (next_range.input_base < 0xFFFF) {
            next_range.id_count = 0xFFFF - next_range.input_base;
            g_array_append_val(its_idmaps, next_range);
        }

        nb_nodes = 3; /* RC, ITS, SMMUv3 */
        rc_mapping_count = smmu_idmaps->len + its_idmaps->len;
    } else {
        nb_nodes = 2; /* RC, ITS */
        rc_mapping_count = 1;
    }
    /* Number of IORT Nodes */
    build_append_int_noprefix(table_data, nb_nodes, 4);

    /* Offset to Array of IORT Nodes */
    build_append_int_noprefix(table_data, IORT_NODE_OFFSET, 4);
    build_append_int_noprefix(table_data, 0, 4); /* Reserved */

    /* Table 12 ITS Group Format */
    build_append_int_noprefix(table_data, 0 /* ITS Group */, 1); /* Type */
    node_size =  20 /* fixed header size */ + 4 /* 1 GIC ITS Identifier */;
    build_append_int_noprefix(table_data, node_size, 2); /* Length */
    build_append_int_noprefix(table_data, 1, 1); /* Revision */
    build_append_int_noprefix(table_data, id++, 4); /* Identifier */
    build_append_int_noprefix(table_data, 0, 4); /* Number of ID mappings */
    build_append_int_noprefix(table_data, 0, 4); /* Reference to ID Array */
    build_append_int_noprefix(table_data, 1, 4); /* Number of ITSs */
    /* GIC ITS Identifier Array */
    build_append_int_noprefix(table_data, 0 /* MADT translation_id */, 4);

    if (vms->iommu == VIRT_IOMMU_SMMUV3) {
        int irq =  vms->irqmap[VIRT_SMMU] + ARM_SPI_BASE;

        smmu_offset = table_data->len - table.table_offset;
        /* Table 9 SMMUv3 Format */
        build_append_int_noprefix(table_data, 4 /* SMMUv3 */, 1); /* Type */
        node_size =  SMMU_V3_ENTRY_SIZE + ID_MAPPING_ENTRY_SIZE;
        build_append_int_noprefix(table_data, node_size, 2); /* Length */
        build_append_int_noprefix(table_data, 4, 1); /* Revision */
        build_append_int_noprefix(table_data, id++, 4); /* Identifier */
        build_append_int_noprefix(table_data, 1, 4); /* Number of ID mappings */
        /* Reference to ID Array */
        build_append_int_noprefix(table_data, SMMU_V3_ENTRY_SIZE, 4);
        /* Base address */
        build_append_int_noprefix(table_data, vms->memmap[VIRT_SMMU].base, 8);
        /* Flags */
        build_append_int_noprefix(table_data, 1 /* COHACC Override */, 4);
        build_append_int_noprefix(table_data, 0, 4); /* Reserved */
        build_append_int_noprefix(table_data, 0, 8); /* VATOS address */
        /* Model */
        build_append_int_noprefix(table_data, 0 /* Generic SMMU-v3 */, 4);
        build_append_int_noprefix(table_data, irq, 4); /* Event */
        build_append_int_noprefix(table_data, irq + 1, 4); /* PRI */
        build_append_int_noprefix(table_data, irq + 3, 4); /* GERR */
        build_append_int_noprefix(table_data, irq + 2, 4); /* Sync */
        build_append_int_noprefix(table_data, 0, 4); /* Proximity domain */
        /* DeviceID mapping index (ignored since interrupts are GSIV based) */
        build_append_int_noprefix(table_data, 0, 4);

        /* output IORT node is the ITS group node (the first node) */
        build_iort_id_mapping(table_data, 0, 0xFFFF, IORT_NODE_OFFSET);
    }

    /* Table 17 Root Complex Node */
    build_append_int_noprefix(table_data, 2 /* Root complex */, 1); /* Type */
    node_size =  ROOT_COMPLEX_ENTRY_SIZE +
                 ID_MAPPING_ENTRY_SIZE * rc_mapping_count;
    build_append_int_noprefix(table_data, node_size, 2); /* Length */
    build_append_int_noprefix(table_data, 3, 1); /* Revision */
    build_append_int_noprefix(table_data, id++, 4); /* Identifier */
    /* Number of ID mappings */
    build_append_int_noprefix(table_data, rc_mapping_count, 4);
    /* Reference to ID Array */
    build_append_int_noprefix(table_data, ROOT_COMPLEX_ENTRY_SIZE, 4);

    /* Table 14 Memory access properties */
    /* CCA: Cache Coherent Attribute */
    build_append_int_noprefix(table_data, 1 /* fully coherent */, 4);
    build_append_int_noprefix(table_data, 0, 1); /* AH: Note Allocation Hints */
    build_append_int_noprefix(table_data, 0, 2); /* Reserved */
    /* Table 15 Memory Access Flags */
    build_append_int_noprefix(table_data, 0x3 /* CCA = CPM = DACS = 1 */, 1);

    build_append_int_noprefix(table_data, 0, 4); /* ATS Attribute */
    /* MCFG pci_segment */
    build_append_int_noprefix(table_data, 0, 4); /* PCI Segment number */

    /* Memory address size limit */
    build_append_int_noprefix(table_data, 64, 1);

    build_append_int_noprefix(table_data, 0, 3); /* Reserved */

    /* Output Reference */
    if (vms->iommu == VIRT_IOMMU_SMMUV3) {
        AcpiIortIdMapping *range;

        /* translated RIDs connect to SMMUv3 node: RC -> SMMUv3 -> ITS */
        for (i = 0; i < smmu_idmaps->len; i++) {
            range = &g_array_index(smmu_idmaps, AcpiIortIdMapping, i);
            /* output IORT node is the smmuv3 node */
            build_iort_id_mapping(table_data, range->input_base,
                                  range->id_count, smmu_offset);
        }

        /* bypassed RIDs connect to ITS group node directly: RC -> ITS */
        for (i = 0; i < its_idmaps->len; i++) {
            range = &g_array_index(its_idmaps, AcpiIortIdMapping, i);
            /* output IORT node is the ITS group node (the first node) */
            build_iort_id_mapping(table_data, range->input_base,
                                  range->id_count, iort_node_offset);
        }
    } else {
        /* output IORT node is the ITS group node (the first node) */
        build_iort_id_mapping(table_data, 0, 0xFFFF, IORT_NODE_OFFSET);
    }

    acpi_table_end(linker, &table);
    g_array_free(smmu_idmaps, true);
    g_array_free(its_idmaps, true);
}

/*
 * Serial Port Console Redirection Table (SPCR)
 * Rev: 1.07
 */
static void
build_spcr(GArray *table_data, BIOSLinker *linker, VirtMachineState *vms)
{
    AcpiTable table = { .sig = "SPCR", .rev = 2, .oem_id = vms->oem_id,
                        .oem_table_id = vms->oem_table_id };

    acpi_table_begin(&table, table_data);

    /* Interface Type */
    build_append_int_noprefix(table_data, 3, 1); /* ARM PL011 UART */
    build_append_int_noprefix(table_data, 0, 3); /* Reserved */
    /* Base Address */
    build_append_gas(table_data, AML_AS_SYSTEM_MEMORY, 32, 0, 3,
                     vms->memmap[VIRT_UART].base);
    /* Interrupt Type */
    build_append_int_noprefix(table_data,
        (1 << 3) /* Bit[3] ARMH GIC interrupt */, 1);
    build_append_int_noprefix(table_data, 0, 1); /* IRQ */
    /* Global System Interrupt */
    build_append_int_noprefix(table_data,
                              vms->irqmap[VIRT_UART] + ARM_SPI_BASE, 4);
    build_append_int_noprefix(table_data, 3 /* 9600 */, 1); /* Baud Rate */
    build_append_int_noprefix(table_data, 0 /* No Parity */, 1); /* Parity */
    /* Stop Bits */
    build_append_int_noprefix(table_data, 1 /* 1 Stop bit */, 1);
    /* Flow Control */
    build_append_int_noprefix(table_data,
        (1 << 1) /* RTS/CTS hardware flow control */, 1);
    /* Terminal Type */
    build_append_int_noprefix(table_data, 0 /* VT100 */, 1);
    build_append_int_noprefix(table_data, 0, 1); /* Language */
    /* PCI Device ID  */
    build_append_int_noprefix(table_data, 0xffff /* not a PCI device*/, 2);
    /* PCI Vendor ID */
    build_append_int_noprefix(table_data, 0xffff /* not a PCI device*/, 2);
    build_append_int_noprefix(table_data, 0, 1); /* PCI Bus Number */
    build_append_int_noprefix(table_data, 0, 1); /* PCI Device Number */
    build_append_int_noprefix(table_data, 0, 1); /* PCI Function Number */
    build_append_int_noprefix(table_data, 0, 4); /* PCI Flags */
    build_append_int_noprefix(table_data, 0, 1); /* PCI Segment */
    build_append_int_noprefix(table_data, 0, 4); /* Reserved */

    acpi_table_end(linker, &table);
}

/*
 * ACPI spec, Revision 5.1
 * 5.2.16 System Resource Affinity Table (SRAT)
 */
static void
build_srat(GArray *table_data, BIOSLinker *linker, VirtMachineState *vms)
{
    int i;
    uint64_t mem_base;
    MachineClass *mc = MACHINE_GET_CLASS(vms);
    MachineState *ms = MACHINE(vms);
    const CPUArchIdList *cpu_list = mc->possible_cpu_arch_ids(ms);
    AcpiTable table = { .sig = "SRAT", .rev = 3, .oem_id = vms->oem_id,
                        .oem_table_id = vms->oem_table_id };

    acpi_table_begin(&table, table_data);
    build_append_int_noprefix(table_data, 1, 4); /* Reserved */
    build_append_int_noprefix(table_data, 0, 8); /* Reserved */

    for (i = 0; i < cpu_list->len; ++i) {
        uint32_t nodeid = cpu_list->cpus[i].props.node_id;
        /*
         * 5.2.16.4 GICC Affinity Structure
         */
        build_append_int_noprefix(table_data, 3, 1);      /* Type */
        build_append_int_noprefix(table_data, 18, 1);     /* Length */
        build_append_int_noprefix(table_data, nodeid, 4); /* Proximity Domain */
        build_append_int_noprefix(table_data, i, 4); /* ACPI Processor UID */
        /* Flags, Table 5-76 */
        build_append_int_noprefix(table_data, 1 /* Enabled */, 4);
        build_append_int_noprefix(table_data, 0, 4); /* Clock Domain */
    }

    mem_base = vms->memmap[VIRT_MEM].base;
    for (i = 0; i < ms->numa_state->num_nodes; ++i) {
        if (ms->numa_state->nodes[i].node_mem > 0) {
            build_srat_memory(table_data, mem_base,
                              ms->numa_state->nodes[i].node_mem, i,
                              MEM_AFFINITY_ENABLED);
            mem_base += ms->numa_state->nodes[i].node_mem;
        }
    }

    if (ms->nvdimms_state->is_enabled) {
        nvdimm_build_srat(table_data);
    }

    if (ms->device_memory) {
        build_srat_memory(table_data, ms->device_memory->base,
                          memory_region_size(&ms->device_memory->mr),
                          ms->numa_state->num_nodes - 1,
                          MEM_AFFINITY_HOTPLUGGABLE | MEM_AFFINITY_ENABLED);
    }

    acpi_table_end(linker, &table);
}

/*
 * ACPI spec, Revision 5.1
 * 5.2.24 Generic Timer Description Table (GTDT)
 */
static void
build_gtdt(GArray *table_data, BIOSLinker *linker, VirtMachineState *vms)
{
    VirtMachineClass *vmc = VIRT_MACHINE_GET_CLASS(vms);
    /*
     * Table 5-117 Flag Definitions
     * set only "Timer interrupt Mode" and assume "Timer Interrupt
     * polarity" bit as '0: Interrupt is Active high'
     */
    uint32_t irqflags = vmc->claim_edge_triggered_timers ?
        1 : /* Interrupt is Edge triggered */
        0;  /* Interrupt is Level triggered  */
    AcpiTable table = { .sig = "GTDT", .rev = 2, .oem_id = vms->oem_id,
                        .oem_table_id = vms->oem_table_id };

    acpi_table_begin(&table, table_data);

    /* CntControlBase Physical Address */
    build_append_int_noprefix(table_data, 0xFFFFFFFFFFFFFFFF, 8);
    build_append_int_noprefix(table_data, 0, 4); /* Reserved */
    /*
     * FIXME: clarify comment:
     * The interrupt values are the same with the device tree when adding 16
     */
    /* Secure EL1 timer GSIV */
    build_append_int_noprefix(table_data, ARCH_TIMER_S_EL1_IRQ, 4);
    /* Secure EL1 timer Flags */
    build_append_int_noprefix(table_data, irqflags, 4);
    /* Non-Secure EL1 timer GSIV */
    build_append_int_noprefix(table_data, ARCH_TIMER_NS_EL1_IRQ, 4);
    /* Non-Secure EL1 timer Flags */
    build_append_int_noprefix(table_data, irqflags |
                              1UL << 2, /* Always-on Capability */
                              4);
    /* Virtual timer GSIV */
    build_append_int_noprefix(table_data, ARCH_TIMER_VIRT_IRQ, 4);
    /* Virtual Timer Flags */
    build_append_int_noprefix(table_data, irqflags, 4);
    /* Non-Secure EL2 timer GSIV */
    build_append_int_noprefix(table_data, ARCH_TIMER_NS_EL2_IRQ, 4);
    /* Non-Secure EL2 timer Flags */
    build_append_int_noprefix(table_data, irqflags, 4);
    /* CntReadBase Physical address */
    build_append_int_noprefix(table_data, 0xFFFFFFFFFFFFFFFF, 8);
    /* Platform Timer Count */
    build_append_int_noprefix(table_data, 0, 4);
    /* Platform Timer Offset */
    build_append_int_noprefix(table_data, 0, 4);

    acpi_table_end(linker, &table);
}

/* Debug Port Table 2 (DBG2) */
static void
build_dbg2(GArray *table_data, BIOSLinker *linker, VirtMachineState *vms)
{
    AcpiTable table = { .sig = "DBG2", .rev = 0, .oem_id = vms->oem_id,
                        .oem_table_id = vms->oem_table_id };
    int dbg2devicelength;
    const char name[] = "COM0";
    const int namespace_length = sizeof(name);

    acpi_table_begin(&table, table_data);

    dbg2devicelength = 22 + /* BaseAddressRegister[] offset */
                       12 + /* BaseAddressRegister[] */
                       4 + /* AddressSize[] */
                       namespace_length /* NamespaceString[] */;

    /* OffsetDbgDeviceInfo */
    build_append_int_noprefix(table_data, 44, 4);
    /* NumberDbgDeviceInfo */
    build_append_int_noprefix(table_data, 1, 4);

    /* Table 2. Debug Device Information structure format */
    build_append_int_noprefix(table_data, 0, 1); /* Revision */
    build_append_int_noprefix(table_data, dbg2devicelength, 2); /* Length */
    /* NumberofGenericAddressRegisters */
    build_append_int_noprefix(table_data, 1, 1);
    /* NameSpaceStringLength */
    build_append_int_noprefix(table_data, namespace_length, 2);
    build_append_int_noprefix(table_data, 38, 2); /* NameSpaceStringOffset */
    build_append_int_noprefix(table_data, 0, 2); /* OemDataLength */
    /* OemDataOffset (0 means no OEM data) */
    build_append_int_noprefix(table_data, 0, 2);

    /* Port Type */
    build_append_int_noprefix(table_data, 0x8000 /* Serial */, 2);
    /* Port Subtype */
    build_append_int_noprefix(table_data, 0x3 /* ARM PL011 UART */, 2);
    build_append_int_noprefix(table_data, 0, 2); /* Reserved */
    /* BaseAddressRegisterOffset */
    build_append_int_noprefix(table_data, 22, 2);
    /* AddressSizeOffset */
    build_append_int_noprefix(table_data, 34, 2);

    /* BaseAddressRegister[] */
    build_append_gas(table_data, AML_AS_SYSTEM_MEMORY, 32, 0, 3,
                     vms->memmap[VIRT_UART].base);

    /* AddressSize[] */
    build_append_int_noprefix(table_data,
                              vms->memmap[VIRT_UART].size, 4);

    /* NamespaceString[] */
    g_array_append_vals(table_data, name, namespace_length);

    acpi_table_end(linker, &table);
};

/*
 * ACPI spec, Revision 6.0 Errata A
 * 5.2.12 Multiple APIC Description Table (MADT)
 */
static void build_append_gicr(GArray *table_data, uint64_t base, uint32_t size)
{
    build_append_int_noprefix(table_data, 0xE, 1);  /* Type */
    build_append_int_noprefix(table_data, 16, 1);   /* Length */
    build_append_int_noprefix(table_data, 0, 2);    /* Reserved */
    /* Discovery Range Base Address */
    build_append_int_noprefix(table_data, base, 8);
    build_append_int_noprefix(table_data, size, 4); /* Discovery Range Length */
}

static void
build_madt(GArray *table_data, BIOSLinker *linker, VirtMachineState *vms)
{
    int i;
    VirtMachineClass *vmc = VIRT_MACHINE_GET_CLASS(vms);
    const MemMapEntry *memmap = vms->memmap;
    AcpiTable table = { .sig = "APIC", .rev = 4, .oem_id = vms->oem_id,
                        .oem_table_id = vms->oem_table_id };

    acpi_table_begin(&table, table_data);
    /* Local Interrupt Controller Address */
    build_append_int_noprefix(table_data, 0, 4);
    build_append_int_noprefix(table_data, 0, 4);   /* Flags */

    /* 5.2.12.15 GIC Distributor Structure */
    build_append_int_noprefix(table_data, 0xC, 1); /* Type */
    build_append_int_noprefix(table_data, 24, 1);  /* Length */
    build_append_int_noprefix(table_data, 0, 2);   /* Reserved */
    build_append_int_noprefix(table_data, 0, 4);   /* GIC ID */
    /* Physical Base Address */
    build_append_int_noprefix(table_data, memmap[VIRT_GIC_DIST].base, 8);
    build_append_int_noprefix(table_data, 0, 4);   /* System Vector Base */
    /* GIC version */
    build_append_int_noprefix(table_data, vms->gic_version, 1);
    build_append_int_noprefix(table_data, 0, 3);   /* Reserved */

    for (i = 0; i < MACHINE(vms)->smp.cpus; i++) {
        ARMCPU *armcpu = ARM_CPU(qemu_get_cpu(i));
        uint64_t physical_base_address = 0, gich = 0, gicv = 0;
        uint32_t vgic_interrupt = vms->virt ? ARCH_GIC_MAINT_IRQ : 0;
        uint32_t pmu_interrupt = arm_feature(&armcpu->env, ARM_FEATURE_PMU) ?
                                             VIRTUAL_PMU_IRQ : 0;

        if (vms->gic_version == VIRT_GIC_VERSION_2) {
            physical_base_address = memmap[VIRT_GIC_CPU].base;
            gicv = memmap[VIRT_GIC_VCPU].base;
            gich = memmap[VIRT_GIC_HYP].base;
        }

        /* 5.2.12.14 GIC Structure */
        build_append_int_noprefix(table_data, 0xB, 1);  /* Type */
        build_append_int_noprefix(table_data, 80, 1);   /* Length */
        build_append_int_noprefix(table_data, 0, 2);    /* Reserved */
        build_append_int_noprefix(table_data, i, 4);    /* GIC ID */
        build_append_int_noprefix(table_data, i, 4);    /* ACPI Processor UID */
        /* Flags */
        build_append_int_noprefix(table_data, 1, 4);    /* Enabled */
        /* Parking Protocol Version */
        build_append_int_noprefix(table_data, 0, 4);
        /* Performance Interrupt GSIV */
        build_append_int_noprefix(table_data, pmu_interrupt, 4);
        build_append_int_noprefix(table_data, 0, 8); /* Parked Address */
        /* Physical Base Address */
        build_append_int_noprefix(table_data, physical_base_address, 8);
        build_append_int_noprefix(table_data, gicv, 8); /* GICV */
        build_append_int_noprefix(table_data, gich, 8); /* GICH */
        /* VGIC Maintenance interrupt */
        build_append_int_noprefix(table_data, vgic_interrupt, 4);
        build_append_int_noprefix(table_data, 0, 8);    /* GICR Base Address*/
        /* MPIDR */
        build_append_int_noprefix(table_data, armcpu->mp_affinity, 8);
        /* Processor Power Efficiency Class */
        build_append_int_noprefix(table_data, 0, 1);
        /* Reserved */
        build_append_int_noprefix(table_data, 0, 3);
    }

    if (vms->gic_version != VIRT_GIC_VERSION_2) {
        build_append_gicr(table_data, memmap[VIRT_GIC_REDIST].base,
                                      memmap[VIRT_GIC_REDIST].size);
        if (virt_gicv3_redist_region_count(vms) == 2) {
            build_append_gicr(table_data, memmap[VIRT_HIGH_GIC_REDIST2].base,
                                          memmap[VIRT_HIGH_GIC_REDIST2].size);
        }

        if (its_class_name() && !vmc->no_its) {
            /*
             * ACPI spec, Revision 6.0 Errata A
             * (original 6.0 definition has invalid Length)
             * 5.2.12.18 GIC ITS Structure
             */
            build_append_int_noprefix(table_data, 0xF, 1);  /* Type */
            build_append_int_noprefix(table_data, 20, 1);   /* Length */
            build_append_int_noprefix(table_data, 0, 2);    /* Reserved */
            build_append_int_noprefix(table_data, 0, 4);    /* GIC ITS ID */
            /* Physical Base Address */
            build_append_int_noprefix(table_data, memmap[VIRT_GIC_ITS].base, 8);
            build_append_int_noprefix(table_data, 0, 4);    /* Reserved */
        }
    } else {
        const uint16_t spi_base = vms->irqmap[VIRT_GIC_V2M] + ARM_SPI_BASE;

        /* 5.2.12.16 GIC MSI Frame Structure */
        build_append_int_noprefix(table_data, 0xD, 1);  /* Type */
        build_append_int_noprefix(table_data, 24, 1);   /* Length */
        build_append_int_noprefix(table_data, 0, 2);    /* Reserved */
        build_append_int_noprefix(table_data, 0, 4);    /* GIC MSI Frame ID */
        /* Physical Base Address */
        build_append_int_noprefix(table_data, memmap[VIRT_GIC_V2M].base, 8);
        build_append_int_noprefix(table_data, 1, 4);    /* Flags */
        /* SPI Count */
        build_append_int_noprefix(table_data, NUM_GICV2M_SPIS, 2);
        build_append_int_noprefix(table_data, spi_base, 2); /* SPI Base */
    }
    acpi_table_end(linker, &table);
}

/* FADT */
static void build_fadt_rev6(GArray *table_data, BIOSLinker *linker,
                            VirtMachineState *vms, unsigned dsdt_tbl_offset)
{
    /* ACPI v6.0 */
    AcpiFadtData fadt = {
        .rev = 6,
        .minor_ver = 0,
        .flags = 1 << ACPI_FADT_F_HW_REDUCED_ACPI,
        .xdsdt_tbl_offset = &dsdt_tbl_offset,
    };

    switch (vms->psci_conduit) {
    case QEMU_PSCI_CONDUIT_DISABLED:
        fadt.arm_boot_arch = 0;
        break;
    case QEMU_PSCI_CONDUIT_HVC:
        fadt.arm_boot_arch = ACPI_FADT_ARM_PSCI_COMPLIANT |
                             ACPI_FADT_ARM_PSCI_USE_HVC;
        break;
    case QEMU_PSCI_CONDUIT_SMC:
        fadt.arm_boot_arch = ACPI_FADT_ARM_PSCI_COMPLIANT;
        break;
    default:
        g_assert_not_reached();
    }

    build_fadt(table_data, linker, &fadt, vms->oem_id, vms->oem_table_id);
}

/* DSDT */
static void
build_dsdt(GArray *table_data, BIOSLinker *linker, VirtMachineState *vms)
{
    VirtMachineClass *vmc = VIRT_MACHINE_GET_CLASS(vms);
    Aml *scope, *dsdt;
    MachineState *ms = MACHINE(vms);
    const MemMapEntry *memmap = vms->memmap;
    const int *irqmap = vms->irqmap;
    AcpiTable table = { .sig = "DSDT", .rev = 2, .oem_id = vms->oem_id,
                        .oem_table_id = vms->oem_table_id };

    acpi_table_begin(&table, table_data);
    dsdt = init_aml_allocator();

    /* When booting the VM with UEFI, UEFI takes ownership of the RTC hardware.
     * While UEFI can use libfdt to disable the RTC device node in the DTB that
     * it passes to the OS, it cannot modify AML. Therefore, we won't generate
     * the RTC ACPI device at all when using UEFI.
     */
    scope = aml_scope("\\_SB");
    acpi_dsdt_add_cpus(scope, vms);
    acpi_dsdt_add_uart(scope, &memmap[VIRT_UART],
                       (irqmap[VIRT_UART] + ARM_SPI_BASE));
    if (vmc->acpi_expose_flash) {
        acpi_dsdt_add_flash(scope, &memmap[VIRT_FLASH]);
    }
    acpi_dsdt_add_fw_cfg(scope, &memmap[VIRT_FW_CFG]);
    acpi_dsdt_add_virtio(scope, &memmap[VIRT_MMIO],
                    (irqmap[VIRT_MMIO] + ARM_SPI_BASE), NUM_VIRTIO_TRANSPORTS);
    acpi_dsdt_add_pci(scope, memmap, irqmap[VIRT_PCIE] + ARM_SPI_BASE, vms);
    if (vms->acpi_dev) {
        build_ged_aml(scope, "\\_SB."GED_DEVICE,
                      HOTPLUG_HANDLER(vms->acpi_dev),
                      irqmap[VIRT_ACPI_GED] + ARM_SPI_BASE, AML_SYSTEM_MEMORY,
                      memmap[VIRT_ACPI_GED].base);
    } else {
        acpi_dsdt_add_gpio(scope, &memmap[VIRT_GPIO],
                           (irqmap[VIRT_GPIO] + ARM_SPI_BASE));
    }

    if (vms->acpi_dev) {
        uint32_t event = object_property_get_uint(OBJECT(vms->acpi_dev),
                                                  "ged-event", &error_abort);

        if (event & ACPI_GED_MEM_HOTPLUG_EVT) {
            build_memory_hotplug_aml(scope, ms->ram_slots, "\\_SB", NULL,
                                     AML_SYSTEM_MEMORY,
                                     memmap[VIRT_PCDIMM_ACPI].base);
        }
    }

    acpi_dsdt_add_power_button(scope);
#ifdef CONFIG_TPM
    acpi_dsdt_add_tpm(scope, vms);
#endif

    aml_append(dsdt, scope);

    /* copy AML table into ACPI tables blob */
    g_array_append_vals(table_data, dsdt->buf->data, dsdt->buf->len);

    acpi_table_end(linker, &table);
    free_aml_allocator();
}

typedef
struct AcpiBuildState {
    /* Copy of table in RAM (for patching). */
    MemoryRegion *table_mr;
    MemoryRegion *rsdp_mr;
    MemoryRegion *linker_mr;
    /* Is table patched? */
    bool patched;
} AcpiBuildState;

static void acpi_align_size(GArray *blob, unsigned align)
{
    /*
     * Align size to multiple of given size. This reduces the chance
     * we need to change size in the future (breaking cross version migration).
     */
    g_array_set_size(blob, ROUND_UP(acpi_data_len(blob), align));
}

static
void virt_acpi_build(VirtMachineState *vms, AcpiBuildTables *tables)
{
    VirtMachineClass *vmc = VIRT_MACHINE_GET_CLASS(vms);
    GArray *table_offsets;
    unsigned dsdt, xsdt;
    GArray *tables_blob = tables->table_data;
    MachineState *ms = MACHINE(vms);

    table_offsets = g_array_new(false, true /* clear */,
                                        sizeof(uint32_t));

    bios_linker_loader_alloc(tables->linker,
                             ACPI_BUILD_TABLE_FILE, tables_blob,
                             64, false /* high memory */);

    /* DSDT is pointed to by FADT */
    dsdt = tables_blob->len;
    build_dsdt(tables_blob, tables->linker, vms);

    /* FADT MADT PPTT GTDT MCFG SPCR DBG2 pointed to by RSDT */
    acpi_add_table(table_offsets, tables_blob);
    build_fadt_rev6(tables_blob, tables->linker, vms, dsdt);

    acpi_add_table(table_offsets, tables_blob);
    build_madt(tables_blob, tables->linker, vms);

    if (!vmc->no_cpu_topology) {
        acpi_add_table(table_offsets, tables_blob);
        build_pptt(tables_blob, tables->linker, ms,
                   vms->oem_id, vms->oem_table_id);
    }

    acpi_add_table(table_offsets, tables_blob);
    build_gtdt(tables_blob, tables->linker, vms);

    acpi_add_table(table_offsets, tables_blob);
    {
        AcpiMcfgInfo mcfg = {
           .base = vms->memmap[VIRT_ECAM_ID(vms->highmem_ecam)].base,
           .size = vms->memmap[VIRT_ECAM_ID(vms->highmem_ecam)].size,
        };
        build_mcfg(tables_blob, tables->linker, &mcfg, vms->oem_id,
                   vms->oem_table_id);
    }

    acpi_add_table(table_offsets, tables_blob);
    build_spcr(tables_blob, tables->linker, vms);

    acpi_add_table(table_offsets, tables_blob);
    build_dbg2(tables_blob, tables->linker, vms);

    if (vms->ras) {
        build_ghes_error_table(tables->hardware_errors, tables->linker);
        acpi_add_table(table_offsets, tables_blob);
        acpi_build_hest(tables_blob, tables->linker, vms->oem_id,
                        vms->oem_table_id);
    }

    if (ms->numa_state->num_nodes > 0) {
        acpi_add_table(table_offsets, tables_blob);
        build_srat(tables_blob, tables->linker, vms);
        if (ms->numa_state->have_numa_distance) {
            acpi_add_table(table_offsets, tables_blob);
            build_slit(tables_blob, tables->linker, ms, vms->oem_id,
                       vms->oem_table_id);
        }

        if (ms->numa_state->hmat_enabled) {
            acpi_add_table(table_offsets, tables_blob);
            build_hmat(tables_blob, tables->linker, ms->numa_state,
                       vms->oem_id, vms->oem_table_id);
        }
    }

    if (ms->nvdimms_state->is_enabled) {
        nvdimm_build_acpi(table_offsets, tables_blob, tables->linker,
                          ms->nvdimms_state, ms->ram_slots, vms->oem_id,
                          vms->oem_table_id);
    }

    if (its_class_name() && !vmc->no_its) {
        acpi_add_table(table_offsets, tables_blob);
        build_iort(tables_blob, tables->linker, vms);
    }

#ifdef CONFIG_TPM
    if (tpm_get_version(tpm_find()) == TPM_VERSION_2_0) {
        acpi_add_table(table_offsets, tables_blob);
        build_tpm2(tables_blob, tables->linker, tables->tcpalog, vms->oem_id,
                   vms->oem_table_id);
    }
#endif

    if (vms->iommu == VIRT_IOMMU_VIRTIO) {
        acpi_add_table(table_offsets, tables_blob);
        build_viot(ms, tables_blob, tables->linker, vms->virtio_iommu_bdf,
                   vms->oem_id, vms->oem_table_id);
    }

    /* XSDT is pointed to by RSDP */
    xsdt = tables_blob->len;
    build_xsdt(tables_blob, tables->linker, table_offsets, vms->oem_id,
               vms->oem_table_id);

    /* RSDP is in FSEG memory, so allocate it separately */
    {
        AcpiRsdpData rsdp_data = {
            .revision = 2,
            .oem_id = vms->oem_id,
            .xsdt_tbl_offset = &xsdt,
            .rsdt_tbl_offset = NULL,
        };
        build_rsdp(tables->rsdp, tables->linker, &rsdp_data);
    }

    /*
     * The align size is 128, warn if 64k is not enough therefore
     * the align size could be resized.
     */
    if (tables_blob->len > ACPI_BUILD_TABLE_SIZE / 2) {
        warn_report("ACPI table size %u exceeds %d bytes,"
                    " migration may not work",
                    tables_blob->len, ACPI_BUILD_TABLE_SIZE / 2);
        error_printf("Try removing CPUs, NUMA nodes, memory slots"
                     " or PCI bridges.");
    }
    acpi_align_size(tables_blob, ACPI_BUILD_TABLE_SIZE);


    /* Cleanup memory that's no longer used. */
    g_array_free(table_offsets, true);
}

static void acpi_ram_update(MemoryRegion *mr, GArray *data)
{
    uint32_t size = acpi_data_len(data);

    /* Make sure RAM size is correct - in case it got changed
     * e.g. by migration */
    memory_region_ram_resize(mr, size, &error_abort);

    memcpy(memory_region_get_ram_ptr(mr), data->data, size);
    memory_region_set_dirty(mr, 0, size);
}

static void virt_acpi_build_update(void *build_opaque)
{
    AcpiBuildState *build_state = build_opaque;
    AcpiBuildTables tables;

    /* No state to update or already patched? Nothing to do. */
    if (!build_state || build_state->patched) {
        return;
    }
    build_state->patched = true;

    acpi_build_tables_init(&tables);

    virt_acpi_build(VIRT_MACHINE(qdev_get_machine()), &tables);

    acpi_ram_update(build_state->table_mr, tables.table_data);
    acpi_ram_update(build_state->rsdp_mr, tables.rsdp);
    acpi_ram_update(build_state->linker_mr, tables.linker->cmd_blob);

    acpi_build_tables_cleanup(&tables, true);
}

static void virt_acpi_build_reset(void *build_opaque)
{
    AcpiBuildState *build_state = build_opaque;
    build_state->patched = false;
}

static const VMStateDescription vmstate_virt_acpi_build = {
    .name = "virt_acpi_build",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_BOOL(patched, AcpiBuildState),
        VMSTATE_END_OF_LIST()
    },
};

void virt_acpi_setup(VirtMachineState *vms)
{
    AcpiBuildTables tables;
    AcpiBuildState *build_state;
    AcpiGedState *acpi_ged_state;

    if (!vms->fw_cfg) {
        trace_virt_acpi_setup();
        return;
    }

    if (!virt_is_acpi_enabled(vms)) {
        trace_virt_acpi_setup();
        return;
    }

    build_state = g_malloc0(sizeof *build_state);

    acpi_build_tables_init(&tables);
    virt_acpi_build(vms, &tables);

    /* Now expose it all to Guest */
    build_state->table_mr = acpi_add_rom_blob(virt_acpi_build_update,
                                              build_state, tables.table_data,
                                              ACPI_BUILD_TABLE_FILE);
    assert(build_state->table_mr != NULL);

    build_state->linker_mr = acpi_add_rom_blob(virt_acpi_build_update,
                                               build_state,
                                               tables.linker->cmd_blob,
                                               ACPI_BUILD_LOADER_FILE);

    fw_cfg_add_file(vms->fw_cfg, ACPI_BUILD_TPMLOG_FILE, tables.tcpalog->data,
                    acpi_data_len(tables.tcpalog));

    if (vms->ras) {
        assert(vms->acpi_dev);
        acpi_ged_state = ACPI_GED(vms->acpi_dev);
        acpi_ghes_add_fw_cfg(&acpi_ged_state->ghes_state,
                             vms->fw_cfg, tables.hardware_errors);
    }

    build_state->rsdp_mr = acpi_add_rom_blob(virt_acpi_build_update,
                                             build_state, tables.rsdp,
                                             ACPI_BUILD_RSDP_FILE);

    qemu_register_reset(virt_acpi_build_reset, build_state);
    virt_acpi_build_reset(build_state);
    vmstate_register(NULL, 0, &vmstate_virt_acpi_build, build_state);

    /* Cleanup tables but don't free the memory: we track it
     * in build_state.
     */
    acpi_build_tables_cleanup(&tables, false);
}
