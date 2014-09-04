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
 * Debugging
 ****************************************************************/

Scope(\) {
    /* Debug Output */
    OperationRegion(DBG, SystemIO, 0x0402, 0x01)
    Field(DBG, ByteAcc, NoLock, Preserve) {
        DBGB,   8,
    }

    /* Debug method - use this method to send output to the QEMU
     * BIOS debug port.  This method handles strings, integers,
     * and buffers.  For example: DBUG("abc") DBUG(0x123) */
    Method(DBUG, 1) {
        ToHexString(Arg0, Local0)
        ToBuffer(Local0, Local0)
        Subtract(SizeOf(Local0), 1, Local1)
        Store(Zero, Local2)
        While (LLess(Local2, Local1)) {
            Store(DerefOf(Index(Local0, Local2)), DBGB)
            Increment(Local2)
        }
        Store(0x0A, DBGB)
    }
}
