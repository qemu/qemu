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

/* This file is the basis for the ssdt table generated in src/acpi.c.
 * It defines the contents of the per-cpu Processor() object.  At
 * runtime, a dynamically generated SSDT will contain one copy of this
 * AML snippet for every possible cpu in the system.  The objects will
 * be placed in the \_SB_ namespace.
 *
 * In addition to the aml code generated from this file, the
 * src/acpi.c file creates a NTFY method with an entry for each cpu:
 *     Method(NTFY, 2) {
 *         If (LEqual(Arg0, 0x00)) { Notify(CP00, Arg1) }
 *         If (LEqual(Arg0, 0x01)) { Notify(CP01, Arg1) }
 *         ...
 *     }
 * and a CPON array with the list of active and inactive cpus:
 *     Name(CPON, Package() { One, One, ..., Zero, Zero, ... })
 */

ACPI_EXTRACT_ALL_CODE ssdp_proc_aml

DefinitionBlock ("ssdt-proc.aml", "SSDT", 0x01, "BXPC", "BXSSDT", 0x1)
{
    ACPI_EXTRACT_PROCESSOR_START ssdt_proc_start
    ACPI_EXTRACT_PROCESSOR_END ssdt_proc_end
    ACPI_EXTRACT_PROCESSOR_STRING ssdt_proc_name
    Processor(CPAA, 0xAA, 0x00000000, 0x0) {
        ACPI_EXTRACT_NAME_BYTE_CONST ssdt_proc_id
        Name(ID, 0xAA)
/*
 * The src/acpi.c code requires the above ACP_EXTRACT tags so that it can update
 * CPAA and 0xAA with the appropriate CPU id (see
 * SD_OFFSET_CPUHEX/CPUID1/CPUID2).  Don't change the above without
 * also updating the C code.
 */
        Name(_HID, "ACPI0007")
        External(CPMA, MethodObj)
        External(CPST, MethodObj)
        External(CPEJ, MethodObj)
        Method(_MAT, 0) {
            Return (CPMA(ID))
        }
        Method(_STA, 0) {
            Return (CPST(ID))
        }
        Method(_EJ0, 1, NotSerialized) {
            CPEJ(ID, Arg0)
        }
    }
}
