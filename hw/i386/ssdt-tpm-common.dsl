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

/*
 * Common parts for TPM 1.2 and TPM 2 (with slight differences for PPI)
 * to be #included
 */


    External(\_SB.PCI0.ISA, DeviceObj)
    Scope(\_SB.PCI0.ISA) {
        /* TPM with emulated TPM TIS interface */
        Device (TPM) {
            Name (_HID, EisaID ("PNP0C31"))
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, TPM_TIS_ADDR_BASE, TPM_TIS_ADDR_SIZE)
                IRQNoFlags () {TPM_TIS_IRQ}
            })
            Method (_STA, 0, NotSerialized) {
                Return (0x0F)
            }
        }
    }
