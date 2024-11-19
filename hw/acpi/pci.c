/*
 * Support for generating PCI related ACPI tables and passing them to Guests
 *
 * Copyright (C) 2006 Fabrice Bellard
 * Copyright (C) 2008-2010  Kevin O'Connor <kevin@koconnor.net>
 * Copyright (C) 2013-2019 Red Hat Inc
 * Copyright (C) 2019 Intel Corporation
 *
 * Author: Wei Yang <richardw.yang@linux.intel.com>
 * Author: Michael S. Tsirkin <mst@redhat.com>
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
#include "qemu/error-report.h"
#include "qom/object_interfaces.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/acpi/aml-build.h"
#include "hw/acpi/pci.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/pcie_host.h"

/*
 * PCI Firmware Specification, Revision 3.0
 * 4.1.2 MCFG Table Description.
 */
void build_mcfg(GArray *table_data, BIOSLinker *linker, AcpiMcfgInfo *info,
                const char *oem_id, const char *oem_table_id)
{
    AcpiTable table = { .sig = "MCFG", .rev = 1,
                        .oem_id = oem_id, .oem_table_id = oem_table_id };

    acpi_table_begin(&table, table_data);

    /* Reserved */
    build_append_int_noprefix(table_data, 0, 8);
    /*
     * Memory Mapped Enhanced Configuration Space Base Address Allocation
     * Structure
     */
    /* Base address, processor-relative */
    build_append_int_noprefix(table_data, info->base, 8);
    /* PCI segment group number */
    build_append_int_noprefix(table_data, 0, 2);
    /* Starting PCI Bus number */
    build_append_int_noprefix(table_data, 0, 1);
    /* Final PCI Bus number */
    build_append_int_noprefix(table_data, PCIE_MMCFG_BUS(info->size - 1), 1);
    /* Reserved */
    build_append_int_noprefix(table_data, 0, 4);

    acpi_table_end(linker, &table);
}

typedef struct AcpiGenericInitiator {
    /* private */
    Object parent;

    /* public */
    char *pci_dev;
    uint32_t node;
} AcpiGenericInitiator;

typedef struct AcpiGenericInitiatorClass {
    ObjectClass parent_class;
} AcpiGenericInitiatorClass;

#define TYPE_ACPI_GENERIC_INITIATOR "acpi-generic-initiator"

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
    object_class_property_set_description(oc, "pci-dev",
        "PCI device to associate with the node");
    object_class_property_add(oc, "node", "int", NULL,
        acpi_generic_initiator_set_node, NULL, NULL);
    object_class_property_set_description(oc, "node",
        "NUMA node associated with the PCI device");
}

static int build_acpi_generic_initiator(Object *obj, void *opaque)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    AcpiGenericInitiator *gi;
    GArray *table_data = opaque;
    int32_t devfn;
    uint8_t bus;
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

    bus = object_property_get_uint(o, "busnr", &error_fatal);
    devfn = object_property_get_uint(o, "addr", &error_fatal);
    /* devfn is constrained in PCI to be 8 bit but storage is an int32_t */
    assert(devfn >= 0 && devfn < PCI_DEVFN_MAX);

    build_srat_pci_generic_initiator(table_data, gi->node, 0, bus, devfn);

    return 0;
}

typedef struct AcpiGenericPort {
    /* private */
    Object parent;

    /* public */
    char *pci_bus;
    uint32_t node;
} AcpiGenericPort;

typedef struct AcpiGenericPortClass {
    ObjectClass parent_class;
} AcpiGenericPortClass;

#define TYPE_ACPI_GENERIC_PORT "acpi-generic-port"

OBJECT_DEFINE_TYPE_WITH_INTERFACES(AcpiGenericPort, acpi_generic_port,
                   ACPI_GENERIC_PORT, OBJECT,
                   { TYPE_USER_CREATABLE },
                   { NULL })

OBJECT_DECLARE_SIMPLE_TYPE(AcpiGenericPort, ACPI_GENERIC_PORT)

static void acpi_generic_port_init(Object *obj)
{
    AcpiGenericPort *gp = ACPI_GENERIC_PORT(obj);

    gp->node = MAX_NODES;
    gp->pci_bus = NULL;
}

static void acpi_generic_port_finalize(Object *obj)
{
    AcpiGenericPort *gp = ACPI_GENERIC_PORT(obj);

    g_free(gp->pci_bus);
}

static void acpi_generic_port_set_pci_bus(Object *obj, const char *val,
                                          Error **errp)
{
    AcpiGenericPort *gp = ACPI_GENERIC_PORT(obj);

    gp->pci_bus = g_strdup(val);
}

static void acpi_generic_port_set_node(Object *obj, Visitor *v,
                                       const char *name, void *opaque,
                                       Error **errp)
{
    AcpiGenericPort *gp = ACPI_GENERIC_PORT(obj);
    uint32_t value;

    if (!visit_type_uint32(v, name, &value, errp)) {
        return;
    }

    if (value >= MAX_NODES) {
        error_printf("%s: Invalid NUMA node specified\n",
                     TYPE_ACPI_GENERIC_INITIATOR);
        exit(1);
    }

    gp->node = value;
}

static void acpi_generic_port_class_init(ObjectClass *oc, void *data)
{
    object_class_property_add_str(oc, "pci-bus", NULL,
        acpi_generic_port_set_pci_bus);
    object_class_property_set_description(oc, "pci-bus",
       "PCI Bus of the host bridge associated with this GP affinity structure");
    object_class_property_add(oc, "node", "int", NULL,
        acpi_generic_port_set_node, NULL, NULL);
    object_class_property_set_description(oc, "node",
       "The NUMA node like ID to index HMAT/SLIT NUMA properties involving GP");
}

static int build_acpi_generic_port(Object *obj, void *opaque)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    const char *hid = "ACPI0016";
    GArray *table_data = opaque;
    AcpiGenericPort *gp;
    uint32_t uid;
    Object *o;

    if (!object_dynamic_cast(obj, TYPE_ACPI_GENERIC_PORT)) {
        return 0;
    }

    gp = ACPI_GENERIC_PORT(obj);

    if (gp->node >= ms->numa_state->num_nodes) {
        error_printf("%s: node %d is invalid.\n",
                     TYPE_ACPI_GENERIC_PORT, gp->node);
        exit(1);
    }

    o = object_resolve_path_type(gp->pci_bus, TYPE_PXB_CXL_BUS, NULL);
    if (!o) {
        error_printf("%s: device must be a CXL host bridge.\n",
                     TYPE_ACPI_GENERIC_PORT);
       exit(1);
    }

    uid = object_property_get_uint(o, "acpi_uid", &error_fatal);
    build_srat_acpi_generic_port(table_data, gp->node, hid, uid);

    return 0;
}

void build_srat_generic_affinity_structures(GArray *table_data)
{
    object_child_foreach_recursive(object_get_root(),
                                   build_acpi_generic_initiator,
                                   table_data);
    object_child_foreach_recursive(object_get_root(), build_acpi_generic_port,
                                   table_data);
}
