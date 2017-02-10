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
#include "qemu-common.h"
#include "qemu/bitmap.h"
#include "trace.h"
#include "qom/cpu.h"
#include "target/arm/cpu.h"
#include "hw/acpi/acpi-defs.h"
#include "hw/acpi/acpi.h"
#include "hw/nvram/fw_cfg.h"
#include "hw/acpi/bios-linker-loader.h"
#include "hw/loader.h"
#include "hw/hw.h"
#include "hw/acpi/aml-build.h"
#include "hw/pci/pcie_host.h"
#include "hw/pci/pci.h"
#include "hw/arm/virt.h"
#include "sysemu/numa.h"
#include "kvm_arm.h"

#define ARM_SPI_BASE 32
#define ACPI_POWER_BUTTON_DEVICE "PWRB"

static void acpi_dsdt_add_cpus(Aml *scope, int smp_cpus)
{
    uint16_t i;

    for (i = 0; i < smp_cpus; i++) {
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

    /* The _ADR entry is used to link this device to the UART described
     * in the SPCR table, i.e. SPCR.base_address.address == _ADR.
     */
    aml_append(dev, aml_name_decl("_ADR", aml_int(uart_memmap->base)));

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
                              uint32_t irq, bool use_highmem)
{
    Aml *method, *crs, *ifctx, *UUID, *ifctx1, *elsectx, *buf;
    int i, bus_no;
    hwaddr base_mmio = memmap[VIRT_PCIE_MMIO].base;
    hwaddr size_mmio = memmap[VIRT_PCIE_MMIO].size;
    hwaddr base_pio = memmap[VIRT_PCIE_PIO].base;
    hwaddr size_pio = memmap[VIRT_PCIE_PIO].size;
    hwaddr base_ecam = memmap[VIRT_PCIE_ECAM].base;
    hwaddr size_ecam = memmap[VIRT_PCIE_ECAM].size;
    int nr_pcie_buses = size_ecam / PCIE_MMCFG_SIZE_MIN;

    Aml *dev = aml_device("%s", "PCI0");
    aml_append(dev, aml_name_decl("_HID", aml_string("PNP0A08")));
    aml_append(dev, aml_name_decl("_CID", aml_string("PNP0A03")));
    aml_append(dev, aml_name_decl("_SEG", aml_int(0)));
    aml_append(dev, aml_name_decl("_BBN", aml_int(0)));
    aml_append(dev, aml_name_decl("_ADR", aml_int(0)));
    aml_append(dev, aml_name_decl("_UID", aml_string("PCI0")));
    aml_append(dev, aml_name_decl("_STR", aml_unicode("PCIe 0 Device")));
    aml_append(dev, aml_name_decl("_CCA", aml_int(1)));

    /* Declare the PCI Routing Table. */
    Aml *rt_pkg = aml_package(nr_pcie_buses * PCI_NUM_PINS);
    for (bus_no = 0; bus_no < nr_pcie_buses; bus_no++) {
        for (i = 0; i < PCI_NUM_PINS; i++) {
            int gsi = (i + bus_no) % PCI_NUM_PINS;
            Aml *pkg = aml_package(4);
            aml_append(pkg, aml_int((bus_no << 16) | 0xFFFF));
            aml_append(pkg, aml_int(i));
            aml_append(pkg, aml_name("GSI%d", gsi));
            aml_append(pkg, aml_int(0));
            aml_append(rt_pkg, pkg);
        }
    }
    aml_append(dev, aml_name_decl("_PRT", rt_pkg));

    /* Create GSI link device */
    for (i = 0; i < PCI_NUM_PINS; i++) {
        uint32_t irqs =  irq + i;
        Aml *dev_gsi = aml_device("GSI%d", i);
        aml_append(dev_gsi, aml_name_decl("_HID", aml_string("PNP0C0F")));
        aml_append(dev_gsi, aml_name_decl("_UID", aml_int(0)));
        crs = aml_resource_template();
        aml_append(crs,
                   aml_interrupt(AML_CONSUMER, AML_LEVEL, AML_ACTIVE_HIGH,
                                 AML_EXCLUSIVE, &irqs, 1));
        aml_append(dev_gsi, aml_name_decl("_PRS", crs));
        crs = aml_resource_template();
        aml_append(crs,
                   aml_interrupt(AML_CONSUMER, AML_LEVEL, AML_ACTIVE_HIGH,
                                 AML_EXCLUSIVE, &irqs, 1));
        aml_append(dev_gsi, aml_name_decl("_CRS", crs));
        method = aml_method("_SRS", 1, AML_NOTSERIALIZED);
        aml_append(dev_gsi, method);
        aml_append(dev, dev_gsi);
    }

    method = aml_method("_CBA", 0, AML_NOTSERIALIZED);
    aml_append(method, aml_return(aml_int(base_ecam)));
    aml_append(dev, method);

    method = aml_method("_CRS", 0, AML_NOTSERIALIZED);
    Aml *rbuf = aml_resource_template();
    aml_append(rbuf,
        aml_word_bus_number(AML_MIN_FIXED, AML_MAX_FIXED, AML_POS_DECODE,
                            0x0000, 0x0000, nr_pcie_buses - 1, 0x0000,
                            nr_pcie_buses));
    aml_append(rbuf,
        aml_dword_memory(AML_POS_DECODE, AML_MIN_FIXED, AML_MAX_FIXED,
                         AML_NON_CACHEABLE, AML_READ_WRITE, 0x0000, base_mmio,
                         base_mmio + size_mmio - 1, 0x0000, size_mmio));
    aml_append(rbuf,
        aml_dword_io(AML_MIN_FIXED, AML_MAX_FIXED, AML_POS_DECODE,
                     AML_ENTIRE_RANGE, 0x0000, 0x0000, size_pio - 1, base_pio,
                     size_pio));

    if (use_highmem) {
        hwaddr base_mmio_high = memmap[VIRT_PCIE_MMIO_HIGH].base;
        hwaddr size_mmio_high = memmap[VIRT_PCIE_MMIO_HIGH].size;

        aml_append(rbuf,
            aml_qword_memory(AML_POS_DECODE, AML_MIN_FIXED, AML_MAX_FIXED,
                             AML_NON_CACHEABLE, AML_READ_WRITE, 0x0000,
                             base_mmio_high,
                             base_mmio_high + size_mmio_high - 1, 0x0000,
                             size_mmio_high));
    }

    aml_append(method, aml_name_decl("RBUF", rbuf));
    aml_append(method, aml_return(rbuf));
    aml_append(dev, method);

    /* Declare an _OSC (OS Control Handoff) method */
    aml_append(dev, aml_name_decl("SUPP", aml_int(0)));
    aml_append(dev, aml_name_decl("CTRL", aml_int(0)));
    method = aml_method("_OSC", 4, AML_NOTSERIALIZED);
    aml_append(method,
        aml_create_dword_field(aml_arg(3), aml_int(0), "CDW1"));

    /* PCI Firmware Specification 3.0
     * 4.5.1. _OSC Interface for PCI Host Bridge Devices
     * The _OSC interface for a PCI/PCI-X/PCI Express hierarchy is
     * identified by the Universal Unique IDentifier (UUID)
     * 33DB4D5B-1FF7-401C-9657-7441C03DD766
     */
    UUID = aml_touuid("33DB4D5B-1FF7-401C-9657-7441C03DD766");
    ifctx = aml_if(aml_equal(aml_arg(0), UUID));
    aml_append(ifctx,
        aml_create_dword_field(aml_arg(3), aml_int(4), "CDW2"));
    aml_append(ifctx,
        aml_create_dword_field(aml_arg(3), aml_int(8), "CDW3"));
    aml_append(ifctx, aml_store(aml_name("CDW2"), aml_name("SUPP")));
    aml_append(ifctx, aml_store(aml_name("CDW3"), aml_name("CTRL")));
    aml_append(ifctx, aml_store(aml_and(aml_name("CTRL"), aml_int(0x1D), NULL),
                                aml_name("CTRL")));

    ifctx1 = aml_if(aml_lnot(aml_equal(aml_arg(1), aml_int(0x1))));
    aml_append(ifctx1, aml_store(aml_or(aml_name("CDW1"), aml_int(0x08), NULL),
                                 aml_name("CDW1")));
    aml_append(ifctx, ifctx1);

    ifctx1 = aml_if(aml_lnot(aml_equal(aml_name("CDW3"), aml_name("CTRL"))));
    aml_append(ifctx1, aml_store(aml_or(aml_name("CDW1"), aml_int(0x10), NULL),
                                 aml_name("CDW1")));
    aml_append(ifctx, ifctx1);

    aml_append(ifctx, aml_store(aml_name("CTRL"), aml_name("CDW3")));
    aml_append(ifctx, aml_return(aml_arg(3)));
    aml_append(method, ifctx);

    elsectx = aml_else();
    aml_append(elsectx, aml_store(aml_or(aml_name("CDW1"), aml_int(4), NULL),
                                  aml_name("CDW1")));
    aml_append(elsectx, aml_return(aml_arg(3)));
    aml_append(method, elsectx);
    aml_append(dev, method);

    method = aml_method("_DSM", 4, AML_NOTSERIALIZED);

    /* PCI Firmware Specification 3.0
     * 4.6.1. _DSM for PCI Express Slot Information
     * The UUID in _DSM in this context is
     * {E5C937D0-3553-4D7A-9117-EA4D19C3434D}
     */
    UUID = aml_touuid("E5C937D0-3553-4D7A-9117-EA4D19C3434D");
    ifctx = aml_if(aml_equal(aml_arg(0), UUID));
    ifctx1 = aml_if(aml_equal(aml_arg(2), aml_int(0)));
    uint8_t byte_list[1] = {1};
    buf = aml_buffer(1, byte_list);
    aml_append(ifctx1, aml_return(buf));
    aml_append(ifctx, ifctx1);
    aml_append(method, ifctx);

    byte_list[0] = 0;
    buf = aml_buffer(1, byte_list);
    aml_append(method, aml_return(buf));
    aml_append(dev, method);

    Aml *dev_rp0 = aml_device("%s", "RP0");
    aml_append(dev_rp0, aml_name_decl("_ADR", aml_int(0)));
    aml_append(dev, dev_rp0);

    Aml *dev_res0 = aml_device("%s", "RES0");
    aml_append(dev_res0, aml_name_decl("_HID", aml_string("PNP0C02")));
    crs = aml_resource_template();
    aml_append(crs, aml_memory32_fixed(base_ecam, size_ecam, AML_READ_WRITE));
    aml_append(dev_res0, aml_name_decl("_CRS", crs));
    aml_append(dev, dev_res0);
    aml_append(scope, dev);
}

static void acpi_dsdt_add_gpio(Aml *scope, const MemMapEntry *gpio_memmap,
                                           uint32_t gpio_irq)
{
    Aml *dev = aml_device("GPO0");
    aml_append(dev, aml_name_decl("_HID", aml_string("ARMH0061")));
    aml_append(dev, aml_name_decl("_ADR", aml_int(0)));
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

static void acpi_dsdt_add_power_button(Aml *scope)
{
    Aml *dev = aml_device(ACPI_POWER_BUTTON_DEVICE);
    aml_append(dev, aml_name_decl("_HID", aml_string("PNP0C0C")));
    aml_append(dev, aml_name_decl("_ADR", aml_int(0)));
    aml_append(dev, aml_name_decl("_UID", aml_int(0)));
    aml_append(scope, dev);
}

/* RSDP */
static GArray *
build_rsdp(GArray *rsdp_table, BIOSLinker *linker, unsigned rsdt_tbl_offset)
{
    AcpiRsdpDescriptor *rsdp = acpi_data_push(rsdp_table, sizeof *rsdp);
    unsigned rsdt_pa_size = sizeof(rsdp->rsdt_physical_address);
    unsigned rsdt_pa_offset =
        (char *)&rsdp->rsdt_physical_address - rsdp_table->data;

    bios_linker_loader_alloc(linker, ACPI_BUILD_RSDP_FILE, rsdp_table, 16,
                             true /* fseg memory */);

    memcpy(&rsdp->signature, "RSD PTR ", sizeof(rsdp->signature));
    memcpy(rsdp->oem_id, ACPI_BUILD_APPNAME6, sizeof(rsdp->oem_id));
    rsdp->length = cpu_to_le32(sizeof(*rsdp));
    rsdp->revision = 0x02;

    /* Address to be filled by Guest linker */
    bios_linker_loader_add_pointer(linker,
        ACPI_BUILD_RSDP_FILE, rsdt_pa_offset, rsdt_pa_size,
        ACPI_BUILD_TABLE_FILE, rsdt_tbl_offset);

    /* Checksum to be filled by Guest linker */
    bios_linker_loader_add_checksum(linker, ACPI_BUILD_RSDP_FILE,
        (char *)rsdp - rsdp_table->data, sizeof *rsdp,
        (char *)&rsdp->checksum - rsdp_table->data);

    return rsdp_table;
}

static void
build_iort(GArray *table_data, BIOSLinker *linker)
{
    int iort_start = table_data->len;
    AcpiIortIdMapping *idmap;
    AcpiIortItsGroup *its;
    AcpiIortTable *iort;
    size_t node_size, iort_length;
    AcpiIortRC *rc;

    iort = acpi_data_push(table_data, sizeof(*iort));

    iort_length = sizeof(*iort);
    iort->node_count = cpu_to_le32(2); /* RC and ITS nodes */
    iort->node_offset = cpu_to_le32(sizeof(*iort));

    /* ITS group node */
    node_size =  sizeof(*its) + sizeof(uint32_t);
    iort_length += node_size;
    its = acpi_data_push(table_data, node_size);

    its->type = ACPI_IORT_NODE_ITS_GROUP;
    its->length = cpu_to_le16(node_size);
    its->its_count = cpu_to_le32(1);
    its->identifiers[0] = 0; /* MADT translation_id */

    /* Root Complex Node */
    node_size = sizeof(*rc) + sizeof(*idmap);
    iort_length += node_size;
    rc = acpi_data_push(table_data, node_size);

    rc->type = ACPI_IORT_NODE_PCI_ROOT_COMPLEX;
    rc->length = cpu_to_le16(node_size);
    rc->mapping_count = cpu_to_le32(1);
    rc->mapping_offset = cpu_to_le32(sizeof(*rc));

    /* fully coherent device */
    rc->memory_properties.cache_coherency = cpu_to_le32(1);
    rc->memory_properties.memory_flags = 0x3; /* CCA = CPM = DCAS = 1 */
    rc->pci_segment_number = 0; /* MCFG pci_segment */

    /* Identity RID mapping covering the whole input RID range */
    idmap = &rc->id_mapping_array[0];
    idmap->input_base = 0;
    idmap->id_count = cpu_to_le32(0xFFFF);
    idmap->output_base = 0;
    /* output IORT node is the ITS group node (the first node) */
    idmap->output_reference = cpu_to_le32(iort->node_offset);

    iort->length = cpu_to_le32(iort_length);

    build_header(linker, table_data, (void *)(table_data->data + iort_start),
                 "IORT", table_data->len - iort_start, 0, NULL, NULL);
}

static void
build_spcr(GArray *table_data, BIOSLinker *linker, VirtMachineState *vms)
{
    AcpiSerialPortConsoleRedirection *spcr;
    const MemMapEntry *uart_memmap = &vms->memmap[VIRT_UART];
    int irq = vms->irqmap[VIRT_UART] + ARM_SPI_BASE;

    spcr = acpi_data_push(table_data, sizeof(*spcr));

    spcr->interface_type = 0x3;    /* ARM PL011 UART */

    spcr->base_address.space_id = AML_SYSTEM_MEMORY;
    spcr->base_address.bit_width = 8;
    spcr->base_address.bit_offset = 0;
    spcr->base_address.access_width = 1;
    spcr->base_address.address = cpu_to_le64(uart_memmap->base);

    spcr->interrupt_types = (1 << 3); /* Bit[3] ARMH GIC interrupt */
    spcr->gsi = cpu_to_le32(irq);  /* Global System Interrupt */

    spcr->baud = 3;                /* Baud Rate: 3 = 9600 */
    spcr->parity = 0;              /* No Parity */
    spcr->stopbits = 1;            /* 1 Stop bit */
    spcr->flowctrl = (1 << 1);     /* Bit[1] = RTS/CTS hardware flow control */
    spcr->term_type = 0;           /* Terminal Type: 0 = VT100 */

    spcr->pci_device_id = 0xffff;  /* PCI Device ID: not a PCI device */
    spcr->pci_vendor_id = 0xffff;  /* PCI Vendor ID: not a PCI device */

    build_header(linker, table_data, (void *)spcr, "SPCR", sizeof(*spcr), 2,
                 NULL, NULL);
}

static void
build_srat(GArray *table_data, BIOSLinker *linker, VirtMachineState *vms)
{
    AcpiSystemResourceAffinityTable *srat;
    AcpiSratProcessorGiccAffinity *core;
    AcpiSratMemoryAffinity *numamem;
    int i, j, srat_start;
    uint64_t mem_base;
    uint32_t *cpu_node = g_malloc0(vms->smp_cpus * sizeof(uint32_t));

    for (i = 0; i < vms->smp_cpus; i++) {
        j = numa_get_node_for_cpu(i);
        if (j < nb_numa_nodes) {
                cpu_node[i] = j;
        }
    }

    srat_start = table_data->len;
    srat = acpi_data_push(table_data, sizeof(*srat));
    srat->reserved1 = cpu_to_le32(1);

    for (i = 0; i < vms->smp_cpus; ++i) {
        core = acpi_data_push(table_data, sizeof(*core));
        core->type = ACPI_SRAT_PROCESSOR_GICC;
        core->length = sizeof(*core);
        core->proximity = cpu_to_le32(cpu_node[i]);
        core->acpi_processor_uid = cpu_to_le32(i);
        core->flags = cpu_to_le32(1);
    }
    g_free(cpu_node);

    mem_base = vms->memmap[VIRT_MEM].base;
    for (i = 0; i < nb_numa_nodes; ++i) {
        numamem = acpi_data_push(table_data, sizeof(*numamem));
        build_srat_memory(numamem, mem_base, numa_info[i].node_mem, i,
                          MEM_AFFINITY_ENABLED);
        mem_base += numa_info[i].node_mem;
    }

    build_header(linker, table_data, (void *)srat, "SRAT",
                 table_data->len - srat_start, 3, NULL, NULL);
}

static void
build_mcfg(GArray *table_data, BIOSLinker *linker, VirtMachineState *vms)
{
    AcpiTableMcfg *mcfg;
    const MemMapEntry *memmap = vms->memmap;
    int len = sizeof(*mcfg) + sizeof(mcfg->allocation[0]);

    mcfg = acpi_data_push(table_data, len);
    mcfg->allocation[0].address = cpu_to_le64(memmap[VIRT_PCIE_ECAM].base);

    /* Only a single allocation so no need to play with segments */
    mcfg->allocation[0].pci_segment = cpu_to_le16(0);
    mcfg->allocation[0].start_bus_number = 0;
    mcfg->allocation[0].end_bus_number = (memmap[VIRT_PCIE_ECAM].size
                                          / PCIE_MMCFG_SIZE_MIN) - 1;

    build_header(linker, table_data, (void *)mcfg, "MCFG", len, 1, NULL, NULL);
}

/* GTDT */
static void
build_gtdt(GArray *table_data, BIOSLinker *linker, VirtMachineState *vms)
{
    VirtMachineClass *vmc = VIRT_MACHINE_GET_CLASS(vms);
    int gtdt_start = table_data->len;
    AcpiGenericTimerTable *gtdt;
    uint32_t irqflags;

    if (vmc->claim_edge_triggered_timers) {
        irqflags = ACPI_GTDT_INTERRUPT_MODE_EDGE;
    } else {
        irqflags = ACPI_GTDT_INTERRUPT_MODE_LEVEL;
    }

    gtdt = acpi_data_push(table_data, sizeof *gtdt);
    /* The interrupt values are the same with the device tree when adding 16 */
    gtdt->secure_el1_interrupt = cpu_to_le32(ARCH_TIMER_S_EL1_IRQ + 16);
    gtdt->secure_el1_flags = cpu_to_le32(irqflags);

    gtdt->non_secure_el1_interrupt = cpu_to_le32(ARCH_TIMER_NS_EL1_IRQ + 16);
    gtdt->non_secure_el1_flags = cpu_to_le32(irqflags |
                                             ACPI_GTDT_CAP_ALWAYS_ON);

    gtdt->virtual_timer_interrupt = cpu_to_le32(ARCH_TIMER_VIRT_IRQ + 16);
    gtdt->virtual_timer_flags = cpu_to_le32(irqflags);

    gtdt->non_secure_el2_interrupt = cpu_to_le32(ARCH_TIMER_NS_EL2_IRQ + 16);
    gtdt->non_secure_el2_flags = cpu_to_le32(irqflags);

    build_header(linker, table_data,
                 (void *)(table_data->data + gtdt_start), "GTDT",
                 table_data->len - gtdt_start, 2, NULL, NULL);
}

/* MADT */
static void
build_madt(GArray *table_data, BIOSLinker *linker, VirtMachineState *vms)
{
    VirtMachineClass *vmc = VIRT_MACHINE_GET_CLASS(vms);
    int madt_start = table_data->len;
    const MemMapEntry *memmap = vms->memmap;
    const int *irqmap = vms->irqmap;
    AcpiMultipleApicTable *madt;
    AcpiMadtGenericDistributor *gicd;
    AcpiMadtGenericMsiFrame *gic_msi;
    int i;

    madt = acpi_data_push(table_data, sizeof *madt);

    gicd = acpi_data_push(table_data, sizeof *gicd);
    gicd->type = ACPI_APIC_GENERIC_DISTRIBUTOR;
    gicd->length = sizeof(*gicd);
    gicd->base_address = cpu_to_le64(memmap[VIRT_GIC_DIST].base);
    gicd->version = vms->gic_version;

    for (i = 0; i < vms->smp_cpus; i++) {
        AcpiMadtGenericCpuInterface *gicc = acpi_data_push(table_data,
                                                           sizeof(*gicc));
        ARMCPU *armcpu = ARM_CPU(qemu_get_cpu(i));

        gicc->type = ACPI_APIC_GENERIC_CPU_INTERFACE;
        gicc->length = sizeof(*gicc);
        if (vms->gic_version == 2) {
            gicc->base_address = cpu_to_le64(memmap[VIRT_GIC_CPU].base);
        }
        gicc->cpu_interface_number = cpu_to_le32(i);
        gicc->arm_mpidr = cpu_to_le64(armcpu->mp_affinity);
        gicc->uid = cpu_to_le32(i);
        gicc->flags = cpu_to_le32(ACPI_MADT_GICC_ENABLED);

        if (arm_feature(&armcpu->env, ARM_FEATURE_PMU)) {
            gicc->performance_interrupt = cpu_to_le32(PPI(VIRTUAL_PMU_IRQ));
        }
        if (vms->virt && vms->gic_version == 3) {
            gicc->vgic_interrupt = cpu_to_le32(PPI(ARCH_GICV3_MAINT_IRQ));
        }
    }

    if (vms->gic_version == 3) {
        AcpiMadtGenericTranslator *gic_its;
        AcpiMadtGenericRedistributor *gicr = acpi_data_push(table_data,
                                                         sizeof *gicr);

        gicr->type = ACPI_APIC_GENERIC_REDISTRIBUTOR;
        gicr->length = sizeof(*gicr);
        gicr->base_address = cpu_to_le64(memmap[VIRT_GIC_REDIST].base);
        gicr->range_length = cpu_to_le32(memmap[VIRT_GIC_REDIST].size);

        if (its_class_name() && !vmc->no_its) {
            gic_its = acpi_data_push(table_data, sizeof *gic_its);
            gic_its->type = ACPI_APIC_GENERIC_TRANSLATOR;
            gic_its->length = sizeof(*gic_its);
            gic_its->translation_id = 0;
            gic_its->base_address = cpu_to_le64(memmap[VIRT_GIC_ITS].base);
        }
    } else {
        gic_msi = acpi_data_push(table_data, sizeof *gic_msi);
        gic_msi->type = ACPI_APIC_GENERIC_MSI_FRAME;
        gic_msi->length = sizeof(*gic_msi);
        gic_msi->gic_msi_frame_id = 0;
        gic_msi->base_address = cpu_to_le64(memmap[VIRT_GIC_V2M].base);
        gic_msi->flags = cpu_to_le32(1);
        gic_msi->spi_count = cpu_to_le16(NUM_GICV2M_SPIS);
        gic_msi->spi_base = cpu_to_le16(irqmap[VIRT_GIC_V2M] + ARM_SPI_BASE);
    }

    build_header(linker, table_data,
                 (void *)(table_data->data + madt_start), "APIC",
                 table_data->len - madt_start, 3, NULL, NULL);
}

/* FADT */
static void build_fadt(GArray *table_data, BIOSLinker *linker,
                       VirtMachineState *vms, unsigned dsdt_tbl_offset)
{
    AcpiFadtDescriptorRev5_1 *fadt = acpi_data_push(table_data, sizeof(*fadt));
    unsigned dsdt_entry_offset = (char *)&fadt->dsdt - table_data->data;
    uint16_t bootflags;

    switch (vms->psci_conduit) {
    case QEMU_PSCI_CONDUIT_DISABLED:
        bootflags = 0;
        break;
    case QEMU_PSCI_CONDUIT_HVC:
        bootflags = ACPI_FADT_ARM_PSCI_COMPLIANT | ACPI_FADT_ARM_PSCI_USE_HVC;
        break;
    case QEMU_PSCI_CONDUIT_SMC:
        bootflags = ACPI_FADT_ARM_PSCI_COMPLIANT;
        break;
    default:
        g_assert_not_reached();
    }

    /* Hardware Reduced = 1 and use PSCI 0.2+ */
    fadt->flags = cpu_to_le32(1 << ACPI_FADT_F_HW_REDUCED_ACPI);
    fadt->arm_boot_flags = cpu_to_le16(bootflags);

    /* ACPI v5.1 (fadt->revision.fadt->minor_revision) */
    fadt->minor_revision = 0x1;

    /* DSDT address to be filled by Guest linker */
    bios_linker_loader_add_pointer(linker,
        ACPI_BUILD_TABLE_FILE, dsdt_entry_offset, sizeof(fadt->dsdt),
        ACPI_BUILD_TABLE_FILE, dsdt_tbl_offset);

    build_header(linker, table_data,
                 (void *)fadt, "FACP", sizeof(*fadt), 5, NULL, NULL);
}

/* DSDT */
static void
build_dsdt(GArray *table_data, BIOSLinker *linker, VirtMachineState *vms)
{
    Aml *scope, *dsdt;
    const MemMapEntry *memmap = vms->memmap;
    const int *irqmap = vms->irqmap;

    dsdt = init_aml_allocator();
    /* Reserve space for header */
    acpi_data_push(dsdt->buf, sizeof(AcpiTableHeader));

    /* When booting the VM with UEFI, UEFI takes ownership of the RTC hardware.
     * While UEFI can use libfdt to disable the RTC device node in the DTB that
     * it passes to the OS, it cannot modify AML. Therefore, we won't generate
     * the RTC ACPI device at all when using UEFI.
     */
    scope = aml_scope("\\_SB");
    acpi_dsdt_add_cpus(scope, vms->smp_cpus);
    acpi_dsdt_add_uart(scope, &memmap[VIRT_UART],
                       (irqmap[VIRT_UART] + ARM_SPI_BASE));
    acpi_dsdt_add_flash(scope, &memmap[VIRT_FLASH]);
    acpi_dsdt_add_fw_cfg(scope, &memmap[VIRT_FW_CFG]);
    acpi_dsdt_add_virtio(scope, &memmap[VIRT_MMIO],
                    (irqmap[VIRT_MMIO] + ARM_SPI_BASE), NUM_VIRTIO_TRANSPORTS);
    acpi_dsdt_add_pci(scope, memmap, (irqmap[VIRT_PCIE] + ARM_SPI_BASE),
                      vms->highmem);
    acpi_dsdt_add_gpio(scope, &memmap[VIRT_GPIO],
                       (irqmap[VIRT_GPIO] + ARM_SPI_BASE));
    acpi_dsdt_add_power_button(scope);

    aml_append(dsdt, scope);

    /* copy AML table into ACPI tables blob and patch header there */
    g_array_append_vals(table_data, dsdt->buf->data, dsdt->buf->len);
    build_header(linker, table_data,
        (void *)(table_data->data + table_data->len - dsdt->buf->len),
        "DSDT", dsdt->buf->len, 2, NULL, NULL);
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

static
void virt_acpi_build(VirtMachineState *vms, AcpiBuildTables *tables)
{
    VirtMachineClass *vmc = VIRT_MACHINE_GET_CLASS(vms);
    GArray *table_offsets;
    unsigned dsdt, rsdt;
    GArray *tables_blob = tables->table_data;

    table_offsets = g_array_new(false, true /* clear */,
                                        sizeof(uint32_t));

    bios_linker_loader_alloc(tables->linker,
                             ACPI_BUILD_TABLE_FILE, tables_blob,
                             64, false /* high memory */);

    /* DSDT is pointed to by FADT */
    dsdt = tables_blob->len;
    build_dsdt(tables_blob, tables->linker, vms);

    /* FADT MADT GTDT MCFG SPCR pointed to by RSDT */
    acpi_add_table(table_offsets, tables_blob);
    build_fadt(tables_blob, tables->linker, vms, dsdt);

    acpi_add_table(table_offsets, tables_blob);
    build_madt(tables_blob, tables->linker, vms);

    acpi_add_table(table_offsets, tables_blob);
    build_gtdt(tables_blob, tables->linker, vms);

    acpi_add_table(table_offsets, tables_blob);
    build_mcfg(tables_blob, tables->linker, vms);

    acpi_add_table(table_offsets, tables_blob);
    build_spcr(tables_blob, tables->linker, vms);

    if (nb_numa_nodes > 0) {
        acpi_add_table(table_offsets, tables_blob);
        build_srat(tables_blob, tables->linker, vms);
    }

    if (its_class_name() && !vmc->no_its) {
        acpi_add_table(table_offsets, tables_blob);
        build_iort(tables_blob, tables->linker);
    }

    /* RSDT is pointed to by RSDP */
    rsdt = tables_blob->len;
    build_rsdt(tables_blob, tables->linker, table_offsets, NULL, NULL);

    /* RSDP is in FSEG memory, so allocate it separately */
    build_rsdp(tables->rsdp, tables->linker, rsdt);

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

static MemoryRegion *acpi_add_rom_blob(AcpiBuildState *build_state,
                                       GArray *blob, const char *name,
                                       uint64_t max_size)
{
    return rom_add_blob(name, blob->data, acpi_data_len(blob), max_size, -1,
                        name, virt_acpi_build_update, build_state, NULL, true);
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

    if (!vms->fw_cfg) {
        trace_virt_acpi_setup();
        return;
    }

    if (!acpi_enabled) {
        trace_virt_acpi_setup();
        return;
    }

    build_state = g_malloc0(sizeof *build_state);

    acpi_build_tables_init(&tables);
    virt_acpi_build(vms, &tables);

    /* Now expose it all to Guest */
    build_state->table_mr = acpi_add_rom_blob(build_state, tables.table_data,
                                               ACPI_BUILD_TABLE_FILE,
                                               ACPI_BUILD_TABLE_MAX_SIZE);
    assert(build_state->table_mr != NULL);

    build_state->linker_mr =
        acpi_add_rom_blob(build_state, tables.linker->cmd_blob,
                          "etc/table-loader", 0);

    fw_cfg_add_file(vms->fw_cfg, ACPI_BUILD_TPMLOG_FILE, tables.tcpalog->data,
                    acpi_data_len(tables.tcpalog));

    build_state->rsdp_mr = acpi_add_rom_blob(build_state, tables.rsdp,
                                              ACPI_BUILD_RSDP_FILE, 0);

    qemu_register_reset(virt_acpi_build_reset, build_state);
    virt_acpi_build_reset(build_state);
    vmstate_register(NULL, 0, &vmstate_virt_acpi_build, build_state);

    /* Cleanup tables but don't free the memory: we track it
     * in build_state.
     */
    acpi_build_tables_cleanup(&tables, false);
}
