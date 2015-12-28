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

/****************************************************************
 * PCI IRQs
 ****************************************************************/

    /* Zero => PIC mode, One => APIC Mode */
    Name(\PICF, Zero)
    Method(\_PIC, 1, NotSerialized) {
        Store(Arg0, \PICF)
    }

    Scope(\_SB) {
        Scope(PCI0) {
#define prt_slot_lnk(nr, lnk0, lnk1, lnk2, lnk3)  \
    Package() { nr##ffff, 0, lnk0, 0 },           \
    Package() { nr##ffff, 1, lnk1, 0 },           \
    Package() { nr##ffff, 2, lnk2, 0 },           \
    Package() { nr##ffff, 3, lnk3, 0 }

#define prt_slot_lnkA(nr) prt_slot_lnk(nr, LNKA, LNKB, LNKC, LNKD)
#define prt_slot_lnkB(nr) prt_slot_lnk(nr, LNKB, LNKC, LNKD, LNKA)
#define prt_slot_lnkC(nr) prt_slot_lnk(nr, LNKC, LNKD, LNKA, LNKB)
#define prt_slot_lnkD(nr) prt_slot_lnk(nr, LNKD, LNKA, LNKB, LNKC)

#define prt_slot_lnkE(nr) prt_slot_lnk(nr, LNKE, LNKF, LNKG, LNKH)
#define prt_slot_lnkF(nr) prt_slot_lnk(nr, LNKF, LNKG, LNKH, LNKE)
#define prt_slot_lnkG(nr) prt_slot_lnk(nr, LNKG, LNKH, LNKE, LNKF)
#define prt_slot_lnkH(nr) prt_slot_lnk(nr, LNKH, LNKE, LNKF, LNKG)

            Name(PRTP, package() {
                prt_slot_lnkE(0x0000),
                prt_slot_lnkF(0x0001),
                prt_slot_lnkG(0x0002),
                prt_slot_lnkH(0x0003),
                prt_slot_lnkE(0x0004),
                prt_slot_lnkF(0x0005),
                prt_slot_lnkG(0x0006),
                prt_slot_lnkH(0x0007),
                prt_slot_lnkE(0x0008),
                prt_slot_lnkF(0x0009),
                prt_slot_lnkG(0x000a),
                prt_slot_lnkH(0x000b),
                prt_slot_lnkE(0x000c),
                prt_slot_lnkF(0x000d),
                prt_slot_lnkG(0x000e),
                prt_slot_lnkH(0x000f),
                prt_slot_lnkE(0x0010),
                prt_slot_lnkF(0x0011),
                prt_slot_lnkG(0x0012),
                prt_slot_lnkH(0x0013),
                prt_slot_lnkE(0x0014),
                prt_slot_lnkF(0x0015),
                prt_slot_lnkG(0x0016),
                prt_slot_lnkH(0x0017),
                prt_slot_lnkE(0x0018),

                /* INTA -> PIRQA for slot 25 - 31
                   see the default value of D<N>IR */
                prt_slot_lnkA(0x0019),
                prt_slot_lnkA(0x001a),
                prt_slot_lnkA(0x001b),
                prt_slot_lnkA(0x001c),
                prt_slot_lnkA(0x001d),

                /* PCIe->PCI bridge. use PIRQ[E-H] */
                prt_slot_lnkE(0x001e),

                prt_slot_lnkA(0x001f)
            })

#define prt_slot_gsi(nr, gsi0, gsi1, gsi2, gsi3)  \
    Package() { nr##ffff, 0, gsi0, 0 },           \
    Package() { nr##ffff, 1, gsi1, 0 },           \
    Package() { nr##ffff, 2, gsi2, 0 },           \
    Package() { nr##ffff, 3, gsi3, 0 }

#define prt_slot_gsiA(nr) prt_slot_gsi(nr, GSIA, GSIB, GSIC, GSID)
#define prt_slot_gsiB(nr) prt_slot_gsi(nr, GSIB, GSIC, GSID, GSIA)
#define prt_slot_gsiC(nr) prt_slot_gsi(nr, GSIC, GSID, GSIA, GSIB)
#define prt_slot_gsiD(nr) prt_slot_gsi(nr, GSID, GSIA, GSIB, GSIC)

#define prt_slot_gsiE(nr) prt_slot_gsi(nr, GSIE, GSIF, GSIG, GSIH)
#define prt_slot_gsiF(nr) prt_slot_gsi(nr, GSIF, GSIG, GSIH, GSIE)
#define prt_slot_gsiG(nr) prt_slot_gsi(nr, GSIG, GSIH, GSIE, GSIF)
#define prt_slot_gsiH(nr) prt_slot_gsi(nr, GSIH, GSIE, GSIF, GSIG)

            Name(PRTA, package() {
                prt_slot_gsiE(0x0000),
                prt_slot_gsiF(0x0001),
                prt_slot_gsiG(0x0002),
                prt_slot_gsiH(0x0003),
                prt_slot_gsiE(0x0004),
                prt_slot_gsiF(0x0005),
                prt_slot_gsiG(0x0006),
                prt_slot_gsiH(0x0007),
                prt_slot_gsiE(0x0008),
                prt_slot_gsiF(0x0009),
                prt_slot_gsiG(0x000a),
                prt_slot_gsiH(0x000b),
                prt_slot_gsiE(0x000c),
                prt_slot_gsiF(0x000d),
                prt_slot_gsiG(0x000e),
                prt_slot_gsiH(0x000f),
                prt_slot_gsiE(0x0010),
                prt_slot_gsiF(0x0011),
                prt_slot_gsiG(0x0012),
                prt_slot_gsiH(0x0013),
                prt_slot_gsiE(0x0014),
                prt_slot_gsiF(0x0015),
                prt_slot_gsiG(0x0016),
                prt_slot_gsiH(0x0017),
                prt_slot_gsiE(0x0018),

                /* INTA -> PIRQA for slot 25 - 31, but 30
                   see the default value of D<N>IR */
                prt_slot_gsiA(0x0019),
                prt_slot_gsiA(0x001a),
                prt_slot_gsiA(0x001b),
                prt_slot_gsiA(0x001c),
                prt_slot_gsiA(0x001d),

                /* PCIe->PCI bridge. use PIRQ[E-H] */
                prt_slot_gsiE(0x001e),

                prt_slot_gsiA(0x001f)
            })

            Method(_PRT, 0, NotSerialized) {
                /* PCI IRQ routing table, example from ACPI 2.0a specification,
                   section 6.2.8.1 */
                /* Note: we provide the same info as the PCI routing
                   table of the Bochs BIOS */
                If (LEqual(\PICF, Zero)) {
                    Return (PRTP)
                } Else {
                    Return (PRTA)
                }
            }
        }

        External(LNKA, DeviceObj)
        External(LNKB, DeviceObj)
        External(LNKC, DeviceObj)
        External(LNKD, DeviceObj)
        External(LNKE, DeviceObj)
        External(LNKF, DeviceObj)
        External(LNKG, DeviceObj)
        External(LNKH, DeviceObj)

        External(GSIA, DeviceObj)
        External(GSIB, DeviceObj)
        External(GSIC, DeviceObj)
        External(GSID, DeviceObj)
        External(GSIE, DeviceObj)
        External(GSIF, DeviceObj)
        External(GSIG, DeviceObj)
        External(GSIH, DeviceObj)
    }
}
