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

/* PCI CRS (current resources) definition. */
Scope(\_SB.PCI0) {

    Name(CRES, ResourceTemplate() {
        WordBusNumber(ResourceProducer, MinFixed, MaxFixed, PosDecode,
            0x0000,             // Address Space Granularity
            0x0000,             // Address Range Minimum
            0x00FF,             // Address Range Maximum
            0x0000,             // Address Translation Offset
            0x0100,             // Address Length
            ,, )
        IO(Decode16,
            0x0CF8,             // Address Range Minimum
            0x0CF8,             // Address Range Maximum
            0x01,               // Address Alignment
            0x08,               // Address Length
            )
        WordIO(ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange,
            0x0000,             // Address Space Granularity
            0x0000,             // Address Range Minimum
            0x0CF7,             // Address Range Maximum
            0x0000,             // Address Translation Offset
            0x0CF8,             // Address Length
            ,, , TypeStatic)
        WordIO(ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange,
            0x0000,             // Address Space Granularity
            0x0D00,             // Address Range Minimum
            0xFFFF,             // Address Range Maximum
            0x0000,             // Address Translation Offset
            0xF300,             // Address Length
            ,, , TypeStatic)
        DWordMemory(ResourceProducer, PosDecode, MinFixed, MaxFixed, Cacheable, ReadWrite,
            0x00000000,         // Address Space Granularity
            0x000A0000,         // Address Range Minimum
            0x000BFFFF,         // Address Range Maximum
            0x00000000,         // Address Translation Offset
            0x00020000,         // Address Length
            ,, , AddressRangeMemory, TypeStatic)
        DWordMemory(ResourceProducer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite,
            0x00000000,         // Address Space Granularity
            0xE0000000,         // Address Range Minimum
            0xFEBFFFFF,         // Address Range Maximum
            0x00000000,         // Address Translation Offset
            0x1EC00000,         // Address Length
            ,, PW32, AddressRangeMemory, TypeStatic)
    })

    Name(CR64, ResourceTemplate() {
        QWordMemory(ResourceProducer, PosDecode, MinFixed, MaxFixed, Cacheable, ReadWrite,
            0x00000000,          // Address Space Granularity
            0x8000000000,        // Address Range Minimum
            0xFFFFFFFFFF,        // Address Range Maximum
            0x00000000,          // Address Translation Offset
            0x8000000000,        // Address Length
            ,, PW64, AddressRangeMemory, TypeStatic)
    })

    Method(_CRS, 0) {
        /* Fields provided by dynamically created ssdt */
        External(P0S, IntObj)
        External(P0E, IntObj)
        External(P1V, IntObj)
        External(P1S, BuffObj)
        External(P1E, BuffObj)
        External(P1L, BuffObj)

        /* fixup 32bit pci io window */
        CreateDWordField(CRES, \_SB.PCI0.PW32._MIN, PS32)
        CreateDWordField(CRES, \_SB.PCI0.PW32._MAX, PE32)
        CreateDWordField(CRES, \_SB.PCI0.PW32._LEN, PL32)
        Store(P0S, PS32)
        Store(P0E, PE32)
        Store(Add(Subtract(P0E, P0S), 1), PL32)

        If (LEqual(P1V, Zero)) {
            Return (CRES)
        }

        /* fixup 64bit pci io window */
        CreateQWordField(CR64, \_SB.PCI0.PW64._MIN, PS64)
        CreateQWordField(CR64, \_SB.PCI0.PW64._MAX, PE64)
        CreateQWordField(CR64, \_SB.PCI0.PW64._LEN, PL64)
        Store(P1S, PS64)
        Store(P1E, PE64)
        Store(P1L, PL64)
        /* add window and return result */
        ConcatenateResTemplate(CRES, CR64, Local0)
        Return (Local0)
    }
}
