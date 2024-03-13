// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#include "qemu/osdep.h"
#include "hw/acpi/acpi_generic_initiator.h"
#include "hw/acpi/aml-build.h"
#include "hw/boards.h"
#include "hw/pci/pci_device.h"
#include "qemu/error-report.h"

typedef struct AcpiGenericInitiatorClass {
    ObjectClass parent_class;
} AcpiGenericInitiatorClass;

OBJECT_DEFINE_TYPE_WITH_INTERFACES(AcpiGenericInitiator, acpi_generic_initiator,
                   ACPI_GENERIC_INITIATOR, OBJECT,
                   { TYPE_USER_CREATABLE },
                   { NULL })

OBJECT_DECLARE_SIMPLE_TYPE(AcpiGenericInitiator, ACPI_GENERIC_INITIATOR)

static void acpi_generic_initiator_init(Object *obj)
{
    AcpiGenericInitiator *gi = ACPI_GENERIC_INITIATOR(obj);

    gi->node = MAX_NODES;
    gi->pci_dev = NULL;
}

static void acpi_generic_initiator_finalize(Object *obj)
{
    AcpiGenericInitiator *gi = ACPI_GENERIC_INITIATOR(obj);

    g_free(gi->pci_dev);
}

static void acpi_generic_initiator_set_pci_device(Object *obj, const char *val,
                                                  Error **errp)
{
    AcpiGenericInitiator *gi = ACPI_GENERIC_INITIATOR(obj);

    gi->pci_dev = g_strdup(val);
}

static void acpi_generic_initiator_set_node(Object *obj, Visitor *v,
                                            const char *name, void *opaque,
                                            Error **errp)
{
    AcpiGenericInitiator *gi = ACPI_GENERIC_INITIATOR(obj);
    MachineState *ms = MACHINE(qdev_get_machine());
    uint32_t value;

    if (!visit_type_uint32(v, name, &value, errp)) {
        return;
    }

    if (value >= MAX_NODES) {
        error_printf("%s: Invalid NUMA node specified\n",
                     TYPE_ACPI_GENERIC_INITIATOR);
        exit(1);
    }

    gi->node = value;
    ms->numa_state->nodes[gi->node].has_gi = true;
}

static void acpi_generic_initiator_class_init(ObjectClass *oc, void *data)
{
    object_class_property_add_str(oc, "pci-dev", NULL,
        acpi_generic_initiator_set_pci_device);
    object_class_property_add(oc, "node", "int", NULL,
        acpi_generic_initiator_set_node, NULL, NULL);
}

/*
 * ACPI 6.3:
 * Table 5-78 Generic Initiator Affinity Structure
 */
static void
build_srat_generic_pci_initiator_affinity(GArray *table_data, int node,
                                          PCIDeviceHandle *handle)
{
    uint8_t index;

    build_append_int_noprefix(table_data, 5, 1);  /* Type */
    build_append_int_noprefix(table_data, 32, 1); /* Length */
    build_append_int_noprefix(table_data, 0, 1);  /* Reserved */
    build_append_int_noprefix(table_data, 1, 1);  /* Device Handle Type: PCI */
    build_append_int_noprefix(table_data, node, 4);  /* Proximity Domain */

    /* Device Handle - PCI */
    build_append_int_noprefix(table_data, handle->segment, 2);
    build_append_int_noprefix(table_data, handle->bdf, 2);
    for (index = 0; index < 12; index++) {
        build_append_int_noprefix(table_data, 0, 1);
    }

    build_append_int_noprefix(table_data, GEN_AFFINITY_ENABLED, 4); /* Flags */
    build_append_int_noprefix(table_data, 0, 4);     /* Reserved */
}

static int build_all_acpi_generic_initiators(Object *obj, void *opaque)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    AcpiGenericInitiator *gi;
    GArray *table_data = opaque;
    PCIDeviceHandle dev_handle;
    PCIDevice *pci_dev;
    Object *o;

    if (!object_dynamic_cast(obj, TYPE_ACPI_GENERIC_INITIATOR)) {
        return 0;
    }

    gi = ACPI_GENERIC_INITIATOR(obj);
    if (gi->node >= ms->numa_state->num_nodes) {
        error_printf("%s: Specified node %d is invalid.\n",
                     TYPE_ACPI_GENERIC_INITIATOR, gi->node);
        exit(1);
    }

    o = object_resolve_path_type(gi->pci_dev, TYPE_PCI_DEVICE, NULL);
    if (!o) {
        error_printf("%s: Specified device must be a PCI device.\n",
                     TYPE_ACPI_GENERIC_INITIATOR);
        exit(1);
    }

    pci_dev = PCI_DEVICE(o);

    dev_handle.segment = 0;
    dev_handle.bdf = PCI_BUILD_BDF(pci_bus_num(pci_get_bus(pci_dev)),
                                               pci_dev->devfn);

    build_srat_generic_pci_initiator_affinity(table_data,
                                              gi->node, &dev_handle);

    return 0;
}

void build_srat_generic_pci_initiator(GArray *table_data)
{
    object_child_foreach_recursive(object_get_root(),
                                   build_all_acpi_generic_initiators,
                                   table_data);
}
