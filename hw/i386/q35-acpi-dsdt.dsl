/*
 * Bochs/QEMU ACPI DSDT ASL definition
 *
 * Copyright (c) 2006 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */
/*
 * Copyright (c) 2010 Isaku Yamahata
 *                    yamahata at valinux co jp
 * Based on acpi-dsdt.dsl, but heavily modified for q35 chipset.
 */


ACPI_EXTRACT_ALL_CODE Q35AcpiDsdtAmlCode

DefinitionBlock (
    "q35-acpi-dsdt.aml",// Output Filename
    "DSDT",             // Signature
    0x01,               // DSDT Compliance Revision
    "BXPC",             // OEMID
    "BXDSDT",           // TABLE ID
    0x2                 // OEM Revision
    )
{

    Scope(\_SB) {
        OperationRegion(PCST, SystemIO, 0xae00, 0x0c)
        OperationRegion(PCSB, SystemIO, 0xae0c, 0x01)
        Field(PCSB, AnyAcc, NoLock, WriteAsZeros) {
            PCIB, 8,
        }
    }
}
