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
 * CPU hotplug
 ****************************************************************/

Scope(\_SB) {
    /* Objects filled in by run-time generated SSDT */
    External(NTFY, MethodObj)
    External(CPON, PkgObj)
    External(PRS, FieldUnitObj)

    /* Methods called by run-time generated SSDT Processor objects */
    Method(PRSC, 0) {
        // Local5 = active cpu bitmap
        Store(PRS, Local5)
        // Local2 = last read byte from bitmap
        Store(Zero, Local2)
        // Local0 = Processor ID / APIC ID iterator
        Store(Zero, Local0)
        While (LLess(Local0, SizeOf(CPON))) {
            // Local1 = CPON flag for this cpu
            Store(DerefOf(Index(CPON, Local0)), Local1)
            If (And(Local0, 0x07)) {
                // Shift down previously read bitmap byte
                ShiftRight(Local2, 1, Local2)
            } Else {
                // Read next byte from cpu bitmap
                Store(DerefOf(Index(Local5, ShiftRight(Local0, 3))), Local2)
            }
            // Local3 = active state for this cpu
            Store(And(Local2, 1), Local3)

            If (LNotEqual(Local1, Local3)) {
                // State change - update CPON with new state
                Store(Local3, Index(CPON, Local0))
                // Do CPU notify
                If (LEqual(Local3, 1)) {
                    NTFY(Local0, 1)
                } Else {
                    NTFY(Local0, 3)
                }
            }
            Increment(Local0)
        }
    }
}
