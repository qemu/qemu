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

    External(MEMORY_SLOT_NOTIFY_METHOD, MethodObj)

    Scope(\_SB.PCI0) {
        Device(MEMORY_HOTPLUG_DEVICE) {
            Name(_HID, "PNP0A06")
            Name(_UID, "Memory hotplug resources")
            External(MEMORY_SLOTS_NUMBER, IntObj)

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
