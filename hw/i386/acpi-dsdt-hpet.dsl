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

/****************************************************************
 * HPET
 ****************************************************************/

Scope(\_SB) {
    Device(HPET) {
        Name(_HID, EISAID("PNP0103"))
        Name(_UID, 0)
        OperationRegion(HPTM, SystemMemory, 0xFED00000, 0x400)
        Field(HPTM, DWordAcc, Lock, Preserve) {
            VEND, 32,
            PRD, 32,
        }
        Method(_STA, 0, NotSerialized) {
            Store(VEND, Local0)
            Store(PRD, Local1)
            ShiftRight(Local0, 16, Local0)
            If (LOr(LEqual(Local0, 0), LEqual(Local0, 0xffff))) {
                Return (0x0)
            }
            If (LOr(LEqual(Local1, 0), LGreater(Local1, 100000000))) {
                Return (0x0)
            }
            Return (0x0F)
        }
        Name(_CRS, ResourceTemplate() {
            Memory32Fixed(ReadOnly,
                0xFED00000,         // Address Base
                0x00000400,         // Address Length
                )
        })
    }
}
