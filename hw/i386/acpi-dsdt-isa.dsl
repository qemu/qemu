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

/* Common legacy ISA style devices. */
Scope(\_SB.PCI0.ISA) {

    Device(LPT) {
        Name(_HID, EisaId("PNP0400"))
        Method(_STA, 0, NotSerialized) {
            Store(LPEN, Local0)
            If (LEqual(Local0, 0)) {
                Return (0x00)
            } Else {
                Return (0x0F)
            }
        }
        Name(_CRS, ResourceTemplate() {
            IO(Decode16, 0x0378, 0x0378, 0x08, 0x08)
            IRQNoFlags() { 7 }
        })
    }

    Device(COM1) {
        Name(_HID, EisaId("PNP0501"))
        Name(_UID, 0x01)
        Method(_STA, 0, NotSerialized) {
            Store(CAEN, Local0)
            If (LEqual(Local0, 0)) {
                Return (0x00)
            } Else {
                Return (0x0F)
            }
        }
        Name(_CRS, ResourceTemplate() {
            IO(Decode16, 0x03F8, 0x03F8, 0x00, 0x08)
            IRQNoFlags() { 4 }
        })
    }

    Device(COM2) {
        Name(_HID, EisaId("PNP0501"))
        Name(_UID, 0x02)
        Method(_STA, 0, NotSerialized) {
            Store(CBEN, Local0)
            If (LEqual(Local0, 0)) {
                Return (0x00)
            } Else {
                Return (0x0F)
            }
        }
        Name(_CRS, ResourceTemplate() {
            IO(Decode16, 0x02F8, 0x02F8, 0x00, 0x08)
            IRQNoFlags() { 3 }
        })
    }
}
