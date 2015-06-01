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
#include "hw/acpi/tpm.h"

ACPI_EXTRACT_ALL_CODE ssdt_tpm2_aml

DefinitionBlock (
    "ssdt-tpm2.aml",    // Output Filename
    "SSDT",             // Signature
    0x01,               // SSDT Compliance Revision
    "BXPC",             // OEMID
    "BXSSDT",           // TABLE ID
    0x1                 // OEM Revision
    )
{
#include "ssdt-tpm-common.dsl"
}
