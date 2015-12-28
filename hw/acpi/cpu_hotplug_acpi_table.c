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

#include "hw/acpi/cpu_hotplug.h"

void build_cpu_hotplug_aml(Aml *ctx)
{
    Aml *method;
    Aml *sb_scope = aml_scope("_SB");
    uint8_t madt_tmpl[8] = {0x00, 0x08, 0x00, 0x00, 0x00, 0, 0, 0};
    Aml *cpu_id = aml_arg(0);
    Aml *cpu_on = aml_local(0);
    Aml *madt = aml_local(1);
    Aml *cpus_map = aml_name(CPU_ON_BITMAP);

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

    method = aml_method(CPU_EJECT_METHOD, 2, AML_NOTSERIALIZED);
    aml_append(method, aml_sleep(200));
    aml_append(sb_scope, method);

    aml_append(ctx, sb_scope);
}
