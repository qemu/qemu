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


/****************************************************************
 * PCI Bus definition
 ****************************************************************/
    Scope(\_SB) {
        Device(PCI0) {
            Name(_HID, EisaId("PNP0A08"))
            Name(_CID, EisaId("PNP0A03"))
            Name(_ADR, 0x00)
            Name(_UID, 1)

            External(ISA, DeviceObj)

            // _OSC: based on sample of ACPI3.0b spec
            Name(SUPP, 0) // PCI _OSC Support Field value
            Name(CTRL, 0) // PCI _OSC Control Field value
            Method(_OSC, 4) {
                // Create DWORD-addressable fields from the Capabilities Buffer
                CreateDWordField(Arg3, 0, CDW1)

                // Check for proper UUID
                If (LEqual(Arg0, ToUUID("33DB4D5B-1FF7-401C-9657-7441C03DD766"))) {
                    // Create DWORD-addressable fields from the Capabilities Buffer
                    CreateDWordField(Arg3, 4, CDW2)
                    CreateDWordField(Arg3, 8, CDW3)

                    // Save Capabilities DWORD2 & 3
                    Store(CDW2, SUPP)
                    Store(CDW3, CTRL)

                    // Always allow native PME, AER (no dependencies)
                    // Never allow SHPC (no SHPC controller in this system)
                    And(CTRL, 0x1D, CTRL)

#if 0 // For now, nothing to do
                    If (Not(And(CDW1, 1))) { // Query flag clear?
                        // Disable GPEs for features granted native control.
                        If (And(CTRL, 0x01)) { // Hot plug control granted?
                            Store(0, HPCE) // clear the hot plug SCI enable bit
                            Store(1, HPCS) // clear the hot plug SCI status bit
                        }
                        If (And(CTRL, 0x04)) { // PME control granted?
                            Store(0, PMCE) // clear the PME SCI enable bit
                            Store(1, PMCS) // clear the PME SCI status bit
                        }
                        If (And(CTRL, 0x10)) { // OS restoring PCI Express cap structure?
                            // Set status to not restore PCI Express cap structure
                            // upon resume from S3
                            Store(1, S3CR)
                        }
                    }
#endif
                    If (LNotEqual(Arg1, One)) {
                        // Unknown revision
                        Or(CDW1, 0x08, CDW1)
                    }
                    If (LNotEqual(CDW3, CTRL)) {
                        // Capabilities bits were masked
                        Or(CDW1, 0x10, CDW1)
                    }
                    // Update DWORD3 in the buffer
                    Store(CTRL, CDW3)
                } Else {
                    Or(CDW1, 4, CDW1) // Unrecognized UUID
                }
                Return (Arg3)
            }
        }
    }
}
