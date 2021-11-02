/*
 * ACPI Virtual I/O Translation table implementation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/aml-build.h"
#include "hw/acpi/viot.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_host.h"

struct viot_pci_ranges {
    GArray *blob;
    size_t count;
    uint16_t output_node;
};

/* Build PCI range for a given PCI host bridge */
static int build_pci_range_node(Object *obj, void *opaque)
{
    struct viot_pci_ranges *pci_ranges = opaque;
    GArray *blob = pci_ranges->blob;

    if (object_dynamic_cast(obj, TYPE_PCI_HOST_BRIDGE)) {
        PCIBus *bus = PCI_HOST_BRIDGE(obj)->bus;

        if (bus && !pci_bus_bypass_iommu(bus)) {
            int min_bus, max_bus;

            pci_bus_range(bus, &min_bus, &max_bus);

            /* Type */
            build_append_int_noprefix(blob, 1 /* PCI range */, 1);
            /* Reserved */
            build_append_int_noprefix(blob, 0, 1);
            /* Length */
            build_append_int_noprefix(blob, 24, 2);
            /* Endpoint start */
            build_append_int_noprefix(blob, PCI_BUILD_BDF(min_bus, 0), 4);
            /* PCI Segment start */
            build_append_int_noprefix(blob, 0, 2);
            /* PCI Segment end */
            build_append_int_noprefix(blob, 0, 2);
            /* PCI BDF start */
            build_append_int_noprefix(blob, PCI_BUILD_BDF(min_bus, 0), 2);
            /* PCI BDF end */
            build_append_int_noprefix(blob, PCI_BUILD_BDF(max_bus, 0xff), 2);
            /* Output node */
            build_append_int_noprefix(blob, pci_ranges->output_node, 2);
            /* Reserved */
            build_append_int_noprefix(blob, 0, 6);

            pci_ranges->count++;
        }
    }

    return 0;
}

/*
 * Generate a VIOT table with one PCI-based virtio-iommu that manages PCI
 * endpoints.
 *
 * Defined in the ACPI Specification (Version TBD)
 */
void build_viot(MachineState *ms, GArray *table_data, BIOSLinker *linker,
                uint16_t virtio_iommu_bdf, const char *oem_id,
                const char *oem_table_id)
{
    /* The virtio-iommu node follows the 48-bytes header */
    int viommu_off = 48;
    AcpiTable table = { .sig = "VIOT", .rev = 0,
                        .oem_id = oem_id, .oem_table_id = oem_table_id };
    struct viot_pci_ranges pci_ranges = {
        .output_node = viommu_off,
        .blob = g_array_new(false, true /* clear */, 1),
    };

    /* Build the list of PCI ranges that this viommu manages */
    object_child_foreach_recursive(OBJECT(ms), build_pci_range_node,
                                   &pci_ranges);

    /* ACPI table header */
    acpi_table_begin(&table, table_data);
    /* Node count */
    build_append_int_noprefix(table_data, pci_ranges.count + 1, 2);
    /* Node offset */
    build_append_int_noprefix(table_data, viommu_off, 2);
    /* Reserved */
    build_append_int_noprefix(table_data, 0, 8);

    /* Virtio-iommu node */
    /* Type */
    build_append_int_noprefix(table_data, 3 /* virtio-pci IOMMU */, 1);
    /* Reserved */
    build_append_int_noprefix(table_data, 0, 1);
    /* Length */
    build_append_int_noprefix(table_data, 16, 2);
    /* PCI Segment */
    build_append_int_noprefix(table_data, 0, 2);
    /* PCI BDF number */
    build_append_int_noprefix(table_data, virtio_iommu_bdf, 2);
    /* Reserved */
    build_append_int_noprefix(table_data, 0, 8);

    /* PCI ranges found above */
    g_array_append_vals(table_data, pci_ranges.blob->data,
                        pci_ranges.blob->len);
    g_array_free(pci_ranges.blob, true);

    acpi_table_end(linker, &table);
}

