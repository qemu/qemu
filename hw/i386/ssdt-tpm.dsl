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

ACPI_EXTRACT_ALL_CODE ssdt_tpm_aml

DefinitionBlock (
    "ssdt-tpm.aml",     // Output Filename
    "SSDT",             // Signature
    0x01,               // SSDT Compliance Revision
    "BXPC",             // OEMID
    "BXSSDT",           // TABLE ID
    0x1                 // OEM Revision
    )
{
    Scope(\_SB) {
        /* TPM with emulated TPM TIS interface */
        Device (TPM) {
            Name (_HID, EisaID ("PNP0C31"))
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, TPM_TIS_ADDR_BASE, TPM_TIS_ADDR_SIZE)
                // older Linux tpm_tis drivers do not work with IRQ
                //IRQNoFlags () {TPM_TIS_IRQ}
            })
            Method (_STA, 0, NotSerialized) {
                Return (0x0F)
            }
        }
    }
}
