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

struct viot_pci_host_range {
    int min_bus;
    int max_bus;
};

static void build_pci_host_range(GArray *table_data, int min_bus, int max_bus,
                                 uint16_t output_node)
{
    /* Type */
    build_append_int_noprefix(table_data, 1 /* PCI range */, 1);
    /* Reserved */
    build_append_int_noprefix(table_data, 0, 1);
    /* Length */
    build_append_int_noprefix(table_data, 24, 2);
    /* Endpoint start */
    build_append_int_noprefix(table_data, PCI_BUILD_BDF(min_bus, 0), 4);
    /* PCI Segment start */
    build_append_int_noprefix(table_data, 0, 2);
    /* PCI Segment end */
    build_append_int_noprefix(table_data, 0, 2);
    /* PCI BDF start */
    build_append_int_noprefix(table_data, PCI_BUILD_BDF(min_bus, 0), 2);
    /* PCI BDF end */
    build_append_int_noprefix(table_data, PCI_BUILD_BDF(max_bus, 0xff), 2);
    /* Output node */
    build_append_int_noprefix(table_data, output_node, 2);
    /* Reserved */
    build_append_int_noprefix(table_data, 0, 6);
}

/* Build PCI range for a given PCI host bridge */
static int enumerate_pci_host_bridges(Object *obj, void *opaque)
{
    GArray *pci_host_ranges = opaque;

    if (object_dynamic_cast(obj, TYPE_PCI_HOST_BRIDGE)) {
        PCIBus *bus = PCI_HOST_BRIDGE(obj)->bus;

        if (bus && !pci_bus_bypass_iommu(bus)) {
            int min_bus, max_bus;

            pci_bus_range(bus, &min_bus, &max_bus);

            const struct viot_pci_host_range pci_host_range = {
                .min_bus = min_bus,
                .max_bus = max_bus,
            };
            g_array_append_val(pci_host_ranges, pci_host_range);
        }
    }

    return 0;
}

static gint pci_host_range_compare(gconstpointer a, gconstpointer b)
{
    struct viot_pci_host_range *range_a = (struct viot_pci_host_range *)a;
    struct viot_pci_host_range *range_b = (struct viot_pci_host_range *)b;

    if (range_a->min_bus < range_b->min_bus) {
        return -1;
    } else if (range_a->min_bus > range_b->min_bus) {
        return 1;
    } else {
        return 0;
    }
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
    GArray *pci_host_ranges =  g_array_new(false, true,
                                           sizeof(struct viot_pci_host_range));
    struct viot_pci_host_range *pci_host_range;
    int i;

    /* Build the list of PCI ranges that this viommu manages */
    object_child_foreach_recursive(OBJECT(ms), enumerate_pci_host_bridges,
                                   pci_host_ranges);

    /* Sort the pci host ranges by min_bus */
    g_array_sort(pci_host_ranges, pci_host_range_compare);

    /* ACPI table header */
    acpi_table_begin(&table, table_data);
    /* Node count */
    build_append_int_noprefix(table_data, pci_host_ranges->len + 1, 2);
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
    for (i = 0; i < pci_host_ranges->len; i++) {
        pci_host_range = &g_array_index(pci_host_ranges,
                                        struct viot_pci_host_range, i);

        build_pci_host_range(table_data, pci_host_range->min_bus,
                             pci_host_range->max_bus, viommu_off);
    }

    g_array_free(pci_host_ranges, true);

    acpi_table_end(linker, &table);
}

