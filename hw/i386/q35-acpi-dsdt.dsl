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

#include "acpi-dsdt-dbug.dsl"

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
#define BOARD_SPECIFIC_PCI_RESOURSES \
     WordIO(ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange, \
         0x0000, \
         0x0000, \
         0x0CD7, \
         0x0000, \
         0x0CD8, \
         ,, , TypeStatic) \
     /* 0xcd8-0xcf7 hole for CPU hotplug, hw/acpi/ich9.c:ICH9_PROC_BASE */ \
     WordIO(ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange, \
         0x0000, \
         0x0D00, \
         0xFFFF, \
         0x0000, \
         0xF300, \
         ,, , TypeStatic)

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

#include "acpi-dsdt-pci-crs.dsl"
#include "acpi-dsdt-hpet.dsl"


/****************************************************************
 * LPC ISA bridge
 ****************************************************************/

    Scope(\_SB.PCI0) {
        /* PCI D31:f0 LPC ISA bridge */
        Device(ISA) {
            Name (_ADR, 0x001F0000)  // _ADR: Address

            /* ICH9 PCI to ISA irq remapping */
            OperationRegion(PIRQ, PCI_Config, 0x60, 0x0C)

            OperationRegion(LPCD, PCI_Config, 0x80, 0x2)
            Field(LPCD, AnyAcc, NoLock, Preserve) {
                COMA,   3,
                    ,   1,
                COMB,   3,

                Offset(0x01),
                LPTD,   2,
                    ,   2,
                FDCD,   2
            }
            OperationRegion(LPCE, PCI_Config, 0x82, 0x2)
            Field(LPCE, AnyAcc, NoLock, Preserve) {
                CAEN,   1,
                CBEN,   1,
                LPEN,   1,
                FDEN,   1
            }
        }
    }

#define DSDT_APPLESMC_STA q35_dsdt_applesmc_sta
#include "acpi-dsdt-isa.dsl"


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

        Field(PCI0.ISA.PIRQ, ByteAcc, NoLock, Preserve) {
            PRQA,   8,
            PRQB,   8,
            PRQC,   8,
            PRQD,   8,

            Offset(0x08),
            PRQE,   8,
            PRQF,   8,
            PRQG,   8,
            PRQH,   8
        }

        Method(IQST, 1, NotSerialized) {
            // _STA method - get status
            If (And(0x80, Arg0)) {
                Return (0x09)
            }
            Return (0x0B)
        }
        Method(IQCR, 1, Serialized) {
            // _CRS method - get current settings
            Name(PRR0, ResourceTemplate() {
                Interrupt(, Level, ActiveHigh, Shared) { 0 }
            })
            CreateDWordField(PRR0, 0x05, PRRI)
            Store(And(Arg0, 0x0F), PRRI)
            Return (PRR0)
        }

#define define_link(link, uid, reg)                             \
        Device(link) {                                          \
            Name(_HID, EISAID("PNP0C0F"))                       \
            Name(_UID, uid)                                     \
            Name(_PRS, ResourceTemplate() {                     \
                Interrupt(, Level, ActiveHigh, Shared) {        \
                    5, 10, 11                                   \
                }                                               \
            })                                                  \
            Method(_STA, 0, NotSerialized) {                    \
                Return (IQST(reg))                              \
            }                                                   \
            Method(_DIS, 0, NotSerialized) {                    \
                Or(reg, 0x80, reg)                              \
            }                                                   \
            Method(_CRS, 0, NotSerialized) {                    \
                Return (IQCR(reg))                              \
            }                                                   \
            Method(_SRS, 1, NotSerialized) {                    \
                CreateDWordField(Arg0, 0x05, PRRI)              \
                Store(PRRI, reg)                                \
            }                                                   \
        }

        define_link(LNKA, 0, PRQA)
        define_link(LNKB, 1, PRQB)
        define_link(LNKC, 2, PRQC)
        define_link(LNKD, 3, PRQD)
        define_link(LNKE, 4, PRQE)
        define_link(LNKF, 5, PRQF)
        define_link(LNKG, 6, PRQG)
        define_link(LNKH, 7, PRQH)

#define define_gsi_link(link, uid, gsi)                         \
        Device(link) {                                          \
            Name(_HID, EISAID("PNP0C0F"))                       \
            Name(_UID, uid)                                     \
            Name(_PRS, ResourceTemplate() {                     \
                Interrupt(, Level, ActiveHigh, Shared) {        \
                    gsi                                         \
                }                                               \
            })                                                  \
            Name(_CRS, ResourceTemplate() {                     \
                Interrupt(, Level, ActiveHigh, Shared) {        \
                    gsi                                         \
                }                                               \
            })                                                  \
            Method(_SRS, 1, NotSerialized) {                    \
            }                                                   \
        }

        define_gsi_link(GSIA, 0, 0x10)
        define_gsi_link(GSIB, 0, 0x11)
        define_gsi_link(GSIC, 0, 0x12)
        define_gsi_link(GSID, 0, 0x13)
        define_gsi_link(GSIE, 0, 0x14)
        define_gsi_link(GSIF, 0, 0x15)
        define_gsi_link(GSIG, 0, 0x16)
        define_gsi_link(GSIH, 0, 0x17)
    }

#include "hw/acpi/cpu_hotplug_defs.h"
#define CPU_STATUS_BASE ICH9_CPU_HOTPLUG_IO_BASE
#include "acpi-dsdt-cpu-hotplug.dsl"


/****************************************************************
 * General purpose events
 ****************************************************************/

    Scope(\_GPE) {
        Name(_HID, "ACPI0006")

        Method(_L00) {
        }
        Method(_L01) {
        }
        Method(_E02) {
            // CPU hotplug event
            \_SB.PRSC()
        }
        Method(_L03) {
        }
        Method(_L04) {
        }
        Method(_L05) {
        }
        Method(_L06) {
        }
        Method(_L07) {
        }
        Method(_L08) {
        }
        Method(_L09) {
        }
        Method(_L0A) {
        }
        Method(_L0B) {
        }
        Method(_L0C) {
        }
        Method(_L0D) {
        }
        Method(_L0E) {
        }
        Method(_L0F) {
        }
    }
}
