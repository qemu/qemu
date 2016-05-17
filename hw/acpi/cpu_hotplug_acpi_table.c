/*
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
#include "hw/acpi/cpu_hotplug.h"
#include "hw/i386/pc.h"

void build_cpu_hotplug_aml(Aml *ctx, MachineState *machine,
                           uint16_t io_base, uint16_t io_len)
{
    Aml *dev;
    Aml *crs;
    Aml *pkg;
    Aml *field;
    Aml *method;
    Aml *if_ctx;
    Aml *else_ctx;
    int i, apic_idx;
    Aml *sb_scope = aml_scope("_SB");
    uint8_t madt_tmpl[8] = {0x00, 0x08, 0x00, 0x00, 0x00, 0, 0, 0};
    Aml *cpu_id = aml_arg(0);
    Aml *cpu_on = aml_local(0);
    Aml *madt = aml_local(1);
    Aml *cpus_map = aml_name(CPU_ON_BITMAP);
    Aml *zero = aml_int(0);
    Aml *one = aml_int(1);
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    CPUArchIdList *apic_ids = mc->possible_cpu_arch_ids(machine);
    PCMachineState *pcms = PC_MACHINE(machine);

    /*
     * _MAT method - creates an madt apic buffer
     * cpu_id = Arg0 = Processor ID = Local APIC ID
     * cpu_on = Local0 = CPON flag for this cpu
     * madt = Local1 = Buffer (in madt apic form) to return
     */
    method = aml_method(CPU_MAT_METHOD, 1, AML_NOTSERIALIZED);
    aml_append(method,
        aml_store(aml_derefof(aml_index(cpus_map, cpu_id)), cpu_on));
    aml_append(method,
        aml_store(aml_buffer(sizeof(madt_tmpl), madt_tmpl), madt));
    /* Update the processor id, lapic id, and enable/disable status */
    aml_append(method, aml_store(cpu_id, aml_index(madt, aml_int(2))));
    aml_append(method, aml_store(cpu_id, aml_index(madt, aml_int(3))));
    aml_append(method, aml_store(cpu_on, aml_index(madt, aml_int(4))));
    aml_append(method, aml_return(madt));
    aml_append(sb_scope, method);

    /*
     * _STA method - return ON status of cpu
     * cpu_id = Arg0 = Processor ID = Local APIC ID
     * cpu_on = Local0 = CPON flag for this cpu
     */
    method = aml_method(CPU_STATUS_METHOD, 1, AML_NOTSERIALIZED);
    aml_append(method,
        aml_store(aml_derefof(aml_index(cpus_map, cpu_id)), cpu_on));
    if_ctx = aml_if(cpu_on);
    {
        aml_append(if_ctx, aml_return(aml_int(0xF)));
    }
    aml_append(method, if_ctx);
    else_ctx = aml_else();
    {
        aml_append(else_ctx, aml_return(zero));
    }
    aml_append(method, else_ctx);
    aml_append(sb_scope, method);

    method = aml_method(CPU_EJECT_METHOD, 2, AML_NOTSERIALIZED);
    aml_append(method, aml_sleep(200));
    aml_append(sb_scope, method);

    method = aml_method(CPU_SCAN_METHOD, 0, AML_NOTSERIALIZED);
    {
        Aml *while_ctx, *if_ctx2, *else_ctx2;
        Aml *bus_check_evt = aml_int(1);
        Aml *remove_evt = aml_int(3);
        Aml *status_map = aml_local(5); /* Local5 = active cpu bitmap */
        Aml *byte = aml_local(2); /* Local2 = last read byte from bitmap */
        Aml *idx = aml_local(0); /* Processor ID / APIC ID iterator */
        Aml *is_cpu_on = aml_local(1); /* Local1 = CPON flag for cpu */
        Aml *status = aml_local(3); /* Local3 = active state for cpu */

        aml_append(method, aml_store(aml_name(CPU_STATUS_MAP), status_map));
        aml_append(method, aml_store(zero, byte));
        aml_append(method, aml_store(zero, idx));

        /* While (idx < SizeOf(CPON)) */
        while_ctx = aml_while(aml_lless(idx, aml_sizeof(cpus_map)));
        aml_append(while_ctx,
            aml_store(aml_derefof(aml_index(cpus_map, idx)), is_cpu_on));

        if_ctx = aml_if(aml_and(idx, aml_int(0x07), NULL));
        {
            /* Shift down previously read bitmap byte */
            aml_append(if_ctx, aml_shiftright(byte, one, byte));
        }
        aml_append(while_ctx, if_ctx);

        else_ctx = aml_else();
        {
            /* Read next byte from cpu bitmap */
            aml_append(else_ctx, aml_store(aml_derefof(aml_index(status_map,
                       aml_shiftright(idx, aml_int(3), NULL))), byte));
        }
        aml_append(while_ctx, else_ctx);

        aml_append(while_ctx, aml_store(aml_and(byte, one, NULL), status));
        if_ctx = aml_if(aml_lnot(aml_equal(is_cpu_on, status)));
        {
            /* State change - update CPON with new state */
            aml_append(if_ctx, aml_store(status, aml_index(cpus_map, idx)));
            if_ctx2 = aml_if(aml_equal(status, one));
            {
                aml_append(if_ctx2,
                    aml_call2(AML_NOTIFY_METHOD, idx, bus_check_evt));
            }
            aml_append(if_ctx, if_ctx2);
            else_ctx2 = aml_else();
            {
                aml_append(else_ctx2,
                    aml_call2(AML_NOTIFY_METHOD, idx, remove_evt));
            }
        }
        aml_append(if_ctx, else_ctx2);
        aml_append(while_ctx, if_ctx);

        aml_append(while_ctx, aml_increment(idx)); /* go to next cpu */
        aml_append(method, while_ctx);
    }
    aml_append(sb_scope, method);

    /* The current AML generator can cover the APIC ID range [0..255],
     * inclusive, for VCPU hotplug. */
    QEMU_BUILD_BUG_ON(ACPI_CPU_HOTPLUG_ID_LIMIT > 256);
    g_assert(pcms->apic_id_limit <= ACPI_CPU_HOTPLUG_ID_LIMIT);

    /* create PCI0.PRES device and its _CRS to reserve CPU hotplug MMIO */
    dev = aml_device("PCI0." stringify(CPU_HOTPLUG_RESOURCE_DEVICE));
    aml_append(dev, aml_name_decl("_HID", aml_eisaid("PNP0A06")));
    aml_append(dev,
        aml_name_decl("_UID", aml_string("CPU Hotplug resources"))
    );
    /* device present, functioning, decoding, not shown in UI */
    aml_append(dev, aml_name_decl("_STA", aml_int(0xB)));
    crs = aml_resource_template();
    aml_append(crs,
        aml_io(AML_DECODE16, io_base, io_base, 1, io_len)
    );
    aml_append(dev, aml_name_decl("_CRS", crs));
    aml_append(sb_scope, dev);
    /* declare CPU hotplug MMIO region and PRS field to access it */
    aml_append(sb_scope, aml_operation_region(
        "PRST", AML_SYSTEM_IO, aml_int(io_base), io_len));
    field = aml_field("PRST", AML_BYTE_ACC, AML_NOLOCK, AML_PRESERVE);
    aml_append(field, aml_named_field("PRS", 256));
    aml_append(sb_scope, field);

    /* build Processor object for each processor */
    for (i = 0; i < apic_ids->len; i++) {
        int apic_id = apic_ids->cpus[i].arch_id;

        assert(apic_id < ACPI_CPU_HOTPLUG_ID_LIMIT);

        dev = aml_processor(apic_id, 0, 0, "CP%.02X", apic_id);

        method = aml_method("_MAT", 0, AML_NOTSERIALIZED);
        aml_append(method,
            aml_return(aml_call1(CPU_MAT_METHOD, aml_int(apic_id))));
        aml_append(dev, method);

        method = aml_method("_STA", 0, AML_NOTSERIALIZED);
        aml_append(method,
            aml_return(aml_call1(CPU_STATUS_METHOD, aml_int(apic_id))));
        aml_append(dev, method);

        method = aml_method("_EJ0", 1, AML_NOTSERIALIZED);
        aml_append(method,
            aml_return(aml_call2(CPU_EJECT_METHOD, aml_int(apic_id),
                aml_arg(0)))
        );
        aml_append(dev, method);

        aml_append(sb_scope, dev);
    }

    /* build this code:
     *   Method(NTFY, 2) {If (LEqual(Arg0, 0x00)) {Notify(CP00, Arg1)} ...}
     */
    /* Arg0 = Processor ID = APIC ID */
    method = aml_method(AML_NOTIFY_METHOD, 2, AML_NOTSERIALIZED);
    for (i = 0; i < apic_ids->len; i++) {
        int apic_id = apic_ids->cpus[i].arch_id;

        if_ctx = aml_if(aml_equal(aml_arg(0), aml_int(apic_id)));
        aml_append(if_ctx,
            aml_notify(aml_name("CP%.02X", apic_id), aml_arg(1))
        );
        aml_append(method, if_ctx);
    }
    aml_append(sb_scope, method);

    /* build "Name(CPON, Package() { One, One, ..., Zero, Zero, ... })"
     *
     * Note: The ability to create variable-sized packages was first
     * introduced in ACPI 2.0. ACPI 1.0 only allowed fixed-size packages
     * ith up to 255 elements. Windows guests up to win2k8 fail when
     * VarPackageOp is used.
     */
    pkg = pcms->apic_id_limit <= 255 ? aml_package(pcms->apic_id_limit) :
                                       aml_varpackage(pcms->apic_id_limit);

    for (i = 0, apic_idx = 0; i < apic_ids->len; i++) {
        int apic_id = apic_ids->cpus[i].arch_id;

        for (; apic_idx < apic_id; apic_idx++) {
            aml_append(pkg, aml_int(0));
        }
        aml_append(pkg, aml_int(apic_ids->cpus[i].cpu ? 1 : 0));
        apic_idx = apic_id + 1;
    }
    aml_append(sb_scope, aml_name_decl(CPU_ON_BITMAP, pkg));
    g_free(apic_ids);

    aml_append(ctx, sb_scope);

    method = aml_method("\\_GPE._E02", 0, AML_NOTSERIALIZED);
    aml_append(method, aml_call0("\\_SB." CPU_SCAN_METHOD));
    aml_append(ctx, method);
}
