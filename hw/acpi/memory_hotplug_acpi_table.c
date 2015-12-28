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
        Aml *one = aml_int(1);
        Aml *zero = aml_int(0);
        Aml *slots_nr = aml_name(stringify(MEMORY_SLOTS_NUMBER));
        Aml *ctrl_lock = aml_name(stringify(MEMORY_SLOT_LOCK));
        Aml *slot_selector = aml_name(stringify(MEMORY_SLOT_SLECTOR));

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

        method = aml_method(stringify(MEMORY_SLOT_SCAN_METHOD), 0,
                            AML_NOTSERIALIZED);
        {
            Aml *else_ctx;
            Aml *while_ctx;
            Aml *idx = aml_local(0);
            Aml *eject_req = aml_int(3);
            Aml *dev_chk = aml_int(1);

            ifctx = aml_if(aml_equal(slots_nr, zero));
            {
                aml_append(ifctx, aml_return(zero));
            }
            aml_append(method, ifctx);

            aml_append(method, aml_store(zero, idx));
            aml_append(method, aml_acquire(ctrl_lock, 0xFFFF));
            /* build AML that:
             * loops over all slots and Notifies DIMMs with
             * Device Check or Eject Request notifications if
             * slot has corresponding status bit set and clears
             * slot status.
             */
            while_ctx = aml_while(aml_lless(idx, slots_nr));
            {
                Aml *ins_evt = aml_name(stringify(MEMORY_SLOT_INSERT_EVENT));
                Aml *rm_evt = aml_name(stringify(MEMORY_SLOT_REMOVE_EVENT));

                aml_append(while_ctx, aml_store(idx, slot_selector));
                ifctx = aml_if(aml_equal(ins_evt, one));
                {
                    aml_append(ifctx,
                               aml_call2(stringify(MEMORY_SLOT_NOTIFY_METHOD),
                                         idx, dev_chk));
                    aml_append(ifctx, aml_store(one, ins_evt));
                }
                aml_append(while_ctx, ifctx);

                else_ctx = aml_else();
                ifctx = aml_if(aml_equal(rm_evt, one));
                {
                    aml_append(ifctx,
                        aml_call2(stringify(MEMORY_SLOT_NOTIFY_METHOD),
                                  idx, eject_req));
                    aml_append(ifctx, aml_store(one, rm_evt));
                }
                aml_append(else_ctx, ifctx);
                aml_append(while_ctx, else_ctx);

                aml_append(while_ctx, aml_add(idx, one, idx));
            }
            aml_append(method, while_ctx);
            aml_append(method, aml_release(ctrl_lock));
            aml_append(method, aml_return(one));
        }
        aml_append(mem_ctrl_dev, method);
    }
    aml_append(pci_scope, mem_ctrl_dev);
    aml_append(ctx, pci_scope);
}
