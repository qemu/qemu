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

    External(\_SB.PCI0.MEMORY_HOTPLUG_DEVICE.MEMORY_SLOT_SCAN_METHOD, MethodObj)

    Scope(\_SB.PCI0) {
        Device(MEMORY_HOTPLUG_DEVICE) {
            Name(_HID, "PNP0A06")
            Name(_UID, "Memory hotplug resources")

            /* Memory hotplug IO registers */
            External(MEMORY_SLOT_ADDR_LOW, FieldUnitObj) // read only
            External(MEMORY_SLOT_ADDR_HIGH, FieldUnitObj) // read only
            External(MEMORY_SLOT_SIZE_LOW, FieldUnitObj) // read only
            External(MEMORY_SLOT_SIZE_HIGH, FieldUnitObj) // read only
            External(MEMORY_SLOT_PROXIMITY, FieldUnitObj) // read only
            External(MEMORY_SLOT_EJECT, FieldUnitObj) // initiates device eject, write only
            External(MEMORY_SLOT_SLECTOR, FieldUnitObj) // DIMM selector, write only
            External(MEMORY_SLOT_OST_EVENT, FieldUnitObj) // _OST event code, write only
            External(MEMORY_SLOT_OST_STATUS, FieldUnitObj) // _OST status code, write only
            External(MEMORY_SLOT_LOCK, MutexObj)

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

            Method(MEMORY_SLOT_EJECT_METHOD, 2) {
                Acquire(MEMORY_SLOT_LOCK, 0xFFFF)
                Store(ToInteger(Arg0), MEMORY_SLOT_SLECTOR) // select DIMM
                Store(1, MEMORY_SLOT_EJECT)
                Release(MEMORY_SLOT_LOCK)
            }
        } // Device()
    } // Scope()
