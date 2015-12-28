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

    method = aml_method(CPU_EJECT_METHOD, 2, AML_NOTSERIALIZED);
    aml_append(method, aml_sleep(200));
    aml_append(sb_scope, method);

    aml_append(ctx, sb_scope);
}
