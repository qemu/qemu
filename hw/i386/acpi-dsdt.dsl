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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

ACPI_EXTRACT_ALL_CODE AcpiDsdtAmlCode

DefinitionBlock (
    "acpi-dsdt.aml",    // Output Filename
    "DSDT",             // Signature
    0x01,               // DSDT Compliance Revision
    "BXPC",             // OEMID
    "BXDSDT",           // TABLE ID
    0x1                 // OEM Revision
    )
{

#include "acpi-dsdt-dbug.dsl"


/****************************************************************
 * PCI Bus definition
 ****************************************************************/
#define BOARD_SPECIFIC_PCI_RESOURSES \
     WordIO(ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange, \
         0x0000, \
         0x0000, \
         0x0CF7, \
         0x0000, \
         0x0CF8, \
         ,, , TypeStatic) \
     WordIO(ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange, \
         0x0000, \
         0x0D00, \
         0xADFF, \
         0x0000, \
         0xA100, \
         ,, , TypeStatic) \
     /* 0xae00-0xae0e hole for PCI hotplug, hw/acpi/piix4.c:PCI_HOTPLUG_ADDR */ \
     WordIO(ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange, \
         0x0000, \
         0xAE0F, \
         0xAEFF, \
         0x0000, \
         0x00F1, \
         ,, , TypeStatic) \
     /* 0xaf00-0xaf1f hole for CPU hotplug, hw/acpi/piix4.c:PIIX4_PROC_BASE */ \
     WordIO(ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange, \
         0x0000, \
         0xAF20, \
         0xAFDF, \
         0x0000, \
         0x00C0, \
         ,, , TypeStatic) \
     /* 0xafe0-0xafe3 hole for ACPI.GPE0, hw/acpi/piix4.c:GPE_BASE */ \
     WordIO(ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange, \
         0x0000, \
         0xAFE4, \
         0xFFFF, \
         0x0000, \
         0x501C, \
         ,, , TypeStatic)

    Scope(\_SB) {
        Device(PCI0) {
            Name(_HID, EisaId("PNP0A03"))
            Name(_ADR, 0x00)
            Name(_UID, 1)
//#define PX13 S0B_
//            External(PX13, DeviceObj)
        }
    }

#include "acpi-dsdt-pci-crs.dsl"
#include "acpi-dsdt-hpet.dsl"


/****************************************************************
 * PIIX4 PM
 ****************************************************************/

    Scope(\_SB.PCI0) {
        Device(PX13) {
            Name(_ADR, 0x00010003)
            OperationRegion(P13C, PCI_Config, 0x00, 0xff)
        }
    }


/****************************************************************
 * PIIX3 ISA bridge
 ****************************************************************/

    Scope(\_SB.PCI0) {

        External(ISA, DeviceObj)

        Device(ISA) {
            Name(_ADR, 0x00010000)

            /* PIIX PCI to ISA irq remapping */
            OperationRegion(P40C, PCI_Config, 0x60, 0x04)

            /* enable bits */
            Field(\_SB.PCI0.PX13.P13C, AnyAcc, NoLock, Preserve) {
                Offset(0x5f),
                , 7,
                LPEN, 1,         // LPT
                Offset(0x67),
                , 3,
                CAEN, 1,         // COM1
                , 3,
                CBEN, 1,         // COM2
            }
            Name(FDEN, 1)
        }
    }

#define DSDT_APPLESMC_STA piix_dsdt_applesmc_sta
#include "acpi-dsdt-isa.dsl"


/****************************************************************
 * PCI hotplug
 ****************************************************************/

    Scope(\_SB.PCI0) {
        OperationRegion(PCST, SystemIO, 0xae00, 0x08)
        Field(PCST, DWordAcc, NoLock, WriteAsZeros) {
            PCIU, 32,
            PCID, 32,
        }

        OperationRegion(SEJ, SystemIO, 0xae08, 0x04)
        Field(SEJ, DWordAcc, NoLock, WriteAsZeros) {
            B0EJ, 32,
        }

        OperationRegion(BNMR, SystemIO, 0xae10, 0x04)
        Field(BNMR, DWordAcc, NoLock, WriteAsZeros) {
            BNUM, 32,
        }

        /* Lock to protect access to fields above. */
        Mutex(BLCK, 0)

        /* Methods called by bulk generated PCI devices below */

        /* Methods called by hotplug devices */
        Method(PCEJ, 2, NotSerialized) {
            // _EJ0 method - eject callback
            Acquire(BLCK, 0xFFFF)
            Store(Arg0, BNUM)
            Store(ShiftLeft(1, Arg1), B0EJ)
            Release(BLCK)
            Return (0x0)
        }

        /* Hotplug notification method supplied by SSDT */
        External(\_SB.PCI0.PCNT, MethodObj)
    }


/****************************************************************
 * PCI IRQs
 ****************************************************************/

    Scope(\_SB) {
        Scope(PCI0) {
            Name(_PRT, Package() {
                /* PCI IRQ routing table, example from ACPI 2.0a specification,
                   section 6.2.8.1 */
                /* Note: we provide the same info as the PCI routing
                   table of the Bochs BIOS */

#define prt_slot(nr, lnk0, lnk1, lnk2, lnk3) \
    Package() { nr##ffff, 0, lnk0, 0 }, \
    Package() { nr##ffff, 1, lnk1, 0 }, \
    Package() { nr##ffff, 2, lnk2, 0 }, \
    Package() { nr##ffff, 3, lnk3, 0 }

#define prt_slot0(nr) prt_slot(nr, LNKD, LNKA, LNKB, LNKC)
#define prt_slot1(nr) prt_slot(nr, LNKA, LNKB, LNKC, LNKD)
#define prt_slot2(nr) prt_slot(nr, LNKB, LNKC, LNKD, LNKA)
#define prt_slot3(nr) prt_slot(nr, LNKC, LNKD, LNKA, LNKB)

                prt_slot0(0x0000),
                /* Device 1 is power mgmt device, and can only use irq 9 */
                prt_slot(0x0001, LNKS, LNKB, LNKC, LNKD),
                prt_slot2(0x0002),
                prt_slot3(0x0003),
                prt_slot0(0x0004),
                prt_slot1(0x0005),
                prt_slot2(0x0006),
                prt_slot3(0x0007),
                prt_slot0(0x0008),
                prt_slot1(0x0009),
                prt_slot2(0x000a),
                prt_slot3(0x000b),
                prt_slot0(0x000c),
                prt_slot1(0x000d),
                prt_slot2(0x000e),
                prt_slot3(0x000f),
                prt_slot0(0x0010),
                prt_slot1(0x0011),
                prt_slot2(0x0012),
                prt_slot3(0x0013),
                prt_slot0(0x0014),
                prt_slot1(0x0015),
                prt_slot2(0x0016),
                prt_slot3(0x0017),
                prt_slot0(0x0018),
                prt_slot1(0x0019),
                prt_slot2(0x001a),
                prt_slot3(0x001b),
                prt_slot0(0x001c),
                prt_slot1(0x001d),
                prt_slot2(0x001e),
                prt_slot3(0x001f),
            })
        }

        Field(PCI0.ISA.P40C, ByteAcc, NoLock, Preserve) {
            PRQ0,   8,
            PRQ1,   8,
            PRQ2,   8,
            PRQ3,   8
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
            If (LLess(Arg0, 0x80)) {
                Store(Arg0, PRRI)
            }
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

        define_link(LNKA, 0, PRQ0)
        define_link(LNKB, 1, PRQ1)
        define_link(LNKC, 2, PRQ2)
        define_link(LNKD, 3, PRQ3)

        Device(LNKS) {
            Name(_HID, EISAID("PNP0C0F"))
            Name(_UID, 4)
            Name(_PRS, ResourceTemplate() {
                Interrupt(, Level, ActiveHigh, Shared) { 9 }
            })

            // The SCI cannot be disabled and is always attached to GSI 9,
            // so these are no-ops.  We only need this link to override the
            // polarity to active high and match the content of the MADT.
            Method(_STA, 0, NotSerialized) { Return (0x0b) }
            Method(_DIS, 0, NotSerialized) { }
            Method(_CRS, 0, NotSerialized) { Return (_PRS) }
            Method(_SRS, 1, NotSerialized) { }
        }
    }

#include "hw/acpi/cpu_hotplug_defs.h"
#define CPU_STATUS_BASE PIIX4_CPU_HOTPLUG_IO_BASE
#include "acpi-dsdt-cpu-hotplug.dsl"


/****************************************************************
 * General purpose events
 ****************************************************************/

    Scope(\_GPE) {
        Name(_HID, "ACPI0006")

        Method(_L00) {
        }
        Method(_E01) {
            // PCI hotplug event
            Acquire(\_SB.PCI0.BLCK, 0xFFFF)
            \_SB.PCI0.PCNT()
            Release(\_SB.PCI0.BLCK)
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
