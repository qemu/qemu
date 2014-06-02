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
#include "hw/acpi/pc-hotplug.h"

ACPI_EXTRACT_ALL_CODE ssdp_misc_aml

DefinitionBlock ("ssdt-misc.aml", "SSDT", 0x01, "BXPC", "BXSSDTSUSP", 0x1)
{

/****************************************************************
 * PCI memory ranges
 ****************************************************************/

    Scope(\) {
       ACPI_EXTRACT_NAME_DWORD_CONST acpi_pci32_start
       Name(P0S, 0x12345678)
       ACPI_EXTRACT_NAME_DWORD_CONST acpi_pci32_end
       Name(P0E, 0x12345678)
       ACPI_EXTRACT_NAME_BYTE_CONST acpi_pci64_valid
       Name(P1V, 0x12)
       ACPI_EXTRACT_NAME_BUFFER8 acpi_pci64_start
       Name(P1S, Buffer() { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 })
       ACPI_EXTRACT_NAME_BUFFER8 acpi_pci64_end
       Name(P1E, Buffer() { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 })
       ACPI_EXTRACT_NAME_BUFFER8 acpi_pci64_length
       Name(P1L, Buffer() { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 })
    }


/****************************************************************
 * Suspend
 ****************************************************************/

    Scope(\) {
    /*
     * S3 (suspend-to-ram), S4 (suspend-to-disk) and S5 (power-off) type codes:
     * must match piix4 emulation.
     */

        ACPI_EXTRACT_NAME_STRING acpi_s3_name
        Name(_S3, Package(0x04) {
            One,  /* PM1a_CNT.SLP_TYP */
            One,  /* PM1b_CNT.SLP_TYP */
            Zero,  /* reserved */
            Zero   /* reserved */
        })
        ACPI_EXTRACT_NAME_STRING acpi_s4_name
        ACPI_EXTRACT_PKG_START acpi_s4_pkg
        Name(_S4, Package(0x04) {
            0x2,  /* PM1a_CNT.SLP_TYP */
            0x2,  /* PM1b_CNT.SLP_TYP */
            Zero,  /* reserved */
            Zero   /* reserved */
        })
        Name(_S5, Package(0x04) {
            Zero,  /* PM1a_CNT.SLP_TYP */
            Zero,  /* PM1b_CNT.SLP_TYP */
            Zero,  /* reserved */
            Zero   /* reserved */
        })
    }

    External(\_SB.PCI0, DeviceObj)
    External(\_SB.PCI0.ISA, DeviceObj)

    Scope(\_SB.PCI0.ISA) {
        Device(PEVT) {
            Name(_HID, "QEMU0001")
            /* PEST will be patched to be Zero if no such device */
            ACPI_EXTRACT_NAME_WORD_CONST ssdt_isa_pest
            Name(PEST, 0xFFFF)
            OperationRegion(PEOR, SystemIO, PEST, 0x01)
            Field(PEOR, ByteAcc, NoLock, Preserve) {
                PEPT,   8,
            }

            Method(_STA, 0, NotSerialized) {
                Store(PEST, Local0)
                If (LEqual(Local0, Zero)) {
                    Return (0x00)
                } Else {
                    Return (0x0F)
                }
            }

            Method(RDPT, 0, NotSerialized) {
                Store(PEPT, Local0)
                Return (Local0)
            }

            Method(WRPT, 1, NotSerialized) {
                Store(Arg0, PEPT)
            }

            Name(_CRS, ResourceTemplate() {
                IO(Decode16, 0x00, 0x00, 0x01, 0x01, IO)
            })

            CreateWordField(_CRS, IO._MIN, IOMN)
            CreateWordField(_CRS, IO._MAX, IOMX)

            Method(_INI, 0, NotSerialized) {
                Store(PEST, IOMN)
                Store(PEST, IOMX)
            }
        }
    }

    External(MEMORY_SLOT_NOTIFY_METHOD, MethodObj)
    Scope(\_SB.PCI0) {
        Device(MEMORY_HOPTLUG_DEVICE) {
            Name(_HID, "PNP0A06")
            Name(_UID, "Memory hotplug resources")

            ACPI_EXTRACT_NAME_DWORD_CONST ssdt_mctrl_nr_slots
            Name(MEMORY_SLOTS_NUMBER, 0x12345678)

            /* Memory hotplug IO registers */
            OperationRegion(MEMORY_HOTPLUG_IO_REGION, SystemIO,
                            ACPI_MEMORY_HOTPLUG_BASE,
                            ACPI_MEMORY_HOTPLUG_IO_LEN)

            Name(_CRS, ResourceTemplate() {
                IO(Decode16, ACPI_MEMORY_HOTPLUG_BASE, ACPI_MEMORY_HOTPLUG_BASE,
                   0, ACPI_MEMORY_HOTPLUG_IO_LEN, IO)
            })

            Method(_STA, 0) {
                If (LEqual(MEMORY_SLOTS_NUMBER, Zero)) {
                    Return(0x0)
                }
                /* present, functioning, decoding, not shown in UI */
                Return(0xB)
            }

            Field(MEMORY_HOTPLUG_IO_REGION, DWordAcc, NoLock, Preserve) {
                MEMORY_SLOT_ADDR_LOW, 32,  // read only
                MEMORY_SLOT_ADDR_HIGH, 32, // read only
                MEMORY_SLOT_SIZE_LOW, 32,  // read only
                MEMORY_SLOT_SIZE_HIGH, 32, // read only
                MEMORY_SLOT_PROXIMITY, 32, // read only
            }
            Field(MEMORY_HOTPLUG_IO_REGION, ByteAcc, NoLock, Preserve) {
                Offset(20),
                MEMORY_SLOT_ENABLED,  1, // 1 if enabled, read only
                MEMORY_SLOT_INSERT_EVENT, 1, // (read) 1 if has a insert event. (write) 1 to clear event
            }

            Mutex (MEMORY_SLOT_LOCK, 0)
            Field (MEMORY_HOTPLUG_IO_REGION, DWordAcc, NoLock, Preserve) {
                MEMORY_SLOT_SLECTOR, 32,  // DIMM selector, write only
                MEMORY_SLOT_OST_EVENT, 32,  // _OST event code, write only
                MEMORY_SLOT_OST_STATUS, 32,  // _OST status code, write only
            }

            Method(MEMORY_SLOT_SCAN_METHOD, 0) {
                If (LEqual(MEMORY_SLOTS_NUMBER, Zero)) {
                     Return(Zero)
                }

                Store(Zero, Local0) // Mem devs iterrator
                Acquire(MEMORY_SLOT_LOCK, 0xFFFF)
                while (LLess(Local0, MEMORY_SLOTS_NUMBER)) {
                    Store(Local0, MEMORY_SLOT_SLECTOR) // select Local0 DIMM
                    If (LEqual(MEMORY_SLOT_INSERT_EVENT, One)) { // Memory device needs check
                        MEMORY_SLOT_NOTIFY_METHOD(Local0, 1)
                        Store(1, MEMORY_SLOT_INSERT_EVENT)
                    }
                    // TODO: handle memory eject request
                    Add(Local0, One, Local0) // goto next DIMM
                }
                Release(MEMORY_SLOT_LOCK)
                Return(One)
            }

            Method(MEMORY_SLOT_STATUS_METHOD, 1) {
                Store(Zero, Local0)

                Acquire(MEMORY_SLOT_LOCK, 0xFFFF)
                Store(ToInteger(Arg0), MEMORY_SLOT_SLECTOR) // select DIMM

                If (LEqual(MEMORY_SLOT_ENABLED, One)) {
                    Store(0xF, Local0)
                }

                Release(MEMORY_SLOT_LOCK)
                Return(Local0)
            }

            Method(MEMORY_SLOT_CRS_METHOD, 1, Serialized) {
                Acquire(MEMORY_SLOT_LOCK, 0xFFFF)
                Store(ToInteger(Arg0), MEMORY_SLOT_SLECTOR) // select DIMM

                Name(MR64, ResourceTemplate() {
                    QWordMemory(ResourceProducer, PosDecode, MinFixed, MaxFixed,
                    Cacheable, ReadWrite,
                    0x0000000000000000,        // Address Space Granularity
                    0x0000000000000000,        // Address Range Minimum
                    0xFFFFFFFFFFFFFFFE,        // Address Range Maximum
                    0x0000000000000000,        // Address Translation Offset
                    0xFFFFFFFFFFFFFFFF,        // Address Length
                    ,, MW64, AddressRangeMemory, TypeStatic)
                })

                CreateDWordField(MR64, 14, MINL)
                CreateDWordField(MR64, 18, MINH)
                CreateDWordField(MR64, 38, LENL)
                CreateDWordField(MR64, 42, LENH)
                CreateDWordField(MR64, 22, MAXL)
                CreateDWordField(MR64, 26, MAXH)

                Store(MEMORY_SLOT_ADDR_HIGH, MINH)
                Store(MEMORY_SLOT_ADDR_LOW, MINL)
                Store(MEMORY_SLOT_SIZE_HIGH, LENH)
                Store(MEMORY_SLOT_SIZE_LOW, LENL)

                // 64-bit math: MAX = MIN + LEN - 1
                Add(MINL, LENL, MAXL)
                Add(MINH, LENH, MAXH)
                If (LLess(MAXL, MINL)) {
                    Add(MAXH, One, MAXH)
                }
                If (LLess(MAXL, One)) {
                    Subtract(MAXH, One, MAXH)
                }
                Subtract(MAXL, One, MAXL)

                If (LEqual(MAXH, Zero)){
                    Name(MR32, ResourceTemplate() {
                        DWordMemory(ResourceProducer, PosDecode, MinFixed, MaxFixed,
                        Cacheable, ReadWrite,
                        0x00000000,        // Address Space Granularity
                        0x00000000,        // Address Range Minimum
                        0xFFFFFFFE,        // Address Range Maximum
                        0x00000000,        // Address Translation Offset
                        0xFFFFFFFF,        // Address Length
                        ,, MW32, AddressRangeMemory, TypeStatic)
                    })
                    CreateDWordField(MR32, MW32._MIN, MIN)
                    CreateDWordField(MR32, MW32._MAX, MAX)
                    CreateDWordField(MR32, MW32._LEN, LEN)
                    Store(MINL, MIN)
                    Store(MAXL, MAX)
                    Store(LENL, LEN)

                    Release(MEMORY_SLOT_LOCK)
                    Return(MR32)
                }

                Release(MEMORY_SLOT_LOCK)
                Return(MR64)
            }

            Method(MEMORY_SLOT_PROXIMITY_METHOD, 1) {
                Acquire(MEMORY_SLOT_LOCK, 0xFFFF)
                Store(ToInteger(Arg0), MEMORY_SLOT_SLECTOR) // select DIMM
                Store(MEMORY_SLOT_PROXIMITY, Local0)
                Release(MEMORY_SLOT_LOCK)
                Return(Local0)
            }

            Method(MEMORY_SLOT_OST_METHOD, 4) {
                Acquire(MEMORY_SLOT_LOCK, 0xFFFF)
                Store(ToInteger(Arg0), MEMORY_SLOT_SLECTOR) // select DIMM
                Store(Arg1, MEMORY_SLOT_OST_EVENT)
                Store(Arg2, MEMORY_SLOT_OST_STATUS)
                Release(MEMORY_SLOT_LOCK)
            }
        } // Device()
    } // Scope()
}
