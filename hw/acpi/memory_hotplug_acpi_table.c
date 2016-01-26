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

#include "qemu/osdep.h"
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
    mem_ctrl_dev = aml_device(MEMORY_HOTPLUG_DEVICE);
    {
        Aml *one = aml_int(1);
        Aml *zero = aml_int(0);
        Aml *ret_val = aml_local(0);
        Aml *slot_arg0 = aml_arg(0);
        Aml *slots_nr = aml_name(MEMORY_SLOTS_NUMBER);
        Aml *ctrl_lock = aml_name(MEMORY_SLOT_LOCK);
        Aml *slot_selector = aml_name(MEMORY_SLOT_SLECTOR);

        aml_append(mem_ctrl_dev, aml_name_decl("_HID", aml_string("PNP0A06")));
        aml_append(mem_ctrl_dev,
            aml_name_decl("_UID", aml_string("Memory hotplug resources")));

        method = aml_method("_STA", 0, AML_NOTSERIALIZED);
        ifctx = aml_if(aml_equal(slots_nr, zero));
        {
            aml_append(ifctx, aml_return(zero));
        }
        aml_append(method, ifctx);
        /* present, functioning, decoding, not shown in UI */
        aml_append(method, aml_return(aml_int(0xB)));
        aml_append(mem_ctrl_dev, method);

        aml_append(mem_ctrl_dev, aml_mutex(MEMORY_SLOT_LOCK, 0));

        method = aml_method(MEMORY_SLOT_SCAN_METHOD, 0, AML_NOTSERIALIZED);
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
                Aml *ins_evt = aml_name(MEMORY_SLOT_INSERT_EVENT);
                Aml *rm_evt = aml_name(MEMORY_SLOT_REMOVE_EVENT);

                aml_append(while_ctx, aml_store(idx, slot_selector));
                ifctx = aml_if(aml_equal(ins_evt, one));
                {
                    aml_append(ifctx,
                               aml_call2(MEMORY_SLOT_NOTIFY_METHOD,
                                         idx, dev_chk));
                    aml_append(ifctx, aml_store(one, ins_evt));
                }
                aml_append(while_ctx, ifctx);

                else_ctx = aml_else();
                ifctx = aml_if(aml_equal(rm_evt, one));
                {
                    aml_append(ifctx,
                        aml_call2(MEMORY_SLOT_NOTIFY_METHOD,
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

        method = aml_method(MEMORY_SLOT_STATUS_METHOD, 1, AML_NOTSERIALIZED);
        {
            Aml *slot_enabled = aml_name(MEMORY_SLOT_ENABLED);

            aml_append(method, aml_store(zero, ret_val));
            aml_append(method, aml_acquire(ctrl_lock, 0xFFFF));
            aml_append(method,
                aml_store(aml_to_integer(slot_arg0), slot_selector));

            ifctx = aml_if(aml_equal(slot_enabled, one));
            {
                aml_append(ifctx, aml_store(aml_int(0xF), ret_val));
            }
            aml_append(method, ifctx);

            aml_append(method, aml_release(ctrl_lock));
            aml_append(method, aml_return(ret_val));
        }
        aml_append(mem_ctrl_dev, method);

        method = aml_method(MEMORY_SLOT_CRS_METHOD, 1, AML_SERIALIZED);
        {
            Aml *mr64 = aml_name("MR64");
            Aml *mr32 = aml_name("MR32");
            Aml *crs_tmpl = aml_resource_template();
            Aml *minl = aml_name("MINL");
            Aml *minh = aml_name("MINH");
            Aml *maxl =  aml_name("MAXL");
            Aml *maxh =  aml_name("MAXH");
            Aml *lenl = aml_name("LENL");
            Aml *lenh = aml_name("LENH");

            aml_append(method, aml_acquire(ctrl_lock, 0xFFFF));
            aml_append(method, aml_store(aml_to_integer(slot_arg0),
                                         slot_selector));

            aml_append(crs_tmpl,
                aml_qword_memory(AML_POS_DECODE, AML_MIN_FIXED, AML_MAX_FIXED,
                                 AML_CACHEABLE, AML_READ_WRITE,
                                 0, 0x0, 0xFFFFFFFFFFFFFFFEULL, 0,
                                 0xFFFFFFFFFFFFFFFFULL));
            aml_append(method, aml_name_decl("MR64", crs_tmpl));
            aml_append(method,
                aml_create_dword_field(mr64, aml_int(14), "MINL"));
            aml_append(method,
                aml_create_dword_field(mr64, aml_int(18), "MINH"));
            aml_append(method,
                aml_create_dword_field(mr64, aml_int(38), "LENL"));
            aml_append(method,
                aml_create_dword_field(mr64, aml_int(42), "LENH"));
            aml_append(method,
                aml_create_dword_field(mr64, aml_int(22), "MAXL"));
            aml_append(method,
                aml_create_dword_field(mr64, aml_int(26), "MAXH"));

            aml_append(method,
                aml_store(aml_name(MEMORY_SLOT_ADDR_HIGH), minh));
            aml_append(method,
                aml_store(aml_name(MEMORY_SLOT_ADDR_LOW), minl));
            aml_append(method,
                aml_store(aml_name(MEMORY_SLOT_SIZE_HIGH), lenh));
            aml_append(method,
                aml_store(aml_name(MEMORY_SLOT_SIZE_LOW), lenl));

            /* 64-bit math: MAX = MIN + LEN - 1 */
            aml_append(method, aml_add(minl, lenl, maxl));
            aml_append(method, aml_add(minh, lenh, maxh));
            ifctx = aml_if(aml_lless(maxl, minl));
            {
                aml_append(ifctx, aml_add(maxh, one, maxh));
            }
            aml_append(method, ifctx);
            ifctx = aml_if(aml_lless(maxl, one));
            {
                aml_append(ifctx, aml_subtract(maxh, one, maxh));
            }
            aml_append(method, ifctx);
            aml_append(method, aml_subtract(maxl, one, maxl));

            /* return 32-bit _CRS if addr/size is in low mem */
            /* TODO: remove it since all hotplugged DIMMs are in high mem */
            ifctx = aml_if(aml_equal(maxh, zero));
            {
                crs_tmpl = aml_resource_template();
                aml_append(crs_tmpl,
                    aml_dword_memory(AML_POS_DECODE, AML_MIN_FIXED,
                                     AML_MAX_FIXED, AML_CACHEABLE,
                                     AML_READ_WRITE,
                                     0, 0x0, 0xFFFFFFFE, 0,
                                     0xFFFFFFFF));
                aml_append(ifctx, aml_name_decl("MR32", crs_tmpl));
                aml_append(ifctx,
                    aml_create_dword_field(mr32, aml_int(10), "MIN"));
                aml_append(ifctx,
                    aml_create_dword_field(mr32, aml_int(14), "MAX"));
                aml_append(ifctx,
                    aml_create_dword_field(mr32, aml_int(22), "LEN"));
                aml_append(ifctx, aml_store(minl, aml_name("MIN")));
                aml_append(ifctx, aml_store(maxl, aml_name("MAX")));
                aml_append(ifctx, aml_store(lenl, aml_name("LEN")));

                aml_append(ifctx, aml_release(ctrl_lock));
                aml_append(ifctx, aml_return(mr32));
            }
            aml_append(method, ifctx);

            aml_append(method, aml_release(ctrl_lock));
            aml_append(method, aml_return(mr64));
        }
        aml_append(mem_ctrl_dev, method);

        method = aml_method(MEMORY_SLOT_PROXIMITY_METHOD, 1,
                            AML_NOTSERIALIZED);
        {
            Aml *proximity = aml_name(MEMORY_SLOT_PROXIMITY);

            aml_append(method, aml_acquire(ctrl_lock, 0xFFFF));
            aml_append(method, aml_store(aml_to_integer(slot_arg0),
                                         slot_selector));
            aml_append(method, aml_store(proximity, ret_val));
            aml_append(method, aml_release(ctrl_lock));
            aml_append(method, aml_return(ret_val));
        }
        aml_append(mem_ctrl_dev, method);

        method = aml_method(MEMORY_SLOT_OST_METHOD, 4, AML_NOTSERIALIZED);
        {
            Aml *ost_evt = aml_name(MEMORY_SLOT_OST_EVENT);
            Aml *ost_status = aml_name(MEMORY_SLOT_OST_STATUS);

            aml_append(method, aml_acquire(ctrl_lock, 0xFFFF));
            aml_append(method, aml_store(aml_to_integer(slot_arg0),
                                         slot_selector));
            aml_append(method, aml_store(aml_arg(1), ost_evt));
            aml_append(method, aml_store(aml_arg(2), ost_status));
            aml_append(method, aml_release(ctrl_lock));
        }
        aml_append(mem_ctrl_dev, method);

        method = aml_method(MEMORY_SLOT_EJECT_METHOD, 2, AML_NOTSERIALIZED);
        {
            Aml *eject = aml_name(MEMORY_SLOT_EJECT);

            aml_append(method, aml_acquire(ctrl_lock, 0xFFFF));
            aml_append(method, aml_store(aml_to_integer(slot_arg0),
                                         slot_selector));
            aml_append(method, aml_store(one, eject));
            aml_append(method, aml_release(ctrl_lock));
        }
        aml_append(mem_ctrl_dev, method);
    }
    aml_append(pci_scope, mem_ctrl_dev);
    aml_append(ctx, pci_scope);
}
