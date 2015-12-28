/*
 * Memory hotplug AML code of DSDT ACPI table
 *
 * Copyright (C) 2015 Red Hat Inc
 *
 * Author: Igor Mammedov <imammedo@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <stdbool.h>
#include "hw/acpi/memory_hotplug.h"
#include "include/hw/acpi/pc-hotplug.h"
#include "hw/boards.h"

void build_memory_hotplug_aml(Aml *ctx, uint32_t nr_mem,
                              uint16_t io_base, uint16_t io_len)
{
    Aml *ifctx;
    Aml *method;
    Aml *pci_scope;
    Aml *mem_ctrl_dev;

    /* scope for memory hotplug controller device node */
    pci_scope = aml_scope("_SB.PCI0");
    mem_ctrl_dev = aml_scope(stringify(MEMORY_HOTPLUG_DEVICE));
    {
        Aml *zero = aml_int(0);
        Aml *slots_nr = aml_name(stringify(MEMORY_SLOTS_NUMBER));

        method = aml_method("_STA", 0, AML_NOTSERIALIZED);
        ifctx = aml_if(aml_equal(slots_nr, zero));
        {
            aml_append(ifctx, aml_return(zero));
        }
        aml_append(method, ifctx);
        /* present, functioning, decoding, not shown in UI */
        aml_append(method, aml_return(aml_int(0xB)));
        aml_append(mem_ctrl_dev, method);

        aml_append(mem_ctrl_dev, aml_mutex(stringify(MEMORY_SLOT_LOCK), 0));
    }
    aml_append(pci_scope, mem_ctrl_dev);
    aml_append(ctx, pci_scope);
}
