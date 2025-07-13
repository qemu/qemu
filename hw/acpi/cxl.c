/*
 * CXL ACPI Implementation
 *
 * Copyright(C) 2020 Intel Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pci_host.h"
#include "hw/cxl/cxl.h"
#include "hw/cxl/cxl_host.h"
#include "hw/mem/memory-device.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/aml-build.h"
#include "hw/acpi/bios-linker-loader.h"
#include "hw/acpi/cxl.h"
#include "qapi/error.h"
#include "qemu/uuid.h"

void build_cxl_dsm_method(Aml *dev)
{
    Aml *method, *ifctx, *ifctx2;

    method = aml_method("_DSM", 4, AML_SERIALIZED);
    {
        Aml *function, *uuid;

        uuid = aml_arg(0);
        function = aml_arg(2);
        /* CXL spec v3.0 9.17.3.1 _DSM Function for Retrieving QTG ID */
        ifctx = aml_if(aml_equal(
            uuid, aml_touuid("F365F9A6-A7DE-4071-A66A-B40C0B4F8E52")));

        /* Function 0, standard DSM query function */
        ifctx2 = aml_if(aml_equal(function, aml_int(0)));
        {
            uint8_t byte_list[1] = { 0x01 }; /* function 1 only */

            aml_append(ifctx2,
                       aml_return(aml_buffer(sizeof(byte_list), byte_list)));
        }
        aml_append(ifctx, ifctx2);

        /*
         * Function 1
         * Creating a package with static values. The max supported QTG ID will
         * be 1 and recommended QTG IDs are 0 and then 1.
         * The values here are statically created to simplify emulation. Values
         * from a real BIOS would be determined by the performance of all the
         * present CXL memory and then assigned.
         */
        ifctx2 = aml_if(aml_equal(function, aml_int(1)));
        {
            Aml *pak, *pak1;

            /*
             * Return: A package containing two elements - a WORD that returns
             * the maximum throttling group that the platform supports, and a
             * package containing the QTG ID(s) that the platform recommends.
             * Package {
             *     Max Supported QTG ID
             *     Package {QTG Recommendations}
             * }
             *
             * While the SPEC specified WORD that hints at the value being
             * 16bit, the ACPI dump of BIOS DSDT table showed that the values
             * are integers with no specific size specification. aml_int() will
             * be used for the values.
             */
            pak1 = aml_package(2);
            /* Set QTG ID of 0 */
            aml_append(pak1, aml_int(0));
            /* Set QTG ID of 1 */
            aml_append(pak1, aml_int(1));

            pak = aml_package(2);
            /* Set Max QTG 1 */
            aml_append(pak, aml_int(1));
            aml_append(pak, pak1);

            aml_append(ifctx2, aml_return(pak));
        }
        aml_append(ifctx, ifctx2);
    }
    aml_append(method, ifctx);
    aml_append(dev, method);
}

static void cedt_build_chbs(GArray *table_data, PXBCXLDev *cxl)
{
    PXBDev *pxb = PXB_DEV(cxl);
    SysBusDevice *sbd = SYS_BUS_DEVICE(cxl->cxl_host_bridge);
    struct MemoryRegion *mr = sbd->mmio[0].memory;

    /* Type */
    build_append_int_noprefix(table_data, 0, 1);

    /* Reserved */
    build_append_int_noprefix(table_data, 0, 1);

    /* Record Length */
    build_append_int_noprefix(table_data, 32, 2);

    /* UID - currently equal to bus number */
    build_append_int_noprefix(table_data, pxb->bus_nr, 4);

    /* Version */
    build_append_int_noprefix(table_data, 1, 4);

    /* Reserved */
    build_append_int_noprefix(table_data, 0, 4);

    /* Base - subregion within a container that is in PA space */
    build_append_int_noprefix(table_data, mr->container->addr + mr->addr, 8);

    /* Length */
    build_append_int_noprefix(table_data, memory_region_size(mr), 8);
}

/*
 * CFMWS entries in CXL 2.0 ECN: CEDT CFMWS & QTG _DSM.
 * Interleave ways encoding in CXL 2.0 ECN: 3, 6, 12 and 16-way memory
 * interleaving.
 */
static void cedt_build_cfmws(CXLFixedWindow *fw, Aml *cedt)
{
    GArray *table_data = cedt->buf;
    int i;

    /* Type */
    build_append_int_noprefix(table_data, 1, 1);

    /* Reserved */
    build_append_int_noprefix(table_data, 0, 1);

    /* Record Length */
    build_append_int_noprefix(table_data, 36 + 4 * fw->num_targets, 2);

    /* Reserved */
    build_append_int_noprefix(table_data, 0, 4);

    /* Base HPA */
    build_append_int_noprefix(table_data, fw->mr.addr, 8);

    /* Window Size */
    build_append_int_noprefix(table_data, fw->size, 8);

    /* Host Bridge Interleave Ways */
    build_append_int_noprefix(table_data, fw->enc_int_ways, 1);

    /* Host Bridge Interleave Arithmetic */
    build_append_int_noprefix(table_data, 0, 1);

    /* Reserved */
    build_append_int_noprefix(table_data, 0, 2);

    /* Host Bridge Interleave Granularity */
    build_append_int_noprefix(table_data, fw->enc_int_gran, 4);

    /* Window Restrictions */
    build_append_int_noprefix(table_data, 0x0f, 2);

    /* QTG ID */
    build_append_int_noprefix(table_data, 0, 2);

    /* Host Bridge List (list of UIDs - currently bus_nr) */
    for (i = 0; i < fw->num_targets; i++) {
        g_assert(fw->target_hbs[i]);
        build_append_int_noprefix(table_data,
                                  PXB_DEV(fw->target_hbs[i])->bus_nr, 4);
    }
}

static int cxl_foreach_pxb_hb(Object *obj, void *opaque)
{
    Aml *cedt = opaque;

    if (object_dynamic_cast(obj, TYPE_PXB_CXL_DEV)) {
        cedt_build_chbs(cedt->buf, PXB_CXL_DEV(obj));
    }

    return 0;
}

void cxl_build_cedt(GArray *table_offsets, GArray *table_data,
                    BIOSLinker *linker, const char *oem_id,
                    const char *oem_table_id, CXLState *cxl_state)
{
    GSList *cfmws_list, *iter;
    Aml *cedt;
    AcpiTable table = { .sig = "CEDT", .rev = 1, .oem_id = oem_id,
                        .oem_table_id = oem_table_id };

    acpi_add_table(table_offsets, table_data);
    acpi_table_begin(&table, table_data);
    cedt = init_aml_allocator();

    /* reserve space for CEDT header */

    object_child_foreach_recursive(object_get_root(), cxl_foreach_pxb_hb, cedt);

    cfmws_list = cxl_fmws_get_all_sorted();
    for (iter = cfmws_list; iter; iter = iter->next) {
        cedt_build_cfmws(CXL_FMW(iter->data), cedt);
    }
    g_slist_free(cfmws_list);

    /* copy AML table into ACPI tables blob and patch header there */
    g_array_append_vals(table_data, cedt->buf->data, cedt->buf->len);
    free_aml_allocator();

    acpi_table_end(linker, &table);
}

static Aml *__build_cxl_osc_method(void)
{
    Aml *method, *if_uuid, *else_uuid, *if_arg1_not_1, *if_cxl, *if_caps_masked;
    Aml *a_ctrl = aml_local(0);
    Aml *a_cdw1 = aml_name("CDW1");

    method = aml_method("_OSC", 4, AML_NOTSERIALIZED);
    /* CDW1 is used for the return value so is present whether or not a match occurs */
    aml_append(method, aml_create_dword_field(aml_arg(3), aml_int(0), "CDW1"));

    /*
     * Generate shared section between:
     * CXL 2.0 - 9.14.2.1.4 and
     * PCI Firmware Specification 3.0
     * 4.5.1. _OSC Interface for PCI Host Bridge Devices
     * The _OSC interface for a PCI/PCI-X/PCI Express hierarchy is
     * identified by the Universal Unique IDentifier (UUID)
     * 33DB4D5B-1FF7-401C-9657-7441C03DD766
     * The _OSC interface for a CXL Host bridge is
     * identified by the UUID 68F2D50B-C469-4D8A-BD3D-941A103FD3FC
     * A CXL Host bridge is compatible with a PCI host bridge so
     * for the shared section match both.
     */
    if_uuid = aml_if(
        aml_lor(aml_equal(aml_arg(0),
                          aml_touuid("33DB4D5B-1FF7-401C-9657-7441C03DD766")),
                aml_equal(aml_arg(0),
                          aml_touuid("68F2D50B-C469-4D8A-BD3D-941A103FD3FC"))));
    aml_append(if_uuid, aml_create_dword_field(aml_arg(3), aml_int(4), "CDW2"));
    aml_append(if_uuid, aml_create_dword_field(aml_arg(3), aml_int(8), "CDW3"));

    aml_append(if_uuid, aml_store(aml_name("CDW3"), a_ctrl));

    /*
     *
     * Allows OS control for all 5 features:
     * PCIeHotplug SHPCHotplug PME AER PCIeCapability
     */
    aml_append(if_uuid, aml_and(a_ctrl, aml_int(0x1F), a_ctrl));

    /*
     * Check _OSC revision.
     * PCI Firmware specification 3.3 and CXL 2.0 both use revision 1
     * Unknown Revision is CDW1 - BIT (3)
     */
    if_arg1_not_1 = aml_if(aml_lnot(aml_equal(aml_arg(1), aml_int(0x1))));
    aml_append(if_arg1_not_1, aml_or(a_cdw1, aml_int(0x08), a_cdw1));
    aml_append(if_uuid, if_arg1_not_1);

    if_caps_masked = aml_if(aml_lnot(aml_equal(aml_name("CDW3"), a_ctrl)));

    /* Capability bits were masked */
    aml_append(if_caps_masked, aml_or(a_cdw1, aml_int(0x10), a_cdw1));
    aml_append(if_uuid, if_caps_masked);

    aml_append(if_uuid, aml_store(aml_name("CDW2"), aml_name("SUPP")));
    aml_append(if_uuid, aml_store(aml_name("CDW3"), aml_name("CTRL")));

    /* Update DWORD3 (the return value) */
    aml_append(if_uuid, aml_store(a_ctrl, aml_name("CDW3")));

    /* CXL only section as per CXL 2.0 - 9.14.2.1.4 */
    if_cxl = aml_if(aml_equal(
        aml_arg(0), aml_touuid("68F2D50B-C469-4D8A-BD3D-941A103FD3FC")));
    /* CXL support field */
    aml_append(if_cxl, aml_create_dword_field(aml_arg(3), aml_int(12), "CDW4"));
    /* CXL capabilities */
    aml_append(if_cxl, aml_create_dword_field(aml_arg(3), aml_int(16), "CDW5"));
    aml_append(if_cxl, aml_store(aml_name("CDW4"), aml_name("SUPC")));
    aml_append(if_cxl, aml_store(aml_name("CDW5"), aml_name("CTRC")));

    /* CXL 2.0 Port/Device Register access */
    aml_append(if_cxl,
               aml_or(aml_name("CDW5"), aml_int(0x1), aml_name("CDW5")));
    aml_append(if_uuid, if_cxl);

    aml_append(if_uuid, aml_return(aml_arg(3)));
    aml_append(method, if_uuid);

    /*
     * If no UUID matched, return Unrecognized UUID via Arg3 DWord 1
     * ACPI 6.4 - 6.2.11
     * Unrecognised UUID - BIT(2)
     */
    else_uuid = aml_else();

    aml_append(else_uuid,
               aml_or(aml_name("CDW1"), aml_int(0x4), aml_name("CDW1")));
    aml_append(else_uuid, aml_return(aml_arg(3)));
    aml_append(method, else_uuid);

    return method;
}

void build_cxl_osc_method(Aml *dev)
{
    aml_append(dev, aml_name_decl("SUPP", aml_int(0)));
    aml_append(dev, aml_name_decl("CTRL", aml_int(0)));
    aml_append(dev, aml_name_decl("SUPC", aml_int(0)));
    aml_append(dev, aml_name_decl("CTRC", aml_int(0)));
    aml_append(dev, __build_cxl_osc_method());
}
